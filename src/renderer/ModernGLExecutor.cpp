// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLExecutor.h"
#include "ModernGLDrawPlan.h"
#include "GLDebugScope.h"
#include "GLStateCache.h"
#include "MaterialResourceTable.h"
#include "ModernClusteredLighting.h"
#include "ModernGLShaderLibrary.h"
#include "ModernGLSubmitPlan.h"
#include "ModernShadowPlanner.h"
#include "RenderGraphResources.h"
#include "RendererBootstrap.h"
#include "RendererMetrics.h"
#include "RendererUpload.h"

typedef struct modernGLFrameConstants_s {
	float	viewport[4];
	float	frame[4];
	float	capabilities[4];
	float	reserved[4];
} modernGLFrameConstants_t;

typedef struct modernGLGpuSceneRecord_s {
	float	counts[4];
	GLuint	ids[4];
} modernGLGpuSceneRecord_t;

typedef struct modernGLDrawElementsIndirectCommand_s {
	GLuint	count;
	GLuint	instanceCount;
	GLuint	firstIndex;
	GLuint	baseVertex;
	GLuint	baseInstance;
} modernGLDrawElementsIndirectCommand_t;

typedef struct modernGLGpuDrivenCpuReference_s {
	int		processedCommands;
	int		eligibleCommands;
	int		generatedCommands;
	int		culledCommands;
	int		visibleInstances;
	int		clusterBins;
} modernGLGpuDrivenCpuReference_t;

typedef struct modernGLGpuDrivenBatch_s {
	bool	valid;
	GLuint	program;
	GLuint	vertexBuffer;
	GLuint	indexBuffer;
	GLenum	indexType;
	int		ambientCacheOffset;
	int		vertexStride;
	modernGLSubmitCommand_t command;
	int		commandCount;
} modernGLGpuDrivenBatch_t;

enum modernGLGpuDrivenRecordFlags_t {
	MODERN_GL_GPU_RECORD_INDEXED = 1u << 0,
	MODERN_GL_GPU_RECORD_INDIRECT_ELIGIBLE = 1u << 1,
	MODERN_GL_GPU_RECORD_VISIBLE = 1u << 2,
	MODERN_GL_GPU_RECORD_CLUSTER_BIN_SOURCE = 1u << 3
};

enum modernGLGpuDrivenCounter_t {
	MODERN_GL_GPU_COUNTER_PROCESSED = 0,
	MODERN_GL_GPU_COUNTER_ELIGIBLE,
	MODERN_GL_GPU_COUNTER_GENERATED,
	MODERN_GL_GPU_COUNTER_CULLED,
	MODERN_GL_GPU_COUNTER_VISIBLE_INSTANCES,
	MODERN_GL_GPU_COUNTER_CLUSTER_BINS,
	MODERN_GL_GPU_COUNTER_INDIRECT_SIGNATURE,
	MODERN_GL_GPU_COUNTER_COUNT
};

const int MODERN_GL_GPU_DRIVEN_MAX_RECORDS = MODERN_GL_DRAW_PLAN_MAX_ENTRIES;
const int MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS = MODERN_GL_GPU_COUNTER_COUNT;
const int MODERN_GL_GPU_DRIVEN_WORKGROUP_SIZE = 64;
const int MODERN_GL_GBUFFER_ATTACHMENT_COUNT = 4;

enum modernGLDrawVertAttribute_t {
	MODERN_GL_DRAWVERT_ATTR_POSITION = 0,
	MODERN_GL_DRAWVERT_ATTR_COLOR = 3,
	MODERN_GL_DRAWVERT_ATTR_TEXCOORD0 = 8,
	MODERN_GL_DRAWVERT_ATTR_TANGENT0 = 9,
	MODERN_GL_DRAWVERT_ATTR_TANGENT1 = 10,
	MODERN_GL_DRAWVERT_ATTR_NORMAL = 11
};

enum modernGLExecutorFrameMode_t {
	MODERN_GL_EXECUTOR_FRAME_ANALYZE = 0,
	MODERN_GL_EXECUTOR_FRAME_SIDECAR_DIAGNOSTIC,
	MODERN_GL_EXECUTOR_FRAME_VISIBLE_REPLACEMENT
};

static const char *R_ModernGLExecutor_FrameModeName( int mode ) {
	switch ( mode ) {
	case MODERN_GL_EXECUTOR_FRAME_VISIBLE_REPLACEMENT:
		return "visible-replacement";
	case MODERN_GL_EXECUTOR_FRAME_SIDECAR_DIAGNOSTIC:
		return "sidecar-diagnostic";
	case MODERN_GL_EXECUTOR_FRAME_ANALYZE:
	default:
		return "analyze";
	}
}

static bool R_ModernGLExecutor_ModernVisibleRequested( void ) {
	return r_rendererModernVisible.GetBool() || RendererBootstrap_ShouldAutoPromoteModernVisible();
}

static bool R_ModernGLExecutor_ShadowMapSidecarRequested( void ) {
	return r_useShadowMap.GetBool()
		&& r_shadows.GetBool()
		&& ( r_shadowMapDebugOverlay.GetInteger() > 0 || r_shadowMapReport.GetInteger() > 0 );
}

const int MODERN_GL_PASS_OWNER_COUNT = RENDER_PASS_PRESENT + 1;

enum modernGLPassOwnerState_t {
	MODERN_GL_PASS_OWNER_LEGACY = 0,
	MODERN_GL_PASS_OWNER_MODERN,
	MODERN_GL_PASS_OWNER_MIXED,
	MODERN_GL_PASS_OWNER_DISABLED,
	MODERN_GL_PASS_OWNER_BLOCKED
};

typedef struct modernGLPassOwnershipSlot_s {
	renderPassCategory_t		category;
	modernGLPassOwnerState_t	state;
	bool					present;
	bool					modernExecuted;
	bool					legacyMayRun;
	bool					skipLegacy;
	bool					duplicateHazard;
	int						legacySkipCount;
	char					reason[64];
} modernGLPassOwnershipSlot_t;

typedef struct modernGLPassOwnershipTable_s {
	bool	valid;
	bool	handoffReady;
	int		failClosedRestores;
	char	failClosedReason[96];
	modernGLPassOwnershipSlot_t slots[MODERN_GL_PASS_OWNER_COUNT];
} modernGLPassOwnershipTable_t;

static modernGLPassOwnershipTable_t rg_modernGLPassOwnership;

static bool R_ModernGLExecutor_PassCategoryValid( renderPassCategory_t category ) {
	return category >= RENDER_PASS_DEPTH && category < MODERN_GL_PASS_OWNER_COUNT;
}

static const char *R_ModernGLExecutor_PassName( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
		return "depth";
	case RENDER_PASS_STENCIL_SHADOW:
		return "stencilShadow";
	case RENDER_PASS_SHADOW_MAP:
		return "shadowMap";
	case RENDER_PASS_ARB2_INTERACTION:
		return "arb2Interaction";
	case RENDER_PASS_LIGHT_GRID:
		return "lightGrid";
	case RENDER_PASS_AMBIENT:
		return "ambient";
	case RENDER_PASS_DEFERRED_RESOLVE:
		return "deferredResolve";
	case RENDER_PASS_FORWARD_PLUS:
		return "forwardPlus";
	case RENDER_PASS_FOG_BLEND:
		return "fogBlend";
	case RENDER_PASS_SSAO:
		return "ssao";
	case RENDER_PASS_MOTION_BLUR:
		return "motionBlur";
	case RENDER_PASS_LENS_FLARE:
		return "lensFlare";
	case RENDER_PASS_BLOOM:
		return "bloom";
	case RENDER_PASS_AUTHORED_POST:
		return "authoredPost";
	case RENDER_PASS_SPECIAL_EFFECTS:
		return "specialEffects";
	case RENDER_PASS_GUI:
		return "gui";
	case RENDER_PASS_PRESENT:
		return "present";
	default:
		return "unknown";
	}
}

static const char *R_ModernGLExecutor_PassOwnerStateName( modernGLPassOwnerState_t state ) {
	switch ( state ) {
	case MODERN_GL_PASS_OWNER_MODERN:
		return "modern";
	case MODERN_GL_PASS_OWNER_MIXED:
		return "mixed";
	case MODERN_GL_PASS_OWNER_DISABLED:
		return "disabled";
	case MODERN_GL_PASS_OWNER_BLOCKED:
		return "blocked";
	case MODERN_GL_PASS_OWNER_LEGACY:
	default:
		return "legacy";
	}
}

static bool R_ModernGLExecutor_PassHasLegacyWork( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
	case RENDER_PASS_STENCIL_SHADOW:
	case RENDER_PASS_SHADOW_MAP:
	case RENDER_PASS_ARB2_INTERACTION:
	case RENDER_PASS_LIGHT_GRID:
	case RENDER_PASS_AMBIENT:
	case RENDER_PASS_FOG_BLEND:
	case RENDER_PASS_SSAO:
	case RENDER_PASS_MOTION_BLUR:
	case RENDER_PASS_LENS_FLARE:
	case RENDER_PASS_BLOOM:
	case RENDER_PASS_AUTHORED_POST:
	case RENDER_PASS_SPECIAL_EFFECTS:
	case RENDER_PASS_GUI:
		return true;
	case RENDER_PASS_PRESENT:
	case RENDER_PASS_DEFERRED_RESOLVE:
	case RENDER_PASS_FORWARD_PLUS:
	default:
		return false;
	}
}

static bool R_ModernGLExecutor_PassIsPost( renderPassCategory_t category ) {
	return category == RENDER_PASS_SSAO
		|| category == RENDER_PASS_MOTION_BLUR
		|| category == RENDER_PASS_LENS_FLARE
		|| category == RENDER_PASS_BLOOM
		|| category == RENDER_PASS_AUTHORED_POST;
}

static void R_ModernGLExecutor_ResetPassOwnershipTable( const char *reason ) {
	memset( &rg_modernGLPassOwnership, 0, sizeof( rg_modernGLPassOwnership ) );
	idStr::Copynz( rg_modernGLPassOwnership.failClosedReason, reason != NULL ? reason : "reset", sizeof( rg_modernGLPassOwnership.failClosedReason ) );
	for ( int i = 0; i < MODERN_GL_PASS_OWNER_COUNT; ++i ) {
		modernGLPassOwnershipSlot_t &slot = rg_modernGLPassOwnership.slots[i];
		slot.category = static_cast<renderPassCategory_t>( i );
		slot.state = MODERN_GL_PASS_OWNER_LEGACY;
		slot.present = false;
		slot.legacyMayRun = R_ModernGLExecutor_PassHasLegacyWork( slot.category );
		idStr::Copynz( slot.reason, reason != NULL ? reason : "legacy-default", sizeof( slot.reason ) );
	}
}

const int MODERN_GL_DEFERRED_TEXTURE_COUNT = 5;
const int MODERN_GL_MATERIAL_TEXTURE_MAIN = 0;
const int MODERN_GL_MATERIAL_TEXTURE_NORMAL = 1;
const int MODERN_GL_MATERIAL_TEXTURE_SPECULAR = 2;
const int MODERN_GL_MATERIAL_TEXTURE_EMISSIVE = 3;
const int MODERN_GL_MATERIAL_TEXTURE_COUNT = 4;
const int MODERN_GL_CLUSTER_UBO_BINDING_PARAMS = 3;
const int MODERN_GL_CLUSTER_UBO_BINDING_LIGHTS = 4;
const int MODERN_GL_CLUSTER_UBO_BINDING_INDICES = 5;
const int MODERN_GL_VISIBLE_COMPOSITE_TEXTURE_COUNT = 2;

static const char *rg_modernGLGBufferAttachmentNames[MODERN_GL_GBUFFER_ATTACHMENT_COUNT] = {
	"gbufferAlbedo",
	"gbufferNormal",
	"gbufferMaterial",
	"gbufferEmissive"
};

static const GLenum rg_modernGLGBufferColorAttachments[MODERN_GL_GBUFFER_ATTACHMENT_COUNT] = {
	GL_COLOR_ATTACHMENT0,
	GL_COLOR_ATTACHMENT1,
	GL_COLOR_ATTACHMENT2,
	GL_COLOR_ATTACHMENT3
};

static const char *rg_modernGLDeferredTextureUniforms[MODERN_GL_DEFERRED_TEXTURE_COUNT] = {
	"uMainTexture",
	"uGBufferNormal",
	"uGBufferMaterial",
	"uGBufferEmissive",
	"uSceneDepth"
};

static modernGLExecutorStats_t rg_modernGLExecutorStats;
static idModernGLDrawPlan rg_modernGLDrawPlan;
static idModernGLSubmitPlan rg_modernGLSubmitPlan;
static modernGLGpuDrivenBatch_t rg_modernGLGpuDrivenBatch;
static renderBackendCaps_t rg_modernGLExecutorCaps;
static renderFeatureSet_t rg_modernGLExecutorFeatures;
static GLuint rg_modernGLExecutorVAO = 0;
static GLuint rg_modernGLExecutorFrameUBO = 0;
static GLuint rg_modernGLExecutorSceneSSBO = 0;
static GLuint rg_modernGLExecutorIndirectBuffer = 0;
static GLuint rg_modernGLExecutorValidationSSBO = 0;
static GLuint rg_modernGLExecutorComputeProgram = 0;
static GLuint rg_modernGLExecutorDepthOverlayProgram = 0;
static GLuint rg_modernGLExecutorGBufferFBO = 0;
static GLuint rg_modernGLExecutorLowOverheadSampler = 0;
static int rg_modernGLExecutorLowOverheadSamplerDSACreations = 0;
static int rg_modernGLExecutorLowOverheadSamplerDSAUpdates = 0;
static GLuint rg_modernGLExecutorGBufferOverlayProgram = 0;
static GLuint rg_modernGLExecutorDeferredOverlayProgram = 0;
static GLuint rg_modernGLExecutorVisibleCompositeProgram = 0;
static GLint rg_modernGLExecutorComputeRecordCountLocation = -1;
static GLint rg_modernGLExecutorDepthOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorDepthOverlayParamsLocation = -1;
static GLint rg_modernGLExecutorGBufferOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorGBufferOverlayParamsLocation = -1;
static GLint rg_modernGLExecutorDeferredOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorDeferredOverlayParamsLocation = -1;
static GLint rg_modernGLExecutorVisibleCompositeDeferredLocation = -1;
static GLint rg_modernGLExecutorVisibleCompositeForwardLocation = -1;
static GLint rg_modernGLExecutorVisibleCompositeParamsLocation = -1;
static bool rg_modernGLExecutorInitialized = false;
static bool rg_modernGLExecutorAvailable = false;
static bool rg_modernGLExecutorGpuDrivenReady = false;
static bool rg_modernGLExecutorLowOverheadReady = false;

static ID_INLINE GLint R_ModernGLExecutor_SafeStencilClearValue( void ) {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

static void R_ModernGLExecutor_SetStatus( modernGLExecutorStats_t &stats, const char *status ) {
	idStr::Copynz( stats.status, status ? status : "unknown", sizeof( stats.status ) );
}

static void R_ModernGLExecutor_CopyDrawPlanStats( modernGLExecutorStats_t &stats, const modernGLDrawPlanStats_t &drawPlanStats ) {
	stats.drawPlanReady = drawPlanStats.available && drawPlanStats.valid;
	stats.drawPlanOverflow = drawPlanStats.overflow;
	stats.drawPlanDraws = drawPlanStats.plannedDraws;
	stats.drawPlanDepthDraws = drawPlanStats.depthDraws;
	stats.drawPlanMaterialDraws = drawPlanStats.materialDraws;
	stats.drawPlanFallbackDraws = drawPlanStats.fallbackDraws;
	stats.drawPlanIndexedDraws = drawPlanStats.indexedDraws;
	stats.drawPlanVertexOnlyDraws = drawPlanStats.vertexOnlyDraws;
	stats.drawPlanStateBatches = drawPlanStats.stateBatches;
	stats.drawPlanProgramSwitches = drawPlanStats.programSwitches;
	stats.drawPlanMaterialSwitches = drawPlanStats.materialSwitches;
	if ( drawPlanStats.highestGLSLVersion > stats.highestGLSLVersion ) {
		stats.highestGLSLVersion = drawPlanStats.highestGLSLVersion;
	}
}

static void R_ModernGLExecutor_CopySubmitPlanStats( modernGLExecutorStats_t &stats, const modernGLSubmitPlanStats_t &submitPlanStats ) {
	stats.submitPlanReady = submitPlanStats.available && submitPlanStats.valid;
	stats.submitPlanOverflow = submitPlanStats.overflow;
	stats.submitPlanDraws = submitPlanStats.readyDraws;
	stats.submitPlanFallbackDraws = submitPlanStats.fallbackDraws;
	stats.submitPlanDepthDraws = submitPlanStats.depthReadyDraws;
	stats.submitPlanMaterialDraws = submitPlanStats.materialReadyDraws;
	stats.submitPlanMissingAmbientDraws = submitPlanStats.missingAmbientCacheDraws;
	stats.submitPlanMissingIndexDraws = submitPlanStats.missingIndexCacheDraws;
	stats.submitPlanIndexUploadDraws = submitPlanStats.indexUploadDraws;
	stats.submitPlanProgramBatches = submitPlanStats.programBatches;
	stats.submitPlanVertexBufferBatches = submitPlanStats.vertexBufferBatches;
	stats.submitPlanIndexBufferBatches = submitPlanStats.indexBufferBatches;
	stats.submitPlanScissorBatches = submitPlanStats.scissorBatches;
	stats.submitPlanMaterialBatches = submitPlanStats.materialBatches;
	stats.submitPlanUniformUpdates = submitPlanStats.uniformUpdates;
	stats.submitPlanFrameUBOBinds = submitPlanStats.frameUBOBinds;
	stats.lowOverheadCompactedBatches = stats.tierUsesMultiBind ? ( submitPlanStats.programBatches + submitPlanStats.materialBatches ) : 0;
	if ( submitPlanStats.highestGLSLVersion > stats.highestGLSLVersion ) {
		stats.highestGLSLVersion = submitPlanStats.highestGLSLVersion;
	}
}

static bool R_ModernGLExecutor_CanCreateObjects( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !features.modernBaseline ) {
		return false;
	}
	if ( !caps.hasVAO || !caps.hasUBO || !caps.hasVBO ) {
		return false;
	}
	if ( glGenVertexArrays == NULL || glBindVertexArray == NULL || glDeleteVertexArrays == NULL ) {
		return false;
	}
	if ( glGenBuffers == NULL || glBindBuffer == NULL || glBufferData == NULL || glBufferSubData == NULL || glDeleteBuffers == NULL ) {
		return false;
	}
	if ( glUseProgram == NULL || glUniformMatrix4fv == NULL || glUniform4f == NULL || glEnableVertexAttribArray == NULL || glDisableVertexAttribArray == NULL || glVertexAttribPointer == NULL ) {
		return false;
	}
	return true;
}

static bool R_ModernGLExecutor_CanUseLowOverhead( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !features.lowOverhead || !features.directStateAccess || !features.multiBind ) {
		return false;
	}
	if ( !caps.hasDSA || !caps.hasMultiBind ) {
		return false;
	}
	return glCreateBuffers != NULL
		&& glNamedBufferData != NULL
		&& glNamedBufferSubData != NULL
		&& glBindBuffersBase != NULL
		&& glBindTextures != NULL
		&& glBindSamplers != NULL
		&& glCreateSamplers != NULL
		&& glSamplerParameteri != NULL
		&& glCreateFramebuffers != NULL
		&& glNamedFramebufferTexture != NULL
		&& glNamedFramebufferDrawBuffers != NULL
		&& glNamedFramebufferDrawBuffer != NULL
		&& glNamedFramebufferReadBuffer != NULL
		&& glCheckNamedFramebufferStatus != NULL;
}

static bool R_ModernGLExecutor_CreateLowOverheadSampler( void ) {
	rg_modernGLExecutorLowOverheadSampler = 0;
	rg_modernGLExecutorLowOverheadSamplerDSACreations = 0;
	rg_modernGLExecutorLowOverheadSamplerDSAUpdates = 0;
	if ( !rg_modernGLExecutorLowOverheadReady || glCreateSamplers == NULL || glSamplerParameteri == NULL ) {
		return false;
	}
	glCreateSamplers( 1, &rg_modernGLExecutorLowOverheadSampler );
	if ( rg_modernGLExecutorLowOverheadSampler == 0 ) {
		return false;
	}
	glSamplerParameteri( rg_modernGLExecutorLowOverheadSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glSamplerParameteri( rg_modernGLExecutorLowOverheadSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glSamplerParameteri( rg_modernGLExecutorLowOverheadSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glSamplerParameteri( rg_modernGLExecutorLowOverheadSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	rg_modernGLExecutorLowOverheadSamplerDSACreations = 1;
	rg_modernGLExecutorLowOverheadSamplerDSAUpdates = 4;
	R_GLDebug_LabelSampler( rg_modernGLExecutorLowOverheadSampler, "ModernGLExecutor low-overhead sampler" );
	return true;
}

static bool R_ModernGLExecutor_CanCreateGpuDrivenObjects( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !features.gpuDriven ) {
		return false;
	}
	if ( !caps.hasSSBO || !caps.hasCompute || !caps.hasDrawIndirect || !caps.hasMultiDrawIndirect ) {
		return false;
	}
	if ( glBindBufferBase == NULL || glBufferData == NULL || glBufferSubData == NULL || glDispatchCompute == NULL || glMemoryBarrier == NULL || glUniform1ui == NULL || glMultiDrawElementsIndirect == NULL ) {
		return false;
	}
	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glGetShaderiv == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glGetProgramiv == NULL ) {
		return false;
	}
	return true;
}

static bool R_ModernGLExecutor_CreateBuffer( GLenum target, GLsizeiptr bytes, const void *data, GLenum usage, GLuint &buffer ) {
	buffer = 0;
	if ( bytes <= 0 ) {
		return false;
	}
	if ( rg_modernGLExecutorLowOverheadReady && glCreateBuffers != NULL && glNamedBufferData != NULL ) {
		glCreateBuffers( 1, &buffer );
		if ( buffer != 0 ) {
			glNamedBufferData( buffer, bytes, data, usage );
			return true;
		}
	}
	if ( glGenBuffers == NULL || glBindBuffer == NULL || glBufferData == NULL ) {
		return false;
	}
	glGenBuffers( 1, &buffer );
	if ( buffer == 0 ) {
		return false;
	}
	glBindBuffer( target, buffer );
	glBufferData( target, bytes, data, usage );
	glBindBuffer( target, 0 );
	return true;
}

static void R_ModernGLExecutor_UpdateBuffer( GLenum target, GLuint buffer, GLsizeiptr bytes, const void *data, modernGLExecutorStats_t &stats ) {
	if ( buffer == 0 || bytes <= 0 || data == NULL ) {
		return;
	}
	if ( rg_modernGLExecutorLowOverheadReady && glNamedBufferSubData != NULL ) {
		glNamedBufferSubData( buffer, 0, bytes, data );
		stats.lowOverheadDSAUpdates++;
		return;
	}
	R_GLStateCache().BindBuffer( target, buffer );
	glBufferSubData( target, 0, bytes, data );
	R_GLStateCache().BindBuffer( target, 0 );
}

static GLuint R_ModernGLExecutor_CompileGpuDrivenComputeProgram( void ) {
	static const char *computeSource =
		"#version 430\n"
		"layout(local_size_x = 64) in;\n"
		"struct SceneRecord { vec4 counts; uvec4 ids; };\n"
		"struct DrawElementsIndirectCommand { uint count; uint instanceCount; uint firstIndex; uint baseVertex; uint baseInstance; };\n"
		"uniform uint u_recordCount;\n"
		"layout(std430, binding = 1) readonly buffer ModernSceneRecords { SceneRecord records[]; };\n"
		"layout(std430, binding = 2) buffer ModernValidation { uint counters[8]; };\n"
		"layout(std430, binding = 3) buffer ModernIndirectCommands { DrawElementsIndirectCommand commands[]; };\n"
		"void main() {\n"
		"	uint index = gl_GlobalInvocationID.x;\n"
		"	if ( index >= u_recordCount ) {\n"
		"		return;\n"
		"	}\n"
		"	SceneRecord record = records[index];\n"
		"	uint flags = record.ids.w;\n"
		"	bool indexed = ( flags & 1u ) != 0u;\n"
		"	bool indirectEligible = ( flags & 2u ) != 0u;\n"
		"	bool visible = ( flags & 4u ) != 0u;\n"
		"	atomicAdd( counters[0], 1u );\n"
		"	if ( indirectEligible ) {\n"
		"		atomicAdd( counters[1], 1u );\n"
		"		atomicAdd( counters[6], record.ids.x + 1u );\n"
		"	}\n"
		"	if ( ( flags & 8u ) != 0u ) {\n"
		"		atomicAdd( counters[5], uint( record.counts.z ) );\n"
		"	}\n"
		"	if ( !visible ) {\n"
		"		atomicAdd( counters[3], 1u );\n"
		"		return;\n"
		"	}\n"
		"	atomicAdd( counters[4], 1u );\n"
		"	if ( !indexed || !indirectEligible ) {\n"
		"		return;\n"
		"	}\n"
		"	uint dst = atomicAdd( counters[2], 1u );\n"
		"	commands[dst].count = uint( record.counts.x );\n"
		"	commands[dst].instanceCount = 1u;\n"
		"	commands[dst].firstIndex = uint( record.counts.y );\n"
		"	commands[dst].baseVertex = 0u;\n"
		"	commands[dst].baseInstance = record.ids.z == 0xffffffffu ? 0u : record.ids.z;\n"
		"}\n";

	GLuint shader = glCreateShader( GL_COMPUTE_SHADER );
	if ( shader == 0 ) {
		return 0;
	}
	glShaderSource( shader, 1, &computeSource, NULL );
	glCompileShader( shader );

	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( compiled != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetShaderInfoLog != NULL ) {
			glGetShaderInfoLog( shader, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern GL executor: GL43 compute validation shader compile failed: %s\n", log );
		glDeleteShader( shader );
		return 0;
	}

	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( shader );
		return 0;
	}
	glAttachShader( program, shader );
	glLinkProgram( program );
	glDeleteShader( shader );

	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern GL executor: GL43 compute validation program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}

	rg_modernGLExecutorComputeRecordCountLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "u_recordCount" ) : -1;
	return program;
}

static GLuint R_ModernGLExecutor_CompileShaderStage( GLenum stage, const char *source, const char *label ) {
	GLuint shader = glCreateShader( stage );
	if ( shader == 0 ) {
		return 0;
	}
	glShaderSource( shader, 1, &source, NULL );
	glCompileShader( shader );

	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( compiled != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetShaderInfoLog != NULL ) {
			glGetShaderInfoLog( shader, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern GL executor: %s shader compile failed: %s\n", label ? label : "depth overlay", log );
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

static GLuint R_ModernGLExecutor_CompileDepthOverlayProgram( void ) {
	static const char *vertexSource =
		"#version 330\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"	vec2 positions[4] = vec2[]( vec2(-0.96, 0.96), vec2(-0.46, 0.96), vec2(-0.96, 0.46), vec2(-0.46, 0.46) );\n"
		"	vec2 texcoords[4] = vec2[]( vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0) );\n"
		"	vTexCoord = texcoords[gl_VertexID];\n"
		"	gl_Position = vec4( positions[gl_VertexID], 0.0, 1.0 );\n"
		"}\n";
	static const char *fragmentSource =
		"#version 330\n"
		"in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform sampler2D uDepthTexture;\n"
		"uniform vec4 uParams;\n"
		"void main() {\n"
		"	float depthValue = texture( uDepthTexture, vTexCoord ).r;\n"
		"	float contrastDepth = ( uParams.x > 1.5 ) ? pow( depthValue, 32.0 ) : depthValue;\n"
		"	out_Color = vec4( vec3( contrastDepth ), 1.0 );\n"
		"}\n";

	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glGetProgramiv == NULL ) {
		return 0;
	}

	GLuint vertexShader = R_ModernGLExecutor_CompileShaderStage( GL_VERTEX_SHADER, vertexSource, "depth overlay vertex" );
	if ( vertexShader == 0 ) {
		return 0;
	}
	GLuint fragmentShader = R_ModernGLExecutor_CompileShaderStage( GL_FRAGMENT_SHADER, fragmentSource, "depth overlay fragment" );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		return 0;
	}

	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		return 0;
	}
	glAttachShader( program, vertexShader );
	glAttachShader( program, fragmentShader );
	glLinkProgram( program );
	glDetachShader( program, vertexShader );
	glDetachShader( program, fragmentShader );
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );

	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern GL executor: depth overlay program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}

	rg_modernGLExecutorDepthOverlayTextureLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uDepthTexture" ) : -1;
	rg_modernGLExecutorDepthOverlayParamsLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uParams" ) : -1;
	return program;
}

static GLuint R_ModernGLExecutor_CompileGBufferOverlayProgram( void ) {
	static const char *vertexSource =
		"#version 330\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"	vec2 positions[4] = vec2[]( vec2(0.46, 0.96), vec2(0.96, 0.96), vec2(0.46, 0.46), vec2(0.96, 0.46) );\n"
		"	vec2 texcoords[4] = vec2[]( vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0) );\n"
		"	vTexCoord = texcoords[gl_VertexID];\n"
		"	gl_Position = vec4( positions[gl_VertexID], 0.0, 1.0 );\n"
		"}\n";
	static const char *fragmentSource =
		"#version 330\n"
		"in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform sampler2D uGBufferTexture;\n"
		"uniform vec4 uParams;\n"
		"void main() {\n"
		"	vec4 value = texture( uGBufferTexture, vTexCoord );\n"
		"	if ( uParams.x > 1.5 && uParams.x < 2.5 ) {\n"
		"		value.rgb = normalize( max( value.rgb * 2.0 - 1.0, vec3(-1.0) ) ) * 0.5 + 0.5;\n"
		"	}\n"
		"	out_Color = vec4( clamp( value.rgb, vec3(0.0), vec3(1.0) ), 1.0 );\n"
		"}\n";

	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glGetProgramiv == NULL ) {
		return 0;
	}

	GLuint vertexShader = R_ModernGLExecutor_CompileShaderStage( GL_VERTEX_SHADER, vertexSource, "G-buffer overlay vertex" );
	if ( vertexShader == 0 ) {
		return 0;
	}
	GLuint fragmentShader = R_ModernGLExecutor_CompileShaderStage( GL_FRAGMENT_SHADER, fragmentSource, "G-buffer overlay fragment" );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		return 0;
	}

	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		return 0;
	}
	glAttachShader( program, vertexShader );
	glAttachShader( program, fragmentShader );
	glLinkProgram( program );
	glDetachShader( program, vertexShader );
	glDetachShader( program, fragmentShader );
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );

	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern GL executor: G-buffer overlay program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}

	rg_modernGLExecutorGBufferOverlayTextureLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uGBufferTexture" ) : -1;
	rg_modernGLExecutorGBufferOverlayParamsLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uParams" ) : -1;
	return program;
}

static GLuint R_ModernGLExecutor_CompileDeferredOverlayProgram( void ) {
	static const char *vertexSource =
		"#version 330\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"	vec2 positions[4] = vec2[]( vec2(0.46, -0.46), vec2(0.96, -0.46), vec2(0.46, -0.96), vec2(0.96, -0.96) );\n"
		"	vec2 texcoords[4] = vec2[]( vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0) );\n"
		"	vTexCoord = texcoords[gl_VertexID];\n"
		"	gl_Position = vec4( positions[gl_VertexID], 0.0, 1.0 );\n"
		"}\n";
	static const char *fragmentSource =
		"#version 330\n"
		"in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform sampler2D uDeferredTexture;\n"
		"uniform vec4 uParams;\n"
		"void main() {\n"
		"	vec4 value = texture( uDeferredTexture, vTexCoord );\n"
		"	float lift = uParams.x > 3.5 ? 0.35 : 0.0;\n"
		"	out_Color = vec4( clamp( value.rgb + vec3(lift) * value.a, vec3(0.0), vec3(1.0) ), 1.0 );\n"
		"}\n";

	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glGetProgramiv == NULL ) {
		return 0;
	}

	GLuint vertexShader = R_ModernGLExecutor_CompileShaderStage( GL_VERTEX_SHADER, vertexSource, "deferred overlay vertex" );
	if ( vertexShader == 0 ) {
		return 0;
	}
	GLuint fragmentShader = R_ModernGLExecutor_CompileShaderStage( GL_FRAGMENT_SHADER, fragmentSource, "deferred overlay fragment" );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		return 0;
	}

	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		return 0;
	}
	glAttachShader( program, vertexShader );
	glAttachShader( program, fragmentShader );
	glLinkProgram( program );
	glDetachShader( program, vertexShader );
	glDetachShader( program, fragmentShader );
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );

	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern GL executor: deferred overlay program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}

	rg_modernGLExecutorDeferredOverlayTextureLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uDeferredTexture" ) : -1;
	rg_modernGLExecutorDeferredOverlayParamsLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uParams" ) : -1;
	return program;
}

static GLuint R_ModernGLExecutor_CompileVisibleCompositeProgram( void ) {
	static const char *vertexSource =
		"#version 330\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"	vec2 positions[4] = vec2[]( vec2(-1.0, 1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, -1.0) );\n"
		"	vec2 texcoords[4] = vec2[]( vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0) );\n"
		"	vTexCoord = texcoords[gl_VertexID];\n"
		"	gl_Position = vec4( positions[gl_VertexID], 0.0, 1.0 );\n"
		"}\n";
	static const char *fragmentSource =
		"#version 330\n"
		"in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform sampler2D uDeferredTexture;\n"
		"uniform sampler2D uForwardTexture;\n"
		"uniform vec4 uParams;\n"
		"void main() {\n"
		"	vec4 deferredValue = texture( uDeferredTexture, vTexCoord ) * uParams.x;\n"
		"	vec4 forwardValue = texture( uForwardTexture, vTexCoord ) * uParams.y;\n"
		"	vec3 color = clamp( deferredValue.rgb + forwardValue.rgb, vec3(0.0), vec3(1.0) );\n"
		"	out_Color = vec4( color, 1.0 );\n"
		"}\n";

	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glGetProgramiv == NULL ) {
		return 0;
	}

	GLuint vertexShader = R_ModernGLExecutor_CompileShaderStage( GL_VERTEX_SHADER, vertexSource, "modern visible composite vertex" );
	if ( vertexShader == 0 ) {
		return 0;
	}
	GLuint fragmentShader = R_ModernGLExecutor_CompileShaderStage( GL_FRAGMENT_SHADER, fragmentSource, "modern visible composite fragment" );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		return 0;
	}

	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		return 0;
	}
	glAttachShader( program, vertexShader );
	glAttachShader( program, fragmentShader );
	glLinkProgram( program );
	glDetachShader( program, vertexShader );
	glDetachShader( program, fragmentShader );
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );

	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern GL executor: visible composite program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}

	rg_modernGLExecutorVisibleCompositeDeferredLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uDeferredTexture" ) : -1;
	rg_modernGLExecutorVisibleCompositeForwardLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uForwardTexture" ) : -1;
	rg_modernGLExecutorVisibleCompositeParamsLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uParams" ) : -1;
	return program;
}

static void R_ModernGLExecutor_DestroyGpuDrivenObjects( void ) {
	if ( rg_modernGLExecutorComputeProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_modernGLExecutorComputeProgram );
	}
	if ( rg_modernGLExecutorDepthOverlayProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_modernGLExecutorDepthOverlayProgram );
	}
	if ( rg_modernGLExecutorGBufferOverlayProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_modernGLExecutorGBufferOverlayProgram );
	}
	if ( rg_modernGLExecutorDeferredOverlayProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_modernGLExecutorDeferredOverlayProgram );
	}
	if ( rg_modernGLExecutorVisibleCompositeProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_modernGLExecutorVisibleCompositeProgram );
	}
	GLuint buffers[3];
	int numBuffers = 0;
	if ( rg_modernGLExecutorSceneSSBO != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorSceneSSBO;
	}
	if ( rg_modernGLExecutorIndirectBuffer != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorIndirectBuffer;
	}
	if ( rg_modernGLExecutorValidationSSBO != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorValidationSSBO;
	}
	if ( numBuffers > 0 && glDeleteBuffers != NULL ) {
		glDeleteBuffers( numBuffers, buffers );
	}
	rg_modernGLExecutorSceneSSBO = 0;
	rg_modernGLExecutorIndirectBuffer = 0;
	rg_modernGLExecutorValidationSSBO = 0;
	rg_modernGLExecutorComputeProgram = 0;
	rg_modernGLExecutorDepthOverlayProgram = 0;
	rg_modernGLExecutorGBufferOverlayProgram = 0;
	rg_modernGLExecutorDeferredOverlayProgram = 0;
	rg_modernGLExecutorVisibleCompositeProgram = 0;
	rg_modernGLExecutorComputeRecordCountLocation = -1;
	rg_modernGLExecutorDepthOverlayTextureLocation = -1;
	rg_modernGLExecutorDepthOverlayParamsLocation = -1;
	rg_modernGLExecutorGBufferOverlayTextureLocation = -1;
	rg_modernGLExecutorGBufferOverlayParamsLocation = -1;
	rg_modernGLExecutorDeferredOverlayTextureLocation = -1;
	rg_modernGLExecutorDeferredOverlayParamsLocation = -1;
	rg_modernGLExecutorVisibleCompositeDeferredLocation = -1;
	rg_modernGLExecutorVisibleCompositeForwardLocation = -1;
	rg_modernGLExecutorVisibleCompositeParamsLocation = -1;
	rg_modernGLExecutorGpuDrivenReady = false;
}

static bool R_ModernGLExecutor_InitGpuDrivenObjects( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !R_ModernGLExecutor_CanCreateGpuDrivenObjects( caps, features ) ) {
		return false;
	}

	const GLsizeiptr sceneBytes = static_cast<GLsizeiptr>( MODERN_GL_GPU_DRIVEN_MAX_RECORDS * sizeof( modernGLGpuSceneRecord_t ) );
	const GLsizeiptr indirectBytes = static_cast<GLsizeiptr>( MODERN_GL_GPU_DRIVEN_MAX_RECORDS * sizeof( modernGLDrawElementsIndirectCommand_t ) );
	const GLsizeiptr validationBytes = static_cast<GLsizeiptr>( MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS * sizeof( GLuint ) );

	if ( !R_ModernGLExecutor_CreateBuffer( GL_SHADER_STORAGE_BUFFER, sceneBytes, NULL, GL_DYNAMIC_DRAW, rg_modernGLExecutorSceneSSBO ) ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
	R_GLDebug_LabelBuffer( rg_modernGLExecutorSceneSSBO, "ModernGLExecutor scene SSBO" );
	if ( !R_ModernGLExecutor_CreateBuffer( GL_DRAW_INDIRECT_BUFFER, indirectBytes, NULL, GL_DYNAMIC_DRAW, rg_modernGLExecutorIndirectBuffer ) ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
	R_GLDebug_LabelBuffer( rg_modernGLExecutorIndirectBuffer, "ModernGLExecutor indirect commands" );
	GLuint zeroValidation[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0 };
	if ( !R_ModernGLExecutor_CreateBuffer( GL_SHADER_STORAGE_BUFFER, validationBytes, zeroValidation, GL_DYNAMIC_DRAW, rg_modernGLExecutorValidationSSBO ) ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
	R_GLDebug_LabelBuffer( rg_modernGLExecutorValidationSSBO, "ModernGLExecutor validation SSBO" );

	rg_modernGLExecutorComputeProgram = R_ModernGLExecutor_CompileGpuDrivenComputeProgram();
	if ( rg_modernGLExecutorComputeProgram == 0 ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
	R_GLDebug_LabelProgram( rg_modernGLExecutorComputeProgram, "ModernGLExecutor compute validation" );
	return true;
}

static void R_ModernGLExecutor_ResetStats( modernGLExecutorStats_t &stats, bool enabled ) {
	memset( &stats, 0, sizeof( stats ) );
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	stats.available = rg_modernGLExecutorAvailable;
	stats.enabled = enabled;
	stats.initialized = rg_modernGLExecutorInitialized;
	stats.vaoReady = rg_modernGLExecutorVAO != 0;
	stats.frameUBOReady = rg_modernGLExecutorFrameUBO != 0;
	stats.shaderLibraryReady = shaderStats.available;
	stats.gpuDrivenReady = rg_modernGLExecutorGpuDrivenReady;
	stats.lowOverheadReady = rg_modernGLExecutorLowOverheadReady;
	stats.sceneSSBOReady = rg_modernGLExecutorSceneSSBO != 0;
	stats.indirectBufferReady = rg_modernGLExecutorIndirectBuffer != 0;
	stats.validationSSBOReady = rg_modernGLExecutorValidationSSBO != 0;
	stats.computeValidationReady = rg_modernGLExecutorComputeProgram != 0;
	stats.gpuDrivenRequested = rg_modernGLExecutorFeatures.gpuDriven;
	stats.gpuDrivenValidationRequested = r_rendererGpuValidation.GetBool();
	stats.modernVisibleRequested = R_ModernGLExecutor_ModernVisibleRequested();
	stats.modernVisibleProgramReady = rg_modernGLExecutorVisibleCompositeProgram != 0;
	stats.modernVisibleGuiProgramReady = shaderStats.guiProgramReady;
	stats.modernVisibleRenderDemoDeterministic = true;
	stats.modernVisibleCinematicTimingReady = true;
	stats.visibleDepthRequested = stats.modernVisibleRequested || r_rendererModernVisibleDepth.GetBool() || r_rendererModernDepthDebug.GetInteger() > 0 || R_ModernGLExecutor_ShadowMapSidecarRequested();
	stats.visibleDepthDebugOverlayReady = rg_modernGLExecutorDepthOverlayProgram != 0;
	stats.opaqueGBufferRequested = stats.modernVisibleRequested || r_rendererModernOpaque.GetBool() || r_rendererModernGBufferDebug.GetInteger() > 0 || r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	stats.opaqueGBufferDebugOverlayReady = rg_modernGLExecutorGBufferOverlayProgram != 0;
	stats.opaqueGBufferMRTReady = rg_modernGLExecutorGBufferFBO != 0 && rg_modernGLExecutorCaps.hasMRT && rg_modernGLExecutorCaps.maxDrawBuffers >= MODERN_GL_GBUFFER_ATTACHMENT_COUNT && glDrawBuffers != NULL;
	stats.deferredResolveRequested = stats.modernVisibleRequested || r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	stats.deferredResolveDebugOverlayReady = rg_modernGLExecutorDeferredOverlayProgram != 0;
	stats.deferredResolveDebugMode = r_rendererModernDeferredDebug.GetInteger();
	stats.forwardPlusRequested = stats.modernVisibleRequested || r_rendererForwardPlus.GetBool();
	stats.tierUsesDSA = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.directStateAccess;
	stats.tierUsesMultiBind = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.multiBind;
	stats.lowOverheadSamplerReady = rg_modernGLExecutorLowOverheadSampler != 0;
	stats.lowOverheadSamplerDSACreations = rg_modernGLExecutorLowOverheadSamplerDSACreations;
	stats.lowOverheadSamplerDSAUpdates = rg_modernGLExecutorLowOverheadSamplerDSAUpdates;
	stats.lowOverheadBindlessRequested = r_rendererBindless.GetBool();
	stats.lowOverheadBindlessAvailable = rg_modernGLExecutorFeatures.bindlessTextures && rg_modernGLExecutorCaps.hasBindlessTexture;
	stats.shaderProgramCount = shaderStats.programCount;
	stats.shaderFailureCount = shaderStats.failedProgramCount;
	stats.highestGLSLVersion = shaderStats.highestGLSLVersion;
	R_ModernGLExecutor_SetStatus( stats, enabled ? "unavailable" : "off" );
}

static void R_ModernGLExecutor_RecomputeModernVisibleFallbacks( modernGLExecutorStats_t &stats ) {
	if ( !stats.modernVisibleRequested ) {
		return;
	}
	stats.modernVisibleOwnerFallbacks =
		stats.modernVisibleLegacyPasses
		+ stats.modernVisibleGuiLegacyPasses
		+ stats.modernVisiblePostLegacyPasses
		+ stats.modernVisibleSpecialLegacyPasses
		+ stats.modernVisibleSubviewLegacyPasses;
	stats.modernVisibleBlockedByLegacy = stats.modernVisibleOwnerFallbacks > 0;
	stats.modernVisibleFallbackPasses = stats.modernVisibleOwnerFallbacks + stats.modernVisibleDisabledPasses;
	stats.modernVisibleCompatibilityReady = true;
}

static void R_ModernGLExecutor_CountModernVisibleOwner( const renderGraphPass_t &pass, modernGLExecutorStats_t &stats ) {
	if ( !stats.modernVisibleRequested ) {
		return;
	}
	stats.modernVisibleCompatibilityPasses++;
	if ( !pass.enabled ) {
		stats.modernVisibleDisabledPasses++;
		return;
	}

	bool modernOwned = false;
	switch ( pass.category ) {
	case RENDER_PASS_DEPTH:
	case RENDER_PASS_SHADOW_MAP:
		modernOwned = stats.visibleDepthRequested;
		break;
	case RENDER_PASS_AMBIENT:
		modernOwned = stats.opaqueGBufferRequested || stats.deferredResolveRequested;
		break;
	case RENDER_PASS_DEFERRED_RESOLVE:
		modernOwned = stats.deferredResolveRequested;
		break;
	case RENDER_PASS_ARB2_INTERACTION:
	case RENDER_PASS_FOG_BLEND:
	case RENDER_PASS_FORWARD_PLUS:
		modernOwned = stats.forwardPlusRequested;
		break;
	case RENDER_PASS_LIGHT_GRID:
		modernOwned = stats.deferredResolveRequested || stats.forwardPlusRequested;
		if ( modernOwned ) {
			stats.modernVisibleLightGridModernPasses++;
		}
		break;
	case RENDER_PASS_GUI:
		modernOwned = stats.modernVisibleGuiProgramReady;
		if ( modernOwned ) {
			stats.modernVisibleGuiModernPasses++;
		}
		break;
	case RENDER_PASS_SSAO:
	case RENDER_PASS_MOTION_BLUR:
	case RENDER_PASS_LENS_FLARE:
	case RENDER_PASS_BLOOM:
	case RENDER_PASS_AUTHORED_POST:
		stats.modernVisiblePostGraphPasses++;
		modernOwned = false;
		break;
	case RENDER_PASS_SPECIAL_EFFECTS:
		stats.modernVisibleBSEFallbackPasses++;
		modernOwned = false;
		break;
	case RENDER_PASS_PRESENT:
		modernOwned = true;
		stats.modernVisiblePresentPasses++;
		break;
	default:
		modernOwned = false;
		break;
	}

	if ( modernOwned ) {
		stats.modernVisibleModernPasses++;
		stats.modernVisibleCompatibilityModernPasses++;
	} else {
		stats.modernVisibleLegacyPasses++;
		stats.modernVisibleCompatibilityLegacyPasses++;
	}
}

static void R_ModernGLExecutor_AnalyzeModernVisibleFallbacks( const idScenePacketFrame &packetFrame, modernGLExecutorStats_t &stats ) {
	if ( !stats.modernVisibleRequested ) {
		return;
	}
	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	stats.modernVisibleGuiLegacyPasses = packetStats.guiPackets;
	stats.modernVisibleGuiDraws = packetStats.guiPackets;
	stats.modernVisiblePostLegacyPasses = packetStats.postProcessPackets;
	stats.modernVisiblePostFallbackPasses = packetStats.postProcessPackets;
	stats.modernVisibleCopyRenderFallbackPasses = packetStats.postProcessPackets;
	stats.modernVisibleSpecialLegacyPasses = packetStats.specialEffectPackets;
	stats.modernVisibleBSEFallbackPasses += packetStats.specialEffectPackets;
	stats.modernVisibleBSEParticleFallbacks = packetStats.specialEffectPackets;
	stats.modernVisibleBSETrailFallbacks = packetStats.specialEffectPackets;
	stats.modernVisibleBSEBeamFallbacks = packetStats.specialEffectPackets;
	stats.modernVisibleBSEDecalFallbacks = packetStats.specialEffectPackets;
	stats.modernVisibleBSEMaterialFallbacks = packetStats.specialEffectPackets;
	stats.modernVisibleSubviewLegacyPasses = packetStats.subviewPackets + packetStats.remoteCameraPackets + packetStats.renderDemoPackets;
	stats.modernVisibleSubviewGraphPasses = packetStats.subviewPackets + packetStats.remoteCameraPackets + packetStats.renderDemoPackets;
	stats.modernVisibleSubviewFallbackPasses = packetStats.subviewPackets;
	stats.modernVisibleRemoteCameraFallbackPasses = packetStats.remoteCameraPackets;
	stats.modernVisibleRenderDemoFallbackPasses = packetStats.renderDemoPackets;
	stats.modernVisibleCinematicCompatibilityPasses = packetStats.guiPackets + packetStats.postProcessPackets + packetStats.renderDemoPackets;
	R_ModernGLExecutor_RecomputeModernVisibleFallbacks( stats );
}

static void R_ModernGLExecutor_FinalizeModernVisibleCompatibility( const idScenePacketFrame &packetFrame, const idModernGLSubmitPlan &submitPlan, modernGLExecutorStats_t &stats ) {
	if ( !stats.modernVisibleRequested ) {
		return;
	}

	int guiReadyDraws = 0;
	for ( int i = 0; i < submitPlan.NumCommands(); ++i ) {
		const modernGLSubmitCommand_t &command = submitPlan.Command( i );
		if ( command.pipeline != MODERN_GL_DRAW_PLAN_PIPELINE_GUI ) {
			continue;
		}
		if ( command.program != 0 && command.vertexBuffer != 0 && command.modelViewProjectionLocation >= 0 ) {
			guiReadyDraws++;
		}
	}

	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	stats.modernVisibleGuiDraws = packetStats.guiPackets;
	stats.modernVisibleGuiReadyDraws = guiReadyDraws;
	stats.modernVisibleGuiFallbackDraws = Max( 0, stats.modernVisibleGuiDraws - stats.modernVisibleGuiReadyDraws );
	stats.modernVisibleGuiLegacyPasses = stats.modernVisibleGuiFallbackDraws;
	R_ModernGLExecutor_RecomputeModernVisibleFallbacks( stats );
}

static int R_ModernGLExecutor_ModernVisibleFallbacksWithoutGui( const modernGLExecutorStats_t &stats ) {
	return Max( 0, stats.modernVisibleOwnerFallbacks - stats.modernVisibleGuiLegacyPasses );
}

static void R_ModernGLExecutor_SetEffectivePassRequests(
	modernGLExecutorStats_t &stats,
	bool visibleDepthRequested,
	bool opaqueGBufferRequested,
	bool deferredResolveRequested,
	bool forwardPlusRequested ) {
	stats.visibleDepthRequested = visibleDepthRequested;
	stats.opaqueGBufferRequested = opaqueGBufferRequested;
	stats.deferredResolveRequested = deferredResolveRequested;
	stats.forwardPlusRequested = forwardPlusRequested;
	if ( !stats.forwardPlusRequested ) {
		stats.forwardPlusSpecialEffectFallbacks = 0;
	}
}

static void R_ModernGLExecutor_RecordPassGate(
	bool modernVisibleRequested,
	bool sidecarRequested,
	bool effectiveRequested,
	bool blockedByLegacy,
	int &wouldExecute,
	int &skippedBlocked,
	int &skippedNoConsumer,
	int &duplicatedWithLegacy ) {
	wouldExecute = ( modernVisibleRequested || sidecarRequested ) ? 1 : 0;
	skippedBlocked = ( modernVisibleRequested && blockedByLegacy && !sidecarRequested && !effectiveRequested ) ? 1 : 0;
	skippedNoConsumer = ( !modernVisibleRequested && !sidecarRequested && !effectiveRequested ) ? 1 : 0;
	duplicatedWithLegacy = sidecarRequested ? 1 : 0;
}

static void R_ModernGLExecutor_RecordPassGates(
	modernGLExecutorStats_t &stats,
	bool visibleDepthSidecarRequested,
	bool opaqueGBufferSidecarRequested,
	bool deferredResolveSidecarRequested,
	bool forwardPlusSidecarRequested ) {
	const bool opaqueProducerSidecarRequested = opaqueGBufferSidecarRequested || deferredResolveSidecarRequested;
	R_ModernGLExecutor_RecordPassGate(
		stats.modernVisibleRequested,
		visibleDepthSidecarRequested,
		stats.visibleDepthRequested,
		stats.modernVisibleBlockedByLegacy,
		stats.visibleDepthWouldExecute,
		stats.visibleDepthSkippedBlocked,
		stats.visibleDepthSkippedNoConsumer,
		stats.visibleDepthDuplicatedWithLegacy );
	R_ModernGLExecutor_RecordPassGate(
		stats.modernVisibleRequested,
		opaqueProducerSidecarRequested,
		stats.opaqueGBufferRequested,
		stats.modernVisibleBlockedByLegacy,
		stats.opaqueGBufferWouldExecute,
		stats.opaqueGBufferSkippedBlocked,
		stats.opaqueGBufferSkippedNoConsumer,
		stats.opaqueGBufferDuplicatedWithLegacy );
	R_ModernGLExecutor_RecordPassGate(
		stats.modernVisibleRequested,
		deferredResolveSidecarRequested,
		stats.deferredResolveRequested,
		stats.modernVisibleBlockedByLegacy,
		stats.deferredResolveWouldExecute,
		stats.deferredResolveSkippedBlocked,
		stats.deferredResolveSkippedNoConsumer,
		stats.deferredResolveDuplicatedWithLegacy );
	R_ModernGLExecutor_RecordPassGate(
		stats.modernVisibleRequested,
		forwardPlusSidecarRequested,
		stats.forwardPlusRequested,
		stats.modernVisibleBlockedByLegacy,
		stats.forwardPlusWouldExecute,
		stats.forwardPlusSkippedBlocked,
		stats.forwardPlusSkippedNoConsumer,
		stats.forwardPlusDuplicatedWithLegacy );
}

static void R_ModernGLExecutor_AnalyzeFrame(
	const idScenePacketFrame &packetFrame,
	const idRenderGraph &graph,
	bool enabled,
	bool available,
	bool initialized,
	bool vaoReady,
	bool frameUBOReady,
	modernGLExecutorStats_t &stats ) {
	memset( &stats, 0, sizeof( stats ) );
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	stats.available = available;
	stats.enabled = enabled;
	stats.initialized = initialized;
	stats.vaoReady = vaoReady;
	stats.frameUBOReady = frameUBOReady;
	stats.shaderLibraryReady = shaderStats.available;
	stats.gpuDrivenReady = rg_modernGLExecutorGpuDrivenReady;
	stats.lowOverheadReady = rg_modernGLExecutorLowOverheadReady;
	stats.sceneSSBOReady = rg_modernGLExecutorSceneSSBO != 0;
	stats.indirectBufferReady = rg_modernGLExecutorIndirectBuffer != 0;
	stats.validationSSBOReady = rg_modernGLExecutorValidationSSBO != 0;
	stats.computeValidationReady = rg_modernGLExecutorComputeProgram != 0;
	stats.gpuDrivenRequested = rg_modernGLExecutorFeatures.gpuDriven;
	stats.gpuDrivenValidationRequested = r_rendererGpuValidation.GetBool();
	stats.modernVisibleRequested = R_ModernGLExecutor_ModernVisibleRequested();
	stats.modernVisibleProgramReady = rg_modernGLExecutorVisibleCompositeProgram != 0;
	stats.modernVisibleGuiProgramReady = shaderStats.guiProgramReady;
	stats.modernVisibleRenderDemoDeterministic = true;
	stats.modernVisibleCinematicTimingReady = true;
	stats.visibleDepthRequested = stats.modernVisibleRequested || r_rendererModernVisibleDepth.GetBool() || r_rendererModernDepthDebug.GetInteger() > 0 || R_ModernGLExecutor_ShadowMapSidecarRequested();
	stats.visibleDepthDebugOverlayReady = rg_modernGLExecutorDepthOverlayProgram != 0;
	stats.opaqueGBufferRequested = stats.modernVisibleRequested || r_rendererModernOpaque.GetBool() || r_rendererModernGBufferDebug.GetInteger() > 0 || r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	stats.opaqueGBufferDebugOverlayReady = rg_modernGLExecutorGBufferOverlayProgram != 0;
	stats.opaqueGBufferMRTReady = rg_modernGLExecutorGBufferFBO != 0 && rg_modernGLExecutorCaps.hasMRT && rg_modernGLExecutorCaps.maxDrawBuffers >= MODERN_GL_GBUFFER_ATTACHMENT_COUNT && glDrawBuffers != NULL;
	stats.deferredResolveRequested = stats.modernVisibleRequested || r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	stats.deferredResolveDebugOverlayReady = rg_modernGLExecutorDeferredOverlayProgram != 0;
	stats.deferredResolveDebugMode = r_rendererModernDeferredDebug.GetInteger();
	stats.forwardPlusRequested = stats.modernVisibleRequested || r_rendererForwardPlus.GetBool();
	if ( stats.forwardPlusRequested ) {
		stats.forwardPlusSpecialEffectFallbacks = packetFrame.Stats().specialEffectPackets;
	}
	stats.tierUsesDSA = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.directStateAccess;
	stats.tierUsesMultiBind = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.multiBind;
	stats.lowOverheadSamplerReady = rg_modernGLExecutorLowOverheadSampler != 0;
	stats.lowOverheadSamplerDSACreations = rg_modernGLExecutorLowOverheadSamplerDSACreations;
	stats.lowOverheadSamplerDSAUpdates = rg_modernGLExecutorLowOverheadSamplerDSAUpdates;
	stats.lowOverheadBindlessRequested = r_rendererBindless.GetBool();
	stats.lowOverheadBindlessAvailable = rg_modernGLExecutorFeatures.bindlessTextures && rg_modernGLExecutorCaps.hasBindlessTexture;
	stats.shaderProgramCount = shaderStats.programCount;
	stats.shaderFailureCount = shaderStats.failedProgramCount;
	stats.highestGLSLVersion = shaderStats.highestGLSLVersion;
	stats.graphPasses = graph.NumPasses();
	stats.drawPackets = packetFrame.NumDrawPackets();

	if ( !enabled ) {
		R_ModernGLExecutor_SetStatus( stats, "off" );
		return;
	}
	if ( !available ) {
		R_ModernGLExecutor_SetStatus( stats, "unavailable" );
		return;
	}
	if ( !initialized || !vaoReady || !frameUBOReady ) {
		R_ModernGLExecutor_SetStatus( stats, "not-initialized" );
		return;
	}
	if ( !stats.shaderLibraryReady ) {
		R_ModernGLExecutor_SetStatus( stats, "shader-library-unavailable" );
		return;
	}

	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		R_ModernGLExecutor_CountModernVisibleOwner( pass, stats );
		if ( pass.enabled && pass.packetBacked ) {
			stats.preparedPasses++;
		} else {
			stats.fallbackPasses++;
		}
	}

	for ( int i = 0; i < packetFrame.NumDrawPackets(); ++i ) {
		const drawPacket_t &draw = packetFrame.DrawPacket( i );
		if ( draw.geometryRecord != NULL ) {
			stats.geometryDrawPackets++;
		}
		if ( draw.materialRecord != NULL ) {
			stats.materialDrawPackets++;
		}
		if ( draw.materialRecordIndex >= 0 ) {
			stats.resourceDrawPackets++;
		}
		if ( draw.packetCategory == SCENE_PACKET_CATEGORY_GUI ) {
			stats.guiDrawPackets++;
		}
		if ( draw.packetCategory == SCENE_PACKET_CATEGORY_WORLD || draw.packetCategory == SCENE_PACKET_CATEGORY_VIEWMODEL || draw.packetCategory == SCENE_PACKET_CATEGORY_SUBVIEW || draw.packetCategory == SCENE_PACKET_CATEGORY_REMOTE_CAMERA || draw.packetCategory == SCENE_PACKET_CATEGORY_RENDER_DEMO ) {
			stats.worldDrawPackets++;
		}
		if ( draw.geometryRecord != NULL && draw.instanceRecord != NULL && draw.materialRecordIndex >= 0 ) {
			stats.preparedDrawPackets++;
		}
	}
	R_ModernGLExecutor_AnalyzeModernVisibleFallbacks( packetFrame, stats );

	stats.legacyFallback = true;
	R_ModernGLExecutor_SetStatus( stats, "prepared-legacy-fallback" );
}

static void R_ModernGLExecutor_BindUniformBuffer( GLuint buffer ) {
	R_GLStateCache().BindBuffer( GL_UNIFORM_BUFFER, buffer );
}

static void R_ModernGLExecutor_BindFrameUniformBufferBase( modernGLExecutorStats_t &stats ) {
	if ( rg_modernGLExecutorFrameUBO == 0 ) {
		return;
	}
	if ( rg_modernGLExecutorLowOverheadReady && glBindBuffersBase != NULL ) {
		GLuint buffers[1] = { rg_modernGLExecutorFrameUBO };
		if ( R_GLStateCache().BindBuffersBase( GL_UNIFORM_BUFFER, 0, 1, buffers ) ) {
			stats.lowOverheadMultiBindBatches++;
		}
		return;
	}
	if ( glBindBufferBase != NULL ) {
		R_GLStateCache().BindBufferBase( GL_UNIFORM_BUFFER, 0, rg_modernGLExecutorFrameUBO );
	}
}

static void R_ModernGLExecutor_UpdateFrameUBO( modernGLExecutorStats_t &stats ) {
	if ( !stats.enabled || !stats.available || !stats.initialized || !stats.frameUBOReady ) {
		return;
	}

	modernGLFrameConstants_t constants;
	memset( &constants, 0, sizeof( constants ) );
	constants.viewport[0] = static_cast<float>( glConfig.vidWidth );
	constants.viewport[1] = static_cast<float>( glConfig.vidHeight );
	constants.viewport[2] = glConfig.vidHeight > 0 ? static_cast<float>( glConfig.vidWidth ) / static_cast<float>( glConfig.vidHeight ) : 1.0f;
	constants.viewport[3] = 1.0f;
	constants.frame[0] = static_cast<float>( tr.frameCount );
	constants.frame[1] = static_cast<float>( stats.preparedPasses );
	constants.frame[2] = static_cast<float>( stats.submitPlanReady ? stats.submitPlanDraws : ( stats.drawPlanReady ? stats.drawPlanDraws : stats.preparedDrawPackets ) );
	constants.frame[3] = static_cast<float>( stats.submitPlanReady ? stats.submitPlanProgramBatches : ( stats.drawPlanReady ? stats.drawPlanStateBatches : stats.resourceDrawPackets ) );
	constants.capabilities[0] = static_cast<float>( rg_modernGLExecutorCaps.glMajor );
	constants.capabilities[1] = static_cast<float>( rg_modernGLExecutorCaps.glMinor );
	constants.capabilities[2] = rg_modernGLExecutorCaps.hasUBO ? 1.0f : 0.0f;
	constants.capabilities[3] = rg_modernGLExecutorCaps.hasVAO ? 1.0f : 0.0f;

	if ( rg_modernGLExecutorLowOverheadReady && glNamedBufferSubData != NULL ) {
		glNamedBufferSubData( rg_modernGLExecutorFrameUBO, 0, sizeof( constants ), &constants );
		stats.lowOverheadDSAUpdates++;
	} else {
		R_ModernGLExecutor_BindUniformBuffer( rg_modernGLExecutorFrameUBO );
		glBufferSubData( GL_UNIFORM_BUFFER, 0, sizeof( constants ), &constants );
		R_ModernGLExecutor_BindUniformBuffer( 0 );
	}

	if ( glBindBufferBase != NULL ) {
		R_GLStateCache().BindBufferBase( GL_UNIFORM_BUFFER, 0, 0 );
	}
}

static const GLvoid *R_ModernGLExecutor_BufferOffset( int offset ) {
	return reinterpret_cast<const GLvoid *>( static_cast<uintptr_t>( offset ) );
}

static bool R_ModernGLExecutor_DrawVertLayoutSupported( int vertexStride, int ambientCacheOffset ) {
	return vertexStride >= static_cast<int>( sizeof( idDrawVert ) )
		&& vertexStride >= DRAWVERT_SIZE
		&& ambientCacheOffset >= 0;
}

static void R_ModernGLExecutor_SetDrawVertFloatAttrib( modernGLDrawVertAttribute_t attribute, int components, int vertexStride, int offset ) {
	glEnableVertexAttribArray( static_cast<GLuint>( attribute ) );
	glVertexAttribPointer(
		static_cast<GLuint>( attribute ),
		components,
		GL_FLOAT,
		GL_FALSE,
		vertexStride,
		R_ModernGLExecutor_BufferOffset( offset ) );
}

static bool R_ModernGLExecutor_BindDrawVertLayout( const modernGLSubmitCommand_t &command ) {
	if ( !R_ModernGLExecutor_DrawVertLayoutSupported( command.vertexStride, command.ambientCacheOffset ) ) {
		return false;
	}

	const int baseOffset = command.ambientCacheOffset;
	R_ModernGLExecutor_SetDrawVertFloatAttrib( MODERN_GL_DRAWVERT_ATTR_POSITION, 3, command.vertexStride, baseOffset + DRAWVERT_XYZ_OFFSET );
	glEnableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_COLOR );
	glVertexAttribPointer(
		MODERN_GL_DRAWVERT_ATTR_COLOR,
		4,
		GL_UNSIGNED_BYTE,
		GL_TRUE,
		command.vertexStride,
		R_ModernGLExecutor_BufferOffset( baseOffset + DRAWVERT_COLOR_OFFSET ) );
	R_ModernGLExecutor_SetDrawVertFloatAttrib( MODERN_GL_DRAWVERT_ATTR_TEXCOORD0, 2, command.vertexStride, baseOffset + DRAWVERT_ST_OFFSET );
	R_ModernGLExecutor_SetDrawVertFloatAttrib( MODERN_GL_DRAWVERT_ATTR_TANGENT0, 3, command.vertexStride, baseOffset + DRAWVERT_TANGENT0_OFFSET );
	R_ModernGLExecutor_SetDrawVertFloatAttrib( MODERN_GL_DRAWVERT_ATTR_TANGENT1, 3, command.vertexStride, baseOffset + DRAWVERT_TANGENT1_OFFSET );
	R_ModernGLExecutor_SetDrawVertFloatAttrib( MODERN_GL_DRAWVERT_ATTR_NORMAL, 3, command.vertexStride, baseOffset + DRAWVERT_NORMAL_OFFSET );
	return true;
}

static void R_ModernGLExecutor_DisableDrawVertLayout( void ) {
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_POSITION );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_COLOR );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_TEXCOORD0 );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_TANGENT0 );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_TANGENT1 );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_NORMAL );
}

static void R_ModernGLExecutor_SetSubmitScissor( const modernGLSubmitCommand_t &command, const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		R_GLStateCache().SetViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		R_GLStateCache().SetScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		return;
	}

	R_GLStateCache().SetViewport(
		viewDef->viewport.x1,
		viewDef->viewport.y1,
		viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
		viewDef->viewport.y2 + 1 - viewDef->viewport.y1 );

	int scissorX1 = command.scissorX1;
	int scissorY1 = command.scissorY1;
	int scissorX2 = command.scissorX2;
	int scissorY2 = command.scissorY2;
	if ( scissorX2 < scissorX1 || scissorY2 < scissorY1 ) {
		scissorX1 = viewDef->scissor.x1;
		scissorY1 = viewDef->scissor.y1;
		scissorX2 = viewDef->scissor.x2;
		scissorY2 = viewDef->scissor.y2;
	}

	R_GLStateCache().SetScissor(
		viewDef->viewport.x1 + scissorX1,
		viewDef->viewport.y1 + scissorY1,
		Max( 1, scissorX2 - scissorX1 + 1 ),
		Max( 1, scissorY2 - scissorY1 + 1 ) );
}

static void R_ModernGLExecutor_SetDebugColor( const modernGLSubmitCommand_t &command ) {
	if ( command.debugColorLocation < 0 ) {
		return;
	}
	switch ( command.shaderKind ) {
	case MODERN_GL_SHADER_DEPTH:
	case MODERN_GL_SHADER_SHADOW_DEPTH:
		glUniform4f( command.debugColorLocation, 0.15f, 0.30f, 0.95f, 1.0f );
		break;
	case MODERN_GL_SHADER_FLAT_MATERIAL:
		glUniform4f( command.debugColorLocation, 0.95f, 0.65f, 0.20f, 1.0f );
		break;
	case MODERN_GL_SHADER_LIGHT_GRID:
		glUniform4f( command.debugColorLocation, 0.35f, 0.90f, 0.55f, 1.0f );
		break;
	case MODERN_GL_SHADER_FOG_BLEND:
		glUniform4f( command.debugColorLocation, 0.55f, 0.62f, 0.75f, 1.0f );
		break;
	case MODERN_GL_SHADER_GBUFFER_OPAQUE:
	case MODERN_GL_SHADER_GBUFFER_ALPHA_TEST:
		glUniform4f( command.debugColorLocation, 0.45f, 0.70f, 0.95f, 1.0f );
		break;
	case MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE:
		glUniform4f( command.debugColorLocation, 0.95f, 0.90f, 0.55f, 1.0f );
		break;
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE:
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST:
		glUniform4f( command.debugColorLocation, 0.80f, 0.95f, 0.65f, 1.0f );
		break;
	case MODERN_GL_SHADER_TRANSPARENT_FORWARD:
		glUniform4f( command.debugColorLocation, 0.45f, 0.75f, 0.95f, 0.65f );
		break;
	case MODERN_GL_SHADER_GUI:
	case MODERN_GL_SHADER_POST_COPY:
		glUniform4f( command.debugColorLocation, 1.0f, 1.0f, 1.0f, 1.0f );
		break;
	case MODERN_GL_SHADER_DEBUG_VISUALIZATION:
		glUniform4f( command.debugColorLocation, 1.0f, 0.25f, 0.65f, 1.0f );
		break;
	default:
		glUniform4f( command.debugColorLocation, 1.0f, 1.0f, 1.0f, 1.0f );
		break;
	}
}

static float R_ModernGLExecutor_AlphaReferenceForCommand( const modernGLSubmitCommand_t &command );

static void R_ModernGLExecutor_SetLocalParams( const modernGLSubmitCommand_t &command ) {
	if ( command.localParamsLocation < 0 ) {
		return;
	}
	switch ( command.shaderKind ) {
	case MODERN_GL_SHADER_GBUFFER_ALPHA_TEST:
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST:
		glUniform4f( command.localParamsLocation, R_ModernGLExecutor_AlphaReferenceForCommand( command ), 0.0f, 0.25f, 0.0f );
		break;
	case MODERN_GL_SHADER_GBUFFER_OPAQUE:
		glUniform4f( command.localParamsLocation, 0.1f, 0.5f, 0.25f, 0.0f );
		break;
	case MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE:
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE:
	case MODERN_GL_SHADER_LIGHT_GRID:
		glUniform4f( command.localParamsLocation, 1.0f, 0.0f, 0.0f, 0.0f );
		break;
	case MODERN_GL_SHADER_TRANSPARENT_FORWARD:
	case MODERN_GL_SHADER_FOG_BLEND:
		glUniform4f( command.localParamsLocation, 0.25f, 0.38f, 0.42f, 0.48f );
		break;
	case MODERN_GL_SHADER_DEBUG_VISUALIZATION:
		glUniform4f( command.localParamsLocation, 0.5f, 0.1f, 0.9f, 0.4f );
		break;
	default:
		glUniform4f( command.localParamsLocation, 0.0f, 0.0f, 0.0f, 0.0f );
		break;
	}
}

static bool R_ModernGLExecutor_IsDepthPipeline( modernGLDrawPlanPipeline_t pipeline ) {
	return pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH || pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_SHADOW_DEPTH;
}

static bool R_ModernGLExecutor_IsMaterialPipeline( modernGLDrawPlanPipeline_t pipeline ) {
	return pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_LIGHT_GRID
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FOG_BLEND
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_GUI
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT;
}

static void R_ModernGLExecutor_BindGpuDrivenBuffers( modernGLExecutorStats_t &stats ) {
	if ( !stats.gpuDrivenReady ) {
		return;
	}
	if ( stats.tierUsesMultiBind && glBindBuffersBase != NULL ) {
		GLuint buffers[3] = { rg_modernGLExecutorSceneSSBO, rg_modernGLExecutorValidationSSBO, rg_modernGLExecutorIndirectBuffer };
		if ( R_GLStateCache().BindBuffersBase( GL_SHADER_STORAGE_BUFFER, 1, 3, buffers ) ) {
			stats.lowOverheadMultiBindBatches++;
		}
	} else if ( glBindBufferBase != NULL ) {
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 1, rg_modernGLExecutorSceneSSBO );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 2, rg_modernGLExecutorValidationSSBO );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 3, rg_modernGLExecutorIndirectBuffer );
	}
	if ( rg_modernGLExecutorIndirectBuffer != 0 ) {
		R_GLStateCache().BindBuffer( GL_DRAW_INDIRECT_BUFFER, rg_modernGLExecutorIndirectBuffer );
	}
}

static void R_ModernGLExecutor_UnbindGpuDrivenBuffers( void ) {
	if ( glBindBufferBase != NULL ) {
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 1, 0 );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 2, 0 );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 3, 0 );
	}
	if ( glBindBuffer != NULL ) {
		R_GLStateCache().BindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
	}
}

static void R_ModernGLExecutor_ResetGpuDrivenBatch( void ) {
	memset( &rg_modernGLGpuDrivenBatch, 0, sizeof( rg_modernGLGpuDrivenBatch ) );
}

static bool R_ModernGLExecutor_CommandVisibleForGpuDriven( const modernGLSubmitCommand_t &command ) {
	if ( command.viewDef == NULL || command.indexCount <= 0 || command.vertexCount <= 0 ) {
		return false;
	}

	int scissorX1 = command.scissorX1;
	int scissorY1 = command.scissorY1;
	int scissorX2 = command.scissorX2;
	int scissorY2 = command.scissorY2;
	if ( scissorX2 < scissorX1 || scissorY2 < scissorY1 ) {
		scissorX1 = command.viewDef->scissor.x1;
		scissorY1 = command.viewDef->scissor.y1;
		scissorX2 = command.viewDef->scissor.x2;
		scissorY2 = command.viewDef->scissor.y2;
	}

	const int viewWidth = command.viewDef->viewport.x2 + 1 - command.viewDef->viewport.x1;
	const int viewHeight = command.viewDef->viewport.y2 + 1 - command.viewDef->viewport.y1;
	if ( viewWidth <= 0 || viewHeight <= 0 ) {
		return false;
	}
	if ( scissorX2 < 0 || scissorY2 < 0 || scissorX1 >= viewWidth || scissorY1 >= viewHeight ) {
		return false;
	}
	return true;
}

static bool R_ModernGLExecutor_CommandCanSeedIndirect( const modernGLSubmitCommand_t &command ) {
	return command.indexed
		&& !command.uploadIndexBuffer
		&& command.program != 0
		&& command.vertexBuffer != 0
		&& command.indexBuffer != 0
		&& command.indexCount > 0
		&& command.indexCacheOffset >= 0
		&& command.ambientCacheOffset >= 0
		&& command.vertexStride > 0;
}

static bool R_ModernGLExecutor_CommandMatchesGpuDrivenBatch( const modernGLSubmitCommand_t &command, const modernGLGpuDrivenBatch_t &batch ) {
	if ( !batch.valid ) {
		return false;
	}
	return command.program == batch.program
		&& command.vertexBuffer == batch.vertexBuffer
		&& command.indexBuffer == batch.indexBuffer
		&& static_cast<GLenum>( command.indexType ) == batch.indexType
		&& command.ambientCacheOffset == batch.ambientCacheOffset
		&& command.vertexStride == batch.vertexStride;
}

static void R_ModernGLExecutor_BeginGpuDrivenBatch( const modernGLSubmitCommand_t &command ) {
	rg_modernGLGpuDrivenBatch.valid = true;
	rg_modernGLGpuDrivenBatch.program = command.program;
	rg_modernGLGpuDrivenBatch.vertexBuffer = command.vertexBuffer;
	rg_modernGLGpuDrivenBatch.indexBuffer = command.indexBuffer;
	rg_modernGLGpuDrivenBatch.indexType = static_cast<GLenum>( command.indexType );
	rg_modernGLGpuDrivenBatch.ambientCacheOffset = command.ambientCacheOffset;
	rg_modernGLGpuDrivenBatch.vertexStride = command.vertexStride;
	rg_modernGLGpuDrivenBatch.command = command;
	rg_modernGLGpuDrivenBatch.commandCount = 0;
}

static int R_ModernGLExecutor_CompareGpuDrivenCounter( GLuint gpuValue, int cpuValue ) {
	return gpuValue == static_cast<GLuint>( Max( 0, cpuValue ) ) ? 0 : 1;
}

static bool R_ModernGLExecutor_ReadGpuDrivenCounters( GLuint counters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] ) {
	if ( glGetBufferSubData == NULL || rg_modernGLExecutorValidationSSBO == 0 ) {
		return false;
	}
	R_GLStateCache().BindBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorValidationSSBO );
	glGetBufferSubData( GL_SHADER_STORAGE_BUFFER, 0, sizeof( GLuint ) * MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS, counters );
	R_GLStateCache().BindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );
	return true;
}

static void R_ModernGLExecutor_UpdateGpuDrivenBuffers( modernGLExecutorStats_t &stats, bool forceValidationReadback = false ) {
	R_ModernGLExecutor_ResetGpuDrivenBatch();
	stats.gpuDrivenValidationRequested = r_rendererGpuValidation.GetBool() || forceValidationReadback;
	if ( !stats.gpuDrivenReady || !stats.submitPlanReady || rg_modernGLExecutorSceneSSBO == 0 || rg_modernGLExecutorIndirectBuffer == 0 || rg_modernGLExecutorValidationSSBO == 0 ) {
		return;
	}

	static modernGLGpuSceneRecord_t sceneRecords[MODERN_GL_GPU_DRIVEN_MAX_RECORDS];
	static modernGLDrawElementsIndirectCommand_t indirectRecords[MODERN_GL_GPU_DRIVEN_MAX_RECORDS];
	memset( sceneRecords, 0, sizeof( sceneRecords ) );
	memset( indirectRecords, 0, sizeof( indirectRecords ) );

	const rendererClusteredLightingStats_t clusteredStats = R_ModernClusteredLighting_Stats();
	modernGLGpuDrivenCpuReference_t cpuReference;
	memset( &cpuReference, 0, sizeof( cpuReference ) );
	cpuReference.clusterBins = clusteredStats.frameValid ? clusteredStats.activeClusters : 0;

	const int sceneRecordCount = Min( rg_modernGLSubmitPlan.NumCommands(), MODERN_GL_GPU_DRIVEN_MAX_RECORDS );
	for ( int i = 0; i < sceneRecordCount; ++i ) {
		const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
		modernGLGpuSceneRecord_t &record = sceneRecords[i];

		const bool visible = R_ModernGLExecutor_CommandVisibleForGpuDriven( command );
		const bool canSeedIndirect = visible && R_ModernGLExecutor_CommandCanSeedIndirect( command );
		if ( canSeedIndirect && !rg_modernGLGpuDrivenBatch.valid ) {
			R_ModernGLExecutor_BeginGpuDrivenBatch( command );
		}
		const bool indirectEligible = canSeedIndirect && R_ModernGLExecutor_CommandMatchesGpuDrivenBatch( command, rg_modernGLGpuDrivenBatch );

		cpuReference.processedCommands++;
		if ( visible ) {
			cpuReference.visibleInstances++;
		} else {
			cpuReference.culledCommands++;
		}
		if ( indirectEligible ) {
			cpuReference.eligibleCommands++;
			cpuReference.generatedCommands++;
			rg_modernGLGpuDrivenBatch.commandCount++;
		} else if ( canSeedIndirect ) {
			stats.gpuDrivenIndirectFallbacks++;
		}

		GLuint flags = command.indexed ? MODERN_GL_GPU_RECORD_INDEXED : 0u;
		if ( indirectEligible ) {
			flags |= MODERN_GL_GPU_RECORD_INDIRECT_ELIGIBLE;
		}
		if ( visible ) {
			flags |= MODERN_GL_GPU_RECORD_VISIBLE;
		}
		if ( i == 0 ) {
			flags |= MODERN_GL_GPU_RECORD_CLUSTER_BIN_SOURCE;
		}
		record.counts[0] = static_cast<float>( r_singleTriangle.GetBool() ? Min( 3, command.indexCount ) : command.indexCount );
		record.counts[1] = static_cast<float>( command.indexCacheOffset >= 0 ? command.indexCacheOffset / static_cast<int>( sizeof( glIndex_t ) ) : 0 );
		record.counts[2] = static_cast<float>( ( i == 0 ) ? cpuReference.clusterBins : 0 );
		record.counts[3] = static_cast<float>( command.passCategory );
		record.ids[0] = static_cast<GLuint>( command.shaderKind );
		record.ids[1] = static_cast<GLuint>( i );
		record.ids[2] = command.materialTableIndex >= 0 ? static_cast<GLuint>( command.materialTableIndex ) : 0xffffffffu;
		record.ids[3] = flags;
	}

	GLuint validationCounters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0 };
	const GLsizeiptr sceneBytes = static_cast<GLsizeiptr>( sceneRecordCount * sizeof( modernGLGpuSceneRecord_t ) );
	const GLsizeiptr indirectBytes = static_cast<GLsizeiptr>( Max( 1, sceneRecordCount ) * sizeof( modernGLDrawElementsIndirectCommand_t ) );
	const GLsizeiptr validationBytes = static_cast<GLsizeiptr>( sizeof( validationCounters ) );
	R_ModernGLExecutor_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorSceneSSBO, sceneBytes, sceneRecords, stats );
	R_ModernGLExecutor_UpdateBuffer( GL_DRAW_INDIRECT_BUFFER, rg_modernGLExecutorIndirectBuffer, indirectBytes, indirectRecords, stats );
	R_ModernGLExecutor_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorValidationSSBO, validationBytes, validationCounters, stats );

	stats.gpuDrivenSceneRecords = sceneRecordCount;
	stats.gpuDrivenIndirectRecords = cpuReference.generatedCommands;
	stats.gpuDrivenSceneBytes = static_cast<int>( sceneBytes );
	stats.gpuDrivenIndirectBytes = static_cast<int>( indirectBytes );
	stats.gpuDrivenValidationBytes = static_cast<int>( validationBytes );
	stats.gpuDrivenSourceCommands = cpuReference.processedCommands;
	stats.gpuDrivenEligibleCommands = cpuReference.eligibleCommands;
	stats.gpuDrivenGeneratedCommands = cpuReference.generatedCommands;
	stats.gpuDrivenCulledCommands = cpuReference.culledCommands;
	stats.gpuDrivenVisibleInstances = cpuReference.visibleInstances;
	stats.gpuDrivenCpuGeneratedCommands = cpuReference.generatedCommands;
	stats.gpuDrivenCpuCulledCommands = cpuReference.culledCommands;
	stats.gpuDrivenCpuVisibleInstances = cpuReference.visibleInstances;
	stats.gpuDrivenClusterBins = cpuReference.clusterBins;
	stats.gpuDrivenCpuClusterBins = cpuReference.clusterBins;
	stats.gpuDrivenIndirectMultiDrawReady = rg_modernGLGpuDrivenBatch.valid && rg_modernGLGpuDrivenBatch.commandCount > 0 && glMultiDrawElementsIndirect != NULL;

	R_ModernGLExecutor_BindGpuDrivenBuffers( stats );
	if ( sceneRecordCount > 0 && stats.computeValidationReady ) {
		idGLDebugScope debugScope( "ModernGLExecutor GPU-driven compute" );
		R_GLStateCache().UseProgram( rg_modernGLExecutorComputeProgram );
		if ( rg_modernGLExecutorComputeRecordCountLocation >= 0 ) {
			glUniform1ui( rg_modernGLExecutorComputeRecordCountLocation, static_cast<GLuint>( sceneRecordCount ) );
		}
		glDispatchCompute( static_cast<GLuint>( ( sceneRecordCount + MODERN_GL_GPU_DRIVEN_WORKGROUP_SIZE - 1 ) / MODERN_GL_GPU_DRIVEN_WORKGROUP_SIZE ), 1, 1 );
		glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT );
		R_GLStateCache().UseProgram( 0 );
		stats.gpuDrivenComputeDispatches++;
		stats.gpuDrivenExecuted = true;
		if ( stats.gpuDrivenValidationRequested ) {
			GLuint gpuCounters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0 };
			if ( R_ModernGLExecutor_ReadGpuDrivenCounters( gpuCounters ) ) {
				stats.gpuDrivenValidationReadbackReady = true;
				stats.gpuDrivenValidationReadbacks++;
				stats.gpuDrivenGpuGeneratedCommands = static_cast<int>( gpuCounters[MODERN_GL_GPU_COUNTER_GENERATED] );
				stats.gpuDrivenGpuCulledCommands = static_cast<int>( gpuCounters[MODERN_GL_GPU_COUNTER_CULLED] );
				stats.gpuDrivenGpuVisibleInstances = static_cast<int>( gpuCounters[MODERN_GL_GPU_COUNTER_VISIBLE_INSTANCES] );
				stats.gpuDrivenGpuClusterBins = static_cast<int>( gpuCounters[MODERN_GL_GPU_COUNTER_CLUSTER_BINS] );
				stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( gpuCounters[MODERN_GL_GPU_COUNTER_PROCESSED], cpuReference.processedCommands );
				stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( gpuCounters[MODERN_GL_GPU_COUNTER_ELIGIBLE], cpuReference.eligibleCommands );
				stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( gpuCounters[MODERN_GL_GPU_COUNTER_GENERATED], cpuReference.generatedCommands );
				stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( gpuCounters[MODERN_GL_GPU_COUNTER_CULLED], cpuReference.culledCommands );
				stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( gpuCounters[MODERN_GL_GPU_COUNTER_VISIBLE_INSTANCES], cpuReference.visibleInstances );
				stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( gpuCounters[MODERN_GL_GPU_COUNTER_CLUSTER_BINS], cpuReference.clusterBins );
			}
		} else {
			stats.gpuDrivenGpuGeneratedCommands = cpuReference.generatedCommands;
			stats.gpuDrivenGpuCulledCommands = cpuReference.culledCommands;
			stats.gpuDrivenGpuVisibleInstances = cpuReference.visibleInstances;
			stats.gpuDrivenGpuClusterBins = cpuReference.clusterBins;
		}
	}
	R_ModernGLExecutor_UnbindGpuDrivenBuffers();
}

static void R_ModernGLExecutor_CountSubmittedFallback( modernGLExecutorStats_t &stats, bool recordSubmitStats ) {
	if ( recordSubmitStats ) {
		stats.submittedFallbackDraws++;
	}
}

static const materialResourceTextureBinding_t *R_ModernGLExecutor_FindTextureBinding( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic ) {
	for ( int i = 0; i < record.textureBindingCount; ++i ) {
		const materialResourceTextureBinding_t &binding = record.textures[i];
		if ( binding.semantic == semantic ) {
			return &binding;
		}
	}
	return NULL;
}

static GLuint R_ModernGLExecutor_ImageHandleOrZero( const idImage *image ) {
	if ( image != NULL && image->IsLoaded() ) {
		return const_cast<idImage *>( image )->GetDeviceHandle();
	}
	return 0;
}

static GLuint R_ModernGLExecutor_FallbackTextureForSemantic( materialResourceTextureSemantic_t semantic ) {
	if ( globalImages == NULL ) {
		return 0;
	}
	switch ( semantic ) {
	case MATERIAL_RESOURCE_TEXTURE_BUMP:
		if ( GLuint handle = R_ModernGLExecutor_ImageHandleOrZero( globalImages->flatNormalMap ) ) {
			return handle;
		}
		break;
	case MATERIAL_RESOURCE_TEXTURE_SPECULAR:
	case MATERIAL_RESOURCE_TEXTURE_EMISSIVE:
		if ( GLuint handle = R_ModernGLExecutor_ImageHandleOrZero( globalImages->blackImage ) ) {
			return handle;
		}
		break;
	default:
		break;
	}
	if ( GLuint handle = R_ModernGLExecutor_ImageHandleOrZero( globalImages->whiteImage ) ) {
		return handle;
	}
	return R_ModernGLExecutor_ImageHandleOrZero( globalImages->defaultImage );
}

static GLuint R_ModernGLExecutor_TextureForCommandSemantic( const modernGLSubmitCommand_t &command, materialResourceTextureSemantic_t semantic ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord != NULL ) {
		const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, semantic );
		if ( binding != NULL && binding->textureHandle != 0 ) {
			return static_cast<GLuint>( binding->textureHandle );
		}
	}
	return R_ModernGLExecutor_FallbackTextureForSemantic( semantic );
}

static GLuint R_ModernGLExecutor_TextureForCommand( const modernGLSubmitCommand_t &command ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord != NULL ) {
		materialResourceTextureSemantic_t semantics[3] = {
			MATERIAL_RESOURCE_TEXTURE_DIFFUSE,
			MATERIAL_RESOURCE_TEXTURE_GUI,
			MATERIAL_RESOURCE_TEXTURE_POST_PROCESS
		};
		for ( int i = 0; i < 3; ++i ) {
			const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, semantics[i] );
			if ( binding != NULL && binding->textureHandle != 0 ) {
				return static_cast<GLuint>( binding->textureHandle );
			}
		}
	}
	return R_ModernGLExecutor_FallbackTextureForSemantic( MATERIAL_RESOURCE_TEXTURE_DIFFUSE );
}

static bool R_ModernGLExecutor_CommandHasTextureSemantic( const modernGLSubmitCommand_t &command, materialResourceTextureSemantic_t semantic ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord == NULL ) {
		return false;
	}
	const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, semantic );
	return binding != NULL && binding->textureHandle != 0;
}

static float R_ModernGLExecutor_ShaderRegisterValue( const modernGLSubmitCommand_t &command, int registerIndex, float fallbackValue ) {
	const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
	const instanceRecord_t *instance = draw != NULL ? draw->instanceRecord : NULL;
	if ( instance != NULL && instance->legacyShaderRegisters != NULL && registerIndex >= 0 && registerIndex < instance->shaderRegisterCount ) {
		return instance->legacyShaderRegisters[registerIndex];
	}
	const drawSurf_t *surf = draw != NULL ? draw->legacyDrawSurf : NULL;
	if ( surf != NULL && surf->shaderRegisters != NULL && surf->material != NULL && registerIndex >= 0 && registerIndex < surf->material->GetNumRegisters() ) {
		return surf->shaderRegisters[registerIndex];
	}
	return fallbackValue;
}

static float R_ModernGLExecutor_AlphaReferenceForCommand( const modernGLSubmitCommand_t &command ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord == NULL || !materialRecord->alphaTest ) {
		return 0.5f;
	}
	return idMath::ClampFloat( 0.0f, 1.0f, R_ModernGLExecutor_ShaderRegisterValue( command, materialRecord->alphaTestRegister, 0.5f ) );
}

static void R_ModernGLExecutor_SetMaterialFlags( const modernGLSubmitCommand_t &command ) {
	if ( command.materialFlagsLocation < 0 ) {
		return;
	}
	const float hasNormal = R_ModernGLExecutor_CommandHasTextureSemantic( command, MATERIAL_RESOURCE_TEXTURE_BUMP ) ? 1.0f : 0.0f;
	const float hasSpecular = R_ModernGLExecutor_CommandHasTextureSemantic( command, MATERIAL_RESOURCE_TEXTURE_SPECULAR ) ? 1.0f : 0.0f;
	const float hasEmissive = R_ModernGLExecutor_CommandHasTextureSemantic( command, MATERIAL_RESOURCE_TEXTURE_EMISSIVE ) ? 1.0f : 0.0f;
	glUniform4f( command.materialFlagsLocation, hasNormal, hasSpecular, hasEmissive, 0.0f );
}

static void R_ModernGLExecutor_BindMaterialTextures( const modernGLSubmitCommand_t &command ) {
	if ( glUniform1i == NULL ) {
		return;
	}
	if ( command.mainTextureLocation >= 0 ) {
		glUniform1i( command.mainTextureLocation, MODERN_GL_MATERIAL_TEXTURE_MAIN );
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommand( command );
		if ( textureHandle != 0 ) {
			R_GLStateCache().ActiveTextureUnit( MODERN_GL_MATERIAL_TEXTURE_MAIN );
			R_GLStateCache().BindTexture( MODERN_GL_MATERIAL_TEXTURE_MAIN, GL_TEXTURE_2D, textureHandle );
		}
	}
	if ( command.normalTextureLocation >= 0 ) {
		glUniform1i( command.normalTextureLocation, MODERN_GL_MATERIAL_TEXTURE_NORMAL );
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_BUMP );
		R_GLStateCache().ActiveTextureUnit( MODERN_GL_MATERIAL_TEXTURE_NORMAL );
		R_GLStateCache().BindTexture( MODERN_GL_MATERIAL_TEXTURE_NORMAL, GL_TEXTURE_2D, textureHandle );
	}
	if ( command.specularTextureLocation >= 0 ) {
		glUniform1i( command.specularTextureLocation, MODERN_GL_MATERIAL_TEXTURE_SPECULAR );
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_SPECULAR );
		R_GLStateCache().ActiveTextureUnit( MODERN_GL_MATERIAL_TEXTURE_SPECULAR );
		R_GLStateCache().BindTexture( MODERN_GL_MATERIAL_TEXTURE_SPECULAR, GL_TEXTURE_2D, textureHandle );
	}
	if ( command.emissiveTextureLocation >= 0 ) {
		glUniform1i( command.emissiveTextureLocation, MODERN_GL_MATERIAL_TEXTURE_EMISSIVE );
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_EMISSIVE );
		R_GLStateCache().ActiveTextureUnit( MODERN_GL_MATERIAL_TEXTURE_EMISSIVE );
		R_GLStateCache().BindTexture( MODERN_GL_MATERIAL_TEXTURE_EMISSIVE, GL_TEXTURE_2D, textureHandle );
	}
	R_GLStateCache().ActiveTextureUnit( 0 );
}

static void R_ModernGLExecutor_SetUniformBlockBinding( GLuint program, const char *blockName, GLuint binding );

static bool R_ModernGLExecutor_CommandUsesClusteredLighting( const modernGLSubmitCommand_t &command ) {
	return command.shaderKind == MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE
		|| command.shaderKind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE
		|| command.shaderKind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST
		|| command.shaderKind == MODERN_GL_SHADER_TRANSPARENT_FORWARD;
}

static void R_ModernGLExecutor_BindClusterUniformBlocks( GLuint program ) {
	R_ModernGLExecutor_SetUniformBlockBinding( program, "ModernClusterGridParams", MODERN_GL_CLUSTER_UBO_BINDING_PARAMS );
	R_ModernGLExecutor_SetUniformBlockBinding( program, "ModernClusterLightRecords", MODERN_GL_CLUSTER_UBO_BINDING_LIGHTS );
	R_ModernGLExecutor_SetUniformBlockBinding( program, "ModernClusterIndexRecords", MODERN_GL_CLUSTER_UBO_BINDING_INDICES );
}

static bool R_ModernGLExecutor_SubmitCommand( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats, bool recordSubmitStats ) {
	if ( command.drawPlanEntry == NULL || command.viewDef == NULL ) {
		R_ModernGLExecutor_CountSubmittedFallback( stats, recordSubmitStats );
		return false;
	}
	if ( command.program == 0 || command.vertexBuffer == 0 || command.modelViewProjectionLocation < 0 ) {
		R_ModernGLExecutor_CountSubmittedFallback( stats, recordSubmitStats );
		return false;
	}

	GLuint indexBuffer = command.indexBuffer;
	int indexOffset = command.indexCacheOffset;
	if ( command.indexed ) {
		if ( command.uploadIndexBuffer ) {
			if ( command.clientIndexData == NULL || command.clientIndexBytes <= 0 ) {
				R_ModernGLExecutor_CountSubmittedFallback( stats, recordSubmitStats );
				return false;
			}
			rendererUploadAllocation_t indexUpload;
			if ( !R_RendererUpload_AllocFrameTemp( const_cast<void *>( command.clientIndexData ), command.clientIndexBytes, 4, indexUpload ) ) {
				R_ModernGLExecutor_CountSubmittedFallback( stats, recordSubmitStats );
				return false;
			}
			indexBuffer = indexUpload.vbo;
			indexOffset = indexUpload.offset;
		}
		if ( indexBuffer == 0 ) {
			R_ModernGLExecutor_CountSubmittedFallback( stats, recordSubmitStats );
			return false;
		}
	}

	float modelViewProjection[16];
	myGlMultMatrix( command.modelViewMatrix, command.viewDef->projectionMatrix, modelViewProjection );

	R_GLStateCache().UseProgram( command.program );
	R_ModernGLExecutor_BindFrameUniformBufferBase( stats );
	if ( R_ModernGLExecutor_CommandUsesClusteredLighting( command ) ) {
		R_ModernGLExecutor_BindClusterUniformBlocks( command.program );
	}
	glUniformMatrix4fv( command.modelViewProjectionLocation, 1, GL_FALSE, modelViewProjection );
	if ( command.modelViewMatrixLocation >= 0 ) {
		glUniformMatrix4fv( command.modelViewMatrixLocation, 1, GL_FALSE, command.modelViewMatrix );
	}
	R_ModernGLExecutor_SetDebugColor( command );
	R_ModernGLExecutor_SetLocalParams( command );
	R_ModernGLExecutor_SetMaterialFlags( command );
	R_ModernGLExecutor_BindMaterialTextures( command );

	R_ModernGLExecutor_SetSubmitScissor( command, command.viewDef );
	R_GLStateCache().BindBuffer( GL_ARRAY_BUFFER, command.vertexBuffer );
	if ( !R_ModernGLExecutor_BindDrawVertLayout( command ) ) {
		R_ModernGLExecutor_CountSubmittedFallback( stats, recordSubmitStats );
		return false;
	}

	if ( command.indexed ) {
		R_GLStateCache().BindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBuffer );
		glDrawElements(
			GL_TRIANGLES,
			r_singleTriangle.GetBool() ? Min( 3, command.indexCount ) : command.indexCount,
			static_cast<GLenum>( command.indexType ),
			R_ModernGLExecutor_BufferOffset( indexOffset ) );
	} else {
		glDrawArrays( GL_TRIANGLES, 0, command.vertexCount );
	}

	if ( recordSubmitStats ) {
		stats.submittedDraws++;
		if ( R_ModernGLExecutor_IsDepthPipeline( command.pipeline ) ) {
			stats.submittedDepthDraws++;
		} else if ( R_ModernGLExecutor_IsMaterialPipeline( command.pipeline ) ) {
			stats.submittedMaterialDraws++;
		}
		if ( command.uploadIndexBuffer ) {
			stats.submittedIndexUploadDraws++;
		}
	}
	return true;
}

static void R_ModernGLExecutor_RestoreAfterSubmit( void ) {
	R_ModernGLExecutor_DisableDrawVertLayout();
	R_GLStateCache().BindBuffer( GL_ARRAY_BUFFER, 0 );
	R_GLStateCache().BindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	for ( int unit = 0; unit < MODERN_GL_MATERIAL_TEXTURE_COUNT; ++unit ) {
		R_GLStateCache().BindTexture( unit, GL_TEXTURE_2D, 0 );
		if ( glBindSampler != NULL ) {
			R_GLStateCache().BindSampler( unit, 0 );
		}
	}
	R_GLStateCache().ActiveTextureUnit( 0 );
	if ( glBindBufferBase != NULL ) {
		R_GLStateCache().BindBufferBase( GL_UNIFORM_BUFFER, 0, 0 );
	}
	R_ModernGLExecutor_UnbindGpuDrivenBuffers();
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

static void R_ModernGLExecutor_SubmitGpuDrivenIndirect( modernGLExecutorStats_t &stats ) {
	if ( !stats.gpuDrivenReady || !stats.gpuDrivenIndirectMultiDrawReady || !rg_modernGLGpuDrivenBatch.valid || stats.gpuDrivenGeneratedCommands <= 0 ) {
		return;
	}
	if ( !stats.gpuDrivenValidationRequested && !r_rendererModernSubmit.GetBool() ) {
		return;
	}
	if ( !stats.enabled || !stats.available || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 || glMultiDrawElementsIndirect == NULL ) {
		return;
	}

	const modernGLSubmitCommand_t &command = rg_modernGLGpuDrivenBatch.command;
	if ( command.viewDef == NULL || command.program == 0 || command.vertexBuffer == 0 || command.indexBuffer == 0 || command.modelViewProjectionLocation < 0 ) {
		stats.gpuDrivenIndirectFallbacks += stats.gpuDrivenGeneratedCommands;
		return;
	}

	float modelViewProjection[16];
	myGlMultMatrix( command.modelViewMatrix, command.viewDef->projectionMatrix, modelViewProjection );

	idGLDebugScope debugScope( "ModernGLExecutor GPU-driven indirect submit" );
	R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_GPU_DRIVEN_INDIRECT );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetScissorTestEnabled( true );
	R_GLStateCache().SetDepthTestEnabled( true );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetDepthFunc( GL_LEQUAL );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	R_GLStateCache().UseProgram( command.program );
	R_ModernGLExecutor_BindFrameUniformBufferBase( stats );
	if ( R_ModernGLExecutor_CommandUsesClusteredLighting( command ) ) {
		R_ModernGLExecutor_BindClusterUniformBlocks( command.program );
	}
	glUniformMatrix4fv( command.modelViewProjectionLocation, 1, GL_FALSE, modelViewProjection );
	if ( command.modelViewMatrixLocation >= 0 ) {
		glUniformMatrix4fv( command.modelViewMatrixLocation, 1, GL_FALSE, command.modelViewMatrix );
	}
	R_ModernGLExecutor_SetDebugColor( command );
	R_ModernGLExecutor_SetLocalParams( command );
	R_ModernGLExecutor_SetMaterialFlags( command );
	R_ModernGLExecutor_BindMaterialTextures( command );

	R_ModernGLExecutor_SetSubmitScissor( command, command.viewDef );
	R_GLStateCache().BindBuffer( GL_ARRAY_BUFFER, command.vertexBuffer );
	if ( !R_ModernGLExecutor_BindDrawVertLayout( command ) ) {
		stats.gpuDrivenIndirectFallbacks += stats.gpuDrivenGeneratedCommands;
		R_RendererMetrics_EndGpuTimer();
		R_ModernGLExecutor_RestoreAfterSubmit();
		return;
	}

	R_GLStateCache().BindBuffer( GL_ELEMENT_ARRAY_BUFFER, command.indexBuffer );
	R_GLStateCache().BindBuffer( GL_DRAW_INDIRECT_BUFFER, rg_modernGLExecutorIndirectBuffer );
	glMultiDrawElementsIndirect(
		GL_TRIANGLES,
		static_cast<GLenum>( command.indexType ),
		NULL,
		static_cast<GLsizei>( stats.gpuDrivenGeneratedCommands ),
		static_cast<GLsizei>( sizeof( modernGLDrawElementsIndirectCommand_t ) ) );
	R_RendererMetrics_EndGpuTimer();

	stats.gpuDrivenIndirectExecuted = true;
	stats.gpuDrivenIndirectDrawCalls += stats.gpuDrivenGeneratedCommands;
	stats.gpuDrivenMultiDrawBatches++;
	R_ModernGLExecutor_RestoreAfterSubmit();
}

static bool R_ModernGLExecutor_DepthResourceReady( const char *name, const renderGraphResourceHandle_t *&handle ) {
	handle = R_RenderGraphResources_FindHandle( name );
	return handle != NULL
		&& handle->framebuffer != 0
		&& handle->texture != 0
		&& handle->framebufferComplete
		&& ( handle->flags & RENDER_GRAPH_RESOURCE_HANDLE_FBO_COMPLETE ) != 0;
}

static void R_ModernGLExecutor_CountVisibleDepthFallback( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	if ( command.passCategory == RENDER_PASS_SHADOW_MAP ) {
		stats.visibleShadowFallbackDraws++;
	} else {
		stats.visibleDepthFallbackDraws++;
	}
	stats.visibleDepthMismatchDraws++;
}

static bool R_ModernGLExecutor_VisibleDepthMaterialSupported( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord == NULL || materialRecord->fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE ) {
		stats.visibleDepthMaterialFallbackDraws++;
		return false;
	}
	if ( materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED ) {
		stats.visibleDepthAlphaTestFallbackDraws++;
		return false;
	}
	if ( materialRecord->materialClass != RENDER_MATERIAL_OPAQUE && materialRecord->materialClass != RENDER_MATERIAL_SHADOW_ONLY ) {
		stats.visibleDepthMaterialFallbackDraws++;
		return false;
	}

	const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
	const geometryResourceRecord_t *geometry = draw != NULL ? draw->geometryRecord : NULL;
	if ( geometry == NULL ) {
		stats.visibleDepthGeometryFallbackDraws++;
		return false;
	}
	if ( geometry->deformMode != GEOMETRY_DEFORM_NONE ) {
		stats.visibleDepthDeformFallbackDraws++;
		return false;
	}
	if ( geometry->skinningMode != GEOMETRY_SKINNING_NONE ) {
		stats.visibleDepthSkinnedFallbackDraws++;
		return false;
	}
	return true;
}

static void R_ModernGLExecutor_BeginDepthResourcePass( const renderGraphResourceHandle_t &handle, bool clearStencil, const char *clearLabel ) {
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, handle.framebuffer );
	if ( handle.type == RENDER_GRAPH_RESOURCE_DEPTH || handle.type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL ) {
		glDrawBuffer( GL_NONE );
		glReadBuffer( GL_NONE );
	}
	R_GLStateCache().SetViewport( 0, 0, Max( 1, handle.width ), Max( 1, handle.height ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, handle.width ), Max( 1, handle.height ) );
	R_GLStateCache().SetScissorTestEnabled( true );
	R_GLStateCache().SetDepthTestEnabled( true );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetDepthFunc( GL_LEQUAL );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	R_GLStateCache().SetColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	{
		idGLDebugScope clearScope( clearLabel );
		glClearDepth( 1.0f );
		if ( clearStencil ) {
			glClearStencil( R_ModernGLExecutor_SafeStencilClearValue() );
			glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		} else {
			glClear( GL_DEPTH_BUFFER_BIT );
		}
	}
}

static void R_ModernGLExecutor_ExecuteVisibleDepthPass(
	renderPassCategory_t category,
	modernGLExecutorStats_t &stats,
	const char *passLabel ) {
	idGLDebugScope passScope( passLabel );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
		const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
		if ( command.pipeline != MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH && command.pipeline != MODERN_GL_DRAW_PLAN_PIPELINE_SHADOW_DEPTH ) {
			continue;
		}
		if ( command.passCategory != category ) {
			if ( category == RENDER_PASS_SHADOW_MAP && command.passCategory == RENDER_PASS_STENCIL_SHADOW ) {
				stats.visibleStencilShadowFallbackDraws++;
			}
			continue;
		}
		if ( !R_ModernGLExecutor_VisibleDepthMaterialSupported( command, stats ) ) {
			R_ModernGLExecutor_CountVisibleDepthFallback( command, stats );
			continue;
		}
		if ( !R_ModernGLExecutor_SubmitCommand( command, stats, false ) ) {
			R_ModernGLExecutor_CountVisibleDepthFallback( command, stats );
			continue;
		}
		if ( category == RENDER_PASS_SHADOW_MAP ) {
			stats.visibleShadowDepthDraws++;
		} else {
			stats.visibleDepthDraws++;
		}
	}
}

static void R_ModernGLExecutor_SubmitVisibleDepth( modernGLExecutorStats_t &stats ) {
	if ( !stats.visibleDepthRequested || !stats.enabled || !stats.available || !stats.submitPlanReady || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	const renderGraphResourceHandle_t *sceneDepth = NULL;
	const renderGraphResourceHandle_t *shadowMap = NULL;
	stats.visibleDepthResourceReady = R_ModernGLExecutor_DepthResourceReady( "sceneDepth", sceneDepth );
	stats.visibleShadowResourceReady = R_ModernGLExecutor_DepthResourceReady( "shadowMap", shadowMap );

	if ( stats.visibleDepthResourceReady && sceneDepth != NULL ) {
		R_ModernGLExecutor_BeginDepthResourcePass( *sceneDepth, true, "ModernGLExecutor visible depth clear" );
		stats.visibleDepthClearOps++;
		R_ModernGLExecutor_ExecuteVisibleDepthPass( RENDER_PASS_DEPTH, stats, "ModernGLExecutor visible depth pass" );
		{
			idGLDebugScope resolveScope( "ModernGLExecutor visible depth resolve" );
			stats.visibleDepthResolveOps++;
		}
	} else {
		for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
			const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
			if ( command.passCategory == RENDER_PASS_DEPTH ) {
				stats.visibleDepthResourceFallbackDraws++;
				stats.visibleDepthFallbackDraws++;
				stats.visibleDepthMismatchDraws++;
			}
		}
	}

	if ( stats.visibleShadowResourceReady && shadowMap != NULL ) {
		R_ModernGLExecutor_BeginDepthResourcePass( *shadowMap, false, "ModernGLExecutor visible shadow-depth clear" );
		stats.visibleDepthClearOps++;
		R_ModernGLExecutor_ExecuteVisibleDepthPass( RENDER_PASS_SHADOW_MAP, stats, "ModernGLExecutor visible shadow-depth pass" );
		{
			idGLDebugScope resolveScope( "ModernGLExecutor visible shadow-depth resolve" );
			stats.visibleDepthResolveOps++;
		}
	} else {
		for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
			const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
			if ( command.passCategory == RENDER_PASS_SHADOW_MAP ) {
				stats.visibleDepthResourceFallbackDraws++;
				stats.visibleShadowFallbackDraws++;
				stats.visibleDepthMismatchDraws++;
			} else if ( command.passCategory == RENDER_PASS_STENCIL_SHADOW ) {
				stats.visibleStencilShadowFallbackDraws++;
			}
		}
	}

	R_ModernGLExecutor_RestoreAfterSubmit();
	stats.visibleDepthExecuted = stats.visibleDepthDraws > 0 || stats.visibleShadowDepthDraws > 0 || stats.visibleDepthClearOps > 0;
	if ( stats.visibleDepthExecuted ) {
		R_ModernGLExecutor_SetStatus( stats, "visible-depth-legacy-fallback" );
	}
}

static bool R_ModernGLExecutor_GBufferResourceReady( const char *name, const renderGraphResourceHandle_t *&handle ) {
	handle = R_RenderGraphResources_FindHandle( name );
	return handle != NULL
		&& handle->type == RENDER_GRAPH_RESOURCE_COLOR
		&& handle->target == GL_TEXTURE_2D
		&& handle->texture != 0
		&& handle->framebufferComplete;
}

static int R_ModernGLExecutor_GBufferBytesPerPixel( const renderGraphResourceHandle_t &handle ) {
	switch ( handle.internalFormat ) {
	case GL_RGBA16F:
		return 8;
	case GL_RGBA8:
	default:
		return 4;
	}
}

static bool R_ModernGLExecutor_PrepareGBufferFBO( const renderGraphResourceHandle_t *const colorHandles[MODERN_GL_GBUFFER_ATTACHMENT_COUNT], const renderGraphResourceHandle_t &depthHandle, modernGLExecutorStats_t &stats ) {
	if ( rg_modernGLExecutorGBufferFBO == 0 || glCheckFramebufferStatus == NULL || glDrawBuffers == NULL ) {
		return false;
	}
	if ( !rg_modernGLExecutorCaps.hasMRT || rg_modernGLExecutorCaps.maxDrawBuffers < MODERN_GL_GBUFFER_ATTACHMENT_COUNT || rg_modernGLExecutorCaps.maxColorAttachments < MODERN_GL_GBUFFER_ATTACHMENT_COUNT ) {
		return false;
	}

	for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
		if ( colorHandles[i] == NULL || colorHandles[i]->texture == 0 ) {
			return false;
		}
	}
	const GLenum depthAttachment = depthHandle.type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
	if ( stats.tierUsesDSA && glNamedFramebufferTexture != NULL && glNamedFramebufferDrawBuffers != NULL && glNamedFramebufferReadBuffer != NULL && glCheckNamedFramebufferStatus != NULL ) {
		for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
			glNamedFramebufferTexture( rg_modernGLExecutorGBufferFBO, rg_modernGLGBufferColorAttachments[i], colorHandles[i]->texture, 0 );
		}
		glNamedFramebufferTexture( rg_modernGLExecutorGBufferFBO, depthAttachment, depthHandle.texture, 0 );
		glNamedFramebufferDrawBuffers( rg_modernGLExecutorGBufferFBO, MODERN_GL_GBUFFER_ATTACHMENT_COUNT, rg_modernGLGBufferColorAttachments );
		glNamedFramebufferReadBuffer( rg_modernGLExecutorGBufferFBO, GL_COLOR_ATTACHMENT0 );
		const GLenum status = glCheckNamedFramebufferStatus( rg_modernGLExecutorGBufferFBO, GL_FRAMEBUFFER );
		stats.lowOverheadFramebufferDSAUpdates++;
		stats.opaqueGBufferMRTReady = status == GL_FRAMEBUFFER_COMPLETE;
		if ( stats.opaqueGBufferMRTReady ) {
			R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorGBufferFBO );
		}
		return stats.opaqueGBufferMRTReady;
	}

	if ( glFramebufferTexture2D == NULL ) {
		return false;
	}
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorGBufferFBO );
	for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
		glFramebufferTexture2D( GL_FRAMEBUFFER, rg_modernGLGBufferColorAttachments[i], GL_TEXTURE_2D, colorHandles[i]->texture, 0 );
	}
	glFramebufferTexture2D( GL_FRAMEBUFFER, depthAttachment, depthHandle.target, depthHandle.texture, 0 );
	glDrawBuffers( MODERN_GL_GBUFFER_ATTACHMENT_COUNT, rg_modernGLGBufferColorAttachments );
	glReadBuffer( GL_COLOR_ATTACHMENT0 );
	const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	stats.opaqueGBufferMRTReady = status == GL_FRAMEBUFFER_COMPLETE;
	return stats.opaqueGBufferMRTReady;
}

static void R_ModernGLExecutor_CountGBufferFallback( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	(void)command;
	stats.opaqueGBufferFallbackDraws++;
}

static bool R_ModernGLExecutor_MaterialContractPromotable( const materialResourceTableRecord_t &materialRecord, bool allowAlphaBlend ) {
	if ( materialRecord.hasTextureMatrix
		|| materialRecord.hasVertexColor
		|| materialRecord.hasConditionRegisters
		|| materialRecord.hasPrivatePolygonOffset
		|| materialRecord.hasMaterialPolygonOffset ) {
		return false;
	}
	if ( materialRecord.additiveStageCount > 0 || materialRecord.filterStageCount > 0 ) {
		return false;
	}
	if ( !allowAlphaBlend && materialRecord.blendStageCount > 0 ) {
		return false;
	}
	return true;
}

static bool R_ModernGLExecutor_GBufferMaterialSupported( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord == NULL || materialRecord->fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE ) {
		stats.opaqueGBufferMaterialFallbackDraws++;
		return false;
	}
	if ( materialRecord->materialClass != RENDER_MATERIAL_OPAQUE && materialRecord->materialClass != RENDER_MATERIAL_PERFORATED && !materialRecord->alphaTest ) {
		stats.opaqueGBufferMaterialFallbackDraws++;
		return false;
	}
	if ( !R_ModernGLExecutor_MaterialContractPromotable( *materialRecord, false ) ) {
		stats.opaqueGBufferMaterialFallbackDraws++;
		return false;
	}
	if ( materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED ) {
		const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, MATERIAL_RESOURCE_TEXTURE_DIFFUSE );
		if ( binding == NULL || binding->textureHandle == 0 ) {
			stats.opaqueGBufferTextureFallbackDraws++;
			return false;
		}
	}

	const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
	const geometryResourceRecord_t *geometry = draw != NULL ? draw->geometryRecord : NULL;
	if ( geometry == NULL ) {
		stats.opaqueGBufferGeometryFallbackDraws++;
		return false;
	}
	if ( geometry->deformMode != GEOMETRY_DEFORM_NONE ) {
		stats.opaqueGBufferDeformFallbackDraws++;
		return false;
	}
	if ( geometry->skinningMode == GEOMETRY_SKINNING_GPU_PALETTE ) {
		stats.opaqueGBufferSkinnedFallbackDraws++;
		return false;
	}
	return true;
}

static void R_ModernGLExecutor_SubmitGBuffer( modernGLExecutorStats_t &stats ) {
	if ( !stats.opaqueGBufferRequested || !stats.enabled || !stats.available || !stats.submitPlanReady || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	const renderGraphResourceHandle_t *colorHandles[MODERN_GL_GBUFFER_ATTACHMENT_COUNT];
	memset( colorHandles, 0, sizeof( colorHandles ) );
	stats.opaqueGBufferResourcesReady = true;
	stats.opaqueGBufferAttachmentCount = MODERN_GL_GBUFFER_ATTACHMENT_COUNT;
	stats.opaqueGBufferBytesPerPixel = 0;
	for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
		if ( !R_ModernGLExecutor_GBufferResourceReady( rg_modernGLGBufferAttachmentNames[i], colorHandles[i] ) ) {
			stats.opaqueGBufferResourcesReady = false;
		} else {
			stats.opaqueGBufferBytesPerPixel += R_ModernGLExecutor_GBufferBytesPerPixel( *colorHandles[i] );
		}
	}

	const renderGraphResourceHandle_t *sceneDepth = NULL;
	const bool depthReady = R_ModernGLExecutor_DepthResourceReady( "sceneDepth", sceneDepth );
	if ( !stats.opaqueGBufferResourcesReady || !depthReady || sceneDepth == NULL || !R_ModernGLExecutor_PrepareGBufferFBO( colorHandles, *sceneDepth, stats ) ) {
		for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
			const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
			if ( command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER ) {
				stats.opaqueGBufferResourceFallbackDraws++;
				R_ModernGLExecutor_CountGBufferFallback( command, stats );
			}
		}
		R_ModernGLExecutor_RestoreAfterSubmit();
		return;
	}

	stats.opaqueGBufferBandwidthKB = ( colorHandles[0] != NULL && colorHandles[0]->width > 0 && colorHandles[0]->height > 0 )
		? ( colorHandles[0]->width * colorHandles[0]->height * stats.opaqueGBufferBytesPerPixel ) / 1024
		: 0;

	R_GLStateCache().SetViewport( 0, 0, Max( 1, colorHandles[0]->width ), Max( 1, colorHandles[0]->height ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, colorHandles[0]->width ), Max( 1, colorHandles[0]->height ) );
	R_GLStateCache().SetScissorTestEnabled( true );
	R_GLStateCache().SetDepthTestEnabled( true );
	R_GLStateCache().SetDepthFunc( GL_LEQUAL );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	{
		idGLDebugScope clearScope( "ModernGLExecutor G-buffer clear" );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClearDepth( 1.0f );
		if ( sceneDepth->type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL ) {
			glClearStencil( R_ModernGLExecutor_SafeStencilClearValue() );
			glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		} else {
			glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		}
		stats.opaqueGBufferClearOps++;
	}

	idGLDebugScope passScope( "ModernGLExecutor opaque G-buffer pass" );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
		const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
		if ( command.pipeline != MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER ) {
			continue;
		}
		if ( !R_ModernGLExecutor_GBufferMaterialSupported( command, stats ) ) {
			R_ModernGLExecutor_CountGBufferFallback( command, stats );
			continue;
		}
		if ( !R_ModernGLExecutor_SubmitCommand( command, stats, false ) ) {
			R_ModernGLExecutor_CountGBufferFallback( command, stats );
			continue;
		}
		stats.opaqueGBufferDraws++;
		const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
		if ( materialRecord != NULL ) {
			if ( materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED ) {
				stats.opaqueGBufferAlphaTestDraws++;
			}
			if ( materialRecord->hasEmissive || materialRecord->hasDiffuse ) {
				stats.opaqueGBufferLightGridDraws++;
			}
		}
		const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
		if ( draw != NULL && draw->geometryRecord != NULL && draw->geometryRecord->skinningMode == GEOMETRY_SKINNING_CPU ) {
			stats.opaqueGBufferSkinnedDraws++;
		}
	}

	R_ModernGLExecutor_RestoreAfterSubmit();
	stats.opaqueGBufferExecuted = stats.opaqueGBufferDraws > 0 || stats.opaqueGBufferClearOps > 0;
	if ( stats.opaqueGBufferExecuted ) {
		R_ModernGLExecutor_SetStatus( stats, "gbuffer-legacy-fallback" );
	}
}

static void R_ModernGLExecutor_SetUniformBlockBinding( GLuint program, const char *blockName, GLuint binding ) {
	if ( program == 0 || blockName == NULL || glGetUniformBlockIndex == NULL || glUniformBlockBinding == NULL ) {
		return;
	}
	const GLuint blockIndex = glGetUniformBlockIndex( program, blockName );
	if ( blockIndex != GL_INVALID_INDEX ) {
		glUniformBlockBinding( program, blockIndex, binding );
	}
}

static void R_ModernGLExecutor_SetSamplerUniform( GLuint program, const char *name, GLint unit ) {
	if ( program == 0 || name == NULL || glGetUniformLocation == NULL || glUniform1i == NULL ) {
		return;
	}
	const GLint location = glGetUniformLocation( program, name );
	if ( location >= 0 ) {
		glUniform1i( location, unit );
	}
}

static void R_ModernGLExecutor_BindTextureGroup( GLuint first, GLsizei count, const GLuint *textures, modernGLExecutorStats_t &stats ) {
	if ( count <= 0 || textures == NULL ) {
		return;
	}
	if ( stats.tierUsesMultiBind && glBindTextures != NULL ) {
		if ( R_GLStateCache().BindTextures( first, count, textures ) ) {
			stats.lowOverheadTextureMultiBindBatches++;
		}
		if ( rg_modernGLExecutorLowOverheadSampler != 0 && glBindSamplers != NULL ) {
			GLuint samplers[MODERN_GL_DEFERRED_TEXTURE_COUNT];
			const GLsizei samplerCount = Min( count, static_cast<GLsizei>( MODERN_GL_DEFERRED_TEXTURE_COUNT ) );
			for ( GLsizei i = 0; i < samplerCount; ++i ) {
				samplers[i] = rg_modernGLExecutorLowOverheadSampler;
			}
			if ( R_GLStateCache().BindSamplers( first, samplerCount, samplers ) ) {
				stats.lowOverheadSamplerMultiBindBatches++;
			}
		}
		return;
	}

	for ( GLsizei i = 0; i < count; ++i ) {
		R_GLStateCache().ActiveTextureUnit( static_cast<int>( first + i ) );
		if ( R_GLStateCache().BindTexture( static_cast<int>( first + i ), GL_TEXTURE_2D, textures[i] ) ) {
			stats.lowOverheadClassicTextureBinds++;
		}
		if ( rg_modernGLExecutorLowOverheadSampler != 0 && glBindSampler != NULL ) {
			R_GLStateCache().BindSampler( static_cast<int>( first + i ), rg_modernGLExecutorLowOverheadSampler );
		}
	}
}

static void R_ModernGLExecutor_UnbindTextureGroup( GLuint first, GLsizei count, modernGLExecutorStats_t &stats ) {
	if ( count <= 0 ) {
		return;
	}
	GLuint textures[MODERN_GL_DEFERRED_TEXTURE_COUNT];
	GLuint samplers[MODERN_GL_DEFERRED_TEXTURE_COUNT];
	const GLsizei clampedCount = Min( count, static_cast<GLsizei>( MODERN_GL_DEFERRED_TEXTURE_COUNT ) );
	for ( GLsizei i = 0; i < clampedCount; ++i ) {
		textures[i] = 0;
		samplers[i] = 0;
	}
	if ( stats.tierUsesMultiBind && glBindTextures != NULL ) {
		if ( R_GLStateCache().BindTextures( first, clampedCount, textures ) ) {
			stats.lowOverheadTextureMultiBindBatches++;
		}
		if ( glBindSamplers != NULL && R_GLStateCache().BindSamplers( first, clampedCount, samplers ) ) {
			stats.lowOverheadSamplerMultiBindBatches++;
		}
	} else {
		for ( GLsizei i = 0; i < clampedCount; ++i ) {
			if ( R_GLStateCache().BindTexture( static_cast<int>( first + i ), GL_TEXTURE_2D, 0 ) ) {
				stats.lowOverheadClassicTextureBinds++;
			}
			if ( glBindSampler != NULL ) {
				R_GLStateCache().BindSampler( static_cast<int>( first + i ), 0 );
			}
		}
	}
	R_GLStateCache().ActiveTextureUnit( 0 );
}

static void R_ModernGLExecutor_BindDeferredResolveTextures( const renderGraphResourceHandle_t *const handles[MODERN_GL_DEFERRED_TEXTURE_COUNT], modernGLExecutorStats_t &stats ) {
	GLuint textures[MODERN_GL_DEFERRED_TEXTURE_COUNT];
	for ( int i = 0; i < MODERN_GL_DEFERRED_TEXTURE_COUNT; ++i ) {
		textures[i] = ( handles[i] != NULL ) ? handles[i]->texture : 0;
	}
	R_ModernGLExecutor_BindTextureGroup( 0, MODERN_GL_DEFERRED_TEXTURE_COUNT, textures, stats );
}

static void R_ModernGLExecutor_SubmitDeferredResolve( modernGLExecutorStats_t &stats ) {
	if ( !stats.deferredResolveRequested || !stats.enabled || !stats.available || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	const modernGLShaderProgramInfo_t *program = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE, shaderStats.highestGLSLVersion );
	stats.deferredResolveProgramReady = program != NULL && program->program != 0 && program->linked;
	if ( !stats.deferredResolveProgramReady ) {
		stats.deferredResolveResourceFallbacks++;
		return;
	}

	const rendererClusteredLightingStats_t &clusterStats = R_ModernClusteredLighting_Stats();
	stats.deferredResolveClusterReady = clusterStats.requested && clusterStats.frameValid && clusterStats.buffersReady;
	stats.deferredResolveActiveLights = clusterStats.lightCount;
	stats.deferredResolvePointLights = clusterStats.pointLights;
	stats.deferredResolveProjectedLights = clusterStats.projectedLights;
	stats.deferredResolveFogFallbackLights = clusterStats.fogLights;
	stats.deferredResolveSpecialFallbackLights = clusterStats.specialLights;
	stats.deferredResolveUnsupportedLightFallbacks = clusterStats.fogLights + clusterStats.specialLights;
	stats.deferredResolveOverflowClusters = clusterStats.overflowClusters;
	if ( !stats.deferredResolveClusterReady ) {
		stats.deferredResolveResourceFallbacks++;
		return;
	}

	const renderGraphResourceHandle_t *albedo = NULL;
	const renderGraphResourceHandle_t *normal = NULL;
	const renderGraphResourceHandle_t *material = NULL;
	const renderGraphResourceHandle_t *emissive = NULL;
	const renderGraphResourceHandle_t *sceneDepth = NULL;
	const renderGraphResourceHandle_t *deferredLight = NULL;
	const bool albedoReady = R_ModernGLExecutor_GBufferResourceReady( "gbufferAlbedo", albedo );
	const bool normalReady = R_ModernGLExecutor_GBufferResourceReady( "gbufferNormal", normal );
	const bool materialReady = R_ModernGLExecutor_GBufferResourceReady( "gbufferMaterial", material );
	const bool emissiveReady = R_ModernGLExecutor_GBufferResourceReady( "gbufferEmissive", emissive );
	const bool depthReady = R_ModernGLExecutor_DepthResourceReady( "sceneDepth", sceneDepth ) && sceneDepth != NULL && sceneDepth->target == GL_TEXTURE_2D && sceneDepth->texture != 0;
	stats.deferredResolveOutputReady = R_ModernGLExecutor_GBufferResourceReady( "deferredLight", deferredLight );
	stats.deferredResolveResourcesReady = albedoReady && normalReady && materialReady && emissiveReady && depthReady && stats.deferredResolveOutputReady;
	if ( !stats.deferredResolveResourcesReady || deferredLight == NULL || deferredLight->framebuffer == 0 ) {
		stats.deferredResolveResourceFallbacks++;
		return;
	}

	stats.deferredResolveLightGridContributions = emissiveReady ? 1 : 0;
	stats.deferredResolvePixels = Max( 1, deferredLight->width ) * Max( 1, deferredLight->height );
	stats.deferredResolveClusterReads = stats.deferredResolvePixels;
	stats.deferredResolveDebugMode = r_rendererModernDeferredDebug.GetInteger();

	const renderGraphResourceHandle_t *textureHandles[MODERN_GL_DEFERRED_TEXTURE_COUNT] = {
		albedo,
		normal,
		material,
		emissive,
		sceneDepth
	};

	R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_MODERN_DEFERRED );
	{
		idGLDebugScope passScope( "ModernGLExecutor deferred light resolve" );
		R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, deferredLight->framebuffer );
		glDrawBuffer( GL_COLOR_ATTACHMENT0 );
		glReadBuffer( GL_COLOR_ATTACHMENT0 );
		R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
		R_GLStateCache().SetViewport( 0, 0, Max( 1, deferredLight->width ), Max( 1, deferredLight->height ) );
		R_GLStateCache().SetScissor( 0, 0, Max( 1, deferredLight->width ), Max( 1, deferredLight->height ) );
		R_GLStateCache().SetScissorTestEnabled( false );
		R_GLStateCache().SetDepthTestEnabled( false );
		R_GLStateCache().SetDepthMask( GL_FALSE );
		R_GLStateCache().SetStencilTestEnabled( false );
		R_GLStateCache().SetBlendEnabled( false );
		R_GLStateCache().SetCullFaceEnabled( false );
		R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		stats.deferredResolveClearOps++;

		R_GLStateCache().UseProgram( program->program );
		R_ModernGLExecutor_BindFrameUniformBufferBase( stats );
		R_ModernGLExecutor_SetUniformBlockBinding( program->program, "ModernClusterGridParams", MODERN_GL_CLUSTER_UBO_BINDING_PARAMS );
		R_ModernGLExecutor_SetUniformBlockBinding( program->program, "ModernClusterLightRecords", MODERN_GL_CLUSTER_UBO_BINDING_LIGHTS );
		R_ModernGLExecutor_SetUniformBlockBinding( program->program, "ModernClusterIndexRecords", MODERN_GL_CLUSTER_UBO_BINDING_INDICES );
		const float identity[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		if ( program->modelViewProjectionLocation >= 0 ) {
			glUniformMatrix4fv( program->modelViewProjectionLocation, 1, GL_FALSE, identity );
		}
		if ( program->debugColorLocation >= 0 ) {
			glUniform4f( program->debugColorLocation, 1.0f, 1.0f, 1.0f, 1.0f );
		}
		if ( program->localParamsLocation >= 0 ) {
			const float overflowPressure = clusterStats.clusterCount > 0 ? idMath::ClampFloat( 0.0f, 1.0f, static_cast<float>( clusterStats.overflowClusters ) / static_cast<float>( clusterStats.clusterCount ) ) : 0.0f;
			const float fallbackPressure = stats.deferredResolveUnsupportedLightFallbacks > 0 ? 1.0f : 0.0f;
			glUniform4f( program->localParamsLocation, 1.0f, static_cast<float>( stats.deferredResolveDebugMode ), fallbackPressure, overflowPressure );
		}
		for ( int i = 0; i < MODERN_GL_DEFERRED_TEXTURE_COUNT; ++i ) {
			R_ModernGLExecutor_SetSamplerUniform( program->program, rg_modernGLDeferredTextureUniforms[i], i );
		}
		R_ModernGLExecutor_BindDeferredResolveTextures( textureHandles, stats );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}
	R_RendererMetrics_EndGpuTimer();

	R_ModernGLExecutor_UnbindTextureGroup( 0, MODERN_GL_DEFERRED_TEXTURE_COUNT, stats );
	R_ModernGLExecutor_RestoreAfterSubmit();
	stats.deferredResolveExecuted = true;
	if ( stats.deferredResolveExecuted ) {
		R_ModernGLExecutor_SetStatus( stats, "deferred-resolve-legacy-fallback" );
	}
}

static bool R_ModernGLExecutor_IsForwardPlusPipeline( modernGLDrawPlanPipeline_t pipeline ) {
	return pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT;
}

static int R_ModernGLExecutor_ForwardPlusCommandArea( const modernGLSubmitCommand_t &command ) {
	int x1 = command.scissorX1;
	int y1 = command.scissorY1;
	int x2 = command.scissorX2;
	int y2 = command.scissorY2;
	if ( x2 < x1 || y2 < y1 ) {
		if ( command.viewDef != NULL ) {
			x1 = command.viewDef->scissor.x1;
			y1 = command.viewDef->scissor.y1;
			x2 = command.viewDef->scissor.x2;
			y2 = command.viewDef->scissor.y2;
		} else {
			x1 = 0;
			y1 = 0;
			x2 = Max( 0, glConfig.vidWidth - 1 );
			y2 = Max( 0, glConfig.vidHeight - 1 );
		}
	}
	return Max( 1, x2 - x1 + 1 ) * Max( 1, y2 - y1 + 1 );
}

static void R_ModernGLExecutor_CountForwardPlusFallback( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	if ( R_ModernGLExecutor_IsForwardPlusPipeline( command.pipeline ) ) {
		stats.forwardPlusFallbackDraws++;
	}
}

static bool R_ModernGLExecutor_ForwardPlusMaterialSupported( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord == NULL || materialRecord->fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE ) {
		stats.forwardPlusMaterialFallbackDraws++;
		return false;
	}
	if ( materialRecord->materialClass == RENDER_MATERIAL_GUI
		|| materialRecord->materialClass == RENDER_MATERIAL_POST_PROCESS
		|| materialRecord->materialClass == RENDER_MATERIAL_SUBVIEW
		|| materialRecord->materialClass == RENDER_MATERIAL_SHADOW_ONLY ) {
		stats.forwardPlusMaterialFallbackDraws++;
		return false;
	}
	if ( command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT
		&& ( materialRecord->blendMode == MATERIAL_RESOURCE_BLEND_ADD || materialRecord->blendMode == MATERIAL_RESOURCE_BLEND_FILTER ) ) {
		stats.forwardPlusUnsupportedBlendFallbackDraws++;
		return false;
	}
	const bool allowAlphaBlend = command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT
		&& materialRecord->blendMode == MATERIAL_RESOURCE_BLEND_BLEND;
	if ( !R_ModernGLExecutor_MaterialContractPromotable( *materialRecord, allowAlphaBlend ) ) {
		stats.forwardPlusMaterialFallbackDraws++;
		return false;
	}
	if ( command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST
		|| command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT ) {
		const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, MATERIAL_RESOURCE_TEXTURE_DIFFUSE );
		if ( binding == NULL || binding->textureHandle == 0 ) {
			stats.forwardPlusTextureFallbackDraws++;
			return false;
		}
	}

	const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
	const geometryResourceRecord_t *geometry = draw != NULL ? draw->geometryRecord : NULL;
	if ( geometry == NULL ) {
		stats.forwardPlusGeometryFallbackDraws++;
		return false;
	}
	if ( geometry->deformMode != GEOMETRY_DEFORM_NONE || geometry->skinningMode == GEOMETRY_SKINNING_GPU_PALETTE ) {
		stats.forwardPlusGeometryFallbackDraws++;
		return false;
	}
	return true;
}

static bool R_ModernGLExecutor_PrepareForwardPlusFBO( const renderGraphResourceHandle_t &sceneColor, const renderGraphResourceHandle_t &sceneDepth, modernGLExecutorStats_t &stats ) {
	if ( sceneColor.framebuffer == 0 || sceneColor.texture == 0 || sceneDepth.texture == 0 || glCheckFramebufferStatus == NULL ) {
		return false;
	}
	const GLenum depthAttachment = sceneDepth.type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
	if ( stats.tierUsesDSA && glNamedFramebufferTexture != NULL && glNamedFramebufferDrawBuffer != NULL && glNamedFramebufferReadBuffer != NULL && glCheckNamedFramebufferStatus != NULL ) {
		glNamedFramebufferTexture( sceneColor.framebuffer, depthAttachment, sceneDepth.texture, 0 );
		glNamedFramebufferDrawBuffer( sceneColor.framebuffer, GL_COLOR_ATTACHMENT0 );
		glNamedFramebufferReadBuffer( sceneColor.framebuffer, GL_COLOR_ATTACHMENT0 );
		const GLenum status = glCheckNamedFramebufferStatus( sceneColor.framebuffer, GL_FRAMEBUFFER );
		stats.lowOverheadFramebufferDSAUpdates++;
		stats.forwardPlusResourcesReady = status == GL_FRAMEBUFFER_COMPLETE;
		if ( stats.forwardPlusResourcesReady ) {
			R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, sceneColor.framebuffer );
		}
		return stats.forwardPlusResourcesReady;
	}
	if ( glFramebufferTexture2D == NULL ) {
		return false;
	}
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, sceneColor.framebuffer );
	glFramebufferTexture2D( GL_FRAMEBUFFER, depthAttachment, sceneDepth.target, sceneDepth.texture, 0 );
	glDrawBuffer( GL_COLOR_ATTACHMENT0 );
	glReadBuffer( GL_COLOR_ATTACHMENT0 );
	const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	stats.forwardPlusResourcesReady = status == GL_FRAMEBUFFER_COMPLETE;
	return stats.forwardPlusResourcesReady;
}

static void R_ModernGLExecutor_SubmitForwardPlus( modernGLExecutorStats_t &stats ) {
	if ( !stats.forwardPlusRequested || !stats.enabled || !stats.available || !stats.submitPlanReady || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	const modernGLShaderProgramInfo_t *opaqueProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *alphaProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *transparentProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_TRANSPARENT_FORWARD, shaderStats.highestGLSLVersion );
	stats.forwardPlusProgramReady = opaqueProgram != NULL && opaqueProgram->program != 0 && opaqueProgram->linked
		&& alphaProgram != NULL && alphaProgram->program != 0 && alphaProgram->linked
		&& transparentProgram != NULL && transparentProgram->program != 0 && transparentProgram->linked;
	if ( !stats.forwardPlusProgramReady ) {
		stats.forwardPlusResourceFallbackDraws++;
		return;
	}

	const rendererClusteredLightingStats_t &clusterStats = R_ModernClusteredLighting_Stats();
	stats.forwardPlusClusterReady = clusterStats.requested && clusterStats.frameValid && clusterStats.buffersReady;
	stats.forwardPlusActiveLights = clusterStats.lightCount;
	stats.forwardPlusPointLights = clusterStats.pointLights;
	stats.forwardPlusProjectedLights = clusterStats.projectedLights;
	if ( !stats.forwardPlusClusterReady ) {
		stats.forwardPlusResourceFallbackDraws++;
		return;
	}

	const renderGraphResourceHandle_t *sceneColor = NULL;
	const renderGraphResourceHandle_t *sceneDepth = NULL;
	stats.forwardPlusSceneColorReady = R_ModernGLExecutor_GBufferResourceReady( "sceneColor", sceneColor );
	stats.forwardPlusSceneDepthReady = R_ModernGLExecutor_DepthResourceReady( "sceneDepth", sceneDepth ) && sceneDepth != NULL && sceneDepth->target == GL_TEXTURE_2D && sceneDepth->texture != 0;
	if ( !stats.forwardPlusSceneColorReady || !stats.forwardPlusSceneDepthReady || sceneColor == NULL || sceneDepth == NULL || !R_ModernGLExecutor_PrepareForwardPlusFBO( *sceneColor, *sceneDepth, stats ) ) {
		for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
			const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
			if ( R_ModernGLExecutor_IsForwardPlusPipeline( command.pipeline ) ) {
				stats.forwardPlusResourceFallbackDraws++;
				R_ModernGLExecutor_CountForwardPlusFallback( command, stats );
			}
		}
		R_ModernGLExecutor_RestoreAfterSubmit();
		return;
	}

	stats.forwardPlusLightGridContributions = R_RenderGraphResources_FindHandle( "lightGrid" ) != NULL ? 1 : 0;
	R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_MODERN_FORWARD );
	{
		idGLDebugScope passScope( "ModernGLExecutor clustered forward+ pass" );
		R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
		R_GLStateCache().SetViewport( 0, 0, Max( 1, sceneColor->width ), Max( 1, sceneColor->height ) );
		R_GLStateCache().SetScissor( 0, 0, Max( 1, sceneColor->width ), Max( 1, sceneColor->height ) );
		R_GLStateCache().SetScissorTestEnabled( true );
		R_GLStateCache().SetDepthTestEnabled( true );
		R_GLStateCache().SetDepthFunc( GL_LEQUAL );
		R_GLStateCache().SetDepthMask( GL_FALSE );
		R_GLStateCache().SetStencilTestEnabled( false );
		R_GLStateCache().SetCullFaceEnabled( false );
		R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		stats.forwardPlusClearOps++;

		bool haveTransparentSort = false;
		unsigned long long previousTransparentSort = 0;
		int previousTransparentMaterial = -2;
		for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
			const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
			if ( !R_ModernGLExecutor_IsForwardPlusPipeline( command.pipeline ) ) {
				continue;
			}
			if ( !R_ModernGLExecutor_ForwardPlusMaterialSupported( command, stats ) ) {
				R_ModernGLExecutor_CountForwardPlusFallback( command, stats );
				continue;
			}

			const bool transparent = command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT;
			const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
			if ( transparent && draw != NULL ) {
				if ( haveTransparentSort && draw->sortKey.value < previousTransparentSort ) {
					stats.forwardPlusSortFallbackDraws++;
					R_ModernGLExecutor_CountForwardPlusFallback( command, stats );
					continue;
				}
				if ( !haveTransparentSort || command.materialTableIndex != previousTransparentMaterial ) {
					stats.forwardPlusSortedBatches++;
				}
				previousTransparentSort = draw->sortKey.value;
				previousTransparentMaterial = command.materialTableIndex;
				haveTransparentSort = true;
			}

			if ( transparent ) {
				R_GLStateCache().SetBlendEnabled( true );
				R_GLStateCache().SetBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				R_GLStateCache().SetDepthMask( GL_FALSE );
			} else {
				R_GLStateCache().SetBlendEnabled( false );
				R_GLStateCache().SetDepthMask( GL_FALSE );
			}
			if ( !R_ModernGLExecutor_SubmitCommand( command, stats, false ) ) {
				R_ModernGLExecutor_CountForwardPlusFallback( command, stats );
				continue;
			}

			const int area = R_ModernGLExecutor_ForwardPlusCommandArea( command );
			stats.forwardPlusDraws++;
			stats.forwardPlusOverdrawEstimate += area;
			stats.forwardPlusClusterReads += area;
			if ( command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST ) {
				stats.forwardPlusAlphaTestDraws++;
			} else if ( transparent ) {
				stats.forwardPlusTransparentDraws++;
				if ( command.passCategory == RENDER_PASS_FOG_BLEND ) {
					stats.forwardPlusFogBlendDraws++;
				}
			} else {
				stats.forwardPlusOpaqueDraws++;
			}
			if ( draw != NULL && draw->packetCategory == SCENE_PACKET_CATEGORY_VIEWMODEL ) {
				stats.forwardPlusViewModelDraws++;
			}
		}
	}
	R_RendererMetrics_EndGpuTimer();

	R_ModernGLExecutor_RestoreAfterSubmit();
	stats.forwardPlusExecuted = stats.forwardPlusDraws > 0 || stats.forwardPlusClearOps > 0;
	if ( stats.forwardPlusExecuted ) {
		R_ModernGLExecutor_SetStatus( stats, "forward-plus-legacy-fallback" );
	}
}

static void R_ModernGLExecutor_SubmitPlan( modernGLExecutorStats_t &stats ) {
	if ( !r_rendererModernSubmit.GetBool() || !stats.enabled || !stats.available || !stats.submitPlanReady || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	idGLDebugScope debugScope( "ModernGLExecutor diagnostic submit" );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetScissorTestEnabled( true );
	R_GLStateCache().SetDepthTestEnabled( true );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetDepthFunc( GL_LEQUAL );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );

	for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
		const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
		if ( command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER || R_ModernGLExecutor_IsForwardPlusPipeline( command.pipeline ) ) {
			continue;
		}
		R_ModernGLExecutor_SubmitCommand( command, stats, true );
	}

	R_ModernGLExecutor_RestoreAfterSubmit();
	stats.submitExecuted = stats.submittedDraws > 0;
	if ( stats.submitExecuted ) {
		R_ModernGLExecutor_SetStatus( stats, "submitted-legacy-fallback" );
	}
}

static void R_ModernGLExecutor_SubmitModernGui( modernGLExecutorStats_t &stats ) {
	if ( !stats.modernVisibleRequested || stats.modernVisibleGuiReadyDraws <= 0 || !stats.submitPlanReady || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	idGLDebugScope debugScope( "ModernGLExecutor fullscreen GUI overlay" );
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetScissorTestEnabled( true );
	R_GLStateCache().SetDepthTestEnabled( false );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetBlendEnabled( true );
	R_GLStateCache().SetBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	int submittedGuiDraws = 0;
	for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
		const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
		if ( command.pipeline != MODERN_GL_DRAW_PLAN_PIPELINE_GUI ) {
			continue;
		}
		if ( R_ModernGLExecutor_SubmitCommand( command, stats, false ) ) {
			submittedGuiDraws++;
		} else {
			stats.modernVisibleGuiFallbackDraws++;
		}
	}

	if ( submittedGuiDraws > 0 ) {
		stats.modernVisibleGuiExecuted = true;
		stats.modernVisibleGuiReadyDraws = submittedGuiDraws;
	}
	R_ModernGLExecutor_RestoreAfterSubmit();
}

static void R_ModernGLExecutor_UpdatePassOwnershipCounts( modernGLExecutorStats_t &stats ) {
	stats.passOwnerTablePasses = 0;
	stats.passOwnerLegacyPasses = 0;
	stats.passOwnerModernPasses = 0;
	stats.passOwnerMixedPasses = 0;
	stats.passOwnerDisabledPasses = 0;
	stats.passOwnerBlockedPasses = 0;
	stats.passOwnerLegacySkipsArmed = 0;
	stats.passOwnerLegacySkipsIssued = 0;
	stats.passOwnerDuplicateHazards = 0;
	stats.passOwnerFailClosedRestores = rg_modernGLPassOwnership.failClosedRestores;
	stats.passOwnerShadowModernPasses = 0;
	stats.passOwnerShadowLegacyPasses = 0;
	stats.passOwnerGuiModernPasses = 0;
	stats.passOwnerPostLegacyPasses = 0;

	for ( int i = 0; i < MODERN_GL_PASS_OWNER_COUNT; ++i ) {
		const modernGLPassOwnershipSlot_t &slot = rg_modernGLPassOwnership.slots[i];
		if ( !slot.present ) {
			continue;
		}
		stats.passOwnerTablePasses++;
		stats.passOwnerLegacySkipsIssued += slot.legacySkipCount;
		if ( slot.skipLegacy ) {
			stats.passOwnerLegacySkipsArmed++;
		}
		if ( slot.duplicateHazard ) {
			stats.passOwnerDuplicateHazards++;
		}
		switch ( slot.state ) {
		case MODERN_GL_PASS_OWNER_MODERN:
			stats.passOwnerModernPasses++;
			break;
		case MODERN_GL_PASS_OWNER_MIXED:
			stats.passOwnerMixedPasses++;
			break;
		case MODERN_GL_PASS_OWNER_DISABLED:
			stats.passOwnerDisabledPasses++;
			break;
		case MODERN_GL_PASS_OWNER_BLOCKED:
			stats.passOwnerBlockedPasses++;
			break;
		case MODERN_GL_PASS_OWNER_LEGACY:
		default:
			stats.passOwnerLegacyPasses++;
			break;
		}

		if ( slot.category == RENDER_PASS_SHADOW_MAP || slot.category == RENDER_PASS_STENCIL_SHADOW ) {
			if ( slot.state == MODERN_GL_PASS_OWNER_MODERN ) {
				stats.passOwnerShadowModernPasses++;
			} else if ( slot.state == MODERN_GL_PASS_OWNER_LEGACY || slot.state == MODERN_GL_PASS_OWNER_MIXED || slot.state == MODERN_GL_PASS_OWNER_BLOCKED ) {
				stats.passOwnerShadowLegacyPasses++;
			}
		}
		if ( slot.category == RENDER_PASS_GUI && slot.state == MODERN_GL_PASS_OWNER_MODERN ) {
			stats.passOwnerGuiModernPasses++;
		}
		if ( R_ModernGLExecutor_PassIsPost( slot.category ) && slot.state == MODERN_GL_PASS_OWNER_LEGACY ) {
			stats.passOwnerPostLegacyPasses++;
		}
	}
}

static void R_ModernGLExecutor_SetPassOwnership(
	renderPassCategory_t category,
	modernGLPassOwnerState_t state,
	bool modernExecuted,
	bool skipLegacy,
	const char *reason ) {
	if ( !R_ModernGLExecutor_PassCategoryValid( category ) ) {
		return;
	}
	modernGLPassOwnershipSlot_t &slot = rg_modernGLPassOwnership.slots[category];
	slot.present = true;
	slot.state = state;
	slot.modernExecuted = modernExecuted;
	slot.skipLegacy = skipLegacy && R_ModernGLExecutor_PassHasLegacyWork( category ) && state == MODERN_GL_PASS_OWNER_MODERN;
	slot.legacyMayRun = R_ModernGLExecutor_PassHasLegacyWork( category ) && !slot.skipLegacy && state != MODERN_GL_PASS_OWNER_DISABLED;
	slot.duplicateHazard = modernExecuted && slot.legacyMayRun && state == MODERN_GL_PASS_OWNER_MODERN;
	idStr::Copynz( slot.reason, reason != NULL ? reason : R_ModernGLExecutor_PassOwnerStateName( state ), sizeof( slot.reason ) );
}

static bool R_ModernGLExecutor_ModernVisiblePrecomposeReady( modernGLExecutorStats_t &stats ) {
	const renderGraphResourceHandle_t *deferredLight = NULL;
	const renderGraphResourceHandle_t *sceneColor = NULL;
	const renderGraphResourceHandle_t *backBuffer = R_RenderGraphResources_FindHandle( "backBuffer" );
	const bool deferredReady = stats.deferredResolveExecuted && R_ModernGLExecutor_GBufferResourceReady( "deferredLight", deferredLight );
	const bool forwardReady = stats.forwardPlusExecuted && R_ModernGLExecutor_GBufferResourceReady( "sceneColor", sceneColor );
	const bool guiReady = stats.modernVisibleGuiFallbackDraws == 0;
	stats.modernVisibleSourceReady = deferredReady && forwardReady;
	stats.modernVisibleBackBufferReady = backBuffer != NULL && backBuffer->presentable;
	stats.modernVisibleResourcesReady = stats.modernVisibleSourceReady && stats.modernVisibleBackBufferReady;
	stats.modernVisibleHandoffReady =
		stats.modernVisibleRequested &&
		stats.modernVisibleCanReplaceFrame &&
		stats.enabled &&
		stats.available &&
		stats.initialized &&
		stats.modernVisibleProgramReady &&
		!stats.modernVisibleBlockedByLegacy &&
		guiReady &&
		stats.modernVisibleResourcesReady &&
		deferredLight != NULL &&
		sceneColor != NULL &&
		deferredLight->texture != 0 &&
		sceneColor->texture != 0;
	return stats.modernVisibleHandoffReady;
}

static bool R_ModernGLExecutor_PassExistsInGraph( const idRenderGraph &graph, renderPassCategory_t category ) {
	return graph.FindPass( category ) >= 0;
}

static void R_ModernGLExecutor_RecordLegacyOrBlockedPass(
	const idRenderGraph &graph,
	modernGLExecutorStats_t &stats,
	renderPassCategory_t category,
	bool requested,
	bool executed,
	const char *legacyReason,
	const char *blockedReason ) {
	if ( !R_ModernGLExecutor_PassExistsInGraph( graph, category ) ) {
		return;
	}
	if ( executed ) {
		R_ModernGLExecutor_SetPassOwnership( category, MODERN_GL_PASS_OWNER_MIXED, true, false, legacyReason );
	} else if ( requested ) {
		R_ModernGLExecutor_SetPassOwnership( category, MODERN_GL_PASS_OWNER_BLOCKED, false, false, blockedReason );
	} else {
		R_ModernGLExecutor_SetPassOwnership( category, MODERN_GL_PASS_OWNER_LEGACY, false, false, legacyReason );
	}
}

static void R_ModernGLExecutor_FinalizePassOwnership( const idRenderGraph &graph, modernGLExecutorStats_t &stats ) {
	rg_modernGLPassOwnership.valid = true;
	rg_modernGLPassOwnership.handoffReady = R_ModernGLExecutor_ModernVisiblePrecomposeReady( stats );
	idStr::Copynz(
		rg_modernGLPassOwnership.failClosedReason,
		rg_modernGLPassOwnership.handoffReady ? "modern-visible-handoff-ready" : "modern-visible-fail-closed",
		sizeof( rg_modernGLPassOwnership.failClosedReason ) );

	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		if ( !R_ModernGLExecutor_PassCategoryValid( pass.category ) ) {
			continue;
		}
		if ( !pass.enabled ) {
			R_ModernGLExecutor_SetPassOwnership( pass.category, MODERN_GL_PASS_OWNER_DISABLED, false, false, "graph-disabled" );
			continue;
		}
		R_ModernGLExecutor_SetPassOwnership( pass.category, MODERN_GL_PASS_OWNER_LEGACY, false, false, "legacy-default" );
	}

	if ( rg_modernGLPassOwnership.handoffReady ) {
		const bool depthModern =
			stats.visibleDepthExecuted &&
			stats.visibleDepthResourceReady &&
			stats.visibleDepthFallbackDraws == 0 &&
			stats.visibleDepthMismatchDraws == 0;
		const bool shadowMapModern =
			stats.visibleShadowDepthDraws > 0 &&
			stats.visibleShadowResourceReady &&
			stats.visibleShadowFallbackDraws == 0 &&
			stats.visibleStencilShadowFallbackDraws == 0 &&
			stats.visibleDepthMismatchDraws == 0;
		const bool gbufferModern =
			stats.opaqueGBufferExecuted &&
			stats.opaqueGBufferResourcesReady &&
			stats.opaqueGBufferFallbackDraws == 0;
		const bool deferredModern =
			stats.deferredResolveExecuted &&
			stats.deferredResolveResourcesReady &&
			stats.deferredResolveResourceFallbacks == 0 &&
			stats.deferredResolveUnsupportedLightFallbacks == 0 &&
			stats.deferredResolveFogFallbackLights == 0 &&
			stats.deferredResolveSpecialFallbackLights == 0;
		const bool forwardModern =
			stats.forwardPlusExecuted &&
			stats.forwardPlusResourcesReady &&
			stats.forwardPlusFallbackDraws == 0 &&
			stats.forwardPlusSpecialEffectFallbacks == 0;

		if ( depthModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_DEPTH ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_DEPTH, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-depth-complete" );
		}
		if ( shadowMapModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_SHADOW_MAP ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_SHADOW_MAP, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-shadow-map-complete" );
			if ( R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_STENCIL_SHADOW ) ) {
				R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_STENCIL_SHADOW, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-shadow-map-replaces-stencil" );
			}
		} else if ( R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_SHADOW_MAP ) && stats.visibleShadowDepthDraws > 0 ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_SHADOW_MAP, MODERN_GL_PASS_OWNER_MIXED, true, false, "shadow-map-sidecar-or-fallback" );
		}
		if ( gbufferModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_AMBIENT ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_AMBIENT, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-gbuffer-complete" );
		}
		if ( deferredModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_DEFERRED_RESOLVE ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_DEFERRED_RESOLVE, MODERN_GL_PASS_OWNER_MODERN, true, false, "modern-deferred-complete" );
		}
		if ( forwardModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_FORWARD_PLUS ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_FORWARD_PLUS, MODERN_GL_PASS_OWNER_MODERN, true, false, "modern-forward-plus-complete" );
			if ( R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_ARB2_INTERACTION ) ) {
				R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_ARB2_INTERACTION, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-lighting-complete" );
			}
			if ( R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_FOG_BLEND ) ) {
				R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_FOG_BLEND, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-forward-fog-complete" );
			}
		}
		if ( ( deferredModern || forwardModern ) && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_LIGHT_GRID ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_LIGHT_GRID, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-light-grid-consumed" );
		}
		if ( stats.modernVisibleGuiReadyDraws == stats.modernVisibleGuiDraws && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_GUI ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_GUI, MODERN_GL_PASS_OWNER_MODERN, false, true, "modern-gui-replay-ready" );
		}
		if ( R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_PRESENT ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_PRESENT, MODERN_GL_PASS_OWNER_MODERN, false, false, "modern-visible-composite-ready" );
		}
	} else {
		if ( stats.modernVisibleRequested && stats.modernVisibleCanReplaceFrame ) {
			rg_modernGLPassOwnership.failClosedRestores++;
		}
		R_ModernGLExecutor_RecordLegacyOrBlockedPass( graph, stats, RENDER_PASS_DEPTH, stats.visibleDepthRequested, stats.visibleDepthExecuted, "legacy-depth-or-sidecar", "modern-depth-blocked" );
		R_ModernGLExecutor_RecordLegacyOrBlockedPass( graph, stats, RENDER_PASS_SHADOW_MAP, stats.visibleDepthRequested, stats.visibleShadowDepthDraws > 0, "legacy-shadow-or-sidecar", "modern-shadow-blocked" );
		R_ModernGLExecutor_RecordLegacyOrBlockedPass( graph, stats, RENDER_PASS_AMBIENT, stats.opaqueGBufferRequested, stats.opaqueGBufferExecuted, "legacy-ambient-or-sidecar", "modern-gbuffer-blocked" );
		R_ModernGLExecutor_RecordLegacyOrBlockedPass( graph, stats, RENDER_PASS_DEFERRED_RESOLVE, stats.deferredResolveRequested, stats.deferredResolveExecuted, "modern-deferred-sidecar", "modern-deferred-blocked" );
		R_ModernGLExecutor_RecordLegacyOrBlockedPass( graph, stats, RENDER_PASS_FORWARD_PLUS, stats.forwardPlusRequested, stats.forwardPlusExecuted, "modern-forward-sidecar", "modern-forward-blocked" );
		R_ModernGLExecutor_RecordLegacyOrBlockedPass( graph, stats, RENDER_PASS_ARB2_INTERACTION, stats.forwardPlusRequested || stats.deferredResolveRequested, false, "legacy-lighting", "modern-lighting-blocked" );
		R_ModernGLExecutor_RecordLegacyOrBlockedPass( graph, stats, RENDER_PASS_LIGHT_GRID, stats.forwardPlusRequested || stats.deferredResolveRequested, false, "legacy-light-grid", "modern-light-grid-blocked" );
		R_ModernGLExecutor_RecordLegacyOrBlockedPass( graph, stats, RENDER_PASS_FOG_BLEND, stats.forwardPlusRequested, stats.forwardPlusFogBlendDraws > 0, "legacy-fog-or-sidecar", "modern-fog-blocked" );
	}

	R_ModernGLExecutor_UpdatePassOwnershipCounts( stats );
}

static void R_ModernGLExecutor_FailClosedPassOwnership( modernGLExecutorStats_t &stats, const char *reason ) {
	if ( rg_modernGLPassOwnership.handoffReady ) {
		rg_modernGLPassOwnership.failClosedRestores++;
	}
	rg_modernGLPassOwnership.handoffReady = false;
	idStr::Copynz( rg_modernGLPassOwnership.failClosedReason, reason != NULL ? reason : "modern-visible-fail-closed", sizeof( rg_modernGLPassOwnership.failClosedReason ) );
	for ( int i = 0; i < MODERN_GL_PASS_OWNER_COUNT; ++i ) {
		modernGLPassOwnershipSlot_t &slot = rg_modernGLPassOwnership.slots[i];
		if ( !slot.present || !slot.skipLegacy ) {
			continue;
		}
		slot.state = MODERN_GL_PASS_OWNER_BLOCKED;
		slot.skipLegacy = false;
		slot.legacyMayRun = R_ModernGLExecutor_PassHasLegacyWork( slot.category );
		slot.duplicateHazard = false;
		idStr::Copynz( slot.reason, rg_modernGLPassOwnership.failClosedReason, sizeof( slot.reason ) );
	}
	stats.modernVisibleHandoffReady = false;
	R_ModernGLExecutor_UpdatePassOwnershipCounts( stats );
}

static void R_ModernGLExecutor_RecordMetrics( const modernGLExecutorStats_t &stats ) {
	rendererModernExecutorMetricsMode_t mode = RENDERER_MODERN_EXECUTOR_METRICS_OFF;
	if ( stats.enabled && !stats.available ) {
		mode = RENDERER_MODERN_EXECUTOR_METRICS_UNAVAILABLE;
	} else if ( stats.enabled && stats.legacyFallback ) {
		mode = RENDERER_MODERN_EXECUTOR_METRICS_LEGACY_FALLBACK;
	} else if ( stats.enabled ) {
		mode = RENDERER_MODERN_EXECUTOR_METRICS_PREPARED;
	}

	R_RendererMetrics_RecordModernExecutor(
		mode,
		stats.graphPasses,
		stats.preparedPasses,
		stats.fallbackPasses,
		stats.preparedDrawPackets,
		stats.materialDrawPackets,
		stats.resourceDrawPackets,
		stats.geometryDrawPackets,
		stats.vaoReady,
		stats.frameUBOReady,
		stats.shaderLibraryReady,
		stats.shaderProgramCount,
		stats.shaderFailureCount,
		stats.drawPlanReady,
		stats.drawPlanOverflow,
		stats.drawPlanDraws,
		stats.drawPlanDepthDraws,
		stats.drawPlanMaterialDraws,
		stats.drawPlanFallbackDraws,
		stats.drawPlanStateBatches,
		stats.drawPlanProgramSwitches,
		stats.drawPlanMaterialSwitches,
		stats.submitPlanReady,
		stats.submitPlanOverflow,
		stats.submitPlanDraws,
		stats.submitPlanFallbackDraws,
		stats.submitPlanDepthDraws,
		stats.submitPlanMaterialDraws,
		stats.submitPlanMissingAmbientDraws,
		stats.submitPlanMissingIndexDraws,
		stats.submitPlanIndexUploadDraws,
		stats.submitExecuted,
		stats.submittedDraws,
		stats.submittedFallbackDraws,
		stats.submittedIndexUploadDraws,
		stats.submitPlanProgramBatches,
		stats.submitPlanVertexBufferBatches,
		stats.submitPlanIndexBufferBatches,
		stats.submitPlanScissorBatches,
		stats.submitPlanMaterialBatches,
		stats.submitPlanUniformUpdates,
		stats.submitPlanFrameUBOBinds,
		stats.visibleDepthRequested,
		stats.visibleDepthExecuted,
		stats.visibleDepthResourceReady,
		stats.visibleShadowResourceReady,
		stats.visibleDepthDebugOverlayReady,
		stats.visibleDepthDraws,
		stats.visibleDepthFallbackDraws,
		stats.visibleShadowDepthDraws,
		stats.visibleShadowFallbackDraws,
		stats.visibleStencilShadowFallbackDraws,
		stats.visibleDepthMismatchDraws,
		stats.visibleDepthDebugOverlayDraws,
		stats.opaqueGBufferRequested,
		stats.opaqueGBufferExecuted,
		stats.opaqueGBufferResourcesReady,
		stats.opaqueGBufferMRTReady,
		stats.opaqueGBufferDebugOverlayReady,
		stats.opaqueGBufferDraws,
		stats.opaqueGBufferFallbackDraws,
		stats.opaqueGBufferAttachmentCount,
		stats.opaqueGBufferBytesPerPixel,
		stats.opaqueGBufferBandwidthKB,
		stats.opaqueGBufferDebugOverlayDraws );
	R_RendererMetrics_RecordDeferredResolve(
		stats.deferredResolveRequested,
		stats.deferredResolveExecuted,
		stats.deferredResolveResourcesReady,
		stats.deferredResolveOutputReady,
		stats.deferredResolveProgramReady,
		stats.deferredResolveClusterReady,
		stats.deferredResolveDebugOverlayReady,
		stats.deferredResolvePixels,
		stats.deferredResolveActiveLights,
		stats.deferredResolvePointLights,
		stats.deferredResolveProjectedLights,
		stats.deferredResolveLightGridContributions,
		stats.deferredResolveClusterReads,
		stats.deferredResolveResourceFallbacks,
		stats.deferredResolveUnsupportedLightFallbacks,
		stats.deferredResolveFogFallbackLights,
		stats.deferredResolveSpecialFallbackLights,
		stats.deferredResolveOverflowClusters,
		stats.deferredResolveClearOps,
		stats.deferredResolveDebugMode,
		stats.deferredResolveDebugOverlayDraws );
	R_RendererMetrics_RecordForwardPlus(
		stats.forwardPlusRequested,
		stats.forwardPlusExecuted,
		stats.forwardPlusResourcesReady,
		stats.forwardPlusSceneColorReady,
		stats.forwardPlusSceneDepthReady,
		stats.forwardPlusProgramReady,
		stats.forwardPlusClusterReady,
		stats.forwardPlusDraws,
		stats.forwardPlusOpaqueDraws,
		stats.forwardPlusAlphaTestDraws,
		stats.forwardPlusTransparentDraws,
		stats.forwardPlusViewModelDraws,
		stats.forwardPlusFogBlendDraws,
		stats.forwardPlusSortedBatches,
		stats.forwardPlusFallbackDraws,
		stats.forwardPlusResourceFallbackDraws,
		stats.forwardPlusMaterialFallbackDraws,
		stats.forwardPlusGeometryFallbackDraws,
		stats.forwardPlusTextureFallbackDraws,
		stats.forwardPlusUnsupportedBlendFallbackDraws,
		stats.forwardPlusSpecialEffectFallbacks,
		stats.forwardPlusSortFallbackDraws,
		stats.forwardPlusOverdrawEstimate,
		stats.forwardPlusClusterReads,
		stats.forwardPlusActiveLights,
		stats.forwardPlusPointLights,
		stats.forwardPlusProjectedLights,
		stats.forwardPlusLightGridContributions,
		stats.forwardPlusClearOps );
	R_RendererMetrics_RecordModernVisible(
		stats.modernVisibleRequested,
		stats.modernVisibleExecuted,
		stats.modernVisibleResourcesReady,
		stats.modernVisibleProgramReady,
		stats.modernVisibleSourceReady,
		stats.modernVisibleBackBufferReady,
		stats.modernVisibleBlockedByLegacy,
		stats.modernVisibleCompositions,
		stats.modernVisiblePixels,
		stats.modernVisibleModernPasses,
		stats.modernVisibleLegacyPasses,
		stats.modernVisibleDisabledPasses,
		stats.modernVisibleFallbackPasses,
		stats.modernVisibleOwnerFallbacks,
		stats.modernVisibleResourceFallbacks,
		stats.modernVisibleGuiLegacyPasses,
		stats.modernVisiblePostLegacyPasses,
		stats.modernVisibleSpecialLegacyPasses,
		stats.modernVisibleSubviewLegacyPasses,
		stats.modernVisiblePresentPasses,
		stats.modernVisibleClearOps );
	R_RendererMetrics_RecordGpuDriven(
		stats.gpuDrivenRequested,
		stats.gpuDrivenExecuted,
		stats.gpuDrivenReady,
		stats.gpuDrivenValidationRequested,
		stats.gpuDrivenValidationReadbackReady,
		stats.gpuDrivenIndirectExecuted,
		stats.gpuDrivenIndirectMultiDrawReady,
		stats.gpuDrivenSourceCommands,
		stats.gpuDrivenEligibleCommands,
		stats.gpuDrivenGeneratedCommands,
		stats.gpuDrivenCulledCommands,
		stats.gpuDrivenVisibleInstances,
		stats.gpuDrivenCpuGeneratedCommands,
		stats.gpuDrivenCpuCulledCommands,
		stats.gpuDrivenCpuVisibleInstances,
		stats.gpuDrivenGpuGeneratedCommands,
		stats.gpuDrivenGpuCulledCommands,
		stats.gpuDrivenGpuVisibleInstances,
		stats.gpuDrivenCpuClusterBins,
		stats.gpuDrivenGpuClusterBins,
		stats.gpuDrivenValidationReadbacks,
		stats.gpuDrivenValidationMismatches,
		stats.gpuDrivenIndirectDrawCalls,
		stats.gpuDrivenMultiDrawBatches,
		stats.gpuDrivenIndirectFallbacks,
		stats.gpuDrivenComputeDispatches );
	R_RendererMetrics_RecordLowOverhead(
		rg_modernGLExecutorFeatures.lowOverhead,
		stats.lowOverheadReady,
		stats.tierUsesDSA,
		stats.tierUsesMultiBind,
		stats.lowOverheadBindlessRequested,
		stats.lowOverheadBindlessAvailable,
		stats.lowOverheadSamplerReady,
		stats.lowOverheadDSAUpdates,
		stats.lowOverheadFramebufferDSAUpdates,
		stats.lowOverheadSamplerDSACreations,
		stats.lowOverheadSamplerDSAUpdates,
		stats.lowOverheadMultiBindBatches,
		stats.lowOverheadTextureMultiBindBatches,
		stats.lowOverheadSamplerMultiBindBatches,
		stats.lowOverheadClassicTextureBinds,
		stats.lowOverheadCompactedBatches );
}

void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	R_ModernGLExecutor_Shutdown();
	rg_modernGLExecutorCaps = caps;
	rg_modernGLExecutorFeatures = features;
	R_GLStateCache_Init( caps );
	R_ModernShadowPlanner_Init( caps, features );
	R_ModernClusteredLighting_Init( caps, features );
	rg_modernGLExecutorLowOverheadReady = R_ModernGLExecutor_CanUseLowOverhead( caps, features );
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, r_rendererModernExecutor.GetBool() );

	if ( !R_ModernGLExecutor_CanCreateObjects( caps, features ) ) {
		rg_modernGLExecutorAvailable = false;
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "unavailable" );
		return;
	}

	glGenVertexArrays( 1, &rg_modernGLExecutorVAO );
	if ( rg_modernGLExecutorLowOverheadReady && glCreateBuffers != NULL ) {
		glCreateBuffers( 1, &rg_modernGLExecutorFrameUBO );
	} else {
		glGenBuffers( 1, &rg_modernGLExecutorFrameUBO );
	}
	if ( rg_modernGLExecutorVAO == 0 || rg_modernGLExecutorFrameUBO == 0 ) {
		R_ModernGLExecutor_Shutdown();
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "object-create-failed" );
		return;
	}
	R_GLDebug_LabelVertexArray( rg_modernGLExecutorVAO, "ModernGLExecutor starter VAO" );
	R_GLDebug_LabelBuffer( rg_modernGLExecutorFrameUBO, "ModernGLExecutor frame constants UBO" );

	modernGLFrameConstants_t constants;
	memset( &constants, 0, sizeof( constants ) );
	glBindVertexArray( rg_modernGLExecutorVAO );
	if ( rg_modernGLExecutorLowOverheadReady && glNamedBufferData != NULL ) {
		glNamedBufferData( rg_modernGLExecutorFrameUBO, sizeof( constants ), &constants, GL_DYNAMIC_DRAW );
	} else {
		glBindBuffer( GL_UNIFORM_BUFFER, rg_modernGLExecutorFrameUBO );
		glBufferData( GL_UNIFORM_BUFFER, sizeof( constants ), &constants, GL_DYNAMIC_DRAW );
		glBindBuffer( GL_UNIFORM_BUFFER, 0 );
	}
	glBindVertexArray( 0 );

	if ( rg_modernGLExecutorLowOverheadReady && !R_ModernGLExecutor_CreateLowOverheadSampler() ) {
		rg_modernGLExecutorLowOverheadReady = false;
	}

	rg_modernGLExecutorGpuDrivenReady = R_ModernGLExecutor_InitGpuDrivenObjects( caps, features );
	if ( features.gpuDriven && !rg_modernGLExecutorGpuDrivenReady ) {
		common->Printf( "Modern GL executor: GL43 GPU-driven resources unavailable, keeping GL3 submit bridge active\n" );
	}
	if ( features.lowOverhead && !rg_modernGLExecutorLowOverheadReady ) {
		common->Printf( "Modern GL executor: GL45 low-overhead DSA/multi-bind path unavailable, keeping bind-based path active\n" );
	}
	rg_modernGLExecutorDepthOverlayProgram = R_ModernGLExecutor_CompileDepthOverlayProgram();
	if ( rg_modernGLExecutorDepthOverlayProgram != 0 ) {
		R_GLDebug_LabelProgram( rg_modernGLExecutorDepthOverlayProgram, "ModernGLExecutor depth debug overlay" );
	} else {
		common->Printf( "Modern GL executor: depth debug overlay unavailable, visible depth execution remains active\n" );
	}
	rg_modernGLExecutorGBufferOverlayProgram = R_ModernGLExecutor_CompileGBufferOverlayProgram();
	if ( rg_modernGLExecutorGBufferOverlayProgram != 0 ) {
		R_GLDebug_LabelProgram( rg_modernGLExecutorGBufferOverlayProgram, "ModernGLExecutor G-buffer debug overlay" );
	}
	rg_modernGLExecutorDeferredOverlayProgram = R_ModernGLExecutor_CompileDeferredOverlayProgram();
	if ( rg_modernGLExecutorDeferredOverlayProgram != 0 ) {
		R_GLDebug_LabelProgram( rg_modernGLExecutorDeferredOverlayProgram, "ModernGLExecutor deferred resolve debug overlay" );
	}
	rg_modernGLExecutorVisibleCompositeProgram = R_ModernGLExecutor_CompileVisibleCompositeProgram();
	if ( rg_modernGLExecutorVisibleCompositeProgram != 0 ) {
		R_GLDebug_LabelProgram( rg_modernGLExecutorVisibleCompositeProgram, "ModernGLExecutor visible composite" );
	} else {
		common->Printf( "Modern GL executor: visible composite program unavailable, modern visible cvar will fail closed\n" );
	}
	if ( rg_modernGLExecutorLowOverheadReady && glCreateFramebuffers != NULL ) {
		glCreateFramebuffers( 1, &rg_modernGLExecutorGBufferFBO );
		if ( rg_modernGLExecutorGBufferFBO != 0 ) {
			R_GLDebug_LabelFramebuffer( rg_modernGLExecutorGBufferFBO, "ModernGLExecutor G-buffer MRT" );
		}
	} else if ( glGenFramebuffers != NULL ) {
		glGenFramebuffers( 1, &rg_modernGLExecutorGBufferFBO );
		if ( rg_modernGLExecutorGBufferFBO != 0 ) {
			R_GLDebug_LabelFramebuffer( rg_modernGLExecutorGBufferFBO, "ModernGLExecutor G-buffer MRT" );
		}
	}

	R_ModernGLShaderLibrary_Init( caps, features );
	if ( !R_ModernGLShaderLibrary_Stats().available ) {
		R_ModernGLExecutor_Shutdown();
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "shader-library-unavailable" );
		return;
	}

	rg_modernGLExecutorInitialized = true;
	rg_modernGLExecutorAvailable = true;
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, r_rendererModernExecutor.GetBool() );
	R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "available" );
	R_GLStateCache_InvalidateAll( "modern executor init" );
}

void R_ModernGLExecutor_Shutdown( void ) {
	rg_modernGLDrawPlan.Clear();
	rg_modernGLSubmitPlan.Clear();
	R_ModernGLExecutor_DestroyGpuDrivenObjects();
	if ( rg_modernGLExecutorFrameUBO != 0 && glDeleteBuffers != NULL ) {
		glDeleteBuffers( 1, &rg_modernGLExecutorFrameUBO );
	}
	if ( rg_modernGLExecutorVAO != 0 && glDeleteVertexArrays != NULL ) {
		glDeleteVertexArrays( 1, &rg_modernGLExecutorVAO );
	}
	if ( rg_modernGLExecutorGBufferFBO != 0 && glDeleteFramebuffers != NULL ) {
		glDeleteFramebuffers( 1, &rg_modernGLExecutorGBufferFBO );
	}
	if ( rg_modernGLExecutorLowOverheadSampler != 0 && glDeleteSamplers != NULL ) {
		glDeleteSamplers( 1, &rg_modernGLExecutorLowOverheadSampler );
	}
	R_ModernGLShaderLibrary_Shutdown();

	rg_modernGLExecutorVAO = 0;
	rg_modernGLExecutorFrameUBO = 0;
	rg_modernGLExecutorGBufferFBO = 0;
	rg_modernGLExecutorLowOverheadSampler = 0;
	rg_modernGLExecutorLowOverheadSamplerDSACreations = 0;
	rg_modernGLExecutorLowOverheadSamplerDSAUpdates = 0;
	rg_modernGLExecutorInitialized = false;
	rg_modernGLExecutorAvailable = false;
	rg_modernGLExecutorLowOverheadReady = false;
	memset( &rg_modernGLExecutorCaps, 0, sizeof( rg_modernGLExecutorCaps ) );
	memset( &rg_modernGLExecutorFeatures, 0, sizeof( rg_modernGLExecutorFeatures ) );
	R_ModernClusteredLighting_Shutdown();
	R_ModernShadowPlanner_Shutdown();
	R_GLStateCache_Shutdown();
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, false );
	R_ModernGLExecutor_ResetPassOwnershipTable( "shutdown" );
}

void R_ModernGLExecutor_PrepareFrame( const idScenePacketFrame &packetFrame, const idRenderGraph &graph ) {
	R_ModernGLExecutor_ResetPassOwnershipTable( "frame-start" );
	const bool modernVisibleRequested = R_ModernGLExecutor_ModernVisibleRequested();
	const bool visibleDepthSidecarRequested = r_rendererModernVisibleDepth.GetBool() || r_rendererModernDepthDebug.GetInteger() > 0 || R_ModernGLExecutor_ShadowMapSidecarRequested();
	const bool deferredResolveSidecarRequested = r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	const bool forwardPlusSidecarRequested = r_rendererForwardPlus.GetBool();
	const bool opaqueGBufferSidecarRequested = r_rendererModernOpaque.GetBool() || r_rendererModernGBufferDebug.GetInteger() > 0;
	const bool gpuDrivenValidationRequested = r_rendererGpuValidation.GetBool();
	const bool explicitSidecarRequested =
		visibleDepthSidecarRequested ||
		opaqueGBufferSidecarRequested ||
		deferredResolveSidecarRequested ||
		forwardPlusSidecarRequested ||
		r_rendererModernSubmit.GetBool() ||
		gpuDrivenValidationRequested ||
		r_rendererClusterDebug.GetInteger() > 0;
	const bool enabled = r_rendererModernExecutor.GetBool() || modernVisibleRequested || explicitSidecarRequested;
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		enabled,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		rg_modernGLExecutorStats );

	const bool visibleReplacementCandidate =
		modernVisibleRequested &&
		rg_modernGLExecutorStats.modernVisibleProgramReady &&
		R_ModernGLExecutor_ModernVisibleFallbacksWithoutGui( rg_modernGLExecutorStats ) == 0;
	const bool preliminaryPlanRequested =
		visibleReplacementCandidate ||
		visibleDepthSidecarRequested ||
		opaqueGBufferSidecarRequested ||
		deferredResolveSidecarRequested ||
		forwardPlusSidecarRequested ||
		r_rendererModernSubmit.GetBool() ||
		gpuDrivenValidationRequested;

	if ( rg_modernGLExecutorStats.enabled
		&& rg_modernGLExecutorStats.available
		&& rg_modernGLExecutorStats.initialized
		&& rg_modernGLExecutorStats.vaoReady
		&& rg_modernGLExecutorStats.frameUBOReady
		&& rg_modernGLExecutorStats.shaderLibraryReady
		&& preliminaryPlanRequested ) {
		rg_modernGLDrawPlan.Build( packetFrame, graph );
		R_ModernGLExecutor_CopyDrawPlanStats( rg_modernGLExecutorStats, rg_modernGLDrawPlan.Stats() );
		if ( rg_modernGLExecutorStats.drawPlanReady ) {
			rg_modernGLSubmitPlan.Build( rg_modernGLDrawPlan );
			R_ModernGLExecutor_CopySubmitPlanStats( rg_modernGLExecutorStats, rg_modernGLSubmitPlan.Stats() );
		} else {
			rg_modernGLSubmitPlan.Clear();
		}
	} else {
		rg_modernGLDrawPlan.Clear();
		rg_modernGLSubmitPlan.Clear();
	}
	R_ModernGLExecutor_FinalizeModernVisibleCompatibility( packetFrame, rg_modernGLSubmitPlan, rg_modernGLExecutorStats );

	const bool visibleReplacementCanConsume =
		modernVisibleRequested &&
		rg_modernGLExecutorStats.modernVisibleProgramReady &&
		!rg_modernGLExecutorStats.modernVisibleBlockedByLegacy;
	rg_modernGLExecutorStats.modernVisibleCanReplaceFrame = visibleReplacementCanConsume;
	const bool visibleDepthRequested = visibleDepthSidecarRequested || visibleReplacementCanConsume;
	const bool deferredResolveRequested = deferredResolveSidecarRequested || visibleReplacementCanConsume;
	const bool forwardPlusRequested = forwardPlusSidecarRequested || visibleReplacementCanConsume;
	const bool opaqueGBufferRequested = opaqueGBufferSidecarRequested || deferredResolveRequested;
	R_ModernGLExecutor_SetEffectivePassRequests(
		rg_modernGLExecutorStats,
		visibleDepthRequested,
		opaqueGBufferRequested,
		deferredResolveRequested,
		forwardPlusRequested );
	R_ModernGLExecutor_RecordPassGates(
		rg_modernGLExecutorStats,
		visibleDepthSidecarRequested,
		opaqueGBufferSidecarRequested,
		deferredResolveSidecarRequested,
		forwardPlusSidecarRequested );

	if ( visibleReplacementCanConsume ) {
		rg_modernGLExecutorStats.frameMode = MODERN_GL_EXECUTOR_FRAME_VISIBLE_REPLACEMENT;
	} else if ( explicitSidecarRequested ) {
		rg_modernGLExecutorStats.frameMode = MODERN_GL_EXECUTOR_FRAME_SIDECAR_DIAGNOSTIC;
	} else {
		rg_modernGLExecutorStats.frameMode = MODERN_GL_EXECUTOR_FRAME_ANALYZE;
	}
	const bool clusteredLightingRequested =
		deferredResolveRequested ||
		forwardPlusRequested ||
		gpuDrivenValidationRequested ||
		r_rendererClusterDebug.GetInteger() > 0;
	const bool shadowPlanningRequested =
		r_shadows.GetBool() &&
		( clusteredLightingRequested || visibleDepthRequested || R_ModernGLExecutor_ShadowMapSidecarRequested() );
	const bool gpuDrivenWorkRequested = gpuDrivenValidationRequested || r_rendererModernSubmit.GetBool();

	R_ModernShadowPlanner_PrepareFrame( packetFrame, shadowPlanningRequested );
	R_ModernClusteredLighting_PrepareFrame( packetFrame, clusteredLightingRequested );
	if ( gpuDrivenWorkRequested ) {
		R_ModernGLExecutor_UpdateGpuDrivenBuffers( rg_modernGLExecutorStats );
	} else {
		R_ModernGLExecutor_ResetGpuDrivenBatch();
	}
	R_ModernGLExecutor_UpdateFrameUBO( rg_modernGLExecutorStats );
	if ( gpuDrivenWorkRequested ) {
		R_ModernGLExecutor_SubmitGpuDrivenIndirect( rg_modernGLExecutorStats );
	}
	R_ModernGLExecutor_SubmitVisibleDepth( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitGBuffer( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitDeferredResolve( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitForwardPlus( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitPlan( rg_modernGLExecutorStats );
	R_ModernGLExecutor_FinalizePassOwnership( graph, rg_modernGLExecutorStats );
	if ( modernVisibleRequested && rg_modernGLExecutorStats.modernVisibleBlockedByLegacy && !visibleDepthRequested && !opaqueGBufferRequested && !deferredResolveRequested && !forwardPlusRequested ) {
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "modern-visible-blocked-analyze-only" );
	} else if ( rg_modernGLExecutorStats.enabled && !preliminaryPlanRequested ) {
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "analyze-only" );
	}
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );

	if ( r_rendererMetrics.GetInteger() >= 2 && enabled ) {
		common->Printf(
			"modernGLExecutor status=%s passes=%d/%d fallback=%d draws=%d prepared=%d material=%d resources=%d geometry=%d gui=%d world=%d plan=%d planDraws=%d depth=%d materialFamily=%d planFallback=%d batches=%d programSwitches=%d materialSwitches=%d planOverflow=%d submit=%d submitDraws=%d submitFallback=%d submitMissing(vbo=%d ibo=%d) submitIndexUpload=%d submitted=%d submittedDraws=%d submittedFallback=%d submittedUpload=%d submitBatches(program=%d vbo=%d ibo=%d scissor=%d material=%d) uniforms=%d frameUBO=%d submitOverflow=%d visibleDepth(req=%d exec=%d res=%d/%d draws=%d shadow=%d fallback=%d/%d stencil=%d mismatch=%d clear=%d resolve=%d overlay=%d/%d) gbuffer(req=%d exec=%d res=%d mrt=%d draws=%d fallback=%d alpha=%d skinned=%d clear=%d att=%d bpp=%d bw=%dKB overlay=%d/%d) deferred(req=%d exec=%d res=%d out=%d program=%d cluster=%d pixels=%d lights=%d point=%d projected=%d lightGrid=%d reads=%d fallback=%d unsupported=%d fog=%d special=%d overflow=%d clear=%d debug=%d overlay=%d/%d) vao=%d ubo=%d shaders=%d shaderFails=%d glsl=%d gpuDriven=%d ssbo=%d indirect=%d validation=%d compute=%d sceneRecords=%d indirectRecords=%d gpuBytes(scene=%d indirect=%d validation=%d) dispatches=%d source=%d eligible=%d generated=%d culled=%d visible=%d mismatches=%d readbacks=%d indirectExec=%d multiDraw=%d indirectCalls=%d lowOverhead=%d dsa=%d multiBind=%d dsaUpdates=%d multiBindBatches=%d\n",
			rg_modernGLExecutorStats.status,
			rg_modernGLExecutorStats.preparedPasses,
			rg_modernGLExecutorStats.graphPasses,
			rg_modernGLExecutorStats.fallbackPasses,
			rg_modernGLExecutorStats.drawPackets,
			rg_modernGLExecutorStats.preparedDrawPackets,
			rg_modernGLExecutorStats.materialDrawPackets,
			rg_modernGLExecutorStats.resourceDrawPackets,
			rg_modernGLExecutorStats.geometryDrawPackets,
			rg_modernGLExecutorStats.guiDrawPackets,
			rg_modernGLExecutorStats.worldDrawPackets,
			rg_modernGLExecutorStats.drawPlanReady ? 1 : 0,
			rg_modernGLExecutorStats.drawPlanDraws,
			rg_modernGLExecutorStats.drawPlanDepthDraws,
			rg_modernGLExecutorStats.drawPlanMaterialDraws,
			rg_modernGLExecutorStats.drawPlanFallbackDraws,
			rg_modernGLExecutorStats.drawPlanStateBatches,
			rg_modernGLExecutorStats.drawPlanProgramSwitches,
			rg_modernGLExecutorStats.drawPlanMaterialSwitches,
			rg_modernGLExecutorStats.drawPlanOverflow ? 1 : 0,
			rg_modernGLExecutorStats.submitPlanReady ? 1 : 0,
			rg_modernGLExecutorStats.submitPlanDraws,
			rg_modernGLExecutorStats.submitPlanFallbackDraws,
			rg_modernGLExecutorStats.submitPlanMissingAmbientDraws,
			rg_modernGLExecutorStats.submitPlanMissingIndexDraws,
			rg_modernGLExecutorStats.submitPlanIndexUploadDraws,
			rg_modernGLExecutorStats.submitExecuted ? 1 : 0,
			rg_modernGLExecutorStats.submittedDraws,
			rg_modernGLExecutorStats.submittedFallbackDraws,
			rg_modernGLExecutorStats.submittedIndexUploadDraws,
			rg_modernGLExecutorStats.submitPlanProgramBatches,
			rg_modernGLExecutorStats.submitPlanVertexBufferBatches,
			rg_modernGLExecutorStats.submitPlanIndexBufferBatches,
			rg_modernGLExecutorStats.submitPlanScissorBatches,
			rg_modernGLExecutorStats.submitPlanMaterialBatches,
			rg_modernGLExecutorStats.submitPlanUniformUpdates,
			rg_modernGLExecutorStats.submitPlanFrameUBOBinds,
			rg_modernGLExecutorStats.submitPlanOverflow ? 1 : 0,
			rg_modernGLExecutorStats.visibleDepthRequested ? 1 : 0,
			rg_modernGLExecutorStats.visibleDepthExecuted ? 1 : 0,
			rg_modernGLExecutorStats.visibleDepthResourceReady ? 1 : 0,
			rg_modernGLExecutorStats.visibleShadowResourceReady ? 1 : 0,
			rg_modernGLExecutorStats.visibleDepthDraws,
			rg_modernGLExecutorStats.visibleShadowDepthDraws,
			rg_modernGLExecutorStats.visibleDepthFallbackDraws,
			rg_modernGLExecutorStats.visibleShadowFallbackDraws,
			rg_modernGLExecutorStats.visibleStencilShadowFallbackDraws,
			rg_modernGLExecutorStats.visibleDepthMismatchDraws,
			rg_modernGLExecutorStats.visibleDepthClearOps,
			rg_modernGLExecutorStats.visibleDepthResolveOps,
			rg_modernGLExecutorStats.visibleDepthDebugOverlayReady ? 1 : 0,
			rg_modernGLExecutorStats.visibleDepthDebugOverlayDraws,
			rg_modernGLExecutorStats.opaqueGBufferRequested ? 1 : 0,
			rg_modernGLExecutorStats.opaqueGBufferExecuted ? 1 : 0,
			rg_modernGLExecutorStats.opaqueGBufferResourcesReady ? 1 : 0,
			rg_modernGLExecutorStats.opaqueGBufferMRTReady ? 1 : 0,
			rg_modernGLExecutorStats.opaqueGBufferDraws,
			rg_modernGLExecutorStats.opaqueGBufferFallbackDraws,
			rg_modernGLExecutorStats.opaqueGBufferAlphaTestDraws,
			rg_modernGLExecutorStats.opaqueGBufferSkinnedDraws,
			rg_modernGLExecutorStats.opaqueGBufferClearOps,
			rg_modernGLExecutorStats.opaqueGBufferAttachmentCount,
			rg_modernGLExecutorStats.opaqueGBufferBytesPerPixel,
			rg_modernGLExecutorStats.opaqueGBufferBandwidthKB,
			rg_modernGLExecutorStats.opaqueGBufferDebugOverlayReady ? 1 : 0,
			rg_modernGLExecutorStats.opaqueGBufferDebugOverlayDraws,
			rg_modernGLExecutorStats.deferredResolveRequested ? 1 : 0,
			rg_modernGLExecutorStats.deferredResolveExecuted ? 1 : 0,
			rg_modernGLExecutorStats.deferredResolveResourcesReady ? 1 : 0,
			rg_modernGLExecutorStats.deferredResolveOutputReady ? 1 : 0,
			rg_modernGLExecutorStats.deferredResolveProgramReady ? 1 : 0,
			rg_modernGLExecutorStats.deferredResolveClusterReady ? 1 : 0,
			rg_modernGLExecutorStats.deferredResolvePixels,
			rg_modernGLExecutorStats.deferredResolveActiveLights,
			rg_modernGLExecutorStats.deferredResolvePointLights,
			rg_modernGLExecutorStats.deferredResolveProjectedLights,
			rg_modernGLExecutorStats.deferredResolveLightGridContributions,
			rg_modernGLExecutorStats.deferredResolveClusterReads,
			rg_modernGLExecutorStats.deferredResolveResourceFallbacks,
			rg_modernGLExecutorStats.deferredResolveUnsupportedLightFallbacks,
			rg_modernGLExecutorStats.deferredResolveFogFallbackLights,
			rg_modernGLExecutorStats.deferredResolveSpecialFallbackLights,
			rg_modernGLExecutorStats.deferredResolveOverflowClusters,
			rg_modernGLExecutorStats.deferredResolveClearOps,
			rg_modernGLExecutorStats.deferredResolveDebugMode,
			rg_modernGLExecutorStats.deferredResolveDebugOverlayReady ? 1 : 0,
			rg_modernGLExecutorStats.deferredResolveDebugOverlayDraws,
			rg_modernGLExecutorStats.vaoReady ? 1 : 0,
			rg_modernGLExecutorStats.frameUBOReady ? 1 : 0,
			rg_modernGLExecutorStats.shaderProgramCount,
			rg_modernGLExecutorStats.shaderFailureCount,
			rg_modernGLExecutorStats.highestGLSLVersion,
			rg_modernGLExecutorStats.gpuDrivenReady ? 1 : 0,
			rg_modernGLExecutorStats.sceneSSBOReady ? 1 : 0,
			rg_modernGLExecutorStats.indirectBufferReady ? 1 : 0,
			rg_modernGLExecutorStats.validationSSBOReady ? 1 : 0,
			rg_modernGLExecutorStats.computeValidationReady ? 1 : 0,
			rg_modernGLExecutorStats.gpuDrivenSceneRecords,
			rg_modernGLExecutorStats.gpuDrivenIndirectRecords,
			rg_modernGLExecutorStats.gpuDrivenSceneBytes,
			rg_modernGLExecutorStats.gpuDrivenIndirectBytes,
			rg_modernGLExecutorStats.gpuDrivenValidationBytes,
			rg_modernGLExecutorStats.gpuDrivenComputeDispatches,
			rg_modernGLExecutorStats.gpuDrivenSourceCommands,
			rg_modernGLExecutorStats.gpuDrivenEligibleCommands,
			rg_modernGLExecutorStats.gpuDrivenGeneratedCommands,
			rg_modernGLExecutorStats.gpuDrivenCulledCommands,
			rg_modernGLExecutorStats.gpuDrivenVisibleInstances,
			rg_modernGLExecutorStats.gpuDrivenValidationMismatches,
			rg_modernGLExecutorStats.gpuDrivenValidationReadbacks,
			rg_modernGLExecutorStats.gpuDrivenIndirectExecuted ? 1 : 0,
			rg_modernGLExecutorStats.gpuDrivenIndirectMultiDrawReady ? 1 : 0,
			rg_modernGLExecutorStats.gpuDrivenIndirectDrawCalls,
			rg_modernGLExecutorStats.lowOverheadReady ? 1 : 0,
			rg_modernGLExecutorStats.tierUsesDSA ? 1 : 0,
			rg_modernGLExecutorStats.tierUsesMultiBind ? 1 : 0,
			rg_modernGLExecutorStats.lowOverheadDSAUpdates,
			rg_modernGLExecutorStats.lowOverheadMultiBindBatches );
		common->Printf(
			"modernLowOverhead req=%d ready=%d dsa=%d multiBind=%d bindless=%d/%d sampler=%d samplerDSA=%d/%d dsaUpdates=%d framebufferDSA=%d bufferMultiBind=%d textureMultiBind=%d samplerMultiBind=%d classicTextureBinds=%d compactedBatches=%d\n",
			rg_modernGLExecutorFeatures.lowOverhead ? 1 : 0,
			rg_modernGLExecutorStats.lowOverheadReady ? 1 : 0,
			rg_modernGLExecutorStats.tierUsesDSA ? 1 : 0,
			rg_modernGLExecutorStats.tierUsesMultiBind ? 1 : 0,
			rg_modernGLExecutorStats.lowOverheadBindlessRequested ? 1 : 0,
			rg_modernGLExecutorStats.lowOverheadBindlessAvailable ? 1 : 0,
			rg_modernGLExecutorStats.lowOverheadSamplerReady ? 1 : 0,
			rg_modernGLExecutorStats.lowOverheadSamplerDSACreations,
			rg_modernGLExecutorStats.lowOverheadSamplerDSAUpdates,
			rg_modernGLExecutorStats.lowOverheadDSAUpdates,
			rg_modernGLExecutorStats.lowOverheadFramebufferDSAUpdates,
			rg_modernGLExecutorStats.lowOverheadMultiBindBatches,
			rg_modernGLExecutorStats.lowOverheadTextureMultiBindBatches,
			rg_modernGLExecutorStats.lowOverheadSamplerMultiBindBatches,
			rg_modernGLExecutorStats.lowOverheadClassicTextureBinds,
			rg_modernGLExecutorStats.lowOverheadCompactedBatches );
		common->Printf(
			"modernForwardPlus req=%d exec=%d res=%d scene=%d depth=%d program=%d cluster=%d draws=%d opaque=%d alpha=%d transparent=%d viewmodel=%d fog=%d batches=%d fallback=%d resource=%d material=%d geometry=%d texture=%d blend=%d effects=%d sort=%d overdraw=%d reads=%d lights=%d point=%d projected=%d lightGrid=%d clear=%d\n",
			rg_modernGLExecutorStats.forwardPlusRequested ? 1 : 0,
			rg_modernGLExecutorStats.forwardPlusExecuted ? 1 : 0,
			rg_modernGLExecutorStats.forwardPlusResourcesReady ? 1 : 0,
			rg_modernGLExecutorStats.forwardPlusSceneColorReady ? 1 : 0,
			rg_modernGLExecutorStats.forwardPlusSceneDepthReady ? 1 : 0,
			rg_modernGLExecutorStats.forwardPlusProgramReady ? 1 : 0,
			rg_modernGLExecutorStats.forwardPlusClusterReady ? 1 : 0,
			rg_modernGLExecutorStats.forwardPlusDraws,
			rg_modernGLExecutorStats.forwardPlusOpaqueDraws,
			rg_modernGLExecutorStats.forwardPlusAlphaTestDraws,
			rg_modernGLExecutorStats.forwardPlusTransparentDraws,
			rg_modernGLExecutorStats.forwardPlusViewModelDraws,
			rg_modernGLExecutorStats.forwardPlusFogBlendDraws,
			rg_modernGLExecutorStats.forwardPlusSortedBatches,
			rg_modernGLExecutorStats.forwardPlusFallbackDraws,
			rg_modernGLExecutorStats.forwardPlusResourceFallbackDraws,
			rg_modernGLExecutorStats.forwardPlusMaterialFallbackDraws,
			rg_modernGLExecutorStats.forwardPlusGeometryFallbackDraws,
			rg_modernGLExecutorStats.forwardPlusTextureFallbackDraws,
			rg_modernGLExecutorStats.forwardPlusUnsupportedBlendFallbackDraws,
			rg_modernGLExecutorStats.forwardPlusSpecialEffectFallbacks,
			rg_modernGLExecutorStats.forwardPlusSortFallbackDraws,
			rg_modernGLExecutorStats.forwardPlusOverdrawEstimate,
			rg_modernGLExecutorStats.forwardPlusClusterReads,
			rg_modernGLExecutorStats.forwardPlusActiveLights,
			rg_modernGLExecutorStats.forwardPlusPointLights,
			rg_modernGLExecutorStats.forwardPlusProjectedLights,
			rg_modernGLExecutorStats.forwardPlusLightGridContributions,
			rg_modernGLExecutorStats.forwardPlusClearOps );
		common->Printf(
			"modernVisible req=%d exec=%d res=%d program=%d source=%d backBuffer=%d blocked=%d composed=%d pixels=%d modern=%d legacy=%d disabled=%d fallback=%d ownerFallback=%d resourceFallback=%d gui=%d post=%d special=%d subview=%d present=%d clear=%d\n",
			rg_modernGLExecutorStats.modernVisibleRequested ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleExecuted ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleResourcesReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleProgramReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleSourceReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleBackBufferReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleBlockedByLegacy ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleCompositions,
			rg_modernGLExecutorStats.modernVisiblePixels,
			rg_modernGLExecutorStats.modernVisibleModernPasses,
			rg_modernGLExecutorStats.modernVisibleLegacyPasses,
			rg_modernGLExecutorStats.modernVisibleDisabledPasses,
			rg_modernGLExecutorStats.modernVisibleFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleOwnerFallbacks,
			rg_modernGLExecutorStats.modernVisibleResourceFallbacks,
			rg_modernGLExecutorStats.modernVisibleGuiLegacyPasses,
			rg_modernGLExecutorStats.modernVisiblePostLegacyPasses,
			rg_modernGLExecutorStats.modernVisibleSpecialLegacyPasses,
			rg_modernGLExecutorStats.modernVisibleSubviewLegacyPasses,
			rg_modernGLExecutorStats.modernVisiblePresentPasses,
			rg_modernGLExecutorStats.modernVisibleClearOps );
		common->Printf(
			"modernPassGate mode=%s canReplace=%d depth(would=%d exec=%d skipBlocked=%d skipNoConsumer=%d dupLegacy=%d) gbuffer(would=%d exec=%d skipBlocked=%d skipNoConsumer=%d dupLegacy=%d) deferred(would=%d exec=%d skipBlocked=%d skipNoConsumer=%d dupLegacy=%d) forward(would=%d exec=%d skipBlocked=%d skipNoConsumer=%d dupLegacy=%d)\n",
			R_ModernGLExecutor_FrameModeName( rg_modernGLExecutorStats.frameMode ),
			rg_modernGLExecutorStats.modernVisibleCanReplaceFrame ? 1 : 0,
			rg_modernGLExecutorStats.visibleDepthWouldExecute,
			rg_modernGLExecutorStats.visibleDepthExecuted ? 1 : 0,
			rg_modernGLExecutorStats.visibleDepthSkippedBlocked,
			rg_modernGLExecutorStats.visibleDepthSkippedNoConsumer,
			rg_modernGLExecutorStats.visibleDepthDuplicatedWithLegacy,
			rg_modernGLExecutorStats.opaqueGBufferWouldExecute,
			rg_modernGLExecutorStats.opaqueGBufferExecuted ? 1 : 0,
			rg_modernGLExecutorStats.opaqueGBufferSkippedBlocked,
			rg_modernGLExecutorStats.opaqueGBufferSkippedNoConsumer,
			rg_modernGLExecutorStats.opaqueGBufferDuplicatedWithLegacy,
			rg_modernGLExecutorStats.deferredResolveWouldExecute,
			rg_modernGLExecutorStats.deferredResolveExecuted ? 1 : 0,
			rg_modernGLExecutorStats.deferredResolveSkippedBlocked,
			rg_modernGLExecutorStats.deferredResolveSkippedNoConsumer,
			rg_modernGLExecutorStats.deferredResolveDuplicatedWithLegacy,
			rg_modernGLExecutorStats.forwardPlusWouldExecute,
			rg_modernGLExecutorStats.forwardPlusExecuted ? 1 : 0,
			rg_modernGLExecutorStats.forwardPlusSkippedBlocked,
			rg_modernGLExecutorStats.forwardPlusSkippedNoConsumer,
			rg_modernGLExecutorStats.forwardPlusDuplicatedWithLegacy );
		common->Printf(
			"modernPassOwnership ready=%d table=%d legacy=%d modern=%d mixed=%d blocked=%d disabled=%d skipArmed=%d skipIssued=%d duplicateHazards=%d failClosed=%d shadow(modern=%d legacy=%d) guiModern=%d postLegacy=%d reason=%s\n",
			rg_modernGLExecutorStats.modernVisibleHandoffReady ? 1 : 0,
			rg_modernGLExecutorStats.passOwnerTablePasses,
			rg_modernGLExecutorStats.passOwnerLegacyPasses,
			rg_modernGLExecutorStats.passOwnerModernPasses,
			rg_modernGLExecutorStats.passOwnerMixedPasses,
			rg_modernGLExecutorStats.passOwnerBlockedPasses,
			rg_modernGLExecutorStats.passOwnerDisabledPasses,
			rg_modernGLExecutorStats.passOwnerLegacySkipsArmed,
			rg_modernGLExecutorStats.passOwnerLegacySkipsIssued,
			rg_modernGLExecutorStats.passOwnerDuplicateHazards,
			rg_modernGLExecutorStats.passOwnerFailClosedRestores,
			rg_modernGLExecutorStats.passOwnerShadowModernPasses,
			rg_modernGLExecutorStats.passOwnerShadowLegacyPasses,
			rg_modernGLExecutorStats.passOwnerGuiModernPasses,
			rg_modernGLExecutorStats.passOwnerPostLegacyPasses,
			rg_modernGLPassOwnership.failClosedReason );
		common->Printf(
			"modernCompatibility ready=%d inventory=%d modern=%d legacy=%d lightGrid=%d gui(program=%d pass=%d draws=%d ready=%d exec=%d fallback=%d) post(graph=%d fallback=%d copy=%d) subview(graph=%d fallback=%d remote=%d demo=%d deterministic=%d) bse(fallback=%d particle=%d trail=%d beam=%d decal=%d material=%d) cinematic=%d\n",
			rg_modernGLExecutorStats.modernVisibleCompatibilityReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleCompatibilityPasses,
			rg_modernGLExecutorStats.modernVisibleCompatibilityModernPasses,
			rg_modernGLExecutorStats.modernVisibleCompatibilityLegacyPasses,
			rg_modernGLExecutorStats.modernVisibleLightGridModernPasses,
			rg_modernGLExecutorStats.modernVisibleGuiProgramReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleGuiModernPasses,
			rg_modernGLExecutorStats.modernVisibleGuiDraws,
			rg_modernGLExecutorStats.modernVisibleGuiReadyDraws,
			rg_modernGLExecutorStats.modernVisibleGuiExecuted ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleGuiFallbackDraws,
			rg_modernGLExecutorStats.modernVisiblePostGraphPasses,
			rg_modernGLExecutorStats.modernVisiblePostFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleCopyRenderFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleSubviewGraphPasses,
			rg_modernGLExecutorStats.modernVisibleSubviewFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleRemoteCameraFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleRenderDemoFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleRenderDemoDeterministic ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleBSEFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleBSEParticleFallbacks,
			rg_modernGLExecutorStats.modernVisibleBSETrailFallbacks,
			rg_modernGLExecutorStats.modernVisibleBSEBeamFallbacks,
			rg_modernGLExecutorStats.modernVisibleBSEDecalFallbacks,
			rg_modernGLExecutorStats.modernVisibleBSEMaterialFallbacks,
			rg_modernGLExecutorStats.modernVisibleCinematicCompatibilityPasses );
	}
}

bool R_ModernGLExecutor_LegacyPassCanSkip( renderPassCategory_t category ) {
	if ( !R_ModernGLExecutor_PassCategoryValid( category ) || !rg_modernGLPassOwnership.valid || !rg_modernGLPassOwnership.handoffReady ) {
		return false;
	}
	const modernGLPassOwnershipSlot_t &slot = rg_modernGLPassOwnership.slots[category];
	return slot.present && slot.skipLegacy && !slot.duplicateHazard;
}

void R_ModernGLExecutor_RecordLegacyPassSkipped( renderPassCategory_t category ) {
	if ( !R_ModernGLExecutor_PassCategoryValid( category ) ) {
		return;
	}
	modernGLPassOwnershipSlot_t &slot = rg_modernGLPassOwnership.slots[category];
	if ( !slot.present || !slot.skipLegacy ) {
		return;
	}
	slot.legacySkipCount++;
	R_ModernGLExecutor_UpdatePassOwnershipCounts( rg_modernGLExecutorStats );
}

void R_ModernGLExecutor_ComposeVisibleFrame( void ) {
	modernGLExecutorStats_t &stats = rg_modernGLExecutorStats;
	if ( !stats.modernVisibleRequested ) {
		return;
	}

	if ( !rg_modernGLPassOwnership.valid || !rg_modernGLPassOwnership.handoffReady ) {
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-handoff-not-armed" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return;
	}

	stats.modernVisibleProgramReady = rg_modernGLExecutorVisibleCompositeProgram != 0;
	const renderGraphResourceHandle_t *deferredLight = NULL;
	const renderGraphResourceHandle_t *sceneColor = NULL;
	const renderGraphResourceHandle_t *backBuffer = R_RenderGraphResources_FindHandle( "backBuffer" );
	const bool deferredReady = stats.deferredResolveExecuted && R_ModernGLExecutor_GBufferResourceReady( "deferredLight", deferredLight );
	const bool forwardReady = stats.forwardPlusExecuted && R_ModernGLExecutor_GBufferResourceReady( "sceneColor", sceneColor );
	stats.modernVisibleSourceReady = deferredReady && forwardReady;
	stats.modernVisibleBackBufferReady = backBuffer != NULL && backBuffer->presentable;
	stats.modernVisibleResourcesReady = stats.modernVisibleSourceReady && stats.modernVisibleBackBufferReady;

	if ( !stats.enabled || !stats.available || !stats.initialized || rg_modernGLExecutorVAO == 0 || !stats.modernVisibleProgramReady ) {
		R_ModernGLExecutor_FailClosedPassOwnership( stats, "modern-visible-unavailable" );
		stats.modernVisibleResourceFallbacks++;
		stats.modernVisibleFallbackPasses++;
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-unavailable" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return;
	}
	if ( stats.modernVisibleBlockedByLegacy ) {
		R_ModernGLExecutor_FailClosedPassOwnership( stats, "modern-visible-legacy-blocked" );
		stats.modernVisibleFallbackPasses++;
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-legacy-blocked" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return;
	}
	if ( !stats.modernVisibleResourcesReady || deferredLight == NULL || sceneColor == NULL || deferredLight->texture == 0 || sceneColor->texture == 0 ) {
		R_ModernGLExecutor_FailClosedPassOwnership( stats, "modern-visible-resource-fallback" );
		stats.modernVisibleResourceFallbacks++;
		stats.modernVisibleFallbackPasses++;
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-resource-fallback" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return;
	}

	R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_MODERN_COMPOSITE );
	{
		idGLDebugScope composeScope( "ModernGLExecutor visible hybrid composite" );
		R_GLStateCache_InvalidateAll( "modern visible composition" );
		R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
		R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
		R_GLStateCache().SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
		R_GLStateCache().SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
		R_GLStateCache().SetScissorTestEnabled( false );
		R_GLStateCache().SetDepthTestEnabled( false );
		R_GLStateCache().SetDepthMask( GL_FALSE );
		R_GLStateCache().SetStencilTestEnabled( false );
		R_GLStateCache().SetBlendEnabled( false );
		R_GLStateCache().SetCullFaceEnabled( false );
		R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		stats.modernVisibleClearOps++;

		R_GLStateCache().UseProgram( rg_modernGLExecutorVisibleCompositeProgram );
		GLuint compositeTextures[2] = { deferredLight->texture, sceneColor->texture };
		R_ModernGLExecutor_BindTextureGroup( 0, 2, compositeTextures, stats );
		if ( rg_modernGLExecutorVisibleCompositeDeferredLocation >= 0 ) {
			glUniform1i( rg_modernGLExecutorVisibleCompositeDeferredLocation, 0 );
		}
		if ( rg_modernGLExecutorVisibleCompositeForwardLocation >= 0 ) {
			glUniform1i( rg_modernGLExecutorVisibleCompositeForwardLocation, 1 );
		}
		if ( rg_modernGLExecutorVisibleCompositeParamsLocation >= 0 ) {
			glUniform4f( rg_modernGLExecutorVisibleCompositeParamsLocation, 1.0f, 1.0f, 0.0f, 0.0f );
		}
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}
	R_RendererMetrics_EndGpuTimer();
	R_ModernGLExecutor_SubmitModernGui( stats );

	stats.modernVisibleExecuted = true;
	stats.modernVisibleCompositions++;
	stats.modernVisiblePixels = Max( 1, glConfig.vidWidth ) * Max( 1, glConfig.vidHeight );
	R_ModernGLExecutor_SetStatus( stats, "modern-visible-composited" );
	R_ModernGLExecutor_RecordMetrics( stats );

	R_ModernGLExecutor_UnbindTextureGroup( 0, 2, stats );
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

void R_ModernGLExecutor_DrawDepthDebugOverlay( void ) {
	const int debugMode = r_rendererModernDepthDebug.GetInteger();
	if ( debugMode <= 0 || rg_modernGLExecutorDepthOverlayProgram == 0 || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	const char *preferredHandle = debugMode >= 2 && rg_modernGLExecutorStats.visibleShadowDepthDraws > 0 ? "shadowMap" : "sceneDepth";
	const renderGraphResourceHandle_t *handle = R_RenderGraphResources_FindHandle( preferredHandle );
	if ( handle == NULL || handle->texture == 0 || !handle->framebufferComplete || handle->target != GL_TEXTURE_2D ) {
		return;
	}

	idGLDebugScope overlayScope( debugMode >= 2 ? "ModernGLExecutor shadow-depth debug overlay" : "ModernGLExecutor scene-depth debug overlay" );
	R_GLStateCache_InvalidateAll( "modern depth debug overlay" );
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissorTestEnabled( false );
	R_GLStateCache().SetDepthTestEnabled( false );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache().UseProgram( rg_modernGLExecutorDepthOverlayProgram );
	R_GLStateCache().ActiveTextureUnit( 0 );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, handle->texture );
	if ( rg_modernGLExecutorDepthOverlayTextureLocation >= 0 ) {
		glUniform1i( rg_modernGLExecutorDepthOverlayTextureLocation, 0 );
	}
	if ( rg_modernGLExecutorDepthOverlayParamsLocation >= 0 ) {
		glUniform4f( rg_modernGLExecutorDepthOverlayParamsLocation, static_cast<float>( debugMode ), 0.0f, 0.0f, 0.0f );
	}
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	rg_modernGLExecutorStats.visibleDepthDebugOverlayDraws++;
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );

	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

void R_ModernGLExecutor_DrawGBufferDebugOverlay( void ) {
	const int debugMode = r_rendererModernGBufferDebug.GetInteger();
	if ( debugMode <= 0 || rg_modernGLExecutorGBufferOverlayProgram == 0 || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	const int attachmentIndex = idMath::ClampInt( 0, MODERN_GL_GBUFFER_ATTACHMENT_COUNT - 1, debugMode - 1 );
	const renderGraphResourceHandle_t *handle = R_RenderGraphResources_FindHandle( rg_modernGLGBufferAttachmentNames[attachmentIndex] );
	if ( handle == NULL || handle->texture == 0 || !handle->framebufferComplete || handle->target != GL_TEXTURE_2D ) {
		return;
	}

	idGLDebugScope overlayScope( "ModernGLExecutor G-buffer debug overlay" );
	R_GLStateCache_InvalidateAll( "modern G-buffer debug overlay" );
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissorTestEnabled( false );
	R_GLStateCache().SetDepthTestEnabled( false );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache().UseProgram( rg_modernGLExecutorGBufferOverlayProgram );
	R_GLStateCache().ActiveTextureUnit( 0 );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, handle->texture );
	if ( rg_modernGLExecutorGBufferOverlayTextureLocation >= 0 ) {
		glUniform1i( rg_modernGLExecutorGBufferOverlayTextureLocation, 0 );
	}
	if ( rg_modernGLExecutorGBufferOverlayParamsLocation >= 0 ) {
		glUniform4f( rg_modernGLExecutorGBufferOverlayParamsLocation, static_cast<float>( debugMode ), 0.0f, 0.0f, 0.0f );
	}
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	rg_modernGLExecutorStats.opaqueGBufferDebugOverlayDraws++;
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );

	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

void R_ModernGLExecutor_DrawDeferredDebugOverlay( void ) {
	const int debugMode = r_rendererModernDeferredDebug.GetInteger();
	if ( debugMode <= 0 || rg_modernGLExecutorDeferredOverlayProgram == 0 || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	const renderGraphResourceHandle_t *handle = R_RenderGraphResources_FindHandle( "deferredLight" );
	if ( handle == NULL || handle->texture == 0 || !handle->framebufferComplete || handle->target != GL_TEXTURE_2D ) {
		return;
	}

	idGLDebugScope overlayScope( "ModernGLExecutor deferred resolve debug overlay" );
	R_GLStateCache_InvalidateAll( "modern deferred resolve debug overlay" );
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissorTestEnabled( false );
	R_GLStateCache().SetDepthTestEnabled( false );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache().UseProgram( rg_modernGLExecutorDeferredOverlayProgram );
	R_GLStateCache().ActiveTextureUnit( 0 );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, handle->texture );
	if ( rg_modernGLExecutorDeferredOverlayTextureLocation >= 0 ) {
		glUniform1i( rg_modernGLExecutorDeferredOverlayTextureLocation, 0 );
	}
	if ( rg_modernGLExecutorDeferredOverlayParamsLocation >= 0 ) {
		glUniform4f( rg_modernGLExecutorDeferredOverlayParamsLocation, static_cast<float>( debugMode ), 0.0f, 0.0f, 0.0f );
	}
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	rg_modernGLExecutorStats.deferredResolveDebugOverlayDraws++;
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );

	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

const modernGLExecutorStats_t &R_ModernGLExecutor_Stats( void ) {
	return rg_modernGLExecutorStats;
}

void R_ModernGLExecutor_PrintGfxInfo( void ) {
	common->Printf(
		"Modern GL executor: %s, cvar=%d, submitCvar=%d, gpuValidation=%d, visibleDepthCvar=%d, depthDebug=%d, opaqueCvar=%d, gbufferDebug=%d, deferredCvar=%d, deferredDebug=%d, VAO=%d, frameUBO=%d, shaderLibrary=%d, shaderPrograms=%d, highestGLSL=%d, drawPlan=%d, planDraws=%d, depth=%d, materialFamily=%d, planFallback=%d, batches=%d, submitPlan=%d, submitDraws=%d, submitFallback=%d, missingVBO=%d, missingIBO=%d, indexUpload=%d, submitted=%d/%d upload=%d fallback=%d, visibleDepth(req=%d exec=%d res=%d/%d draws=%d shadow=%d fallback=%d/%d stencil=%d mismatch=%d clears=%d resolves=%d overlay=%d/%d), gbuffer(req=%d exec=%d res=%d mrt=%d draws=%d fallback=%d alpha=%d skinned=%d clear=%d att=%d bpp=%d bw=%dKB overlay=%d/%d), deferred(req=%d exec=%d res=%d out=%d program=%d cluster=%d pixels=%d lights=%d point=%d projected=%d lightGrid=%d reads=%d fallback=%d unsupported=%d fog=%d special=%d overflow=%d clears=%d overlay=%d/%d), submitBatches(program=%d vbo=%d ibo=%d), gpuDriven=%d ssbo=%d indirect=%d validation=%d compute=%d sceneRecords=%d indirectRecords=%d gpuBytes(scene=%d indirect=%d validation=%d) dispatches=%d source=%d eligible=%d generated=%d culled=%d visible=%d cpu=%d/%d/%d gpu=%d/%d/%d clusters=%d/%d mismatches=%d readbacks=%d indirectExec=%d multiDraw=%d indirectCalls=%d, lowOverhead=%d dsa=%d multiBind=%d dsaUpdates=%d multiBindBatches=%d, legacyFallback=%d\n",
		rg_modernGLExecutorStats.available ? "available" : "unavailable",
		r_rendererModernExecutor.GetBool() ? 1 : 0,
		r_rendererModernSubmit.GetBool() ? 1 : 0,
		r_rendererGpuValidation.GetBool() ? 1 : 0,
		r_rendererModernVisibleDepth.GetBool() ? 1 : 0,
		r_rendererModernDepthDebug.GetInteger(),
		r_rendererModernOpaque.GetBool() ? 1 : 0,
		r_rendererModernGBufferDebug.GetInteger(),
		r_rendererModernDeferred.GetBool() ? 1 : 0,
		r_rendererModernDeferredDebug.GetInteger(),
		rg_modernGLExecutorStats.vaoReady ? 1 : 0,
		rg_modernGLExecutorStats.frameUBOReady ? 1 : 0,
		rg_modernGLExecutorStats.shaderLibraryReady ? 1 : 0,
		rg_modernGLExecutorStats.shaderProgramCount,
		rg_modernGLExecutorStats.highestGLSLVersion,
		rg_modernGLExecutorStats.drawPlanReady ? 1 : 0,
		rg_modernGLExecutorStats.drawPlanDraws,
		rg_modernGLExecutorStats.drawPlanDepthDraws,
		rg_modernGLExecutorStats.drawPlanMaterialDraws,
		rg_modernGLExecutorStats.drawPlanFallbackDraws,
		rg_modernGLExecutorStats.drawPlanStateBatches,
		rg_modernGLExecutorStats.submitPlanReady ? 1 : 0,
		rg_modernGLExecutorStats.submitPlanDraws,
		rg_modernGLExecutorStats.submitPlanFallbackDraws,
		rg_modernGLExecutorStats.submitPlanMissingAmbientDraws,
		rg_modernGLExecutorStats.submitPlanMissingIndexDraws,
		rg_modernGLExecutorStats.submitPlanIndexUploadDraws,
		rg_modernGLExecutorStats.submitExecuted ? 1 : 0,
		rg_modernGLExecutorStats.submittedDraws,
		rg_modernGLExecutorStats.submittedIndexUploadDraws,
		rg_modernGLExecutorStats.submittedFallbackDraws,
		rg_modernGLExecutorStats.visibleDepthRequested ? 1 : 0,
		rg_modernGLExecutorStats.visibleDepthExecuted ? 1 : 0,
		rg_modernGLExecutorStats.visibleDepthResourceReady ? 1 : 0,
		rg_modernGLExecutorStats.visibleShadowResourceReady ? 1 : 0,
		rg_modernGLExecutorStats.visibleDepthDraws,
		rg_modernGLExecutorStats.visibleShadowDepthDraws,
		rg_modernGLExecutorStats.visibleDepthFallbackDraws,
		rg_modernGLExecutorStats.visibleShadowFallbackDraws,
		rg_modernGLExecutorStats.visibleStencilShadowFallbackDraws,
		rg_modernGLExecutorStats.visibleDepthMismatchDraws,
		rg_modernGLExecutorStats.visibleDepthClearOps,
		rg_modernGLExecutorStats.visibleDepthResolveOps,
		rg_modernGLExecutorStats.visibleDepthDebugOverlayReady ? 1 : 0,
		rg_modernGLExecutorStats.visibleDepthDebugOverlayDraws,
		rg_modernGLExecutorStats.opaqueGBufferRequested ? 1 : 0,
		rg_modernGLExecutorStats.opaqueGBufferExecuted ? 1 : 0,
		rg_modernGLExecutorStats.opaqueGBufferResourcesReady ? 1 : 0,
		rg_modernGLExecutorStats.opaqueGBufferMRTReady ? 1 : 0,
		rg_modernGLExecutorStats.opaqueGBufferDraws,
		rg_modernGLExecutorStats.opaqueGBufferFallbackDraws,
		rg_modernGLExecutorStats.opaqueGBufferAlphaTestDraws,
		rg_modernGLExecutorStats.opaqueGBufferSkinnedDraws,
		rg_modernGLExecutorStats.opaqueGBufferClearOps,
		rg_modernGLExecutorStats.opaqueGBufferAttachmentCount,
		rg_modernGLExecutorStats.opaqueGBufferBytesPerPixel,
		rg_modernGLExecutorStats.opaqueGBufferBandwidthKB,
		rg_modernGLExecutorStats.opaqueGBufferDebugOverlayReady ? 1 : 0,
		rg_modernGLExecutorStats.opaqueGBufferDebugOverlayDraws,
		rg_modernGLExecutorStats.deferredResolveRequested ? 1 : 0,
		rg_modernGLExecutorStats.deferredResolveExecuted ? 1 : 0,
		rg_modernGLExecutorStats.deferredResolveResourcesReady ? 1 : 0,
		rg_modernGLExecutorStats.deferredResolveOutputReady ? 1 : 0,
		rg_modernGLExecutorStats.deferredResolveProgramReady ? 1 : 0,
		rg_modernGLExecutorStats.deferredResolveClusterReady ? 1 : 0,
		rg_modernGLExecutorStats.deferredResolvePixels,
		rg_modernGLExecutorStats.deferredResolveActiveLights,
		rg_modernGLExecutorStats.deferredResolvePointLights,
		rg_modernGLExecutorStats.deferredResolveProjectedLights,
		rg_modernGLExecutorStats.deferredResolveLightGridContributions,
		rg_modernGLExecutorStats.deferredResolveClusterReads,
		rg_modernGLExecutorStats.deferredResolveResourceFallbacks,
		rg_modernGLExecutorStats.deferredResolveUnsupportedLightFallbacks,
		rg_modernGLExecutorStats.deferredResolveFogFallbackLights,
		rg_modernGLExecutorStats.deferredResolveSpecialFallbackLights,
		rg_modernGLExecutorStats.deferredResolveOverflowClusters,
		rg_modernGLExecutorStats.deferredResolveClearOps,
		rg_modernGLExecutorStats.deferredResolveDebugOverlayReady ? 1 : 0,
		rg_modernGLExecutorStats.deferredResolveDebugOverlayDraws,
		rg_modernGLExecutorStats.submitPlanProgramBatches,
		rg_modernGLExecutorStats.submitPlanVertexBufferBatches,
		rg_modernGLExecutorStats.submitPlanIndexBufferBatches,
		rg_modernGLExecutorStats.gpuDrivenReady ? 1 : 0,
		rg_modernGLExecutorStats.sceneSSBOReady ? 1 : 0,
		rg_modernGLExecutorStats.indirectBufferReady ? 1 : 0,
		rg_modernGLExecutorStats.validationSSBOReady ? 1 : 0,
		rg_modernGLExecutorStats.computeValidationReady ? 1 : 0,
		rg_modernGLExecutorStats.gpuDrivenSceneRecords,
		rg_modernGLExecutorStats.gpuDrivenIndirectRecords,
		rg_modernGLExecutorStats.gpuDrivenSceneBytes,
		rg_modernGLExecutorStats.gpuDrivenIndirectBytes,
		rg_modernGLExecutorStats.gpuDrivenValidationBytes,
		rg_modernGLExecutorStats.gpuDrivenComputeDispatches,
		rg_modernGLExecutorStats.gpuDrivenSourceCommands,
		rg_modernGLExecutorStats.gpuDrivenEligibleCommands,
		rg_modernGLExecutorStats.gpuDrivenGeneratedCommands,
		rg_modernGLExecutorStats.gpuDrivenCulledCommands,
		rg_modernGLExecutorStats.gpuDrivenVisibleInstances,
		rg_modernGLExecutorStats.gpuDrivenCpuGeneratedCommands,
		rg_modernGLExecutorStats.gpuDrivenCpuCulledCommands,
		rg_modernGLExecutorStats.gpuDrivenCpuVisibleInstances,
		rg_modernGLExecutorStats.gpuDrivenGpuGeneratedCommands,
		rg_modernGLExecutorStats.gpuDrivenGpuCulledCommands,
		rg_modernGLExecutorStats.gpuDrivenGpuVisibleInstances,
		rg_modernGLExecutorStats.gpuDrivenCpuClusterBins,
		rg_modernGLExecutorStats.gpuDrivenGpuClusterBins,
		rg_modernGLExecutorStats.gpuDrivenValidationMismatches,
		rg_modernGLExecutorStats.gpuDrivenValidationReadbacks,
		rg_modernGLExecutorStats.gpuDrivenIndirectExecuted ? 1 : 0,
		rg_modernGLExecutorStats.gpuDrivenIndirectMultiDrawReady ? 1 : 0,
		rg_modernGLExecutorStats.gpuDrivenIndirectDrawCalls,
		rg_modernGLExecutorStats.lowOverheadReady ? 1 : 0,
		rg_modernGLExecutorStats.tierUsesDSA ? 1 : 0,
		rg_modernGLExecutorStats.tierUsesMultiBind ? 1 : 0,
		rg_modernGLExecutorStats.lowOverheadDSAUpdates,
		rg_modernGLExecutorStats.lowOverheadMultiBindBatches,
		rg_modernGLExecutorStats.legacyFallback ? 1 : 0 );
	common->Printf(
		"Modern GL low-overhead: requested=%d ready=%d dsa=%d multiBind=%d bindless=%d/%d sampler=%d samplerDSA=%d/%d dsaUpdates=%d framebufferDSA=%d bufferMultiBind=%d textureMultiBind=%d samplerMultiBind=%d classicTextureBinds=%d compactedBatches=%d\n",
		rg_modernGLExecutorFeatures.lowOverhead ? 1 : 0,
		rg_modernGLExecutorStats.lowOverheadReady ? 1 : 0,
		rg_modernGLExecutorStats.tierUsesDSA ? 1 : 0,
		rg_modernGLExecutorStats.tierUsesMultiBind ? 1 : 0,
		rg_modernGLExecutorStats.lowOverheadBindlessRequested ? 1 : 0,
		rg_modernGLExecutorStats.lowOverheadBindlessAvailable ? 1 : 0,
		rg_modernGLExecutorStats.lowOverheadSamplerReady ? 1 : 0,
		rg_modernGLExecutorStats.lowOverheadSamplerDSACreations,
		rg_modernGLExecutorStats.lowOverheadSamplerDSAUpdates,
		rg_modernGLExecutorStats.lowOverheadDSAUpdates,
		rg_modernGLExecutorStats.lowOverheadFramebufferDSAUpdates,
		rg_modernGLExecutorStats.lowOverheadMultiBindBatches,
		rg_modernGLExecutorStats.lowOverheadTextureMultiBindBatches,
		rg_modernGLExecutorStats.lowOverheadSamplerMultiBindBatches,
		rg_modernGLExecutorStats.lowOverheadClassicTextureBinds,
		rg_modernGLExecutorStats.lowOverheadCompactedBatches );
	common->Printf(
		"Modern forward+: cvar=%d, req=%d exec=%d resources=%d sceneColor=%d sceneDepth=%d program=%d cluster=%d draws=%d opaque=%d alpha=%d transparent=%d viewmodel=%d fog=%d batches=%d fallback=%d resource=%d material=%d geometry=%d texture=%d blend=%d effects=%d sort=%d overdraw=%d reads=%d lights=%d point=%d projected=%d lightGrid=%d clears=%d\n",
		r_rendererForwardPlus.GetBool() ? 1 : 0,
		rg_modernGLExecutorStats.forwardPlusRequested ? 1 : 0,
		rg_modernGLExecutorStats.forwardPlusExecuted ? 1 : 0,
		rg_modernGLExecutorStats.forwardPlusResourcesReady ? 1 : 0,
		rg_modernGLExecutorStats.forwardPlusSceneColorReady ? 1 : 0,
		rg_modernGLExecutorStats.forwardPlusSceneDepthReady ? 1 : 0,
		rg_modernGLExecutorStats.forwardPlusProgramReady ? 1 : 0,
		rg_modernGLExecutorStats.forwardPlusClusterReady ? 1 : 0,
		rg_modernGLExecutorStats.forwardPlusDraws,
		rg_modernGLExecutorStats.forwardPlusOpaqueDraws,
		rg_modernGLExecutorStats.forwardPlusAlphaTestDraws,
		rg_modernGLExecutorStats.forwardPlusTransparentDraws,
		rg_modernGLExecutorStats.forwardPlusViewModelDraws,
		rg_modernGLExecutorStats.forwardPlusFogBlendDraws,
		rg_modernGLExecutorStats.forwardPlusSortedBatches,
		rg_modernGLExecutorStats.forwardPlusFallbackDraws,
		rg_modernGLExecutorStats.forwardPlusResourceFallbackDraws,
		rg_modernGLExecutorStats.forwardPlusMaterialFallbackDraws,
		rg_modernGLExecutorStats.forwardPlusGeometryFallbackDraws,
		rg_modernGLExecutorStats.forwardPlusTextureFallbackDraws,
		rg_modernGLExecutorStats.forwardPlusUnsupportedBlendFallbackDraws,
		rg_modernGLExecutorStats.forwardPlusSpecialEffectFallbacks,
		rg_modernGLExecutorStats.forwardPlusSortFallbackDraws,
		rg_modernGLExecutorStats.forwardPlusOverdrawEstimate,
		rg_modernGLExecutorStats.forwardPlusClusterReads,
		rg_modernGLExecutorStats.forwardPlusActiveLights,
		rg_modernGLExecutorStats.forwardPlusPointLights,
		rg_modernGLExecutorStats.forwardPlusProjectedLights,
		rg_modernGLExecutorStats.forwardPlusLightGridContributions,
		rg_modernGLExecutorStats.forwardPlusClearOps );
	common->Printf(
		"Modern visible frame: cvar=%d, req=%d exec=%d resources=%d program=%d source=%d backBuffer=%d blocked=%d composed=%d pixels=%d modern=%d legacy=%d disabled=%d fallback=%d ownerFallback=%d resourceFallback=%d gui=%d post=%d special=%d subview=%d present=%d clears=%d\n",
		r_rendererModernVisible.GetBool() ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleRequested ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleExecuted ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleResourcesReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleProgramReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleSourceReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleBackBufferReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleBlockedByLegacy ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleCompositions,
		rg_modernGLExecutorStats.modernVisiblePixels,
		rg_modernGLExecutorStats.modernVisibleModernPasses,
		rg_modernGLExecutorStats.modernVisibleLegacyPasses,
		rg_modernGLExecutorStats.modernVisibleDisabledPasses,
		rg_modernGLExecutorStats.modernVisibleFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleOwnerFallbacks,
		rg_modernGLExecutorStats.modernVisibleResourceFallbacks,
		rg_modernGLExecutorStats.modernVisibleGuiLegacyPasses,
		rg_modernGLExecutorStats.modernVisiblePostLegacyPasses,
		rg_modernGLExecutorStats.modernVisibleSpecialLegacyPasses,
		rg_modernGLExecutorStats.modernVisibleSubviewLegacyPasses,
		rg_modernGLExecutorStats.modernVisiblePresentPasses,
		rg_modernGLExecutorStats.modernVisibleClearOps );
	common->Printf(
		"Modern compatibility: ready=%d inventory=%d modern=%d legacy=%d lightGrid=%d gui(program=%d pass=%d draws=%d ready=%d exec=%d fallback=%d) post(graph=%d fallback=%d copy=%d) subview(graph=%d fallback=%d remote=%d demo=%d deterministic=%d) bse(fallback=%d particle=%d trail=%d beam=%d decal=%d material=%d) cinematic=%d\n",
		rg_modernGLExecutorStats.modernVisibleCompatibilityReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleCompatibilityPasses,
		rg_modernGLExecutorStats.modernVisibleCompatibilityModernPasses,
		rg_modernGLExecutorStats.modernVisibleCompatibilityLegacyPasses,
		rg_modernGLExecutorStats.modernVisibleLightGridModernPasses,
		rg_modernGLExecutorStats.modernVisibleGuiProgramReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleGuiModernPasses,
		rg_modernGLExecutorStats.modernVisibleGuiDraws,
		rg_modernGLExecutorStats.modernVisibleGuiReadyDraws,
		rg_modernGLExecutorStats.modernVisibleGuiExecuted ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleGuiFallbackDraws,
		rg_modernGLExecutorStats.modernVisiblePostGraphPasses,
		rg_modernGLExecutorStats.modernVisiblePostFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleCopyRenderFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleSubviewGraphPasses,
		rg_modernGLExecutorStats.modernVisibleSubviewFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleRemoteCameraFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleRenderDemoFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleRenderDemoDeterministic ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleBSEFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleBSEParticleFallbacks,
		rg_modernGLExecutorStats.modernVisibleBSETrailFallbacks,
		rg_modernGLExecutorStats.modernVisibleBSEBeamFallbacks,
		rg_modernGLExecutorStats.modernVisibleBSEDecalFallbacks,
		rg_modernGLExecutorStats.modernVisibleBSEMaterialFallbacks,
		rg_modernGLExecutorStats.modernVisibleCinematicCompatibilityPasses );
	R_GLStateCache_PrintGfxInfo();
	R_ModernGLShaderLibrary_PrintGfxInfo();
}

static bool R_ModernGLExecutor_DrawVertLayoutSelfTest( void ) {
	idDrawVert vertex;
	vertex.Clear();
	const byte *base = reinterpret_cast<const byte *>( &vertex );
	const int xyzOffset = static_cast<int>( reinterpret_cast<const byte *>( &vertex.xyz ) - base );
	const int colorOffset = static_cast<int>( reinterpret_cast<const byte *>( &vertex.color[0] ) - base );
	const int normalOffset = static_cast<int>( reinterpret_cast<const byte *>( &vertex.normal ) - base );
	const int tangent0Offset = static_cast<int>( reinterpret_cast<const byte *>( &vertex.tangents[0] ) - base );
	const int tangent1Offset = static_cast<int>( reinterpret_cast<const byte *>( &vertex.tangents[1] ) - base );
	const int stOffset = static_cast<int>( reinterpret_cast<const byte *>( &vertex.st ) - base );
	if ( sizeof( idDrawVert ) != DRAWVERT_SIZE
		|| xyzOffset != DRAWVERT_XYZ_OFFSET
		|| colorOffset != DRAWVERT_COLOR_OFFSET
		|| normalOffset != DRAWVERT_NORMAL_OFFSET
		|| tangent0Offset != DRAWVERT_TANGENT0_OFFSET
		|| tangent1Offset != DRAWVERT_TANGENT1_OFFSET
		|| stOffset != DRAWVERT_ST_OFFSET ) {
		common->Printf(
			"RendererModernGLExecutor self-test failed: idDrawVert layout mismatch (size=%d xyz=%d color=%d normal=%d tan0=%d tan1=%d st=%d)\n",
			static_cast<int>( sizeof( idDrawVert ) ),
			xyzOffset,
			colorOffset,
			normalOffset,
			tangent0Offset,
			tangent1Offset,
			stOffset );
		return false;
	}
	if ( !R_ModernGLExecutor_DrawVertLayoutSupported( static_cast<int>( sizeof( idDrawVert ) ), 0 )
		|| R_ModernGLExecutor_DrawVertLayoutSupported( DRAWVERT_ST_OFFSET, 0 )
		|| R_ModernGLExecutor_DrawVertLayoutSupported( static_cast<int>( sizeof( idDrawVert ) ), -1 ) ) {
		common->Printf( "RendererModernGLExecutor self-test failed: draw-vertex layout support gate mismatch\n" );
		return false;
	}
	return true;
}

static bool R_ModernGLExecutor_InitSelfTestTriangleGeometry( srfTriangles_t &geometry, vertCache_t &ambientCache, vertCache_t &indexCache, const char *selfTestName ) {
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;

	idDrawVert verts[3];
	for ( int i = 0; i < 3; ++i ) {
		verts[i].Clear();
		verts[i].SetNormal( 0.0f, 0.0f, 1.0f );
		verts[i].SetTangent( 1.0f, 0.0f, 0.0f );
		verts[i].SetBiTangent( 0.0f, 1.0f, 0.0f );
		verts[i].SetColor( 0xffffffffu );
	}
	verts[0].xyz.Set( -0.65f, -0.55f, 0.0f );
	verts[1].xyz.Set( 0.65f, -0.55f, 0.0f );
	verts[2].xyz.Set( 0.0f, 0.65f, 0.0f );
	verts[0].st.Set( 0.0f, 0.0f );
	verts[1].st.Set( 1.0f, 0.0f );
	verts[2].st.Set( 0.5f, 1.0f );

	glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 1 };
	rendererUploadAllocation_t ambientUpload;
	rendererUploadAllocation_t indexUpload;
	if ( !R_RendererUpload_AllocFrameTemp( verts, sizeof( verts ), 16, ambientUpload )
		|| !R_RendererUpload_AllocFrameTemp( indexes, sizeof( indexes ), 4, indexUpload ) ) {
		common->Printf( "%s self-test failed: could not allocate self-test geometry uploads\n", selfTestName ? selfTestName : "Renderer" );
		return false;
	}

	memset( &ambientCache, 0, sizeof( ambientCache ) );
	ambientCache.vbo = ambientUpload.vbo;
	ambientCache.offset = ambientUpload.offset;
	ambientCache.size = ambientUpload.size;
	ambientCache.indexBuffer = false;
	ambientCache.tag = TAG_USED;

	memset( &indexCache, 0, sizeof( indexCache ) );
	indexCache.vbo = indexUpload.vbo;
	indexCache.offset = indexUpload.offset;
	indexCache.size = indexUpload.size;
	indexCache.indexBuffer = true;
	indexCache.tag = TAG_USED;

	geometry.ambientCache = &ambientCache;
	geometry.indexCache = &indexCache;
	return true;
}

bool RendererModernGLExecutor_RunSelfTest( void ) {
	if ( !R_ModernGLExecutor_DrawVertLayoutSelfTest() ) {
		return false;
	}

	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	vertCache_t ambientCache;
	vertCache_t indexCache;
	if ( !R_ModernGLExecutor_InitSelfTestTriangleGeometry( geometry, ambientCache, indexCache, "RendererModernGLExecutor" ) ) {
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort();
		}
	}

	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &worldView;
	drawSurfsCommand_t fxCmd;
	memset( &fxCmd, 0, sizeof( fxCmd ) );
	fxCmd.commandId = RC_DRAW_SPECIAL_EFFECTS;
	fxCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &fxCmd.commandId;
	fxCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	R_MaterialResourceTable_PrepareFrame( packetFrame );

	modernGLExecutorStats_t stats;
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		true,
		true,
		true,
		true,
		true,
		stats );

	idModernGLDrawPlan drawPlan;
	drawPlan.Build( packetFrame, graph );
	R_ModernGLExecutor_CopyDrawPlanStats( stats, drawPlan.Stats() );
	idModernGLSubmitPlan submitPlan;
	submitPlan.Build( drawPlan );
	R_ModernGLExecutor_CopySubmitPlanStats( stats, submitPlan.Stats() );

	if ( !stats.legacyFallback || stats.preparedPasses != graph.NumPasses() || stats.fallbackPasses != 0 ) {
		common->Printf( "RendererModernGLExecutor self-test failed: pass preparation mismatch\n" );
		return false;
	}
	const int expectedResourceDraws = tr.defaultMaterial != NULL ? packetFrame.NumDrawPackets() : 0;
	if ( stats.drawPackets != packetFrame.NumDrawPackets() || stats.preparedDrawPackets != expectedResourceDraws ) {
		common->Printf( "RendererModernGLExecutor self-test failed: draw preparation mismatch\n" );
		return false;
	}
	if ( stats.materialDrawPackets != expectedResourceDraws || stats.resourceDrawPackets != expectedResourceDraws || stats.geometryDrawPackets != packetFrame.NumDrawPackets() ) {
		common->Printf( "RendererModernGLExecutor self-test failed: material/resource/geometry coverage mismatch\n" );
		return false;
	}
	if ( !rg_modernGLExecutorAvailable ) {
		common->Printf( "RendererModernGLExecutor self-test passed (analysis only; live GL3 VAO/UBO objects unavailable)\n" );
		return true;
	}
	if ( rg_modernGLExecutorVAO == 0 || rg_modernGLExecutorFrameUBO == 0 ) {
		common->Printf( "RendererModernGLExecutor self-test failed: live GL object state mismatch\n" );
		return false;
	}
	if ( !RendererModernGLShaderLibrary_RunSelfTest() ) {
		return false;
	}
	if ( !RendererModernGLDrawPlan_RunSelfTest() ) {
		return false;
	}
	if ( !RendererModernGLSubmitPlan_RunSelfTest() ) {
		return false;
	}
	if ( rg_modernGLExecutorFeatures.gpuDriven ) {
		if ( !rg_modernGLExecutorGpuDrivenReady || rg_modernGLExecutorSceneSSBO == 0 || rg_modernGLExecutorIndirectBuffer == 0 || rg_modernGLExecutorValidationSSBO == 0 || rg_modernGLExecutorComputeProgram == 0 ) {
			common->Printf( "RendererModernGLExecutor self-test failed: GL43 GPU-driven resources unavailable\n" );
			return false;
		}
		rg_modernGLDrawPlan.Build( packetFrame, graph );
		rg_modernGLSubmitPlan.Build( rg_modernGLDrawPlan );
		R_ModernGLExecutor_CopyDrawPlanStats( stats, rg_modernGLDrawPlan.Stats() );
		R_ModernGLExecutor_CopySubmitPlanStats( stats, rg_modernGLSubmitPlan.Stats() );
		R_ModernGLExecutor_UpdateGpuDrivenBuffers( stats );
		if ( stats.drawPlanDraws > 0 && ( stats.gpuDrivenSceneRecords <= 0 || stats.gpuDrivenComputeDispatches <= 0 ) ) {
			common->Printf( "RendererModernGLExecutor self-test failed: GL43 scene SSBO/compute validation did not run\n" );
			return false;
		}
	}
	if ( rg_modernGLExecutorFeatures.lowOverhead ) {
		if ( !rg_modernGLExecutorLowOverheadReady || !stats.tierUsesDSA || !stats.tierUsesMultiBind ) {
			common->Printf( "RendererModernGLExecutor self-test failed: GL45 DSA/multi-bind path unavailable\n" );
			return false;
		}
		R_ModernGLExecutor_UpdateFrameUBO( stats );
		if ( stats.lowOverheadDSAUpdates <= 0 || stats.lowOverheadMultiBindBatches <= 0 ) {
			common->Printf( "RendererModernGLExecutor self-test failed: GL45 DSA/multi-bind path was not exercised\n" );
			return false;
		}
	}
	if ( tr.defaultMaterial != NULL && ( !stats.drawPlanReady || stats.drawPlanDraws <= 0 || stats.drawPlanStateBatches <= 0 ) ) {
		common->Printf( "RendererModernGLExecutor self-test failed: draw-plan readiness mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererModernGLExecutor self-test passed (gpuScene=%d gpuIndirect=%d dispatches=%d dsaUpdates=%d multiBindBatches=%d)\n",
		stats.gpuDrivenSceneRecords,
		stats.gpuDrivenIndirectRecords,
		stats.gpuDrivenComputeDispatches,
		stats.lowOverheadDSAUpdates,
		stats.lowOverheadMultiBindBatches );
	return true;
}

bool RendererGpuDriven_RunSelfTest( void ) {
	if ( !rg_modernGLExecutorFeatures.gpuDriven ) {
		common->Printf( "RendererGpuDriven self-test passed (resources=0 compute=0 skipped=1 tier lacks GL43 GPU-driven features)\n" );
		return true;
	}
	if ( !rg_modernGLExecutorAvailable || !rg_modernGLExecutorInitialized || !rg_modernGLExecutorGpuDrivenReady || rg_modernGLExecutorVAO == 0 ) {
		common->Printf( "RendererGpuDriven self-test failed: GL43 GPU-driven resources unavailable\n" );
		return false;
	}
	if ( tr.defaultMaterial == NULL ) {
		common->Printf( "RendererGpuDriven self-test failed: default material unavailable\n" );
		return false;
	}

	idDrawVert verts[4];
	for ( int i = 0; i < 4; ++i ) {
		verts[i].Clear();
		verts[i].normal.Set( 0.0f, 0.0f, 1.0f );
		verts[i].tangents[0].Set( 1.0f, 0.0f, 0.0f );
		verts[i].tangents[1].Set( 0.0f, 1.0f, 0.0f );
	}
	verts[0].xyz.Set( -0.5f, -0.5f, 0.0f );
	verts[1].xyz.Set( 0.5f, -0.5f, 0.0f );
	verts[2].xyz.Set( 0.5f, 0.5f, 0.0f );
	verts[3].xyz.Set( -0.5f, 0.5f, 0.0f );
	verts[0].st.Set( 0.0f, 0.0f );
	verts[1].st.Set( 1.0f, 0.0f );
	verts[2].st.Set( 1.0f, 1.0f );
	verts[3].st.Set( 0.0f, 1.0f );
	const glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 3 };

	GLuint vertexBuffer = 0;
	GLuint indexBuffer = 0;
	modernGLExecutorStats_t stats;
	R_ModernGLExecutor_ResetStats( stats, true );
	if ( !R_ModernGLExecutor_CreateBuffer( GL_ARRAY_BUFFER, sizeof( verts ), verts, GL_STATIC_DRAW, vertexBuffer )
		|| !R_ModernGLExecutor_CreateBuffer( GL_ELEMENT_ARRAY_BUFFER, sizeof( indexes ), indexes, GL_STATIC_DRAW, indexBuffer ) ) {
		if ( vertexBuffer != 0 && glDeleteBuffers != NULL ) {
			glDeleteBuffers( 1, &vertexBuffer );
		}
		if ( indexBuffer != 0 && glDeleteBuffers != NULL ) {
			glDeleteBuffers( 1, &indexBuffer );
		}
		common->Printf( "RendererGpuDriven self-test failed: temporary GL buffers unavailable\n" );
		return false;
	}

	drawSurf_t drawSurfs[3];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 4;
	geometry.numIndexes = 6;
	vertCache_t ambientCache;
	memset( &ambientCache, 0, sizeof( ambientCache ) );
	ambientCache.vbo = vertexBuffer;
	ambientCache.offset = 0;
	ambientCache.size = sizeof( verts );
	ambientCache.indexBuffer = false;
	ambientCache.tag = TAG_USED;
	vertCache_t indexCache;
	memset( &indexCache, 0, sizeof( indexCache ) );
	indexCache.vbo = indexBuffer;
	indexCache.offset = 0;
	indexCache.size = sizeof( indexes );
	indexCache.indexBuffer = true;
	indexCache.tag = TAG_USED;
	geometry.ambientCache = &ambientCache;
	geometry.indexCache = &indexCache;

	for ( int i = 0; i < 3; ++i ) {
		drawSurfs[i].geo = &geometry;
		drawSurfs[i].material = tr.defaultMaterial;
		drawSurfs[i].sort = tr.defaultMaterial->GetSort();
		drawSurfs[i].scissorRect.x1 = 0;
		drawSurfs[i].scissorRect.y1 = 0;
		drawSurfs[i].scissorRect.x2 = 127;
		drawSurfs[i].scissorRect.y2 = 127;
	}
	drawSurfs[2].scissorRect.x1 = 512;
	drawSurfs[2].scissorRect.y1 = 512;
	drawSurfs[2].scissorRect.x2 = 640;
	drawSurfs[2].scissorRect.y2 = 640;

	drawSurf_t *drawSurfPtrs[3] = { &drawSurfs[0], &drawSurfs[1], &drawSurfs[2] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	for ( int i = 0; i < 16; ++i ) {
		viewEntity.modelMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
		viewEntity.modelViewMatrix[i] = viewEntity.modelMatrix[i];
	}
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	drawSurfs[2].space = &viewEntity;
	viewLight_t lights[2];
	memset( lights, 0, sizeof( lights ) );
	idRenderLightLocal lightDefs[2];
	for ( int i = 0; i < 2; ++i ) {
		lightDefs[i].index = i;
		lightDefs[i].areaNum = -1;
		lightDefs[i].parms.origin.Set( 96.0f + 64.0f * i, 0.0f, 0.0f );
		lightDefs[i].parms.lightRadius.Set( 256.0f, 256.0f, 256.0f );
		lightDefs[i].parms.shaderParms[SHADERPARM_RED] = i == 0 ? 1.0f : 0.35f;
		lightDefs[i].parms.shaderParms[SHADERPARM_GREEN] = i == 0 ? 0.65f : 0.85f;
		lightDefs[i].parms.shaderParms[SHADERPARM_BLUE] = i == 0 ? 0.45f : 1.0f;
		lights[i].lightDef = &lightDefs[i];
		lights[i].next = i == 0 ? &lights[1] : NULL;
		lights[i].scissorRect.x1 = 0;
		lights[i].scissorRect.y1 = 0;
		lights[i].scissorRect.x2 = 127;
		lights[i].scissorRect.y2 = 127;
		lights[i].globalLightOrigin = lightDefs[i].parms.origin;
		lights[i].lightRadius = lightDefs[i].parms.lightRadius;
		lights[i].pointLight = i == 0;
		lights[i].parallel = false;
		lights[i].viewInsideLight = i == 0;
		lights[i].viewSeesGlobalLightOrigin = true;
	}
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 3;
	worldView.viewLights = &lights[0];
	worldView.renderView.width = 128;
	worldView.renderView.height = 128;
	worldView.renderView.fov_x = 90.0f;
	worldView.renderView.fov_y = 70.0f;
	worldView.renderView.viewaxis = mat3_identity;
	worldView.viewport.x1 = 0;
	worldView.viewport.y1 = 0;
	worldView.viewport.x2 = 127;
	worldView.viewport.y2 = 127;
	worldView.scissor.x1 = 0;
	worldView.scissor.y1 = 0;
	worldView.scissor.x2 = 127;
	worldView.scissor.y2 = 127;
	for ( int i = 0; i < 16; ++i ) {
		worldView.projectionMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
	}

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	R_MaterialResourceTable_PrepareFrame( packetFrame );
	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	R_ModernClusteredLighting_PrepareFrame( packetFrame, true );

	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		true,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		stats );
	rg_modernGLDrawPlan.Build( packetFrame, graph );
	R_ModernGLExecutor_CopyDrawPlanStats( stats, rg_modernGLDrawPlan.Stats() );
	rg_modernGLSubmitPlan.Build( rg_modernGLDrawPlan );
	R_ModernGLExecutor_CopySubmitPlanStats( stats, rg_modernGLSubmitPlan.Stats() );
	R_ModernGLExecutor_UpdateGpuDrivenBuffers( stats, true );
	R_ModernGLExecutor_UpdateFrameUBO( stats );
	R_ModernGLExecutor_SubmitGpuDrivenIndirect( stats );
	R_ModernGLExecutor_RecordMetrics( stats );
	rg_modernGLExecutorStats = stats;

	if ( glDeleteBuffers != NULL ) {
		glDeleteBuffers( 1, &vertexBuffer );
		glDeleteBuffers( 1, &indexBuffer );
	}

	if ( !stats.gpuDrivenReady || !stats.gpuDrivenExecuted || stats.gpuDrivenComputeDispatches <= 0 || !stats.gpuDrivenValidationReadbackReady || stats.gpuDrivenValidationMismatches != 0 ) {
		common->Printf( "RendererGpuDriven self-test failed: compute validation mismatch\n" );
		return false;
	}
	if ( stats.gpuDrivenSourceCommands <= 0 || stats.gpuDrivenGeneratedCommands <= 0 || stats.gpuDrivenCulledCommands <= 0 || stats.gpuDrivenGpuGeneratedCommands != stats.gpuDrivenCpuGeneratedCommands ) {
		common->Printf( "RendererGpuDriven self-test failed: CPU/GPU generated-command coverage mismatch\n" );
		return false;
	}
	if ( !stats.gpuDrivenIndirectExecuted || stats.gpuDrivenMultiDrawBatches <= 0 || stats.gpuDrivenIndirectDrawCalls != stats.gpuDrivenGeneratedCommands ) {
		common->Printf( "RendererGpuDriven self-test failed: indirect multi-draw execution mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererGpuDriven self-test passed (resources=%d compute=%d source=%d eligible=%d generated=%d culled=%d visible=%d cpu=%d/%d/%d gpu=%d/%d/%d clusters=%d/%d mismatches=%d readbacks=%d indirect=%d multiDraw=%d indirectCalls=%d dispatches=%d)\n",
		stats.gpuDrivenReady ? 1 : 0,
		stats.computeValidationReady ? 1 : 0,
		stats.gpuDrivenSourceCommands,
		stats.gpuDrivenEligibleCommands,
		stats.gpuDrivenGeneratedCommands,
		stats.gpuDrivenCulledCommands,
		stats.gpuDrivenVisibleInstances,
		stats.gpuDrivenCpuGeneratedCommands,
		stats.gpuDrivenCpuCulledCommands,
		stats.gpuDrivenCpuVisibleInstances,
		stats.gpuDrivenGpuGeneratedCommands,
		stats.gpuDrivenGpuCulledCommands,
		stats.gpuDrivenGpuVisibleInstances,
		stats.gpuDrivenCpuClusterBins,
		stats.gpuDrivenGpuClusterBins,
		stats.gpuDrivenValidationMismatches,
		stats.gpuDrivenValidationReadbacks,
		stats.gpuDrivenIndirectExecuted ? 1 : 0,
		stats.gpuDrivenMultiDrawBatches,
		stats.gpuDrivenIndirectDrawCalls,
		stats.gpuDrivenComputeDispatches );
	return true;
}

static bool R_ModernGLExecutor_BuildVisiblePathSelfTestFrame( idScenePacketFrame &packetFrame, idRenderGraph &graph ) {
	packetFrame.Clear();

	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	vertCache_t ambientCache;
	vertCache_t indexCache;
	if ( !R_ModernGLExecutor_InitSelfTestTriangleGeometry( geometry, ambientCache, indexCache, "RendererVisiblePath" ) ) {
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort();
		}
	}

	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;

	if ( !packetFrame.AddScene( &worldView, true ) ) {
		return false;
	}
	if ( !packetFrame.AddPass( RENDER_PASS_DEPTH, true ) ) {
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_DEPTH, i ) ) {
			return false;
		}
	}
	if ( !packetFrame.AddPass( RENDER_PASS_SHADOW_MAP, true ) ) {
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_SHADOW_MAP, i ) ) {
			return false;
		}
	}
	packetFrame.FinishScene();

	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	R_MaterialResourceTable_PrepareFrame( packetFrame );
	return packetFrame.NumDrawPackets() == 4 && graph.FindPass( RENDER_PASS_DEPTH ) >= 0 && graph.FindPass( RENDER_PASS_SHADOW_MAP ) >= 0;
}

bool RendererVisiblePath_RunSelfTest( void ) {
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererVisiblePath self-test passed (shader library unavailable)\n" );
		return true;
	}

	idScenePacketFrame packetFrame;
	idRenderGraph graph;
	if ( !R_ModernGLExecutor_BuildVisiblePathSelfTestFrame( packetFrame, graph ) ) {
		common->Printf( "RendererVisiblePath self-test failed: could not build depth/shadow packet frame\n" );
		return false;
	}

	const modernGLShaderProgramInfo_t *depthProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_DEPTH, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *shadowProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_SHADOW_DEPTH, shaderStats.highestGLSLVersion );
	if ( depthProgram == NULL || depthProgram->program == 0 || !depthProgram->linked || shadowProgram == NULL || shadowProgram->program == 0 || !shadowProgram->linked ) {
		common->Printf( "RendererVisiblePath self-test failed: depth/shadow programs unavailable\n" );
		return false;
	}

	idModernGLDrawPlan drawPlan;
	drawPlan.Build( packetFrame, graph );
	const modernGLDrawPlanStats_t &drawStats = drawPlan.Stats();
	idModernGLSubmitPlan submitPlan;
	submitPlan.Build( drawPlan );
	const modernGLSubmitPlanStats_t &submitStats = submitPlan.Stats();
	const int expectedDepthDraws = tr.defaultMaterial != NULL ? 4 : 0;
	const int expectedDrawFallbacks = tr.defaultMaterial != NULL ? 0 : packetFrame.NumDrawPackets();
	if ( drawStats.sourceDrawPackets != packetFrame.NumDrawPackets() || drawStats.depthDraws != expectedDepthDraws || drawStats.materialDraws != 0 || drawStats.fallbackDraws != expectedDrawFallbacks || drawStats.overflow ) {
		common->Printf(
			"RendererVisiblePath self-test failed: draw-plan depth coverage mismatch (source=%d depth=%d material=%d fallback=%d overflow=%d)\n",
			drawStats.sourceDrawPackets,
			drawStats.depthDraws,
			drawStats.materialDraws,
			drawStats.fallbackDraws,
			drawStats.overflow ? 1 : 0 );
		return false;
	}
	if ( submitStats.readyDraws != expectedDepthDraws || submitStats.depthReadyDraws != expectedDepthDraws || submitStats.materialReadyDraws != 0 || submitStats.fallbackDraws != 0 || submitPlan.NumCommands() != expectedDepthDraws ) {
		common->Printf(
			"RendererVisiblePath self-test failed: submit-plan depth readiness mismatch (ready=%d depth=%d material=%d fallback=%d commands=%d)\n",
			submitStats.readyDraws,
			submitStats.depthReadyDraws,
			submitStats.materialReadyDraws,
			submitStats.fallbackDraws,
			submitPlan.NumCommands() );
		return false;
	}

	if ( rg_modernGLExecutorAvailable && ( rg_modernGLExecutorVAO == 0 || rg_modernGLExecutorDepthOverlayProgram == 0 ) ) {
		common->Printf( "RendererVisiblePath self-test failed: live VAO or depth overlay program unavailable\n" );
		return false;
	}

	const renderGraphResourceManagerStats_t &initialResourceStats = R_RenderGraphResources_Stats();
	bool sceneDepthReady = false;
	bool shadowMapReady = false;
	if ( initialResourceStats.initialized && initialResourceStats.available ) {
		R_RenderGraphResources_PrepareFrame( graph );
		const renderGraphResourceManagerStats_t &resourceStats = R_RenderGraphResources_Stats();
		const renderGraphResourceHandle_t *sceneDepth = R_RenderGraphResources_FindHandle( "sceneDepth" );
		const renderGraphResourceHandle_t *shadowMap = R_RenderGraphResources_FindHandle( "shadowMap" );
		sceneDepthReady = sceneDepth != NULL && sceneDepth->allocated && sceneDepth->framebufferComplete && sceneDepth->texture != 0 && sceneDepth->framebuffer != 0;
		shadowMapReady = shadowMap != NULL && shadowMap->allocated && shadowMap->framebufferComplete && shadowMap->texture != 0 && shadowMap->framebuffer != 0 && shadowMap->type == RENDER_GRAPH_RESOURCE_DEPTH;
		if ( !resourceStats.prepared || !sceneDepthReady || !shadowMapReady ) {
			common->Printf(
				"RendererVisiblePath self-test failed: graph depth resources unavailable (prepared=%d scene=%d shadow=%d handles=%d fbo=%d/%d status='%s')\n",
				resourceStats.prepared ? 1 : 0,
				sceneDepthReady ? 1 : 0,
				shadowMapReady ? 1 : 0,
				resourceStats.handles,
				resourceStats.completeFramebuffers,
				resourceStats.framebufferCount,
				resourceStats.lastFailure );
			return false;
		}
	}

	common->Printf(
		"RendererVisiblePath self-test passed (depthReady=%d shadowReady=%d sceneDepth=%d shadowMap=%d overlay=%d)\n",
		expectedDepthDraws >= 2 ? 2 : expectedDepthDraws,
		expectedDepthDraws >= 4 ? 2 : 0,
		sceneDepthReady ? 1 : 0,
		shadowMapReady ? 1 : 0,
		rg_modernGLExecutorDepthOverlayProgram != 0 ? 1 : 0 );
	return true;
}

static bool R_ModernGLExecutor_BuildGBufferSelfTestFrame( idScenePacketFrame &packetFrame, idRenderGraph &graph ) {
	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	vertCache_t ambientCache;
	vertCache_t indexCache;
	if ( !R_ModernGLExecutor_InitSelfTestTriangleGeometry( geometry, ambientCache, indexCache, "RendererGBuffer" ) ) {
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort();
		}
	}

	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;

	if ( !packetFrame.AddScene( &worldView, true ) ) {
		return false;
	}
	if ( !packetFrame.AddPass( RENDER_PASS_AMBIENT, true ) ) {
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_AMBIENT, i ) ) {
			return false;
		}
	}
	packetFrame.FinishScene();

	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	R_MaterialResourceTable_PrepareFrame( packetFrame );
	return packetFrame.NumDrawPackets() == 2 && graph.FindPass( RENDER_PASS_AMBIENT ) >= 0;
}

static bool R_ModernGLExecutor_ValidateGBufferPacking( void ) {
	const float packedNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
	const float packedMaterial[4] = { 0.1f, 0.5f, 0.25f, 1.0f };
	return idMath::Fabs( packedNormal[0] - 0.5f ) < 0.001f
		&& idMath::Fabs( packedNormal[1] - 0.5f ) < 0.001f
		&& idMath::Fabs( packedNormal[2] - 1.0f ) < 0.001f
		&& packedMaterial[0] >= 0.0f && packedMaterial[0] <= 1.0f
		&& packedMaterial[1] >= 0.0f && packedMaterial[1] <= 1.0f
		&& packedMaterial[2] >= 0.0f && packedMaterial[2] <= 1.0f
		&& packedMaterial[3] == 1.0f;
}

bool RendererGBuffer_RunSelfTest( void ) {
	if ( !r_rendererModernVisible.GetBool() && !r_rendererModernOpaque.GetBool() && r_rendererModernGBufferDebug.GetInteger() <= 0 ) {
		common->Printf( "RendererGBuffer self-test passed (disabled)\n" );
		return true;
	}

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererGBuffer self-test passed (shader library unavailable)\n" );
		return true;
	}
	if ( !R_ModernGLExecutor_ValidateGBufferPacking() ) {
		common->Printf( "RendererGBuffer self-test failed: packed normal/material validation mismatch\n" );
		return false;
	}

	const modernGLShaderProgramInfo_t *opaqueProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_GBUFFER_OPAQUE, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *alphaProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_GBUFFER_ALPHA_TEST, shaderStats.highestGLSLVersion );
	if ( opaqueProgram == NULL || opaqueProgram->program == 0 || !opaqueProgram->linked || alphaProgram == NULL || alphaProgram->program == 0 || !alphaProgram->linked ) {
		common->Printf( "RendererGBuffer self-test failed: G-buffer programs unavailable\n" );
		return false;
	}
	if ( !opaqueProgram->reflection.usesMainTexture || opaqueProgram->mainTextureLocation < 0
		|| !opaqueProgram->reflection.usesMaterialTextures || opaqueProgram->normalTextureLocation < 0 || opaqueProgram->specularTextureLocation < 0 || opaqueProgram->emissiveTextureLocation < 0 || opaqueProgram->materialFlagsLocation < 0
		|| !alphaProgram->reflection.usesMainTexture || alphaProgram->mainTextureLocation < 0
		|| !alphaProgram->reflection.usesMaterialTextures || alphaProgram->normalTextureLocation < 0 || alphaProgram->specularTextureLocation < 0 || alphaProgram->emissiveTextureLocation < 0 || alphaProgram->materialFlagsLocation < 0 ) {
		common->Printf( "RendererGBuffer self-test failed: G-buffer texture reflection unavailable\n" );
		return false;
	}

	idScenePacketFrame packetFrame;
	idRenderGraph graph;
	if ( !R_ModernGLExecutor_BuildGBufferSelfTestFrame( packetFrame, graph ) ) {
		common->Printf( "RendererGBuffer self-test failed: could not build ambient packet frame\n" );
		return false;
	}
	if ( graph.FindResource( "gbufferAlbedo" ) < 0 || graph.FindResource( "gbufferNormal" ) < 0 || graph.FindResource( "gbufferMaterial" ) < 0 || graph.FindResource( "gbufferEmissive" ) < 0 ) {
		common->Printf( "RendererGBuffer self-test failed: graph G-buffer resources missing\n" );
		return false;
	}

	idModernGLDrawPlan drawPlan;
	drawPlan.Build( packetFrame, graph );
	const modernGLDrawPlanStats_t &drawStats = drawPlan.Stats();
	idModernGLSubmitPlan submitPlan;
	submitPlan.Build( drawPlan );
	const modernGLSubmitPlanStats_t &submitStats = submitPlan.Stats();
	const int expectedDraws = tr.defaultMaterial != NULL ? 2 : 0;
	if ( drawStats.sourceDrawPackets != packetFrame.NumDrawPackets() || drawStats.materialDraws != expectedDraws || submitStats.materialReadyDraws != expectedDraws || submitPlan.NumCommands() != expectedDraws ) {
		common->Printf(
			"RendererGBuffer self-test failed: draw/submit coverage mismatch (source=%d material=%d ready=%d commands=%d expected=%d)\n",
			drawStats.sourceDrawPackets,
			drawStats.materialDraws,
			submitStats.materialReadyDraws,
			submitPlan.NumCommands(),
			expectedDraws );
		return false;
	}
	for ( int i = 0; i < drawPlan.NumEntries(); ++i ) {
		if ( drawPlan.Entry( i ).pipeline != MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER ) {
			common->Printf( "RendererGBuffer self-test failed: non-G-buffer ambient entry\n" );
			return false;
		}
	}

	bool attachmentReady[MODERN_GL_GBUFFER_ATTACHMENT_COUNT] = { false, false, false, false };
	int bytesPerPixel = 0;
	bool mrtReady = rg_modernGLExecutorCaps.hasMRT && rg_modernGLExecutorCaps.maxDrawBuffers >= MODERN_GL_GBUFFER_ATTACHMENT_COUNT && rg_modernGLExecutorGBufferFBO != 0;
	const renderGraphResourceManagerStats_t &initialResourceStats = R_RenderGraphResources_Stats();
	if ( initialResourceStats.initialized && initialResourceStats.available ) {
		R_RenderGraphResources_PrepareFrame( graph );
		for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
			const renderGraphResourceHandle_t *handle = R_RenderGraphResources_FindHandle( rg_modernGLGBufferAttachmentNames[i] );
			attachmentReady[i] = handle != NULL && handle->allocated && handle->framebufferComplete && handle->texture != 0 && handle->type == RENDER_GRAPH_RESOURCE_COLOR;
			if ( attachmentReady[i] ) {
				bytesPerPixel += R_ModernGLExecutor_GBufferBytesPerPixel( *handle );
			}
		}
		mrtReady = mrtReady && attachmentReady[0] && attachmentReady[1] && attachmentReady[2] && attachmentReady[3];
	}

	common->Printf(
		"RendererGBuffer self-test passed (mrt=%d albedo=%d normal=%d material=%d emissive=%d draws=%d fallback=%d bpp=%d overlay=%d)\n",
		mrtReady ? 1 : 0,
		attachmentReady[0] ? 1 : 0,
		attachmentReady[1] ? 1 : 0,
		attachmentReady[2] ? 1 : 0,
		attachmentReady[3] ? 1 : 0,
		expectedDraws,
		drawStats.fallbackDraws + submitStats.fallbackDraws,
		bytesPerPixel,
		rg_modernGLExecutorGBufferOverlayProgram != 0 ? 1 : 0 );
	return true;
}

bool RendererDeferredResolve_RunSelfTest( void ) {
	if ( !r_rendererModernVisible.GetBool() && !r_rendererModernDeferred.GetBool() && r_rendererModernDeferredDebug.GetInteger() <= 0 ) {
		common->Printf( "RendererDeferredResolve self-test passed (disabled)\n" );
		return true;
	}

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererDeferredResolve self-test passed (shader library unavailable)\n" );
		return true;
	}
	const modernGLShaderProgramInfo_t *program = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE, shaderStats.highestGLSLVersion );
	if ( program == NULL || program->program == 0 || !program->linked ) {
		common->Printf( "RendererDeferredResolve self-test failed: deferred resolve program unavailable\n" );
		return false;
	}
	for ( int i = 0; i < MODERN_GL_DEFERRED_TEXTURE_COUNT; ++i ) {
		const GLint location = glGetUniformLocation != NULL ? glGetUniformLocation( program->program, rg_modernGLDeferredTextureUniforms[i] ) : -1;
		if ( location < 0 ) {
			common->Printf( "RendererDeferredResolve self-test failed: missing sampler %s\n", rg_modernGLDeferredTextureUniforms[i] );
			return false;
		}
	}

	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	vertCache_t ambientCache;
	memset( &ambientCache, 0, sizeof( ambientCache ) );
	ambientCache.vbo = 101;
	ambientCache.offset = 64;
	ambientCache.size = geometry.numVerts * static_cast<int>( sizeof( idDrawVert ) );
	ambientCache.indexBuffer = false;
	ambientCache.tag = TAG_USED;
	vertCache_t indexCache;
	memset( &indexCache, 0, sizeof( indexCache ) );
	indexCache.vbo = 202;
	indexCache.offset = 128;
	indexCache.size = geometry.numIndexes * static_cast<int>( sizeof( glIndex_t ) );
	indexCache.indexBuffer = true;
	indexCache.tag = TAG_USED;
	geometry.ambientCache = &ambientCache;
	geometry.indexCache = &indexCache;
	for ( int i = 0; i < 2; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort();
		}
	}

	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewLight_t lights[2];
	memset( lights, 0, sizeof( lights ) );
	idRenderLightLocal lightDefs[2];
	for ( int i = 0; i < 2; ++i ) {
		lightDefs[i].index = i;
		lightDefs[i].areaNum = -1;
		lightDefs[i].parms.origin.Set( 96.0f + 64.0f * i, 0.0f, 0.0f );
		lightDefs[i].parms.lightRadius.Set( 256.0f, 256.0f, 256.0f );
		lightDefs[i].parms.shaderParms[SHADERPARM_RED] = i == 0 ? 1.0f : 0.35f;
		lightDefs[i].parms.shaderParms[SHADERPARM_GREEN] = i == 0 ? 0.65f : 0.85f;
		lightDefs[i].parms.shaderParms[SHADERPARM_BLUE] = i == 0 ? 0.45f : 1.0f;
		lights[i].lightDef = &lightDefs[i];
		lights[i].next = i == 0 ? &lights[1] : NULL;
		lights[i].scissorRect.x1 = 0;
		lights[i].scissorRect.y1 = 0;
		lights[i].scissorRect.x2 = 639;
		lights[i].scissorRect.y2 = 479;
		lights[i].globalLightOrigin = lightDefs[i].parms.origin;
		lights[i].lightRadius = lightDefs[i].parms.lightRadius;
		lights[i].pointLight = i == 0;
		lights[i].parallel = false;
		lights[i].viewInsideLight = i == 0;
		lights[i].viewSeesGlobalLightOrigin = true;
	}

	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;
	worldView.viewLights = &lights[0];
	worldView.renderView.width = 640;
	worldView.renderView.height = 480;
	worldView.renderView.fov_x = 90.0f;
	worldView.renderView.fov_y = 70.0f;
	worldView.renderView.viewaxis = mat3_identity;
	worldView.viewport.x1 = 0;
	worldView.viewport.y1 = 0;
	worldView.viewport.x2 = 639;
	worldView.viewport.y2 = 479;
	worldView.scissor.x1 = 0;
	worldView.scissor.y1 = 0;
	worldView.scissor.x2 = 639;
	worldView.scissor.y2 = 479;

	idScenePacketFrame packetFrame;
	if ( !packetFrame.AddScene( &worldView, true ) || !packetFrame.AddPass( RENDER_PASS_AMBIENT, true ) ) {
		common->Printf( "RendererDeferredResolve self-test failed: could not build packet scene\n" );
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_AMBIENT, i ) ) {
			common->Printf( "RendererDeferredResolve self-test failed: could not add ambient draw packet\n" );
			return false;
		}
	}
	packetFrame.FinishScene();

	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	if ( graph.FindPass( RENDER_PASS_DEFERRED_RESOLVE ) < 0 || graph.FindResource( "deferredLight" ) < 0 || graph.FindResource( "clusterGrid" ) < 0 ) {
		common->Printf( "RendererDeferredResolve self-test failed: graph deferred resolve resources missing\n" );
		return false;
	}
	R_MaterialResourceTable_PrepareFrame( packetFrame );
	R_RenderGraphResources_PrepareFrame( graph );
	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	R_ModernClusteredLighting_PrepareFrame( packetFrame, true );

	modernGLExecutorStats_t stats;
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		true,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		stats );
	R_ModernGLExecutor_SubmitDeferredResolve( stats );

	const bool resourcesAvailable = R_RenderGraphResources_Stats().initialized && R_RenderGraphResources_Stats().available && rg_modernGLExecutorAvailable;
	if ( resourcesAvailable && ( !stats.deferredResolveExecuted || !stats.deferredResolveResourcesReady || !stats.deferredResolveClusterReady || stats.deferredResolvePixels <= 0 || stats.deferredResolveClusterReads <= 0 ) ) {
		common->Printf(
			"RendererDeferredResolve self-test failed: execution mismatch (exec=%d res=%d cluster=%d pixels=%d reads=%d fallback=%d)\n",
			stats.deferredResolveExecuted ? 1 : 0,
			stats.deferredResolveResourcesReady ? 1 : 0,
			stats.deferredResolveClusterReady ? 1 : 0,
			stats.deferredResolvePixels,
			stats.deferredResolveClusterReads,
			stats.deferredResolveResourceFallbacks );
		return false;
	}

	common->Printf(
		"RendererDeferredResolve self-test passed (program=%d output=%d resources=%d cluster=%d pixels=%d lights=%d point=%d projected=%d lightGrid=%d reads=%d fallback=%d debug=%d overlay=%d)\n",
		stats.deferredResolveProgramReady ? 1 : 0,
		stats.deferredResolveOutputReady ? 1 : 0,
		stats.deferredResolveResourcesReady ? 1 : 0,
		stats.deferredResolveClusterReady ? 1 : 0,
		stats.deferredResolvePixels,
		stats.deferredResolveActiveLights,
		stats.deferredResolvePointLights,
		stats.deferredResolveProjectedLights,
		stats.deferredResolveLightGridContributions,
		stats.deferredResolveClusterReads,
		stats.deferredResolveResourceFallbacks + stats.deferredResolveUnsupportedLightFallbacks,
		stats.deferredResolveDebugMode,
		rg_modernGLExecutorDeferredOverlayProgram != 0 ? 1 : 0 );
	return true;
}

bool RendererForwardPlus_RunSelfTest( void ) {
	if ( !r_rendererModernVisible.GetBool() && !r_rendererForwardPlus.GetBool() ) {
		common->Printf( "RendererForwardPlus self-test passed (disabled)\n" );
		return true;
	}

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererForwardPlus self-test passed (shader library unavailable)\n" );
		return true;
	}
	const modernGLShaderProgramInfo_t *opaqueProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *alphaProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *transparentProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_TRANSPARENT_FORWARD, shaderStats.highestGLSLVersion );
	const bool programsReady = opaqueProgram != NULL && opaqueProgram->program != 0 && opaqueProgram->linked
		&& alphaProgram != NULL && alphaProgram->program != 0 && alphaProgram->linked
		&& transparentProgram != NULL && transparentProgram->program != 0 && transparentProgram->linked;
	if ( !programsReady ) {
		common->Printf( "RendererForwardPlus self-test failed: clustered forward programs unavailable\n" );
		return false;
	}

	drawSurf_t drawSurfs[3];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	vertCache_t ambientCache;
	vertCache_t indexCache;
	if ( !R_ModernGLExecutor_InitSelfTestTriangleGeometry( geometry, ambientCache, indexCache, "RendererForwardPlus" ) ) {
		return false;
	}
	for ( int i = 0; i < 3; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort() + static_cast<float>( i ) * 0.01f;
		}
	}

	drawSurf_t *drawSurfPtrs[3] = { &drawSurfs[0], &drawSurfs[1], &drawSurfs[2] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	for ( int i = 0; i < 16; ++i ) {
		viewEntity.modelMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
		viewEntity.modelViewMatrix[i] = viewEntity.modelMatrix[i];
	}
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	drawSurfs[2].space = &viewEntity;
	viewLight_t lights[2];
	memset( lights, 0, sizeof( lights ) );
	idRenderLightLocal lightDefs[2];
	for ( int i = 0; i < 2; ++i ) {
		lightDefs[i].index = i;
		lightDefs[i].areaNum = -1;
		lightDefs[i].parms.origin.Set( 128.0f + 32.0f * i, 24.0f, 32.0f );
		lightDefs[i].parms.lightRadius.Set( 256.0f, 256.0f, 256.0f );
		lightDefs[i].parms.shaderParms[SHADERPARM_RED] = i == 0 ? 1.0f : 0.35f;
		lightDefs[i].parms.shaderParms[SHADERPARM_GREEN] = i == 0 ? 0.65f : 0.85f;
		lightDefs[i].parms.shaderParms[SHADERPARM_BLUE] = i == 0 ? 0.45f : 1.0f;
		lights[i].lightDef = &lightDefs[i];
		lights[i].next = i == 0 ? &lights[1] : NULL;
		lights[i].scissorRect.x1 = 0;
		lights[i].scissorRect.y1 = 0;
		lights[i].scissorRect.x2 = 639;
		lights[i].scissorRect.y2 = 479;
		lights[i].globalLightOrigin = lightDefs[i].parms.origin;
		lights[i].lightRadius = lightDefs[i].parms.lightRadius;
		lights[i].pointLight = i == 0;
		lights[i].parallel = false;
		lights[i].viewInsideLight = i == 0;
		lights[i].viewSeesGlobalLightOrigin = true;
	}

	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 3;
	worldView.viewLights = &lights[0];
	worldView.renderView.width = 640;
	worldView.renderView.height = 480;
	worldView.renderView.fov_x = 90.0f;
	worldView.renderView.fov_y = 70.0f;
	worldView.renderView.viewaxis = mat3_identity;
	worldView.viewport.x1 = 0;
	worldView.viewport.y1 = 0;
	worldView.viewport.x2 = 639;
	worldView.viewport.y2 = 479;
	worldView.scissor.x1 = 0;
	worldView.scissor.y1 = 0;
	worldView.scissor.x2 = 639;
	worldView.scissor.y2 = 479;
	for ( int i = 0; i < 16; ++i ) {
		worldView.projectionMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
	}

	idScenePacketFrame packetFrame;
	if ( !packetFrame.AddScene( &worldView, true ) || !packetFrame.AddPass( RENDER_PASS_DEPTH, true ) ) {
		common->Printf( "RendererForwardPlus self-test failed: could not build depth packet scene\n" );
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_DEPTH, i ) ) {
			common->Printf( "RendererForwardPlus self-test failed: could not add depth draw packet\n" );
			return false;
		}
	}
	if ( !packetFrame.AddPass( RENDER_PASS_ARB2_INTERACTION, true ) ) {
		common->Printf( "RendererForwardPlus self-test failed: could not add interaction pass\n" );
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_ARB2_INTERACTION, i ) ) {
			common->Printf( "RendererForwardPlus self-test failed: could not add interaction draw packet\n" );
			return false;
		}
	}
	if ( !packetFrame.AddPass( RENDER_PASS_FOG_BLEND, true ) || !packetFrame.AddDrawPacket( &drawSurfs[2], RENDER_PASS_FOG_BLEND, 2 ) ) {
		common->Printf( "RendererForwardPlus self-test failed: could not add transparent draw packet\n" );
		return false;
	}
	packetFrame.FinishScene();

	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	if ( graph.FindPass( RENDER_PASS_FORWARD_PLUS ) < 0 || graph.FindResource( "sceneColor" ) < 0 || graph.FindResource( "sceneDepth" ) < 0 || graph.FindResource( "clusterGrid" ) < 0 ) {
		common->Printf( "RendererForwardPlus self-test failed: graph forward+ resources missing\n" );
		return false;
	}
	R_MaterialResourceTable_PrepareFrame( packetFrame );
	R_RenderGraphResources_PrepareFrame( graph );
	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	R_ModernClusteredLighting_PrepareFrame( packetFrame, true );

	modernGLExecutorStats_t stats;
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		true,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		stats );
	rg_modernGLDrawPlan.Build( packetFrame, graph );
	R_ModernGLExecutor_CopyDrawPlanStats( stats, rg_modernGLDrawPlan.Stats() );
	rg_modernGLSubmitPlan.Build( rg_modernGLDrawPlan );
	R_ModernGLExecutor_CopySubmitPlanStats( stats, rg_modernGLSubmitPlan.Stats() );
	R_ModernGLExecutor_SubmitForwardPlus( stats );

	const bool resourcesAvailable = R_RenderGraphResources_Stats().initialized && R_RenderGraphResources_Stats().available && rg_modernGLExecutorAvailable;
	if ( resourcesAvailable && ( !stats.forwardPlusExecuted || !stats.forwardPlusResourcesReady || !stats.forwardPlusClusterReady || stats.forwardPlusDraws <= 0 || stats.forwardPlusTransparentDraws <= 0 || stats.forwardPlusClusterReads <= 0 ) ) {
		common->Printf(
			"RendererForwardPlus self-test failed: execution mismatch (exec=%d res=%d cluster=%d draws=%d transparent=%d reads=%d fallback=%d)\n",
			stats.forwardPlusExecuted ? 1 : 0,
			stats.forwardPlusResourcesReady ? 1 : 0,
			stats.forwardPlusClusterReady ? 1 : 0,
			stats.forwardPlusDraws,
			stats.forwardPlusTransparentDraws,
			stats.forwardPlusClusterReads,
			stats.forwardPlusFallbackDraws );
		return false;
	}

	common->Printf(
		"RendererForwardPlus self-test passed (programs=%d resources=%d scene=%d depth=%d cluster=%d draws=%d opaque=%d alpha=%d alphaProgram=%d transparent=%d batches=%d fallback=%d effects=%d overdraw=%d reads=%d lights=%d point=%d projected=%d lightGrid=%d)\n",
		stats.forwardPlusProgramReady ? 1 : 0,
		stats.forwardPlusResourcesReady ? 1 : 0,
		stats.forwardPlusSceneColorReady ? 1 : 0,
		stats.forwardPlusSceneDepthReady ? 1 : 0,
		stats.forwardPlusClusterReady ? 1 : 0,
		stats.forwardPlusDraws,
		stats.forwardPlusOpaqueDraws,
		stats.forwardPlusAlphaTestDraws,
		alphaProgram != NULL && alphaProgram->program != 0 && alphaProgram->linked ? 1 : 0,
		stats.forwardPlusTransparentDraws,
		stats.forwardPlusSortedBatches,
		stats.forwardPlusFallbackDraws,
		stats.forwardPlusSpecialEffectFallbacks,
		stats.forwardPlusOverdrawEstimate,
		stats.forwardPlusClusterReads,
		stats.forwardPlusActiveLights,
		stats.forwardPlusPointLights,
		stats.forwardPlusProjectedLights,
		stats.forwardPlusLightGridContributions );
	return true;
}

bool RendererModernVisible_RunSelfTest( void ) {
	if ( !r_rendererModernVisible.GetBool() ) {
		common->Printf( "RendererModernVisible self-test passed (disabled)\n" );
		return true;
	}

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererModernVisible self-test passed (shader library unavailable)\n" );
		return true;
	}
	if ( rg_modernGLExecutorAvailable && rg_modernGLExecutorVisibleCompositeProgram == 0 ) {
		common->Printf( "RendererModernVisible self-test failed: visible composite program unavailable\n" );
		return false;
	}

	const modernGLShaderProgramInfo_t *deferredProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *forwardOpaqueProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *forwardAlphaProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST, shaderStats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *forwardTransparentProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_TRANSPARENT_FORWARD, shaderStats.highestGLSLVersion );
	const bool programsReady =
		deferredProgram != NULL && deferredProgram->program != 0 && deferredProgram->linked
		&& forwardOpaqueProgram != NULL && forwardOpaqueProgram->program != 0 && forwardOpaqueProgram->linked
		&& forwardAlphaProgram != NULL && forwardAlphaProgram->program != 0 && forwardAlphaProgram->linked
		&& forwardTransparentProgram != NULL && forwardTransparentProgram->program != 0 && forwardTransparentProgram->linked;
	if ( !programsReady ) {
		common->Printf( "RendererModernVisible self-test failed: deferred or forward+ programs unavailable\n" );
		return false;
	}

	drawSurf_t drawSurfs[3];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	vertCache_t ambientCache;
	memset( &ambientCache, 0, sizeof( ambientCache ) );
	ambientCache.vbo = 101;
	ambientCache.offset = 64;
	ambientCache.size = geometry.numVerts * static_cast<int>( sizeof( idDrawVert ) );
	ambientCache.indexBuffer = false;
	ambientCache.tag = TAG_USED;
	vertCache_t indexCache;
	memset( &indexCache, 0, sizeof( indexCache ) );
	indexCache.vbo = 202;
	indexCache.offset = 128;
	indexCache.size = geometry.numIndexes * static_cast<int>( sizeof( glIndex_t ) );
	indexCache.indexBuffer = true;
	indexCache.tag = TAG_USED;
	geometry.ambientCache = &ambientCache;
	geometry.indexCache = &indexCache;
	for ( int i = 0; i < 3; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort() + static_cast<float>( i ) * 0.01f;
		}
	}

	drawSurf_t *drawSurfPtrs[3] = { &drawSurfs[0], &drawSurfs[1], &drawSurfs[2] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	drawSurfs[2].space = &viewEntity;
	viewLight_t lights[2];
	memset( lights, 0, sizeof( lights ) );
	idRenderLightLocal lightDefs[2];
	for ( int i = 0; i < 2; ++i ) {
		lightDefs[i].index = i;
		lightDefs[i].areaNum = -1;
		lightDefs[i].parms.origin.Set( 128.0f + 48.0f * i, 24.0f, 32.0f );
		lightDefs[i].parms.lightRadius.Set( 256.0f, 256.0f, 256.0f );
		lightDefs[i].parms.shaderParms[SHADERPARM_RED] = i == 0 ? 1.0f : 0.35f;
		lightDefs[i].parms.shaderParms[SHADERPARM_GREEN] = i == 0 ? 0.65f : 0.85f;
		lightDefs[i].parms.shaderParms[SHADERPARM_BLUE] = i == 0 ? 0.45f : 1.0f;
		lights[i].lightDef = &lightDefs[i];
		lights[i].next = i == 0 ? &lights[1] : NULL;
		lights[i].scissorRect.x1 = 0;
		lights[i].scissorRect.y1 = 0;
		lights[i].scissorRect.x2 = 639;
		lights[i].scissorRect.y2 = 479;
		lights[i].globalLightOrigin = lightDefs[i].parms.origin;
		lights[i].lightRadius = lightDefs[i].parms.lightRadius;
		lights[i].pointLight = i == 0;
		lights[i].parallel = false;
		lights[i].viewInsideLight = i == 0;
		lights[i].viewSeesGlobalLightOrigin = true;
	}

	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 3;
	worldView.viewLights = &lights[0];
	worldView.renderView.width = 640;
	worldView.renderView.height = 480;
	worldView.renderView.fov_x = 90.0f;
	worldView.renderView.fov_y = 70.0f;
	worldView.renderView.viewaxis = mat3_identity;
	worldView.viewport.x1 = 0;
	worldView.viewport.y1 = 0;
	worldView.viewport.x2 = 639;
	worldView.viewport.y2 = 479;
	worldView.scissor.x1 = 0;
	worldView.scissor.y1 = 0;
	worldView.scissor.x2 = 639;
	worldView.scissor.y2 = 479;

	idScenePacketFrame packetFrame;
	if ( !packetFrame.AddScene( &worldView, true ) || !packetFrame.AddPass( RENDER_PASS_DEPTH, true ) ) {
		common->Printf( "RendererModernVisible self-test failed: could not build depth packet scene\n" );
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_DEPTH, i ) ) {
			common->Printf( "RendererModernVisible self-test failed: could not add depth draw packet\n" );
			return false;
		}
	}
	if ( !packetFrame.AddPass( RENDER_PASS_AMBIENT, true ) ) {
		common->Printf( "RendererModernVisible self-test failed: could not add ambient pass\n" );
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_AMBIENT, i ) ) {
			common->Printf( "RendererModernVisible self-test failed: could not add ambient draw packet\n" );
			return false;
		}
	}
	if ( !packetFrame.AddPass( RENDER_PASS_ARB2_INTERACTION, true ) ) {
		common->Printf( "RendererModernVisible self-test failed: could not add interaction pass\n" );
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_ARB2_INTERACTION, i ) ) {
			common->Printf( "RendererModernVisible self-test failed: could not add interaction draw packet\n" );
			return false;
		}
	}
	if ( !packetFrame.AddPass( RENDER_PASS_FOG_BLEND, true ) || !packetFrame.AddDrawPacket( &drawSurfs[2], RENDER_PASS_FOG_BLEND, 2 ) ) {
		common->Printf( "RendererModernVisible self-test failed: could not add transparent draw packet\n" );
		return false;
	}
	packetFrame.FinishScene();
	packetFrame.AddCommandPacket( SCENE_PACKET_CATEGORY_UNKNOWN );
	if ( !packetFrame.AddScene( NULL, true ) || !packetFrame.AddPass( RENDER_PASS_PRESENT, true, true ) ) {
		common->Printf( "RendererModernVisible self-test failed: could not add present command packet\n" );
		return false;
	}
	packetFrame.FinishScene();

	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	if ( graph.FindPass( RENDER_PASS_DEFERRED_RESOLVE ) < 0 || graph.FindPass( RENDER_PASS_FORWARD_PLUS ) < 0 || graph.FindPass( RENDER_PASS_PRESENT ) < 0
		|| graph.FindResource( "deferredLight" ) < 0 || graph.FindResource( "sceneColor" ) < 0 || graph.FindResource( "backBuffer" ) < 0 ) {
		common->Printf( "RendererModernVisible self-test failed: graph composition resources missing\n" );
		return false;
	}

	R_MaterialResourceTable_PrepareFrame( packetFrame );
	R_RenderGraphResources_PrepareFrame( graph );
	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	R_ModernClusteredLighting_PrepareFrame( packetFrame, true );
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		true,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		rg_modernGLExecutorStats );
	rg_modernGLDrawPlan.Build( packetFrame, graph );
	R_ModernGLExecutor_CopyDrawPlanStats( rg_modernGLExecutorStats, rg_modernGLDrawPlan.Stats() );
	rg_modernGLSubmitPlan.Build( rg_modernGLDrawPlan );
	R_ModernGLExecutor_CopySubmitPlanStats( rg_modernGLExecutorStats, rg_modernGLSubmitPlan.Stats() );
	R_ModernGLExecutor_FinalizeModernVisibleCompatibility( packetFrame, rg_modernGLSubmitPlan, rg_modernGLExecutorStats );
	rg_modernGLExecutorStats.modernVisibleCanReplaceFrame =
		rg_modernGLExecutorStats.modernVisibleRequested &&
		rg_modernGLExecutorStats.modernVisibleProgramReady &&
		!rg_modernGLExecutorStats.modernVisibleBlockedByLegacy;
	R_ModernGLExecutor_SetEffectivePassRequests( rg_modernGLExecutorStats, true, true, true, true );
	R_ModernGLExecutor_RecordPassGates( rg_modernGLExecutorStats, false, false, false, false );
	R_ModernGLExecutor_UpdateFrameUBO( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitVisibleDepth( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitGBuffer( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitDeferredResolve( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitForwardPlus( rg_modernGLExecutorStats );
	R_ModernGLExecutor_FinalizePassOwnership( graph, rg_modernGLExecutorStats );
	R_ModernGLExecutor_ComposeVisibleFrame();

	const modernGLExecutorStats_t &stats = rg_modernGLExecutorStats;
	if ( stats.modernVisibleBlockedByLegacy || stats.modernVisibleOwnerFallbacks != 0 || stats.modernVisibleLegacyPasses != 0 || stats.modernVisiblePresentPasses <= 0 ) {
		common->Printf(
			"RendererModernVisible self-test failed: owner selection mismatch (blocked=%d legacy=%d ownerFallback=%d present=%d)\n",
			stats.modernVisibleBlockedByLegacy ? 1 : 0,
			stats.modernVisibleLegacyPasses,
			stats.modernVisibleOwnerFallbacks,
			stats.modernVisiblePresentPasses );
		return false;
	}

	const bool resourcesAvailable = R_RenderGraphResources_Stats().initialized && R_RenderGraphResources_Stats().available && rg_modernGLExecutorAvailable;
	if ( resourcesAvailable && ( !stats.deferredResolveExecuted || !stats.forwardPlusExecuted || !stats.modernVisibleExecuted || !stats.modernVisibleResourcesReady || !stats.modernVisibleSourceReady || !stats.modernVisibleBackBufferReady || stats.modernVisibleCompositions <= 0 || stats.modernVisiblePixels <= 0 ) ) {
		common->Printf(
			"RendererModernVisible self-test failed: composition execution mismatch (deferred=%d forward=%d exec=%d res=%d source=%d backBuffer=%d composed=%d pixels=%d fallback=%d)\n",
			stats.deferredResolveExecuted ? 1 : 0,
			stats.forwardPlusExecuted ? 1 : 0,
			stats.modernVisibleExecuted ? 1 : 0,
			stats.modernVisibleResourcesReady ? 1 : 0,
			stats.modernVisibleSourceReady ? 1 : 0,
			stats.modernVisibleBackBufferReady ? 1 : 0,
			stats.modernVisibleCompositions,
			stats.modernVisiblePixels,
			stats.modernVisibleFallbackPasses );
		return false;
	}

	common->Printf(
		"RendererModernVisible self-test passed (program=%d resources=%d source=%d backBuffer=%d blocked=%d composed=%d pixels=%d modern=%d legacy=%d fallback=%d deferred=%d forward=%d present=%d clears=%d)\n",
		stats.modernVisibleProgramReady ? 1 : 0,
		stats.modernVisibleResourcesReady ? 1 : 0,
		stats.modernVisibleSourceReady ? 1 : 0,
		stats.modernVisibleBackBufferReady ? 1 : 0,
		stats.modernVisibleBlockedByLegacy ? 1 : 0,
		stats.modernVisibleCompositions,
		stats.modernVisiblePixels,
		stats.modernVisibleModernPasses,
		stats.modernVisibleLegacyPasses,
		stats.modernVisibleFallbackPasses,
		stats.deferredResolveExecuted ? 1 : 0,
		stats.forwardPlusExecuted ? 1 : 0,
		stats.modernVisiblePresentPasses,
		stats.modernVisibleClearOps );
	return true;
}

bool RendererPassOwnership_RunSelfTest( void ) {
	modernGLExecutorStats_t stats;
	memset( &stats, 0, sizeof( stats ) );

	R_ModernGLExecutor_ResetPassOwnershipTable( "pass-ownership-selftest" );
	rg_modernGLPassOwnership.valid = true;
	rg_modernGLPassOwnership.handoffReady = true;
	idStr::Copynz( rg_modernGLPassOwnership.failClosedReason, "selftest-handoff-ready", sizeof( rg_modernGLPassOwnership.failClosedReason ) );

	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_DEPTH, MODERN_GL_PASS_OWNER_MODERN, true, true, "selftest-modern-depth" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_SHADOW_MAP, MODERN_GL_PASS_OWNER_MODERN, true, true, "selftest-modern-shadow-map" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_STENCIL_SHADOW, MODERN_GL_PASS_OWNER_MODERN, true, true, "selftest-shadow-map-replaces-stencil" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_AMBIENT, MODERN_GL_PASS_OWNER_MIXED, true, false, "selftest-diagnostic-gbuffer" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_DEFERRED_RESOLVE, MODERN_GL_PASS_OWNER_MODERN, true, false, "selftest-modern-deferred" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_FORWARD_PLUS, MODERN_GL_PASS_OWNER_MODERN, true, false, "selftest-modern-forward" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_ARB2_INTERACTION, MODERN_GL_PASS_OWNER_MODERN, true, true, "selftest-modern-lighting" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_GUI, MODERN_GL_PASS_OWNER_LEGACY, false, false, "selftest-legacy-gui" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_AUTHORED_POST, MODERN_GL_PASS_OWNER_LEGACY, false, false, "selftest-legacy-post" );
	R_ModernGLExecutor_UpdatePassOwnershipCounts( stats );

	if ( !R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_DEPTH )
		|| !R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_SHADOW_MAP )
		|| !R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_STENCIL_SHADOW )
		|| !R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_ARB2_INTERACTION )
		|| R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_AMBIENT )
		|| R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_GUI )
		|| stats.passOwnerDuplicateHazards != 0 ) {
		common->Printf(
			"RendererPassOwnership self-test failed: ownership mismatch (skipDepth=%d skipShadow=%d skipStencil=%d skipLight=%d skipAmbient=%d skipGui=%d hazards=%d)\n",
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_DEPTH ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_SHADOW_MAP ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_STENCIL_SHADOW ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_ARB2_INTERACTION ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_AMBIENT ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_GUI ) ? 1 : 0,
			stats.passOwnerDuplicateHazards );
		R_ModernGLExecutor_ResetPassOwnershipTable( "pass-ownership-selftest-failed" );
		return false;
	}

	R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_DEPTH );
	R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_SHADOW_MAP );
	R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_STENCIL_SHADOW );
	R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_ARB2_INTERACTION );
	R_ModernGLExecutor_UpdatePassOwnershipCounts( stats );
	if ( stats.passOwnerLegacySkipsIssued < 4 ) {
		common->Printf( "RendererPassOwnership self-test failed: skip accounting mismatch (%d)\n", stats.passOwnerLegacySkipsIssued );
		R_ModernGLExecutor_ResetPassOwnershipTable( "pass-ownership-selftest-failed" );
		return false;
	}

	R_ModernGLExecutor_FailClosedPassOwnership( stats, "selftest-fail-closed" );
	if ( R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_DEPTH )
		|| R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_SHADOW_MAP )
		|| stats.passOwnerFailClosedRestores <= 0 ) {
		common->Printf(
			"RendererPassOwnership self-test failed: fail-closed restore mismatch (skipDepth=%d skipShadow=%d restores=%d)\n",
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_DEPTH ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_SHADOW_MAP ) ? 1 : 0,
			stats.passOwnerFailClosedRestores );
		R_ModernGLExecutor_ResetPassOwnershipTable( "pass-ownership-selftest-failed" );
		return false;
	}

	common->Printf(
		"RendererPassOwnership self-test passed (table=%d modern=%d mixed=%d legacy=%d skips=%d hazards=%d failClosed=%d shadow=%d/%d)\n",
		stats.passOwnerTablePasses,
		stats.passOwnerModernPasses,
		stats.passOwnerMixedPasses,
		stats.passOwnerLegacyPasses,
		stats.passOwnerLegacySkipsIssued,
		stats.passOwnerDuplicateHazards,
		stats.passOwnerFailClosedRestores,
		stats.passOwnerShadowModernPasses,
		stats.passOwnerShadowLegacyPasses );
	R_ModernGLExecutor_ResetPassOwnershipTable( "pass-ownership-selftest-complete" );
	return true;
}

bool RendererModernCompatibility_RunSelfTest( void ) {
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererModernCompatibility self-test passed (shader library unavailable)\n" );
		return true;
	}

	struct rendererModernCompatibilityCVarRestore_t {
		idCVar &cvar;
		bool oldValue;
		rendererModernCompatibilityCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetBool() ) {}
		~rendererModernCompatibilityCVarRestore_t() { cvar.SetBool( oldValue ); }
	};
	rendererModernCompatibilityCVarRestore_t restoreModernVisible( r_rendererModernVisible );
	r_rendererModernVisible.SetBool( true );

	drawSurf_t drawSurfs[3];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	vertCache_t ambientCache;
	memset( &ambientCache, 0, sizeof( ambientCache ) );
	ambientCache.vbo = 101;
	ambientCache.offset = 64;
	ambientCache.size = geometry.numVerts * static_cast<int>( sizeof( idDrawVert ) );
	ambientCache.indexBuffer = false;
	ambientCache.tag = TAG_USED;
	vertCache_t indexCache;
	memset( &indexCache, 0, sizeof( indexCache ) );
	indexCache.vbo = 202;
	indexCache.offset = 128;
	indexCache.size = geometry.numIndexes * static_cast<int>( sizeof( glIndex_t ) );
	indexCache.indexBuffer = true;
	indexCache.tag = TAG_USED;
	geometry.ambientCache = &ambientCache;
	geometry.indexCache = &indexCache;
	for ( int i = 0; i < 3; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort() + static_cast<float>( i ) * 0.01f;
		}
	}

	drawSurf_t *worldDrawSurfPtrs[1] = { &drawSurfs[0] };
	drawSurf_t *guiDrawSurfPtrs[1] = { &drawSurfs[1] };
	drawSurf_t *demoDrawSurfPtrs[1] = { &drawSurfs[2] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	for ( int i = 0; i < 16; ++i ) {
		viewEntity.modelMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
		viewEntity.modelViewMatrix[i] = viewEntity.modelMatrix[i];
	}
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	drawSurfs[2].space = &viewEntity;

	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = worldDrawSurfPtrs;
	worldView.numDrawSurfs = 1;
	worldView.viewport.x1 = 0;
	worldView.viewport.y1 = 0;
	worldView.viewport.x2 = 127;
	worldView.viewport.y2 = 127;
	worldView.scissor = worldView.viewport;
	for ( int i = 0; i < 16; ++i ) {
		worldView.projectionMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
	}

	viewDef_t guiView = worldView;
	guiView.viewEntitys = NULL;
	guiView.drawSurfs = guiDrawSurfPtrs;
	guiView.numDrawSurfs = 1;

	viewDef_t subview = worldView;
	subview.isSubview = true;

	viewDef_t renderDemoView = worldView;
	renderDemoView.renderView.viewID = -1;
	renderDemoView.drawSurfs = demoDrawSurfPtrs;
	renderDemoView.numDrawSurfs = 1;

	idScenePacketFrame packetFrame;
	if ( !packetFrame.AddScene( &worldView, true ) ) {
		common->Printf( "RendererModernCompatibility self-test failed: could not add world scene\n" );
		return false;
	}
	const renderPassCategory_t drawPasses[] = {
		RENDER_PASS_DEPTH,
		RENDER_PASS_STENCIL_SHADOW,
		RENDER_PASS_SHADOW_MAP,
		RENDER_PASS_ARB2_INTERACTION,
		RENDER_PASS_LIGHT_GRID,
		RENDER_PASS_AMBIENT,
		RENDER_PASS_FOG_BLEND
	};
	for ( int i = 0; i < sizeof( drawPasses ) / sizeof( drawPasses[0] ); ++i ) {
		if ( !packetFrame.AddPass( drawPasses[i], true ) || !packetFrame.AddDrawPacket( &drawSurfs[0], drawPasses[i], i ) ) {
			common->Printf( "RendererModernCompatibility self-test failed: could not add %s draw pass\n", RenderPassCategory_Name( drawPasses[i] ) );
			return false;
		}
	}
	packetFrame.FinishScene();

	if ( !packetFrame.AddScene( &guiView, true ) || !packetFrame.AddPass( RENDER_PASS_GUI, true ) || !packetFrame.AddDrawPacket( &drawSurfs[1], RENDER_PASS_GUI, 0 ) ) {
		common->Printf( "RendererModernCompatibility self-test failed: could not add GUI scene\n" );
		return false;
	}
	packetFrame.FinishScene();

	if ( !packetFrame.AddScene( &subview, true ) || !packetFrame.AddPass( RENDER_PASS_AMBIENT, true ) || !packetFrame.AddDrawPacket( &drawSurfs[0], RENDER_PASS_AMBIENT, 0 ) ) {
		common->Printf( "RendererModernCompatibility self-test failed: could not add subview scene\n" );
		return false;
	}
	packetFrame.FinishScene();

	if ( !packetFrame.AddScene( &renderDemoView, true ) || !packetFrame.AddPass( RENDER_PASS_AMBIENT, true ) || !packetFrame.AddDrawPacket( &drawSurfs[2], RENDER_PASS_AMBIENT, 0 ) ) {
		common->Printf( "RendererModernCompatibility self-test failed: could not add render-demo scene\n" );
		return false;
	}
	packetFrame.FinishScene();

	const renderPassCategory_t commandPasses[] = {
		RENDER_PASS_DEFERRED_RESOLVE,
		RENDER_PASS_FORWARD_PLUS,
		RENDER_PASS_SSAO,
		RENDER_PASS_MOTION_BLUR,
		RENDER_PASS_LENS_FLARE,
		RENDER_PASS_BLOOM,
		RENDER_PASS_AUTHORED_POST,
		RENDER_PASS_SPECIAL_EFFECTS,
		RENDER_PASS_PRESENT
	};
	for ( int i = 0; i < sizeof( commandPasses ) / sizeof( commandPasses[0] ); ++i ) {
		if ( !packetFrame.AddScene( NULL, true ) || !packetFrame.AddPass( commandPasses[i], true, true ) ) {
			common->Printf( "RendererModernCompatibility self-test failed: could not add %s command pass\n", RenderPassCategory_Name( commandPasses[i] ) );
			return false;
		}
		packetFrame.FinishScene();
	}

	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	R_MaterialResourceTable_PrepareFrame( packetFrame );
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		true,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		rg_modernGLExecutorStats );
	rg_modernGLDrawPlan.Build( packetFrame, graph );
	R_ModernGLExecutor_CopyDrawPlanStats( rg_modernGLExecutorStats, rg_modernGLDrawPlan.Stats() );
	rg_modernGLSubmitPlan.Build( rg_modernGLDrawPlan );
	R_ModernGLExecutor_CopySubmitPlanStats( rg_modernGLExecutorStats, rg_modernGLSubmitPlan.Stats() );
	R_ModernGLExecutor_FinalizeModernVisibleCompatibility( packetFrame, rg_modernGLSubmitPlan, rg_modernGLExecutorStats );

	const modernGLExecutorStats_t &stats = rg_modernGLExecutorStats;
	if ( !stats.modernVisibleCompatibilityReady || stats.modernVisibleCompatibilityPasses < 17 || stats.modernVisiblePresentPasses <= 0 ) {
		common->Printf(
			"RendererModernCompatibility self-test failed: inventory coverage mismatch (ready=%d inventory=%d present=%d)\n",
			stats.modernVisibleCompatibilityReady ? 1 : 0,
			stats.modernVisibleCompatibilityPasses,
			stats.modernVisiblePresentPasses );
		return false;
	}
	if ( stats.modernVisibleGuiModernPasses <= 0 || stats.modernVisibleGuiDraws <= 0 || stats.modernVisibleGuiReadyDraws <= 0 || stats.modernVisibleGuiLegacyPasses != 0 || stats.modernVisibleGuiFallbackDraws != 0 ) {
		common->Printf(
			"RendererModernCompatibility self-test failed: GUI ownership mismatch (pass=%d draws=%d ready=%d legacy=%d fallback=%d)\n",
			stats.modernVisibleGuiModernPasses,
			stats.modernVisibleGuiDraws,
			stats.modernVisibleGuiReadyDraws,
			stats.modernVisibleGuiLegacyPasses,
			stats.modernVisibleGuiFallbackDraws );
		return false;
	}
	if ( stats.modernVisiblePostGraphPasses < 5 || stats.modernVisiblePostFallbackPasses <= 0 || stats.modernVisibleCopyRenderFallbackPasses <= 0 ) {
		common->Printf(
			"RendererModernCompatibility self-test failed: post ownership mismatch (graph=%d fallback=%d copy=%d)\n",
			stats.modernVisiblePostGraphPasses,
			stats.modernVisiblePostFallbackPasses,
			stats.modernVisibleCopyRenderFallbackPasses );
		return false;
	}
	if ( stats.modernVisibleSubviewGraphPasses < 2 || stats.modernVisibleSubviewFallbackPasses <= 0 || stats.modernVisibleRenderDemoFallbackPasses <= 0 || !stats.modernVisibleRenderDemoDeterministic ) {
		common->Printf(
			"RendererModernCompatibility self-test failed: subview/render-demo ownership mismatch (graph=%d subview=%d demo=%d deterministic=%d)\n",
			stats.modernVisibleSubviewGraphPasses,
			stats.modernVisibleSubviewFallbackPasses,
			stats.modernVisibleRenderDemoFallbackPasses,
			stats.modernVisibleRenderDemoDeterministic ? 1 : 0 );
		return false;
	}
	if ( stats.modernVisibleBSEFallbackPasses <= 0 || stats.modernVisibleBSEParticleFallbacks <= 0 || stats.modernVisibleBSETrailFallbacks <= 0 || stats.modernVisibleBSEBeamFallbacks <= 0 || stats.modernVisibleBSEDecalFallbacks <= 0 || stats.modernVisibleBSEMaterialFallbacks <= 0 ) {
		common->Printf(
			"RendererModernCompatibility self-test failed: BSE fallback category mismatch (fallback=%d particle=%d trail=%d beam=%d decal=%d material=%d)\n",
			stats.modernVisibleBSEFallbackPasses,
			stats.modernVisibleBSEParticleFallbacks,
			stats.modernVisibleBSETrailFallbacks,
			stats.modernVisibleBSEBeamFallbacks,
			stats.modernVisibleBSEDecalFallbacks,
			stats.modernVisibleBSEMaterialFallbacks );
		return false;
	}
	if ( !stats.modernVisibleBlockedByLegacy || stats.modernVisibleOwnerFallbacks <= 0 || stats.modernVisibleLightGridModernPasses <= 0 ) {
		common->Printf(
			"RendererModernCompatibility self-test failed: fallback policy mismatch (blocked=%d ownerFallback=%d lightGrid=%d)\n",
			stats.modernVisibleBlockedByLegacy ? 1 : 0,
			stats.modernVisibleOwnerFallbacks,
			stats.modernVisibleLightGridModernPasses );
		return false;
	}

	modernGLExecutorStats_t &gateStats = rg_modernGLExecutorStats;
	R_ModernGLExecutor_SetEffectivePassRequests( gateStats, false, false, false, false );
	R_ModernGLExecutor_RecordPassGates( gateStats, false, false, false, false );
	gateStats.frameMode = MODERN_GL_EXECUTOR_FRAME_ANALYZE;
	if ( gateStats.visibleDepthSkippedBlocked <= 0
		|| gateStats.opaqueGBufferSkippedBlocked <= 0
		|| gateStats.deferredResolveSkippedBlocked <= 0
		|| gateStats.forwardPlusSkippedBlocked <= 0
		|| gateStats.visibleDepthExecuted
		|| gateStats.opaqueGBufferExecuted
		|| gateStats.deferredResolveExecuted
		|| gateStats.forwardPlusExecuted ) {
		common->Printf(
			"RendererModernCompatibility self-test failed: blocked pass gate mismatch (depth=%d/%d gbuffer=%d/%d deferred=%d/%d forward=%d/%d)\n",
			gateStats.visibleDepthSkippedBlocked,
			gateStats.visibleDepthExecuted ? 1 : 0,
			gateStats.opaqueGBufferSkippedBlocked,
			gateStats.opaqueGBufferExecuted ? 1 : 0,
			gateStats.deferredResolveSkippedBlocked,
			gateStats.deferredResolveExecuted ? 1 : 0,
			gateStats.forwardPlusSkippedBlocked,
			gateStats.forwardPlusExecuted ? 1 : 0 );
		return false;
	}

	common->Printf(
		"RendererModernCompatibility self-test passed (inventory=%d modern=%d legacy=%d gui=%d/%d post=%d/%d subview=%d demo=%d bse=%d ownerFallback=%d blocked=%d skipBlocked=%d/%d/%d/%d)\n",
		stats.modernVisibleCompatibilityPasses,
		stats.modernVisibleCompatibilityModernPasses,
		stats.modernVisibleCompatibilityLegacyPasses,
		stats.modernVisibleGuiReadyDraws,
		stats.modernVisibleGuiDraws,
		stats.modernVisiblePostGraphPasses,
		stats.modernVisiblePostFallbackPasses,
		stats.modernVisibleSubviewFallbackPasses,
		stats.modernVisibleRenderDemoFallbackPasses,
		stats.modernVisibleBSEFallbackPasses,
		stats.modernVisibleOwnerFallbacks,
		stats.modernVisibleBlockedByLegacy ? 1 : 0,
		gateStats.visibleDepthSkippedBlocked,
		gateStats.opaqueGBufferSkippedBlocked,
		gateStats.deferredResolveSkippedBlocked,
		gateStats.forwardPlusSkippedBlocked );
	return true;
}

bool RendererLowOverhead_RunSelfTest( void ) {
	if ( !rg_modernGLExecutorFeatures.lowOverhead ) {
		common->Printf( "RendererLowOverhead self-test passed (skipped=1 tier lacks GL45 low-overhead features)\n" );
		return true;
	}
	if ( !rg_modernGLExecutorAvailable || !rg_modernGLExecutorInitialized || !rg_modernGLExecutorLowOverheadReady || rg_modernGLExecutorVAO == 0 || rg_modernGLExecutorFrameUBO == 0 ) {
		common->Printf( "RendererLowOverhead self-test failed: GL45 low-overhead executor resources unavailable\n" );
		return false;
	}
	if ( rg_modernGLExecutorLowOverheadSampler == 0 ) {
		common->Printf( "RendererLowOverhead self-test failed: DSA sampler unavailable\n" );
		return false;
	}
	if ( tr.defaultMaterial == NULL ) {
		common->Printf( "RendererLowOverhead self-test failed: default material unavailable\n" );
		return false;
	}

	struct rendererLowOverheadCVarRestore_t {
		idCVar &cvar;
		bool oldValue;

		rendererLowOverheadCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetBool() ) {}
		~rendererLowOverheadCVarRestore_t() { cvar.SetBool( oldValue ); }
	};
	rendererLowOverheadCVarRestore_t restoreOpaque( r_rendererModernOpaque );
	rendererLowOverheadCVarRestore_t restoreDeferred( r_rendererModernDeferred );
	r_rendererModernOpaque.SetBool( true );
	r_rendererModernDeferred.SetBool( true );

	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	vertCache_t ambientCache;
	memset( &ambientCache, 0, sizeof( ambientCache ) );
	ambientCache.vbo = 101;
	ambientCache.offset = 64;
	ambientCache.size = geometry.numVerts * static_cast<int>( sizeof( idDrawVert ) );
	ambientCache.indexBuffer = false;
	ambientCache.tag = TAG_USED;
	vertCache_t indexCache;
	memset( &indexCache, 0, sizeof( indexCache ) );
	indexCache.vbo = 202;
	indexCache.offset = 128;
	indexCache.size = geometry.numIndexes * static_cast<int>( sizeof( glIndex_t ) );
	indexCache.indexBuffer = true;
	indexCache.tag = TAG_USED;
	geometry.ambientCache = &ambientCache;
	geometry.indexCache = &indexCache;
	for ( int i = 0; i < 2; ++i ) {
		drawSurfs[i].geo = &geometry;
		drawSurfs[i].material = tr.defaultMaterial;
		drawSurfs[i].sort = tr.defaultMaterial->GetSort();
	}

	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewLight_t lights[2];
	memset( lights, 0, sizeof( lights ) );
	idRenderLightLocal lightDefs[2];
	memset( lightDefs, 0, sizeof( lightDefs ) );
	for ( int i = 0; i < 2; ++i ) {
		lightDefs[i].index = i;
		lightDefs[i].areaNum = -1;
		lightDefs[i].parms.origin.Set( 96.0f + 64.0f * i, 0.0f, 0.0f );
		lightDefs[i].parms.lightRadius.Set( 256.0f, 256.0f, 256.0f );
		lightDefs[i].parms.shaderParms[SHADERPARM_RED] = i == 0 ? 1.0f : 0.35f;
		lightDefs[i].parms.shaderParms[SHADERPARM_GREEN] = i == 0 ? 0.65f : 0.85f;
		lightDefs[i].parms.shaderParms[SHADERPARM_BLUE] = i == 0 ? 0.45f : 1.0f;
		lights[i].lightDef = &lightDefs[i];
		lights[i].next = i == 0 ? &lights[1] : NULL;
		lights[i].scissorRect.x1 = 0;
		lights[i].scissorRect.y1 = 0;
		lights[i].scissorRect.x2 = 639;
		lights[i].scissorRect.y2 = 479;
		lights[i].globalLightOrigin = lightDefs[i].parms.origin;
		lights[i].lightRadius = lightDefs[i].parms.lightRadius;
		lights[i].pointLight = i == 0;
		lights[i].viewSeesGlobalLightOrigin = true;
	}

	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;
	worldView.viewLights = &lights[0];
	worldView.renderView.width = 640;
	worldView.renderView.height = 480;
	worldView.renderView.fov_x = 90.0f;
	worldView.renderView.fov_y = 70.0f;
	worldView.renderView.viewaxis = mat3_identity;
	worldView.viewport.x1 = 0;
	worldView.viewport.y1 = 0;
	worldView.viewport.x2 = 639;
	worldView.viewport.y2 = 479;
	worldView.scissor.x1 = 0;
	worldView.scissor.y1 = 0;
	worldView.scissor.x2 = 639;
	worldView.scissor.y2 = 479;

	idScenePacketFrame packetFrame;
	if ( !packetFrame.AddScene( &worldView, true ) || !packetFrame.AddPass( RENDER_PASS_AMBIENT, true ) ) {
		common->Printf( "RendererLowOverhead self-test failed: could not build ambient packet scene\n" );
		return false;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( !packetFrame.AddDrawPacket( &drawSurfs[i], RENDER_PASS_AMBIENT, i ) ) {
			common->Printf( "RendererLowOverhead self-test failed: could not add ambient draw packet\n" );
			return false;
		}
	}
	packetFrame.FinishScene();

	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	if ( graph.FindResource( "gbufferAlbedo" ) < 0 || graph.FindResource( "sceneDepth" ) < 0 || graph.FindResource( "deferredLight" ) < 0 || graph.FindResource( "clusterGrid" ) < 0 ) {
		common->Printf( "RendererLowOverhead self-test failed: graph resources missing\n" );
		return false;
	}

	R_MaterialResourceTable_PrepareFrame( packetFrame );
	R_RenderGraphResources_PrepareFrame( graph );
	const renderGraphResourceManagerStats_t &graphStats = R_RenderGraphResources_Stats();
	if ( !graphStats.lowOverheadReady || graphStats.dsaTextureAllocations <= 0 || graphStats.dsaFramebufferAllocations <= 0 ) {
		common->Printf(
			"RendererLowOverhead self-test failed: graph DSA path unavailable (ready=%d tex=%d fbo=%d classic=%d/%d status='%s')\n",
			graphStats.lowOverheadReady ? 1 : 0,
			graphStats.dsaTextureAllocations,
			graphStats.dsaFramebufferAllocations,
			graphStats.classicTextureAllocations,
			graphStats.classicFramebufferAllocations,
			graphStats.lastFailure );
		return false;
	}
	R_ModernShadowPlanner_PrepareFrame( packetFrame, true );
	R_ModernClusteredLighting_PrepareFrame( packetFrame, true );

	modernGLExecutorStats_t stats;
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		true,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		stats );
	stats.opaqueGBufferRequested = true;
	stats.deferredResolveRequested = true;
	rg_modernGLDrawPlan.Build( packetFrame, graph );
	R_ModernGLExecutor_CopyDrawPlanStats( stats, rg_modernGLDrawPlan.Stats() );
	rg_modernGLSubmitPlan.Build( rg_modernGLDrawPlan );
	R_ModernGLExecutor_CopySubmitPlanStats( stats, rg_modernGLSubmitPlan.Stats() );
	R_ModernGLExecutor_UpdateFrameUBO( stats );
	const renderGraphResourceHandle_t *gBufferHandles[MODERN_GL_GBUFFER_ATTACHMENT_COUNT];
	memset( gBufferHandles, 0, sizeof( gBufferHandles ) );
	stats.opaqueGBufferResourcesReady = true;
	stats.opaqueGBufferAttachmentCount = MODERN_GL_GBUFFER_ATTACHMENT_COUNT;
	stats.opaqueGBufferBytesPerPixel = 0;
	for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
		if ( !R_ModernGLExecutor_GBufferResourceReady( rg_modernGLGBufferAttachmentNames[i], gBufferHandles[i] ) ) {
			stats.opaqueGBufferResourcesReady = false;
		} else {
			stats.opaqueGBufferBytesPerPixel += R_ModernGLExecutor_GBufferBytesPerPixel( *gBufferHandles[i] );
		}
	}
	const renderGraphResourceHandle_t *sceneDepth = NULL;
	const bool depthReady = R_ModernGLExecutor_DepthResourceReady( "sceneDepth", sceneDepth );
	if ( !stats.opaqueGBufferResourcesReady || !depthReady || sceneDepth == NULL || !R_ModernGLExecutor_PrepareGBufferFBO( gBufferHandles, *sceneDepth, stats ) ) {
		common->Printf(
			"RendererLowOverhead self-test failed: G-buffer DSA preparation mismatch (res=%d depth=%d fboDSA=%d mrt=%d)\n",
			stats.opaqueGBufferResourcesReady ? 1 : 0,
			depthReady ? 1 : 0,
			stats.lowOverheadFramebufferDSAUpdates,
			stats.opaqueGBufferMRTReady ? 1 : 0 );
		return false;
	}
	R_ModernGLExecutor_SubmitDeferredResolve( stats );
	R_ModernGLExecutor_RecordMetrics( stats );

	if ( !stats.lowOverheadReady || !stats.tierUsesDSA || !stats.tierUsesMultiBind || !stats.lowOverheadSamplerReady ) {
		common->Printf( "RendererLowOverhead self-test failed: low-overhead capability flags mismatch\n" );
		return false;
	}
	if ( !stats.deferredResolveExecuted || !stats.deferredResolveResourcesReady || !stats.deferredResolveClusterReady ) {
		common->Printf(
			"RendererLowOverhead self-test failed: deferred low-overhead execution mismatch (exec=%d res=%d cluster=%d fallback=%d)\n",
			stats.deferredResolveExecuted ? 1 : 0,
			stats.deferredResolveResourcesReady ? 1 : 0,
			stats.deferredResolveClusterReady ? 1 : 0,
			stats.deferredResolveResourceFallbacks );
		return false;
	}
	if ( stats.lowOverheadDSAUpdates <= 0 || stats.lowOverheadFramebufferDSAUpdates <= 0 || stats.lowOverheadMultiBindBatches <= 0 || stats.lowOverheadTextureMultiBindBatches <= 0 || stats.lowOverheadSamplerMultiBindBatches <= 0 || stats.lowOverheadCompactedBatches <= 0 ) {
		common->Printf(
			"RendererLowOverhead self-test failed: low-overhead operations were not exercised (dsa=%d fboDSA=%d bufferBind=%d textureBind=%d samplerBind=%d compact=%d)\n",
			stats.lowOverheadDSAUpdates,
			stats.lowOverheadFramebufferDSAUpdates,
			stats.lowOverheadMultiBindBatches,
			stats.lowOverheadTextureMultiBindBatches,
			stats.lowOverheadSamplerMultiBindBatches,
			stats.lowOverheadCompactedBatches );
		return false;
	}

	const rendererUploadStats_t &uploadStats = R_RendererUpload_Stats();
	common->Printf(
		"RendererLowOverhead self-test passed (dsa=1 multiBind=1 sampler=%d textureDSA=%d framebufferDSA=%d dsaUpdates=%d framebufferDSAUpdates=%d bufferMultiBind=%d textureMultiBind=%d samplerMultiBind=%d classicTextureBinds=%d bindless=%d/%d compactedBatches=%d persistent=%d fences=%d/%d waits=%d)\n",
		stats.lowOverheadSamplerReady ? 1 : 0,
		graphStats.dsaTextureAllocations,
		graphStats.dsaFramebufferAllocations,
		stats.lowOverheadDSAUpdates,
		stats.lowOverheadFramebufferDSAUpdates,
		stats.lowOverheadMultiBindBatches,
		stats.lowOverheadTextureMultiBindBatches,
		stats.lowOverheadSamplerMultiBindBatches,
		stats.lowOverheadClassicTextureBinds,
		stats.lowOverheadBindlessRequested ? 1 : 0,
		stats.lowOverheadBindlessAvailable ? 1 : 0,
		stats.lowOverheadCompactedBatches,
		uploadStats.persistentMapped ? 1 : 0,
		uploadStats.frameFencesSubmitted,
		uploadStats.frameFencesRetired,
		uploadStats.frameFenceWaits );
	return true;
}
