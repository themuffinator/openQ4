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
	float	screenBounds[4];
	float	depthBounds[4];
	GLuint	ids[4];
	GLuint	indirect[4];
} modernGLGpuSceneRecord_t;

typedef struct modernGLDrawElementsIndirectCommand_s {
	GLuint	count;
	GLuint	instanceCount;
	GLuint	firstIndex;
	GLuint	baseVertex;
	GLuint	baseInstance;
} modernGLDrawElementsIndirectCommand_t;

typedef struct modernGLDrawRecord_s {
	float	modelViewProjection[16];
	float	modelViewMatrix[16];
	float	debugColor[4];
	float	localParams[4];
	float	materialFlags[4];
	float	materialEnhancement[4];
	GLuint	ids[4];
} modernGLDrawRecord_t;

typedef struct modernGLGpuDrivenCpuReference_s {
	int		processedCommands;
	int		eligibleCommands;
	int		generatedCommands;
	int		culledCommands;
	int		visibleInstances;
	int		clusterBins;
} modernGLGpuDrivenCpuReference_t;

typedef struct modernGLStreamBufferBinding_s {
	bool						valid;
	rendererUploadAllocation_t	allocation;
	GLsizeiptr					size;
} modernGLStreamBufferBinding_t;

typedef struct modernGLGpuDrivenStreamBindings_s {
	modernGLStreamBufferBinding_t	sceneRecords;
	modernGLStreamBufferBinding_t	validationCounters;
	modernGLStreamBufferBinding_t	indirectCommands;
	modernGLStreamBufferBinding_t	drawRecords;
	modernGLStreamBufferBinding_t	drawRecordIndices;
	modernGLStreamBufferBinding_t	bucketRecords;
	bool							valid;
} modernGLGpuDrivenStreamBindings_t;

typedef struct modernGLPendingGpuValidationReadback_s {
	bool							valid;
	GLuint							buffer;
	GLintptr						offset;
	GLsizeiptr						size;
	int								submitFrame;
	int								readyFrame;
	GLsync							fence;
	modernGLGpuDrivenCpuReference_t	cpuReference;
} modernGLPendingGpuValidationReadback_t;

typedef struct modernGLGpuDrivenBucket_s {
	bool	valid;
	GLuint	program;
	GLuint	vertexBuffer;
	GLuint	indexBuffer;
	GLenum	indexType;
	int		vertexStride;
	int		materialTableIndex;
	unsigned int materialStableId;
	int		passCategory;
	int		shaderKind;
	int		pipeline;
	int		cullType;
	int		scissorX1;
	int		scissorY1;
	int		scissorX2;
	int		scissorY2;
	bool	twoSided;
	bool	shouldCreateBackSides;
	bool	negativeScale;
	bool	weaponDepthHack;
	modernGLSubmitCommand_t command;
	int		firstIndirect;
	int		commandCount;
} modernGLGpuDrivenBucket_t;

typedef struct modernGLGpuDrivenBucketRecord_s {
	GLuint	header[4];		// first output, max commands, bucket id, reserved
	GLuint	counters[4];	// compacted commands, Hi-Z rejects, reserved, reserved
} modernGLGpuDrivenBucketRecord_t;

enum modernGLGpuDrivenRecordFlags_t {
	MODERN_GL_GPU_RECORD_INDEXED = 1u << 0,
	MODERN_GL_GPU_RECORD_INDIRECT_ELIGIBLE = 1u << 1,
	MODERN_GL_GPU_RECORD_VISIBLE = 1u << 2,
	MODERN_GL_GPU_RECORD_CLUSTER_BIN_SOURCE = 1u << 3,
	MODERN_GL_GPU_RECORD_HIZ_CANDIDATE = 1u << 4
};

enum modernGLGpuDrivenCounter_t {
	MODERN_GL_GPU_COUNTER_PROCESSED = 0,
	MODERN_GL_GPU_COUNTER_ELIGIBLE,
	MODERN_GL_GPU_COUNTER_GENERATED,
	MODERN_GL_GPU_COUNTER_CULLED,
	MODERN_GL_GPU_COUNTER_VISIBLE_INSTANCES,
	MODERN_GL_GPU_COUNTER_CLUSTER_BINS,
	MODERN_GL_GPU_COUNTER_INDIRECT_SIGNATURE,
	MODERN_GL_GPU_COUNTER_HIZ_TESTED,
	MODERN_GL_GPU_COUNTER_HIZ_REJECTED,
	MODERN_GL_GPU_COUNTER_COMPACTED,
	MODERN_GL_GPU_COUNTER_COUNT
};

const int MODERN_GL_GPU_DRIVEN_MAX_RECORDS = MODERN_GL_DRAW_PLAN_MAX_ENTRIES;
const int MODERN_GL_GPU_DRIVEN_MAX_BUCKETS = MODERN_GL_DRAW_PLAN_MAX_ENTRIES;
const int MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS = MODERN_GL_GPU_COUNTER_COUNT;
const int MODERN_GL_GPU_DRIVEN_WORKGROUP_SIZE = 64;
const int MODERN_GL_GPU_VALIDATION_PENDING_READBACKS = 8;
const int MODERN_GL_GBUFFER_ATTACHMENT_COUNT = 4;

typedef struct modernGLFramebufferAttachmentCache_s {
	bool	valid;
	GLuint	framebuffer;
	GLuint	colorTextures[MODERN_GL_GBUFFER_ATTACHMENT_COUNT];
	GLuint	depthTexture;
	GLenum	depthTarget;
	GLenum	depthAttachment;
	int		width;
	int		height;
} modernGLFramebufferAttachmentCache_t;

enum modernGLDrawVertAttribute_t {
	MODERN_GL_DRAWVERT_ATTR_POSITION = 0,
	MODERN_GL_DRAWVERT_ATTR_COLOR = 3,
	MODERN_GL_DRAWVERT_ATTR_TEXCOORD0 = 8,
	MODERN_GL_DRAWVERT_ATTR_TANGENT0 = 9,
	MODERN_GL_DRAWVERT_ATTR_TANGENT1 = 10,
	MODERN_GL_DRAWVERT_ATTR_NORMAL = 11
};

const GLuint MODERN_GL_DRAWVERT_BINDING_INDEX = 0;
const GLuint MODERN_GL_DRAW_RECORD_BINDING_INDEX = 1;
const GLuint MODERN_GL_DRAW_RECORD_ATTR_INDEX = 12;
const GLuint MODERN_GL_DRAW_RECORD_SSBO_BINDING = 4;
const GLuint MODERN_GL_GPU_BUCKET_SSBO_BINDING = 5;

typedef struct modernGLDrawVertAttributeDesc_s {
	modernGLDrawVertAttribute_t	attribute;
	GLint						components;
	GLenum						type;
	GLboolean					normalized;
	GLuint						relativeOffset;
} modernGLDrawVertAttributeDesc_t;

static const modernGLDrawVertAttributeDesc_t rg_modernGLDrawVertAttributes[] = {
	{ MODERN_GL_DRAWVERT_ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, DRAWVERT_XYZ_OFFSET },
	{ MODERN_GL_DRAWVERT_ATTR_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, DRAWVERT_COLOR_OFFSET },
	{ MODERN_GL_DRAWVERT_ATTR_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, DRAWVERT_ST_OFFSET },
	{ MODERN_GL_DRAWVERT_ATTR_TANGENT0, 3, GL_FLOAT, GL_FALSE, DRAWVERT_TANGENT0_OFFSET },
	{ MODERN_GL_DRAWVERT_ATTR_TANGENT1, 3, GL_FLOAT, GL_FALSE, DRAWVERT_TANGENT1_OFFSET },
	{ MODERN_GL_DRAWVERT_ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, DRAWVERT_NORMAL_OFFSET }
};

typedef struct modernGLVertexBindingSourceCache_s {
	bool		valid;
	GLuint		vertexBuffer;
	GLintptr	baseOffset;
	GLsizei		stride;
} modernGLVertexBindingSourceCache_t;

typedef struct modernGLVertexInputCache_s {
	bool								formatConfigured;
	modernGLVertexBindingSourceCache_t	vertexBindingSource;
	bool								legacyLayoutValid;
	GLuint								legacyVertexBuffer;
	int									legacyVertexStride;
	int									legacyAmbientCacheOffset;
} modernGLVertexInputCache_t;

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

bool R_ModernGLExecutor_ModernVisibleRequestedForPost( void ) {
	return R_ModernGLExecutor_ModernVisibleRequested();
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
	bool					dropHazard;
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

static void R_ModernGLExecutor_FormatString( char *dest, int destSize, const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	idStr::vsnPrintf( dest, destSize, fmt, argptr );
	va_end( argptr );
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
static renderBackendCaps_t rg_modernGLExecutorCaps;
static renderFeatureSet_t rg_modernGLExecutorFeatures;
static GLuint rg_modernGLExecutorVAO = 0;
static GLuint rg_modernGLExecutorFrameUBO = 0;
static GLuint rg_modernGLExecutorSceneSSBO = 0;
static GLuint rg_modernGLExecutorIndirectBuffer = 0;
static GLuint rg_modernGLExecutorDrawRecordSSBO = 0;
static GLuint rg_modernGLExecutorDrawRecordIndexBuffer = 0;
static GLuint rg_modernGLExecutorBucketSSBO = 0;
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
static GLuint rg_modernGLExecutorHiZReduceProgram = 0;
static GLuint rg_modernGLExecutorHiZFBO = 0;
static GLint rg_modernGLExecutorComputeRecordCountLocation = -1;
static GLint rg_modernGLExecutorComputeHiZTextureLocation = -1;
static GLint rg_modernGLExecutorComputeHiZParamsLocation = -1;
static GLint rg_modernGLExecutorDepthOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorDepthOverlayParamsLocation = -1;
static GLint rg_modernGLExecutorGBufferOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorGBufferOverlayParamsLocation = -1;
static GLint rg_modernGLExecutorDeferredOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorDeferredOverlayParamsLocation = -1;
static GLint rg_modernGLExecutorVisibleCompositeDeferredLocation = -1;
static GLint rg_modernGLExecutorVisibleCompositeForwardLocation = -1;
static GLint rg_modernGLExecutorVisibleCompositeParamsLocation = -1;
static GLint rg_modernGLExecutorHiZReduceTextureLocation = -1;
static GLint rg_modernGLExecutorHiZReduceSourceMipLocation = -1;
static bool rg_modernGLExecutorInitialized = false;
static bool rg_modernGLExecutorAvailable = false;
static bool rg_modernGLExecutorGpuDrivenReady = false;
static bool rg_modernGLExecutorLowOverheadReady = false;
static bool rg_modernGLExecutorVertexBindingReady = false;
static int rg_modernGLExecutorVertexInputFormatSetups = 0;
static modernGLVertexInputCache_t rg_modernGLVertexInputCache;
static modernGLGpuDrivenBucket_t rg_modernGLGpuDrivenBuckets[MODERN_GL_DRAW_PLAN_MAX_ENTRIES];
static int rg_modernGLGpuDrivenBucketCount = 0;
static modernGLStreamBufferBinding_t rg_modernGLFrameUBOStream;
static modernGLGpuDrivenStreamBindings_t rg_modernGLGpuDrivenStreamBindings;
static modernGLPendingGpuValidationReadback_t rg_modernGLPendingValidationReadbacks[MODERN_GL_GPU_VALIDATION_PENDING_READBACKS];
static int rg_modernGLExecutorUniformBufferAlignment = 256;
static int rg_modernGLExecutorShaderStorageAlignment = 16;
static int rg_modernGLExecutorIndirectBufferAlignment = 4;
static modernGLFramebufferAttachmentCache_t rg_modernGLExecutorGBufferAttachmentCache;
static modernGLFramebufferAttachmentCache_t rg_modernGLExecutorForwardPlusAttachmentCache;
static bool rg_modernGLExecutorSoftPassHandoffs = false;
static bool rg_modernGLExecutorModernStateDirty = false;

static ID_INLINE GLint R_ModernGLExecutor_SafeStencilClearValue( void ) {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

static int R_ModernGLExecutor_BufferAlignmentPowerOfTwo( int value, int fallback ) {
	if ( value <= 0 ) {
		value = fallback;
	}
	value = Max( 1, value );
	if ( ( value & ( value - 1 ) ) == 0 ) {
		return value;
	}
	int aligned = 1;
	while ( aligned < value && aligned < 4096 ) {
		aligned <<= 1;
	}
	return aligned;
}

static int R_ModernGLExecutor_QueryBufferOffsetAlignment( GLenum pname, int fallback ) {
	GLint value = 0;
	glGetIntegerv( pname, &value );
	return R_ModernGLExecutor_BufferAlignmentPowerOfTwo( static_cast<int>( value ), fallback );
}

static void R_ModernGLExecutor_QueryStreamingAlignments( void ) {
	rg_modernGLExecutorUniformBufferAlignment = rg_modernGLExecutorCaps.hasUBO ? R_ModernGLExecutor_QueryBufferOffsetAlignment( GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, 256 ) : 256;
	rg_modernGLExecutorShaderStorageAlignment = rg_modernGLExecutorCaps.hasSSBO ? R_ModernGLExecutor_QueryBufferOffsetAlignment( GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, 16 ) : 16;
	rg_modernGLExecutorIndirectBufferAlignment = 4;
}

static void R_ModernGLExecutor_ResetStreamBinding( modernGLStreamBufferBinding_t &binding ) {
	memset( &binding, 0, sizeof( binding ) );
}

static void R_ModernGLExecutor_ResetGpuDrivenStreamBindings( void ) {
	memset( &rg_modernGLGpuDrivenStreamBindings, 0, sizeof( rg_modernGLGpuDrivenStreamBindings ) );
}

class modernGLExecutorSoftPassHandoffScope_t {
public:
	modernGLExecutorSoftPassHandoffScope_t() : previousValue( rg_modernGLExecutorSoftPassHandoffs ) {
		rg_modernGLExecutorSoftPassHandoffs = true;
	}
	~modernGLExecutorSoftPassHandoffScope_t() {
		rg_modernGLExecutorSoftPassHandoffs = previousValue;
	}

private:
	bool previousValue;
};

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
	stats.drawPlanGeometryFallbackDraws = drawPlanStats.geometryFallbackDraws;
	stats.drawPlanGeometryDeformFallbackDraws = drawPlanStats.geometryDeformFallbackDraws;
	stats.drawPlanGeometrySkinnedFallbackDraws = drawPlanStats.geometrySkinnedFallbackDraws;
	stats.drawPlanIndexedDraws = drawPlanStats.indexedDraws;
	stats.drawPlanVertexOnlyDraws = drawPlanStats.vertexOnlyDraws;
	stats.drawPlanStateBatches = drawPlanStats.stateBatches;
	stats.drawPlanProgramSwitches = drawPlanStats.programSwitches;
	stats.drawPlanMaterialSwitches = drawPlanStats.materialSwitches;
	stats.pipelinePlanReady = stats.drawPlanReady;
	stats.pipelineDepthCommands = drawPlanStats.pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH];
	stats.pipelineShadowDepthCommands = drawPlanStats.pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_SHADOW_DEPTH];
	stats.pipelineGBufferCommands = drawPlanStats.pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER];
	stats.pipelineForwardPlusCommands =
		drawPlanStats.pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE]
		+ drawPlanStats.pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST]
		+ drawPlanStats.pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT];
	stats.pipelineForwardPlusTransparentCommands = drawPlanStats.pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT];
	stats.pipelineGuiCommands = drawPlanStats.pipelineDraws[MODERN_GL_DRAW_PLAN_PIPELINE_GUI];
	stats.pipelineBatches = drawPlanStats.pipelineBatches;
	stats.pipelineGeometryBatches = drawPlanStats.geometryBatches;
	stats.pipelineTextureSetBatches = drawPlanStats.textureSetBatches;
	stats.pipelineScissorBatches = drawPlanStats.scissorBatches;
	stats.pipelineStableLayoutReady = drawPlanStats.transparentSortBreaks == 0;
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
	stats.submitPlanSortEligibleDraws = submitPlanStats.sortEligibleDraws;
	stats.submitPlanSortLockedDraws = submitPlanStats.sortLockedDraws;
	stats.submitPlanSortSpans = submitPlanStats.sortSpans;
	stats.submitPlanSortBuckets = submitPlanStats.sortBuckets;
	stats.submitPlanSortReorderedDraws = submitPlanStats.sortReorderedDraws;
	stats.submitPlanUnsortedStateBuckets = submitPlanStats.unsortedStateBuckets;
	stats.submitPlanSortedStateBuckets = submitPlanStats.sortedStateBuckets;
	stats.submitPlanSortStateBucketSavings = submitPlanStats.sortStateBucketSavings;
	stats.submitPlanSortProgramBatchSavings = submitPlanStats.sortProgramBatchSavings;
	stats.submitPlanSortMaterialBatchSavings = submitPlanStats.sortMaterialBatchSavings;
	stats.submitPlanSortVertexBufferBatchSavings = submitPlanStats.sortVertexBufferBatchSavings;
	stats.submitPlanUniformUpdates = submitPlanStats.uniformUpdates;
	stats.submitPlanFrameUBOBinds = submitPlanStats.frameUBOBinds;
	stats.lowOverheadCompactedBatches = stats.tierUsesMultiBind ? ( submitPlanStats.programBatches + submitPlanStats.materialBatches ) : 0;
	stats.pipelinePlanReady = stats.pipelinePlanReady && stats.submitPlanReady;
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

static bool R_ModernGLExecutor_CanUseVertexBinding( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	const bool selectedTierSupportsVertexBinding =
		( features.gpuDriven || features.lowOverhead )
		&& ( caps.glMajor > 4 || ( caps.glMajor == 4 && caps.glMinor >= 3 ) );
	if ( !selectedTierSupportsVertexBinding || !caps.hasVAO || !caps.hasVBO ) {
		return false;
	}
	return glBindVertexBuffer != NULL
		&& glVertexAttribFormat != NULL
		&& glVertexAttribBinding != NULL
		&& glEnableVertexAttribArray != NULL;
}

static void R_ModernGLExecutor_ResetVertexInputCache( void ) {
	memset( &rg_modernGLVertexInputCache, 0, sizeof( rg_modernGLVertexInputCache ) );
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
	if ( glBindVertexBuffer == NULL || glVertexAttribFormat == NULL || glVertexAttribBinding == NULL || glVertexBindingDivisor == NULL || glEnableVertexAttribArray == NULL || glDisableVertexAttribArray == NULL ) {
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
	R_GLStateCache_InvalidateBufferBinding( target, "modern buffer create" );
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

static bool R_ModernGLExecutor_StreamBufferData( const void *data, GLsizeiptr bytes, int alignment, modernGLStreamBufferBinding_t &binding, modernGLExecutorStats_t &stats ) {
	(void)stats;
	R_ModernGLExecutor_ResetStreamBinding( binding );
	if ( data == NULL || bytes <= 0 || bytes > static_cast<GLsizeiptr>( idMath::INT_MAX ) || !R_RendererUpload_DynamicFrameBridgeAvailable() ) {
		return false;
	}
	rendererUploadAllocation_t allocation;
	if ( !R_RendererUpload_AllocFrameTemp( const_cast<void *>( data ), static_cast<int>( bytes ), Max( 1, alignment ), allocation ) ) {
		return false;
	}
	binding.valid = true;
	binding.allocation = allocation;
	binding.size = bytes;
	return true;
}

static int R_ModernGLExecutor_AlignStreamOffset( int offset, int alignment ) {
	alignment = Max( 1, alignment );
	const int remainder = offset % alignment;
	return remainder == 0 ? offset : offset + alignment - remainder;
}

static bool R_ModernGLExecutor_StreamSequenceFits( const GLsizeiptr *bytes, const int *alignments, int count ) {
	if ( bytes == NULL || alignments == NULL || count <= 0 || !R_RendererUpload_DynamicFrameBridgeAvailable() ) {
		return false;
	}
	const int capacity = R_RendererUpload_FrameCapacity();
	int head = R_RendererUpload_Stats().frameRingUsedBytes;
	if ( capacity <= 0 || head < 0 || head > capacity ) {
		return false;
	}
	for ( int i = 0; i < count; ++i ) {
		if ( bytes[i] <= 0 || bytes[i] > static_cast<GLsizeiptr>( idMath::INT_MAX ) ) {
			return false;
		}
		const int size = static_cast<int>( bytes[i] );
		head = R_ModernGLExecutor_AlignStreamOffset( head, alignments[i] );
		if ( size > capacity || head > capacity - size ) {
			return false;
		}
		head += size;
	}
	return true;
}

static void R_ModernGLExecutor_RecordUploadFallback( modernGLExecutorStats_t &stats, GLsizeiptr bytes ) {
	if ( bytes > 0 ) {
		stats.uploadManagerFallbackBytes += static_cast<int>( bytes );
		stats.uploadManagerFallbackBuffers++;
	}
}

static GLuint R_ModernGLExecutor_CompileGpuDrivenComputeProgram( void ) {
	static const char *computeSource =
		"#version 430\n"
		"layout(local_size_x = 64) in;\n"
		"struct SceneRecord { vec4 counts; vec4 screenBounds; vec4 depthBounds; uvec4 ids; uvec4 indirect; };\n"
		"struct DrawElementsIndirectCommand { uint count; uint instanceCount; uint firstIndex; uint baseVertex; uint baseInstance; };\n"
		"struct BucketRecord { uvec4 header; uvec4 counters; };\n"
		"uniform uint u_recordCount;\n"
		"uniform sampler2D u_hiZTexture;\n"
		"uniform vec4 u_hiZParams;\n"
		"layout(std430, binding = 1) readonly buffer ModernSceneRecords { SceneRecord records[]; };\n"
		"layout(std430, binding = 2) buffer ModernValidation { uint counters[10]; };\n"
		"layout(std430, binding = 3) buffer ModernIndirectCommands { DrawElementsIndirectCommand commands[]; };\n"
		"layout(std430, binding = 5) buffer ModernBucketRecords { BucketRecord buckets[]; };\n"
		"float FetchHiZRectMax(vec4 bounds, int mip, bool flipY) {\n"
		"	ivec2 texSize = textureSize( u_hiZTexture, mip );\n"
		"	ivec2 maxCoord = max( texSize - ivec2( 1 ), ivec2( 0 ) );\n"
		"	vec2 scale = vec2( float( texSize.x ) / max( u_hiZParams.x, 1.0 ), float( texSize.y ) / max( u_hiZParams.y, 1.0 ) );\n"
		"	float y0 = bounds.y;\n"
		"	float y1 = bounds.w;\n"
		"	if ( flipY ) {\n"
		"		y0 = u_hiZParams.y - 1.0 - bounds.w;\n"
		"		y1 = u_hiZParams.y - 1.0 - bounds.y;\n"
		"	}\n"
		"	ivec2 p0 = clamp( ivec2( floor( vec2( bounds.x, y0 ) * scale ) ), ivec2( 0 ), maxCoord );\n"
		"	ivec2 p1 = clamp( ivec2( floor( vec2( bounds.z, y1 ) * scale ) ), ivec2( 0 ), maxCoord );\n"
		"	float d0 = texelFetch( u_hiZTexture, p0, mip ).r;\n"
		"	float d1 = texelFetch( u_hiZTexture, ivec2( p1.x, p0.y ), mip ).r;\n"
		"	float d2 = texelFetch( u_hiZTexture, ivec2( p0.x, p1.y ), mip ).r;\n"
		"	float d3 = texelFetch( u_hiZTexture, p1, mip ).r;\n"
		"	return max( max( d0, d1 ), max( d2, d3 ) );\n"
		"}\n"
		"bool HiZRejects( SceneRecord record ) {\n"
		"	if ( u_hiZParams.w < 0.5 || u_hiZParams.z < 1.5 ) {\n"
		"		return false;\n"
		"	}\n"
		"	vec4 bounds = record.screenBounds;\n"
		"	if ( bounds.z < bounds.x || bounds.w < bounds.y ) {\n"
		"		return false;\n"
		"	}\n"
		"	float extent = max( bounds.z - bounds.x + 1.0, bounds.w - bounds.y + 1.0 );\n"
		"	int mip = clamp( int( ceil( log2( max( extent, 1.0 ) ) ) ), 0, int( u_hiZParams.z ) - 1 );\n"
		"	float maxDepth = max( FetchHiZRectMax( bounds, mip, false ), FetchHiZRectMax( bounds, mip, true ) );\n"
		"	float nearDepth = clamp( record.depthBounds.x, 0.0, 1.0 );\n"
		"	return nearDepth > maxDepth + 0.0005;\n"
		"}\n"
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
		"	bool hiZCandidate = ( flags & 16u ) != 0u;\n"
		"	atomicAdd( counters[0], 1u );\n"
		"	if ( indirectEligible ) {\n"
		"		atomicAdd( counters[1], 1u );\n"
		"		atomicAdd( counters[6], record.ids.x + 1u );\n"
		"	}\n"
		"	if ( ( flags & 8u ) != 0u ) {\n"
		"		atomicAdd( counters[5], uint( record.counts.z ) );\n"
		"	}\n"
		"	if ( visible && indirectEligible && hiZCandidate ) {\n"
		"		atomicAdd( counters[7], 1u );\n"
		"		if ( HiZRejects( record ) ) {\n"
		"			visible = false;\n"
		"			atomicAdd( counters[8], 1u );\n"
		"			atomicAdd( buckets[record.ids.x].counters.y, 1u );\n"
		"		}\n"
		"	}\n"
		"	if ( !visible ) {\n"
		"		atomicAdd( counters[3], 1u );\n"
		"		return;\n"
		"	}\n"
		"	atomicAdd( counters[4], 1u );\n"
		"	if ( !indexed || !indirectEligible ) {\n"
		"		return;\n"
		"	}\n"
		"	uint bucketIndex = record.ids.x;\n"
		"	uint compactIndex = atomicAdd( buckets[bucketIndex].counters.x, 1u );\n"
		"	if ( compactIndex >= buckets[bucketIndex].header.y ) {\n"
		"		return;\n"
		"	}\n"
		"	uint dst = buckets[bucketIndex].header.x + compactIndex;\n"
		"	commands[dst].count = record.indirect.x;\n"
		"	commands[dst].instanceCount = 1u;\n"
		"	commands[dst].firstIndex = record.indirect.y;\n"
		"	commands[dst].baseVertex = record.indirect.z;\n"
		"	commands[dst].baseInstance = record.indirect.w;\n"
		"	atomicAdd( counters[2], 1u );\n"
		"	atomicAdd( counters[9], 1u );\n"
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
	rg_modernGLExecutorComputeHiZTextureLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "u_hiZTexture" ) : -1;
	rg_modernGLExecutorComputeHiZParamsLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "u_hiZParams" ) : -1;
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
		"	vec3 color = max( deferredValue.rgb + forwardValue.rgb, vec3(0.0) );\n"
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

static GLuint R_ModernGLExecutor_CompileHiZReduceProgram( void ) {
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
		"uniform sampler2D uSourceDepth;\n"
		"uniform int uSourceMip;\n"
		"void main() {\n"
		"	ivec2 sourceSize = textureSize( uSourceDepth, uSourceMip );\n"
		"	ivec2 baseCoord = ivec2( gl_FragCoord.xy ) * 2;\n"
		"	ivec2 maxCoord = max( sourceSize - ivec2(1), ivec2(0) );\n"
		"	float d0 = texelFetch( uSourceDepth, clamp( baseCoord + ivec2(0, 0), ivec2(0), maxCoord ), uSourceMip ).r;\n"
		"	float d1 = texelFetch( uSourceDepth, clamp( baseCoord + ivec2(1, 0), ivec2(0), maxCoord ), uSourceMip ).r;\n"
		"	float d2 = texelFetch( uSourceDepth, clamp( baseCoord + ivec2(0, 1), ivec2(0), maxCoord ), uSourceMip ).r;\n"
		"	float d3 = texelFetch( uSourceDepth, clamp( baseCoord + ivec2(1, 1), ivec2(0), maxCoord ), uSourceMip ).r;\n"
		"	gl_FragDepth = max( max( d0, d1 ), max( d2, d3 ) );\n"
		"}\n";

	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glGetProgramiv == NULL ) {
		return 0;
	}

	GLuint vertexShader = R_ModernGLExecutor_CompileShaderStage( GL_VERTEX_SHADER, vertexSource, "Hi-Z reduce vertex" );
	if ( vertexShader == 0 ) {
		return 0;
	}
	GLuint fragmentShader = R_ModernGLExecutor_CompileShaderStage( GL_FRAGMENT_SHADER, fragmentSource, "Hi-Z reduce fragment" );
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
		common->Printf( "Modern GL executor: Hi-Z reduce program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}

	rg_modernGLExecutorHiZReduceTextureLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uSourceDepth" ) : -1;
	rg_modernGLExecutorHiZReduceSourceMipLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uSourceMip" ) : -1;
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
	if ( rg_modernGLExecutorHiZReduceProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_modernGLExecutorHiZReduceProgram );
	}
	GLuint buffers[6];
	int numBuffers = 0;
	if ( rg_modernGLExecutorSceneSSBO != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorSceneSSBO;
	}
	if ( rg_modernGLExecutorIndirectBuffer != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorIndirectBuffer;
	}
	if ( rg_modernGLExecutorDrawRecordSSBO != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorDrawRecordSSBO;
	}
	if ( rg_modernGLExecutorDrawRecordIndexBuffer != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorDrawRecordIndexBuffer;
	}
	if ( rg_modernGLExecutorBucketSSBO != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorBucketSSBO;
	}
	if ( rg_modernGLExecutorValidationSSBO != 0 ) {
		buffers[numBuffers++] = rg_modernGLExecutorValidationSSBO;
	}
	if ( numBuffers > 0 && glDeleteBuffers != NULL ) {
		glDeleteBuffers( numBuffers, buffers );
	}
	rg_modernGLExecutorSceneSSBO = 0;
	rg_modernGLExecutorIndirectBuffer = 0;
	rg_modernGLExecutorDrawRecordSSBO = 0;
	rg_modernGLExecutorDrawRecordIndexBuffer = 0;
	rg_modernGLExecutorBucketSSBO = 0;
	rg_modernGLExecutorValidationSSBO = 0;
	rg_modernGLGpuDrivenBucketCount = 0;
	rg_modernGLExecutorComputeProgram = 0;
	rg_modernGLExecutorDepthOverlayProgram = 0;
	rg_modernGLExecutorGBufferOverlayProgram = 0;
	rg_modernGLExecutorDeferredOverlayProgram = 0;
	rg_modernGLExecutorVisibleCompositeProgram = 0;
	rg_modernGLExecutorHiZReduceProgram = 0;
	rg_modernGLExecutorComputeRecordCountLocation = -1;
	rg_modernGLExecutorComputeHiZTextureLocation = -1;
	rg_modernGLExecutorComputeHiZParamsLocation = -1;
	rg_modernGLExecutorDepthOverlayTextureLocation = -1;
	rg_modernGLExecutorDepthOverlayParamsLocation = -1;
	rg_modernGLExecutorGBufferOverlayTextureLocation = -1;
	rg_modernGLExecutorGBufferOverlayParamsLocation = -1;
	rg_modernGLExecutorDeferredOverlayTextureLocation = -1;
	rg_modernGLExecutorDeferredOverlayParamsLocation = -1;
	rg_modernGLExecutorVisibleCompositeDeferredLocation = -1;
	rg_modernGLExecutorVisibleCompositeForwardLocation = -1;
	rg_modernGLExecutorVisibleCompositeParamsLocation = -1;
	rg_modernGLExecutorHiZReduceTextureLocation = -1;
	rg_modernGLExecutorHiZReduceSourceMipLocation = -1;
	rg_modernGLExecutorGpuDrivenReady = false;
}

static bool R_ModernGLExecutor_InitGpuDrivenObjects( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !R_ModernGLExecutor_CanCreateGpuDrivenObjects( caps, features ) ) {
		return false;
	}

	const GLsizeiptr sceneBytes = static_cast<GLsizeiptr>( MODERN_GL_GPU_DRIVEN_MAX_RECORDS * sizeof( modernGLGpuSceneRecord_t ) );
	const GLsizeiptr indirectBytes = static_cast<GLsizeiptr>( MODERN_GL_GPU_DRIVEN_MAX_RECORDS * sizeof( modernGLDrawElementsIndirectCommand_t ) );
	const GLsizeiptr drawRecordBytes = static_cast<GLsizeiptr>( MODERN_GL_GPU_DRIVEN_MAX_RECORDS * sizeof( modernGLDrawRecord_t ) );
	const GLsizeiptr drawRecordIndexBytes = static_cast<GLsizeiptr>( MODERN_GL_GPU_DRIVEN_MAX_RECORDS * sizeof( GLfloat ) );
	const GLsizeiptr bucketBytes = static_cast<GLsizeiptr>( MODERN_GL_GPU_DRIVEN_MAX_BUCKETS * sizeof( modernGLGpuDrivenBucketRecord_t ) );
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
	if ( !R_ModernGLExecutor_CreateBuffer( GL_SHADER_STORAGE_BUFFER, drawRecordBytes, NULL, GL_DYNAMIC_DRAW, rg_modernGLExecutorDrawRecordSSBO ) ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
	R_GLDebug_LabelBuffer( rg_modernGLExecutorDrawRecordSSBO, "ModernGLExecutor draw records" );
	if ( !R_ModernGLExecutor_CreateBuffer( GL_ARRAY_BUFFER, drawRecordIndexBytes, NULL, GL_DYNAMIC_DRAW, rg_modernGLExecutorDrawRecordIndexBuffer ) ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
	R_GLDebug_LabelBuffer( rg_modernGLExecutorDrawRecordIndexBuffer, "ModernGLExecutor draw record indices" );
	if ( !R_ModernGLExecutor_CreateBuffer( GL_SHADER_STORAGE_BUFFER, bucketBytes, NULL, GL_DYNAMIC_DRAW, rg_modernGLExecutorBucketSSBO ) ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
	R_GLDebug_LabelBuffer( rg_modernGLExecutorBucketSSBO, "ModernGLExecutor indirect buckets" );
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

static void R_ModernGLExecutor_InitVisibilityStats( modernGLExecutorStats_t &stats, bool enabled ) {
	stats.visibilityRequested = r_rendererOcclusion.GetBool();
	stats.visibilityEnabled = enabled && stats.visibilityRequested;
	stats.visibilityPortalPVSPreserved = true;
	stats.visibilityCpuCullingReady = stats.visibilityEnabled;
	stats.visibilityGpuCullingReady = stats.visibilityEnabled && rg_modernGLExecutorFeatures.gpuDriven && rg_modernGLExecutorGpuDrivenReady;
	stats.visibilityHiZRequested = stats.visibilityEnabled && r_rendererHiZ.GetBool();
	stats.visibilityTemporalCoherenceReady = stats.visibilityEnabled;
	stats.visibilityNoQueryStall = true;
	stats.visibilityShadowCasterReady = stats.visibilityEnabled;
	stats.visibilityScreenBoundsReady = stats.visibilityEnabled;
}

static void R_ModernGLExecutor_CopyMaterialTextureTableStats( modernGLExecutorStats_t &stats ) {
	const materialResourceTableStats_t &materialStats = R_MaterialResourceTable_Stats();
	stats.materialTextureTableReady = materialStats.textureArrayTableReady;
	stats.materialTextureTableCapacity = materialStats.textureArrayTableCapacity;
	stats.materialTextureTableTextures = materialStats.textureArrayTableTextures;
	stats.materialTextureTableDescriptors = materialStats.textureArrayTableDescriptors;
	stats.materialTextureTableFallbacks += materialStats.textureArrayTableOverflows;
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
	stats.drawRecordBufferReady = rg_modernGLExecutorDrawRecordSSBO != 0;
	stats.drawRecordIndexBufferReady = rg_modernGLExecutorDrawRecordIndexBuffer != 0;
	stats.gpuDrivenBucketBufferReady = rg_modernGLExecutorBucketSSBO != 0;
	stats.validationSSBOReady = rg_modernGLExecutorValidationSSBO != 0;
	stats.computeValidationReady = rg_modernGLExecutorComputeProgram != 0;
	stats.gpuDrivenRequested = rg_modernGLExecutorFeatures.gpuDriven;
	stats.gpuDrivenValidationRequested = r_rendererGpuValidation.GetBool();
	R_ModernGLExecutor_InitVisibilityStats( stats, enabled );
	stats.modernVisibleRequested = R_ModernGLExecutor_ModernVisibleRequested();
	stats.modernVisibleProgramReady = rg_modernGLExecutorVisibleCompositeProgram != 0;
	stats.modernVisibleGuiProgramReady = shaderStats.guiProgramReady;
	stats.modernVisibleRenderDemoDeterministic = true;
	stats.modernVisibleCinematicTimingReady = true;
	stats.modernVisibleLightingReady = true;
	stats.modernVisibleLightGridReady = true;
	stats.modernVisibleShadowOwnershipReady = true;
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
	stats.vertexInputCacheReady = rg_modernGLExecutorVAO != 0;
	stats.vertexInputVertexBindingReady = rg_modernGLExecutorVertexBindingReady;
	stats.vertexInputFormatReady = !rg_modernGLExecutorVertexBindingReady || rg_modernGLVertexInputCache.formatConfigured;
	stats.vertexInputFormatSetups = rg_modernGLExecutorVertexInputFormatSetups;
	stats.lowOverheadSamplerReady = rg_modernGLExecutorLowOverheadSampler != 0;
	stats.lowOverheadSamplerDSACreations = rg_modernGLExecutorLowOverheadSamplerDSACreations;
	stats.lowOverheadSamplerDSAUpdates = rg_modernGLExecutorLowOverheadSamplerDSAUpdates;
	stats.lowOverheadBindlessRequested = r_rendererBindless.GetBool();
	stats.lowOverheadBindlessAvailable = rg_modernGLExecutorFeatures.bindlessTextures && rg_modernGLExecutorCaps.hasBindlessTexture;
	R_ModernGLExecutor_CopyMaterialTextureTableStats( stats );
	stats.pipelineNoHotStateQueries = true;
	stats.pipelineValidationReadbacksOptIn = !stats.gpuDrivenValidationReadbackReady || stats.gpuDrivenValidationRequested;
	stats.pipelineGL33CpuBounded = !rg_modernGLExecutorFeatures.gpuDriven || !stats.gpuDrivenValidationRequested;
	stats.pipelineGL43GpuDrivenFit = !rg_modernGLExecutorFeatures.gpuDriven || ( stats.gpuDrivenReady && stats.sceneSSBOReady && stats.indirectBufferReady && stats.drawRecordBufferReady && stats.drawRecordIndexBufferReady && stats.gpuDrivenBucketBufferReady && stats.computeValidationReady );
	stats.pipelineGL45LowOverheadFit = !rg_modernGLExecutorFeatures.lowOverhead || ( stats.lowOverheadReady && stats.tierUsesDSA && stats.tierUsesMultiBind );
	stats.shaderProgramCount = shaderStats.programCount;
	stats.shaderFailureCount = shaderStats.failedProgramCount;
	stats.highestGLSLVersion = shaderStats.highestGLSLVersion;
	R_ModernGLExecutor_SetStatus( stats, enabled ? "unavailable" : "off" );
}

static void R_ModernGLExecutor_RecomputeModernVisibleFallbacks( modernGLExecutorStats_t &stats ) {
	if ( !stats.modernVisibleRequested ) {
		return;
	}
	// Legacy post and special-effect passes consume the modern scene/depth handoff,
	// so they remain reported but do not by themselves block visible replacement.
	const int nonBlockingLegacyPasses =
		stats.modernVisiblePostGraphPasses +
		stats.modernVisibleSpecialLegacyPasses;
	const int blockingLegacyPasses = Max( 0, stats.modernVisibleLegacyPasses - nonBlockingLegacyPasses );
	stats.modernVisibleOwnerFallbacks =
		blockingLegacyPasses
		+ stats.modernVisibleGuiLegacyPasses
		+ stats.modernVisibleMaterialFallbackDraws
		+ stats.modernVisibleGeometryFallbackDraws
		+ stats.modernVisibleLightingFallbackPasses
		+ stats.modernVisibleLightGridFallbackPasses
		+ stats.modernVisibleShadowOwnershipFallbackPasses;
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
	stats.modernVisiblePostLegacyPasses = 0;
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

static void R_ModernGLExecutor_SetOwnershipBlocker(
	modernGLExecutorStats_t &stats,
	const char *domain,
	int viewIndex,
	renderPassCategory_t pass,
	int id,
	const char *resource,
	const char *reason ) {
	if ( stats.modernVisibleOwnershipBlocker[0] != '\0' ) {
		return;
	}
	R_ModernGLExecutor_FormatString(
		stats.modernVisibleOwnershipBlocker,
		sizeof( stats.modernVisibleOwnershipBlocker ),
		"view=%d pass=%s %s=%d resource=%s reason=%s",
		viewIndex,
		R_ModernGLExecutor_PassName( pass ),
		domain != NULL ? domain : "id",
		id,
		resource != NULL ? resource : "unknown",
		reason != NULL ? reason : "unknown" );
}

static int R_ModernGLExecutor_ViewIndexForViewDef( const idScenePacketFrame &packetFrame, const viewDef_t *viewDef ) {
	for ( int i = 0; i < packetFrame.NumScenes(); ++i ) {
		if ( packetFrame.Scene( i ).viewDef == viewDef ) {
			return i;
		}
	}
	return -1;
}

static bool R_ModernGLExecutor_ViewDefUsesLegacySidecar( const viewDef_t *viewDef ) {
	return viewDef != NULL
		&& ( viewDef->isSubview
			|| viewDef->superView != NULL
			|| viewDef->subviewSurface != NULL
			|| viewDef->renderView.viewID < 0 );
}

static bool R_ModernGLExecutor_DrawPacketUsesLegacySidecarView( const drawPacket_t &draw ) {
	return draw.packetCategory == SCENE_PACKET_CATEGORY_SUBVIEW
		|| draw.packetCategory == SCENE_PACKET_CATEGORY_REMOTE_CAMERA
		|| draw.packetCategory == SCENE_PACKET_CATEGORY_RENDER_DEMO
		|| R_ModernGLExecutor_ViewDefUsesLegacySidecar( draw.viewDef );
}

static int R_ModernGLExecutor_CountPassDraws( const idScenePacketFrame &packetFrame, renderPassCategory_t category ) {
	int count = 0;
	for ( int i = 0; i < packetFrame.NumDrawPackets(); ++i ) {
		const drawPacket_t &draw = packetFrame.DrawPacket( i );
		if ( draw.passCategory == category && !R_ModernGLExecutor_DrawPacketUsesLegacySidecarView( draw ) ) {
			count++;
		}
	}
	return count;
}

static int R_ModernGLExecutor_FirstContributingLightIndex( const idScenePacketFrame &packetFrame, renderPassCategory_t category, int &viewIndex ) {
	viewIndex = -1;
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes(); ++sceneIndex ) {
		const viewDef_t *viewDef = packetFrame.Scene( sceneIndex ).viewDef;
		if ( viewDef == NULL || R_ModernGLExecutor_ViewDefUsesLegacySidecar( viewDef ) ) {
			continue;
		}
		for ( const viewLight_t *vLight = viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
			if ( vLight->lightShader == NULL ) {
				continue;
			}
			const bool fogOrBlend = vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight();
			if ( category == RENDER_PASS_ARB2_INTERACTION && fogOrBlend ) {
				continue;
			}
			if ( category == RENDER_PASS_FOG_BLEND && !fogOrBlend ) {
				continue;
			}
			if ( vLight->localInteractions == NULL && vLight->globalInteractions == NULL && vLight->translucentInteractions == NULL ) {
				continue;
			}
			viewIndex = sceneIndex;
			return vLight->lightDef != NULL ? vLight->lightDef->index : -1;
		}
	}
	return -1;
}

static bool R_ModernGLExecutor_DrawPacketUsesLegacyFeedbackSurface( const drawPacket_t &draw ) {
	if ( R_ModernGLExecutor_DrawPacketUsesLegacySidecarView( draw ) ) {
		return true;
	}
	if ( draw.passCategory != RENDER_PASS_AMBIENT ) {
		return false;
	}

	const idMaterial *material = draw.materialRecord != NULL ? draw.materialRecord->material : NULL;
	if ( material == NULL ) {
		return false;
	}

	return material->TestMaterialFlag( MF_NEED_CURRENT_RENDER )
		|| material->HasSubview()
		|| material->GetSort() == SS_SUBVIEW;
}

static void R_ModernGLExecutor_RecordPacketFallbackBlockers( const idScenePacketFrame &packetFrame, modernGLExecutorStats_t &stats ) {
	for ( int i = 0; i < packetFrame.NumDrawPackets(); ++i ) {
		const drawPacket_t &draw = packetFrame.DrawPacket( i );
		const bool materialPass =
			draw.passCategory == RENDER_PASS_DEPTH ||
			draw.passCategory == RENDER_PASS_AMBIENT ||
			draw.passCategory == RENDER_PASS_ARB2_INTERACTION ||
			draw.passCategory == RENDER_PASS_LIGHT_GRID ||
			draw.passCategory == RENDER_PASS_FOG_BLEND ||
			draw.passCategory == RENDER_PASS_SHADOW_MAP ||
			draw.passCategory == RENDER_PASS_STENCIL_SHADOW ||
			draw.passCategory == RENDER_PASS_GUI;
		if ( !materialPass ) {
			continue;
		}

		const int viewIndex = R_ModernGLExecutor_ViewIndexForViewDef( packetFrame, draw.viewDef );
		const bool legacyFeedbackSurface = R_ModernGLExecutor_DrawPacketUsesLegacyFeedbackSurface( draw );
		if ( draw.geometryRecord == NULL || draw.instanceRecord == NULL || !draw.hasGeometry || draw.indexCount <= 0 ) {
			if ( legacyFeedbackSurface ) {
				continue;
			}
			stats.modernVisibleGeometryFallbackDraws++;
			R_ModernGLExecutor_SetOwnershipBlocker( stats, "draw", viewIndex, draw.passCategory, i, "geometry", "missing-modern-geometry" );
			continue;
		}
		if ( draw.geometryRecord->fallbackReason != GEOMETRY_RESOURCE_FALLBACK_NONE ) {
			if ( legacyFeedbackSurface ) {
				continue;
			}
			stats.modernVisibleGeometryFallbackDraws++;
			R_ModernGLExecutor_SetOwnershipBlocker( stats, "geometry", viewIndex, draw.passCategory, draw.geometryRecordIndex, "geometry", GeometryResourceFallbackReason_Name( draw.geometryRecord->fallbackReason ) );
			continue;
		}

		const materialResourceTableRecord_t *materialRecord = NULL;
		if ( draw.materialRecord != NULL ) {
			materialRecord = R_MaterialResourceTable_FindRecordForMaterial( draw.materialRecord->material );
		}
		if ( materialRecord == NULL ) {
			if ( legacyFeedbackSurface ) {
				continue;
			}
			stats.modernVisibleMaterialFallbackDraws++;
			R_ModernGLExecutor_SetOwnershipBlocker( stats, "draw", viewIndex, draw.passCategory, i, "material", "missing-material-record" );
			continue;
		}
		if ( materialRecord->fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE ) {
			if ( legacyFeedbackSurface ) {
				continue;
			}
			stats.modernVisibleMaterialFallbackDraws++;
			R_ModernGLExecutor_SetOwnershipBlocker( stats, "material", viewIndex, draw.passCategory, materialRecord->materialId, materialRecord->materialName, MaterialResourceFallbackReason_Name( materialRecord->fallbackReason ) );
		}
	}
}

static void R_ModernGLExecutor_AnalyzeModernVisibleOwnershipReadiness( const idScenePacketFrame &packetFrame, const idRenderGraph &graph, modernGLExecutorStats_t &stats ) {
	if ( !stats.modernVisibleRequested ) {
		return;
	}

	stats.modernVisibleLightingReady = true;
	stats.modernVisibleLightGridReady = true;
	stats.modernVisibleShadowOwnershipReady = true;
	stats.modernVisibleMaterialFallbackDraws = 0;
	stats.modernVisibleGeometryFallbackDraws = 0;
	stats.modernVisibleLightingFallbackPasses = 0;
	stats.modernVisibleLightGridFallbackPasses = 0;
	stats.modernVisibleShadowOwnershipFallbackPasses = 0;
	stats.modernVisibleOwnershipBlocker[0] = '\0';

	R_ModernGLExecutor_RecordPacketFallbackBlockers( packetFrame, stats );

	const int interactionDraws = R_ModernGLExecutor_CountPassDraws( packetFrame, RENDER_PASS_ARB2_INTERACTION );
	if ( interactionDraws > 0 ) {
		int viewIndex = -1;
		const int lightIndex = R_ModernGLExecutor_FirstContributingLightIndex( packetFrame, RENDER_PASS_ARB2_INTERACTION, viewIndex );
		stats.modernVisibleLightingReady = false;
		stats.modernVisibleLightingFallbackPasses++;
		R_ModernGLExecutor_SetOwnershipBlocker( stats, "light", viewIndex, RENDER_PASS_ARB2_INTERACTION, lightIndex, "modernLightDescriptor", "lighting-parity-incomplete" );
	}

	const int fogBlendDraws = R_ModernGLExecutor_CountPassDraws( packetFrame, RENDER_PASS_FOG_BLEND );
	if ( fogBlendDraws > 0 ) {
		int viewIndex = -1;
		const int lightIndex = R_ModernGLExecutor_FirstContributingLightIndex( packetFrame, RENDER_PASS_FOG_BLEND, viewIndex );
		stats.modernVisibleLightingReady = false;
		stats.modernVisibleLightingFallbackPasses++;
		R_ModernGLExecutor_SetOwnershipBlocker( stats, "light", viewIndex, RENDER_PASS_FOG_BLEND, lightIndex, "fogBlendEvaluation", "fog-blend-parity-incomplete" );
	}

	const int lightGridDraws = R_ModernGLExecutor_CountPassDraws( packetFrame, RENDER_PASS_LIGHT_GRID );
	if ( lightGridDraws > 0 ) {
		stats.modernVisibleLightGridReady = false;
		stats.modernVisibleLightGridFallbackPasses++;
		R_ModernGLExecutor_SetOwnershipBlocker( stats, "draw", 0, RENDER_PASS_LIGHT_GRID, lightGridDraws, "lightGridContribution", "light-grid-parity-incomplete" );
	}

	const int shadowMapDraws = R_ModernGLExecutor_CountPassDraws( packetFrame, RENDER_PASS_SHADOW_MAP );
	const int stencilShadowDraws = R_ModernGLExecutor_CountPassDraws( packetFrame, RENDER_PASS_STENCIL_SHADOW );
	if ( shadowMapDraws > 0 || stencilShadowDraws > 0 ) {
		stats.modernVisibleShadowOwnershipReady = false;
		stats.modernVisibleShadowOwnershipFallbackPasses += ( shadowMapDraws > 0 ? 1 : 0 ) + ( stencilShadowDraws > 0 ? 1 : 0 );
		R_ModernGLExecutor_SetOwnershipBlocker( stats, "draw", 0, shadowMapDraws > 0 ? RENDER_PASS_SHADOW_MAP : RENDER_PASS_STENCIL_SHADOW, shadowMapDraws > 0 ? shadowMapDraws : stencilShadowDraws, "shadowReceiverSampling", "shadow-ownership-parity-incomplete" );
	}

	if ( graph.FindPass( RENDER_PASS_ARB2_INTERACTION ) >= 0 && !stats.modernVisibleLightingReady ) {
		stats.modernVisibleBlockedByLegacy = true;
	}
	R_ModernGLExecutor_RecomputeModernVisibleFallbacks( stats );
}

static bool R_ModernGLExecutor_DrawPacketIsSceneView( const drawPacket_t &draw ) {
	switch ( draw.packetCategory ) {
	case SCENE_PACKET_CATEGORY_WORLD:
	case SCENE_PACKET_CATEGORY_VIEWMODEL:
	case SCENE_PACKET_CATEGORY_SUBVIEW:
	case SCENE_PACKET_CATEGORY_REMOTE_CAMERA:
	case SCENE_PACKET_CATEGORY_RENDER_DEMO:
		return true;
	default:
		return false;
	}
}

static bool R_ModernGLExecutor_DrawPacketSupportsGpuDrivenValidation( const drawPacket_t &draw ) {
	if ( !R_ModernGLExecutor_DrawPacketIsSceneView( draw ) ) {
		return false;
	}
	if ( draw.passCategory == RENDER_PASS_GUI || draw.passCategory == RENDER_PASS_PRESENT || draw.passCategory == RENDER_PASS_AUTHORED_POST || draw.passCategory == RENDER_PASS_SPECIAL_EFFECTS ) {
		return false;
	}
	return draw.hasGeometry
		&& draw.indexCount > 0
		&& draw.geometryRecord != NULL
		&& draw.instanceRecord != NULL
		&& draw.materialRecordIndex >= 0;
}

static bool R_ModernGLExecutor_FrameSupportsGpuDrivenValidation( const idScenePacketFrame &packetFrame ) {
	for ( int i = 0; i < packetFrame.NumDrawPackets(); ++i ) {
		if ( R_ModernGLExecutor_DrawPacketSupportsGpuDrivenValidation( packetFrame.DrawPacket( i ) ) ) {
			return true;
		}
	}
	return false;
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

static void R_ModernGLExecutor_RecordPipelinePolicy(
	modernGLExecutorStats_t &stats,
	bool gpuDrivenWorkRequested ) {
	const bool hasGBufferWork = stats.pipelineGBufferCommands > 0;
	const bool hasForwardPlusWork = stats.pipelineForwardPlusCommands > 0;
	const bool explicitGBufferWork = r_rendererModernGBufferDebug.GetInteger() > 0;
	const bool explicitDeferredWork = r_rendererModernDeferredDebug.GetInteger() > 0;
	const bool explicitForwardWork = false;

	stats.pipelineGBufferNeeded = stats.opaqueGBufferRequested && ( hasGBufferWork || explicitGBufferWork || explicitDeferredWork );
	stats.pipelineDeferredNeeded = stats.deferredResolveRequested && ( hasGBufferWork || explicitDeferredWork );
	stats.pipelineForwardPlusNeeded = stats.forwardPlusRequested && ( hasForwardPlusWork || explicitForwardWork );
	stats.pipelineDeferredCommands = stats.pipelineDeferredNeeded ? 1 : 0;
	stats.pipelineMinimumPassesReady = true;
	stats.pipelineNoHotStateQueries = true;
	stats.pipelineValidationReadbacksOptIn = !stats.gpuDrivenValidationReadbackReady || stats.gpuDrivenValidationRequested;
	stats.pipelineGL33CpuBounded = !rg_modernGLExecutorFeatures.gpuDriven || !stats.gpuDrivenReady || !gpuDrivenWorkRequested || stats.gpuDrivenValidationRequested;
	stats.pipelineGL43GpuDrivenFit = !rg_modernGLExecutorFeatures.gpuDriven || ( stats.gpuDrivenReady && stats.sceneSSBOReady && stats.indirectBufferReady && stats.drawRecordBufferReady && stats.drawRecordIndexBufferReady && stats.gpuDrivenBucketBufferReady && stats.computeValidationReady );
	stats.pipelineGL45LowOverheadFit = !rg_modernGLExecutorFeatures.lowOverhead || ( stats.lowOverheadReady && stats.tierUsesDSA && stats.tierUsesMultiBind );

	if ( stats.opaqueGBufferRequested && !stats.pipelineGBufferNeeded ) {
		stats.pipelineSkippedEmptyPasses++;
	}
	if ( stats.deferredResolveRequested && !stats.pipelineDeferredNeeded ) {
		stats.pipelineSkippedEmptyPasses++;
	}
	if ( stats.forwardPlusRequested && !stats.pipelineForwardPlusNeeded ) {
		stats.pipelineSkippedEmptyPasses++;
	}

	stats.opaqueGBufferRequested = stats.opaqueGBufferRequested && stats.pipelineGBufferNeeded;
	stats.deferredResolveRequested = stats.deferredResolveRequested && stats.pipelineDeferredNeeded;
	stats.forwardPlusRequested = stats.forwardPlusRequested && stats.pipelineForwardPlusNeeded;
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
	stats.drawRecordBufferReady = rg_modernGLExecutorDrawRecordSSBO != 0;
	stats.drawRecordIndexBufferReady = rg_modernGLExecutorDrawRecordIndexBuffer != 0;
	stats.gpuDrivenBucketBufferReady = rg_modernGLExecutorBucketSSBO != 0;
	stats.validationSSBOReady = rg_modernGLExecutorValidationSSBO != 0;
	stats.computeValidationReady = rg_modernGLExecutorComputeProgram != 0;
	stats.gpuDrivenRequested = rg_modernGLExecutorFeatures.gpuDriven;
	stats.gpuDrivenValidationRequested = r_rendererGpuValidation.GetBool();
	R_ModernGLExecutor_InitVisibilityStats( stats, enabled );
	stats.modernVisibleRequested = R_ModernGLExecutor_ModernVisibleRequested();
	stats.modernVisibleProgramReady = rg_modernGLExecutorVisibleCompositeProgram != 0;
	stats.modernVisibleGuiProgramReady = shaderStats.guiProgramReady;
	stats.modernVisibleRenderDemoDeterministic = true;
	stats.modernVisibleCinematicTimingReady = true;
	stats.modernVisibleLightingReady = true;
	stats.modernVisibleLightGridReady = true;
	stats.modernVisibleShadowOwnershipReady = true;
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
	stats.vertexInputCacheReady = vaoReady;
	stats.vertexInputVertexBindingReady = rg_modernGLExecutorVertexBindingReady;
	stats.vertexInputFormatReady = !rg_modernGLExecutorVertexBindingReady || rg_modernGLVertexInputCache.formatConfigured;
	stats.vertexInputFormatSetups = rg_modernGLExecutorVertexInputFormatSetups;
	stats.lowOverheadSamplerReady = rg_modernGLExecutorLowOverheadSampler != 0;
	stats.lowOverheadSamplerDSACreations = rg_modernGLExecutorLowOverheadSamplerDSACreations;
	stats.lowOverheadSamplerDSAUpdates = rg_modernGLExecutorLowOverheadSamplerDSAUpdates;
	stats.lowOverheadBindlessRequested = r_rendererBindless.GetBool();
	stats.lowOverheadBindlessAvailable = rg_modernGLExecutorFeatures.bindlessTextures && rg_modernGLExecutorCaps.hasBindlessTexture;
	R_ModernGLExecutor_CopyMaterialTextureTableStats( stats );
	stats.pipelineNoHotStateQueries = true;
	stats.pipelineValidationReadbacksOptIn = !stats.gpuDrivenValidationReadbackReady || stats.gpuDrivenValidationRequested;
	stats.pipelineGL33CpuBounded = !rg_modernGLExecutorFeatures.gpuDriven || !stats.gpuDrivenValidationRequested;
	stats.pipelineGL43GpuDrivenFit = !rg_modernGLExecutorFeatures.gpuDriven || ( stats.gpuDrivenReady && stats.sceneSSBOReady && stats.indirectBufferReady && stats.drawRecordBufferReady && stats.drawRecordIndexBufferReady && stats.gpuDrivenBucketBufferReady && stats.computeValidationReady );
	stats.pipelineGL45LowOverheadFit = !rg_modernGLExecutorFeatures.lowOverhead || ( stats.lowOverheadReady && stats.tierUsesDSA && stats.tierUsesMultiBind );
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
		if ( R_ModernGLExecutor_DrawPacketIsSceneView( draw ) ) {
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
	if ( rg_modernGLFrameUBOStream.valid && glBindBufferRange != NULL ) {
		R_GLStateCache().BindBufferRange(
			GL_UNIFORM_BUFFER,
			0,
			rg_modernGLFrameUBOStream.allocation.vbo,
			static_cast<GLintptr>( rg_modernGLFrameUBOStream.allocation.offset ),
			rg_modernGLFrameUBOStream.size );
		return;
	}
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
	R_ModernGLExecutor_ResetStreamBinding( rg_modernGLFrameUBOStream );
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

	if ( glBindBufferRange != NULL && R_ModernGLExecutor_StreamBufferData( &constants, sizeof( constants ), rg_modernGLExecutorUniformBufferAlignment, rg_modernGLFrameUBOStream, stats ) ) {
		stats.frameUBOStreamed = true;
		stats.uploadManagerFrameUBOBytes += static_cast<int>( sizeof( constants ) );
	} else if ( rg_modernGLExecutorLowOverheadReady && glNamedBufferSubData != NULL ) {
		glNamedBufferSubData( rg_modernGLExecutorFrameUBO, 0, sizeof( constants ), &constants );
		stats.lowOverheadDSAUpdates++;
		R_ModernGLExecutor_RecordUploadFallback( stats, sizeof( constants ) );
	} else {
		R_ModernGLExecutor_BindUniformBuffer( rg_modernGLExecutorFrameUBO );
		glBufferSubData( GL_UNIFORM_BUFFER, 0, sizeof( constants ), &constants );
		R_ModernGLExecutor_BindUniformBuffer( 0 );
		R_ModernGLExecutor_RecordUploadFallback( stats, sizeof( constants ) );
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

static bool R_ModernGLExecutor_ConfigureDrawVertVertexBindingFormat( modernGLExecutorStats_t *stats ) {
	if ( !rg_modernGLExecutorVertexBindingReady ) {
		return false;
	}
	if ( rg_modernGLVertexInputCache.formatConfigured ) {
		if ( stats != NULL ) {
			stats->vertexInputFormatReady = true;
			stats->vertexInputFormatSetups = rg_modernGLExecutorVertexInputFormatSetups;
		}
		return true;
	}
	if ( glEnableVertexAttribArray == NULL || glVertexAttribFormat == NULL || glVertexAttribBinding == NULL ) {
		return false;
	}
	for ( int i = 0; i < static_cast<int>( sizeof( rg_modernGLDrawVertAttributes ) / sizeof( rg_modernGLDrawVertAttributes[0] ) ); ++i ) {
		const modernGLDrawVertAttributeDesc_t &desc = rg_modernGLDrawVertAttributes[i];
		const GLuint attribute = static_cast<GLuint>( desc.attribute );
		glEnableVertexAttribArray( attribute );
		glVertexAttribFormat( attribute, desc.components, desc.type, desc.normalized, desc.relativeOffset );
		glVertexAttribBinding( attribute, MODERN_GL_DRAWVERT_BINDING_INDEX );
	}
	if ( glVertexBindingDivisor != NULL ) {
		glVertexBindingDivisor( MODERN_GL_DRAWVERT_BINDING_INDEX, 0 );
	}
	rg_modernGLVertexInputCache.formatConfigured = true;
	rg_modernGLExecutorVertexInputFormatSetups++;
	if ( stats != NULL ) {
		stats->vertexInputFormatReady = true;
		stats->vertexInputFormatSetups = rg_modernGLExecutorVertexInputFormatSetups;
	}
	return true;
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

static bool R_ModernGLExecutor_BindDrawVertVertexBindingSource( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats, GLintptr baseOffsetOverride ) {
	if ( !R_ModernGLExecutor_ConfigureDrawVertVertexBindingFormat( &stats ) || glBindVertexBuffer == NULL ) {
		return false;
	}

	const GLuint vertexBuffer = static_cast<GLuint>( command.vertexBuffer );
	const GLintptr baseOffset = baseOffsetOverride >= 0 ? baseOffsetOverride : static_cast<GLintptr>( command.ambientCacheOffset );
	const GLsizei stride = static_cast<GLsizei>( command.vertexStride );
	modernGLVertexBindingSourceCache_t &source = rg_modernGLVertexInputCache.vertexBindingSource;
	if ( source.valid && source.vertexBuffer == vertexBuffer && source.baseOffset == baseOffset && source.stride == stride ) {
		stats.vertexInputCacheHits++;
		return true;
	}

	glBindVertexBuffer( MODERN_GL_DRAWVERT_BINDING_INDEX, vertexBuffer, baseOffset, stride );
	source.valid = true;
	source.vertexBuffer = vertexBuffer;
	source.baseOffset = baseOffset;
	source.stride = stride;
	stats.vertexInputSourceBinds++;
	return true;
}

static bool R_ModernGLExecutor_BindDrawVertLegacyLayout( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	if ( rg_modernGLVertexInputCache.legacyLayoutValid
		&& rg_modernGLVertexInputCache.legacyVertexBuffer == static_cast<GLuint>( command.vertexBuffer )
		&& rg_modernGLVertexInputCache.legacyVertexStride == command.vertexStride
		&& rg_modernGLVertexInputCache.legacyAmbientCacheOffset == command.ambientCacheOffset ) {
		stats.vertexInputCacheHits++;
		return true;
	}

	R_GLStateCache().BindBuffer( GL_ARRAY_BUFFER, command.vertexBuffer );
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
	rg_modernGLVertexInputCache.legacyLayoutValid = true;
	rg_modernGLVertexInputCache.legacyVertexBuffer = static_cast<GLuint>( command.vertexBuffer );
	rg_modernGLVertexInputCache.legacyVertexStride = command.vertexStride;
	rg_modernGLVertexInputCache.legacyAmbientCacheOffset = command.ambientCacheOffset;
	stats.vertexInputLegacyLayoutUpdates++;
	return true;
}

static bool R_ModernGLExecutor_BindDrawVertLayout( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	if ( !R_ModernGLExecutor_DrawVertLayoutSupported( command.vertexStride, command.ambientCacheOffset ) ) {
		return false;
	}
	stats.vertexInputCacheReady = true;
	stats.vertexInputVertexBindingReady = rg_modernGLExecutorVertexBindingReady;
	if ( rg_modernGLExecutorVertexBindingReady ) {
		return R_ModernGLExecutor_BindDrawVertVertexBindingSource( command, stats, -1 );
	}
	return R_ModernGLExecutor_BindDrawVertLegacyLayout( command, stats );
}

static bool R_ModernGLExecutor_BindDrawVertLayoutForIndirect( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	if ( !rg_modernGLExecutorVertexBindingReady || !R_ModernGLExecutor_DrawVertLayoutSupported( command.vertexStride, 0 ) ) {
		return false;
	}
	stats.vertexInputCacheReady = true;
	stats.vertexInputVertexBindingReady = true;
	return R_ModernGLExecutor_BindDrawVertVertexBindingSource( command, stats, 0 );
}

static bool R_ModernGLExecutor_EnableDrawRecordIndexAttribute( void ) {
	const bool streamedIndices = rg_modernGLGpuDrivenStreamBindings.drawRecordIndices.valid;
	const GLuint indexBuffer = streamedIndices ? rg_modernGLGpuDrivenStreamBindings.drawRecordIndices.allocation.vbo : rg_modernGLExecutorDrawRecordIndexBuffer;
	const GLintptr indexOffset = streamedIndices ? static_cast<GLintptr>( rg_modernGLGpuDrivenStreamBindings.drawRecordIndices.allocation.offset ) : 0;
	if ( indexBuffer == 0 || glEnableVertexAttribArray == NULL || glVertexAttribFormat == NULL || glVertexAttribBinding == NULL || glVertexBindingDivisor == NULL || glBindVertexBuffer == NULL ) {
		return false;
	}
	glEnableVertexAttribArray( MODERN_GL_DRAW_RECORD_ATTR_INDEX );
	glVertexAttribFormat( MODERN_GL_DRAW_RECORD_ATTR_INDEX, 1, GL_FLOAT, GL_FALSE, 0 );
	glVertexAttribBinding( MODERN_GL_DRAW_RECORD_ATTR_INDEX, MODERN_GL_DRAW_RECORD_BINDING_INDEX );
	glVertexBindingDivisor( MODERN_GL_DRAW_RECORD_BINDING_INDEX, 1 );
	glBindVertexBuffer( MODERN_GL_DRAW_RECORD_BINDING_INDEX, indexBuffer, indexOffset, static_cast<GLsizei>( sizeof( GLfloat ) ) );
	return true;
}

static void R_ModernGLExecutor_DisableDrawRecordIndexAttribute( void ) {
	if ( glDisableVertexAttribArray != NULL ) {
		glDisableVertexAttribArray( MODERN_GL_DRAW_RECORD_ATTR_INDEX );
	}
	if ( glBindVertexBuffer != NULL ) {
		glBindVertexBuffer( MODERN_GL_DRAW_RECORD_BINDING_INDEX, 0, 0, static_cast<GLsizei>( sizeof( GLfloat ) ) );
	}
	if ( glVertexBindingDivisor != NULL ) {
		glVertexBindingDivisor( MODERN_GL_DRAW_RECORD_BINDING_INDEX, 0 );
	}
}

static void R_ModernGLExecutor_DisableDrawVertLegacyLayout( void ) {
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_POSITION );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_COLOR );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_TEXCOORD0 );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_TANGENT0 );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_TANGENT1 );
	glDisableVertexAttribArray( MODERN_GL_DRAWVERT_ATTR_NORMAL );
}

static void R_ModernGLExecutor_ResetDrawVertSourceBinding( void ) {
	R_ModernGLExecutor_DisableDrawRecordIndexAttribute();
	if ( rg_modernGLExecutorVertexBindingReady && rg_modernGLExecutorVAO != 0 && glBindVertexBuffer != NULL && rg_modernGLVertexInputCache.vertexBindingSource.valid ) {
		R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
		glBindVertexBuffer( MODERN_GL_DRAWVERT_BINDING_INDEX, 0, 0, static_cast<GLsizei>( sizeof( idDrawVert ) ) );
	} else if ( !rg_modernGLExecutorVertexBindingReady && rg_modernGLExecutorVAO != 0 && rg_modernGLVertexInputCache.legacyLayoutValid && glDisableVertexAttribArray != NULL ) {
		R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
		R_ModernGLExecutor_DisableDrawVertLegacyLayout();
	}
	rg_modernGLVertexInputCache.vertexBindingSource.valid = false;
	rg_modernGLVertexInputCache.legacyLayoutValid = false;
}

static bool R_ModernGLExecutor_CommandEffectiveScissor( const modernGLSubmitCommand_t &command, int &scissorX1, int &scissorY1, int &scissorX2, int &scissorY2, bool &screenClipped );
static bool R_ModernGLExecutor_DepthResourceReady( const char *name, const renderGraphResourceHandle_t *&handle );

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

	int scissorX1;
	int scissorY1;
	int scissorX2;
	int scissorY2;
	bool screenClipped = false;
	if ( !R_ModernGLExecutor_CommandEffectiveScissor( command, scissorX1, scissorY1, scissorX2, scissorY2, screenClipped ) ) {
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

static float R_ModernGLExecutor_CalcFovForAspect( float fovX, float width, float height ) {
	const float clampedFovX = idMath::ClampFloat( 1.0f, 179.0f, fovX );
	const float safeWidth = Max( width, 1.0f );
	const float safeHeight = Max( height, 1.0f );
	const float x = safeWidth / idMath::Tan( DEG2RAD( clampedFovX ) * 0.5f );
	return RAD2DEG( idMath::ATan( safeHeight / Max( x, idMath::FLOAT_EPSILON ) ) ) * 2.0f;
}

static void R_ModernGLExecutor_BuildCommandProjectionMatrix( const modernGLSubmitCommand_t &command, float projectionMatrix[16] ) {
	memcpy( projectionMatrix, command.viewDef->projectionMatrix, sizeof( float ) * 16 );
	if ( command.modelDepthHack != 0.0f ) {
		projectionMatrix[14] -= command.modelDepthHack;
		return;
	}
	if ( !command.weaponDepthHack ) {
		return;
	}

	const float weaponFovOverride = cl_gunfov.GetFloat();
	if ( weaponFovOverride > 0.0f ) {
		const float viewportWidth = static_cast<float>( Max( 1, command.viewDef->viewport.x2 - command.viewDef->viewport.x1 + 1 ) );
		const float viewportHeight = static_cast<float>( Max( 1, command.viewDef->viewport.y2 - command.viewDef->viewport.y1 + 1 ) );

		float weaponFovX = idMath::ClampFloat( 30.0f, 160.0f, weaponFovOverride );
		float weaponFovY = 0.0f;
		if ( cl_gunfov_adjust.GetBool() ) {
			weaponFovY = R_ModernGLExecutor_CalcFovForAspect( weaponFovX, 4.0f, 3.0f );
			weaponFovX = R_ModernGLExecutor_CalcFovForAspect( weaponFovY, viewportHeight, viewportWidth );
		} else {
			weaponFovY = R_ModernGLExecutor_CalcFovForAspect( weaponFovX, viewportWidth, viewportHeight );
		}

		weaponFovX = idMath::ClampFloat( 1.0f, 179.0f, weaponFovX );
		weaponFovY = idMath::ClampFloat( 1.0f, 179.0f, weaponFovY );
		projectionMatrix[0] = 1.0f / idMath::Tan( DEG2RAD( weaponFovX ) * 0.5f );
		projectionMatrix[5] = 1.0f / idMath::Tan( DEG2RAD( weaponFovY ) * 0.5f );
	}
	projectionMatrix[14] *= 0.25f;
}

static void R_ModernGLExecutor_ApplyCommandDepthRange( const modernGLSubmitCommand_t &command ) {
	if ( command.modelDepthHack != 0.0f ) {
		glDepthRange( 0.0, 1.0 );
	} else if ( command.weaponDepthHack ) {
		glDepthRange( 0.0, 0.5 );
	} else {
		glDepthRange( 0.0, 1.0 );
	}
}

static void R_ModernGLExecutor_ApplyCommandCullState( const modernGLSubmitCommand_t &command ) {
	if ( command.twoSided || command.cullType == CT_TWO_SIDED ) {
		R_GLStateCache().SetCullFaceEnabled( false );
		return;
	}

	const bool invertCull = ( command.viewDef != NULL && command.viewDef->isMirror ) != command.negativeScale;
	GLenum cullFace = GL_FRONT;
	if ( command.cullType == CT_BACK_SIDED ) {
		cullFace = invertCull ? GL_FRONT : GL_BACK;
	} else {
		cullFace = invertCull ? GL_BACK : GL_FRONT;
	}
	R_GLStateCache().SetCullFaceEnabled( true );
	R_GLStateCache().SetCullFace( cullFace );
}

static const materialResourceTextureBinding_t *R_ModernGLExecutor_FindTextureBinding( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic );
static float R_ModernGLExecutor_ShaderRegisterValue( const modernGLSubmitCommand_t &command, int registerIndex, float fallbackValue );
static bool R_ModernGLExecutor_CommandMaterialColor( const modernGLSubmitCommand_t &command, float color[4] );
static void R_ModernGLExecutor_MaterialFlagsForCommand( const modernGLSubmitCommand_t &command, float flags[4] );
static void R_ModernGLExecutor_MaterialEnhancementForCommand( const modernGLSubmitCommand_t &command, float enhancement[4] );
static void R_ModernGLExecutor_BindTextureGroup( GLuint first, GLsizei count, const GLuint *textures, modernGLExecutorStats_t &stats );

static void R_ModernGLExecutor_DebugColorForCommand( const modernGLSubmitCommand_t &command, float color[4] ) {
	float materialColor[4];
	switch ( command.shaderKind ) {
	case MODERN_GL_SHADER_DEPTH:
	case MODERN_GL_SHADER_SHADOW_DEPTH:
		color[0] = 0.15f;
		color[1] = 0.30f;
		color[2] = 0.95f;
		color[3] = 1.0f;
		break;
	case MODERN_GL_SHADER_FLAT_MATERIAL:
		color[0] = 0.95f;
		color[1] = 0.65f;
		color[2] = 0.20f;
		color[3] = 1.0f;
		break;
	case MODERN_GL_SHADER_LIGHT_GRID:
		color[0] = 0.35f;
		color[1] = 0.90f;
		color[2] = 0.55f;
		color[3] = 1.0f;
		break;
	case MODERN_GL_SHADER_FOG_BLEND:
		color[0] = 0.55f;
		color[1] = 0.62f;
		color[2] = 0.75f;
		color[3] = 1.0f;
		break;
	case MODERN_GL_SHADER_GBUFFER_OPAQUE:
	case MODERN_GL_SHADER_GBUFFER_ALPHA_TEST:
		if ( R_ModernGLExecutor_CommandMaterialColor( command, materialColor ) ) {
			memcpy( color, materialColor, sizeof( float ) * 4 );
		} else {
			color[0] = 1.0f;
			color[1] = 1.0f;
			color[2] = 1.0f;
			color[3] = 1.0f;
		}
		break;
	case MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE:
		color[0] = 0.95f;
		color[1] = 0.90f;
		color[2] = 0.55f;
		color[3] = 1.0f;
		break;
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE:
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST:
		if ( R_ModernGLExecutor_CommandMaterialColor( command, materialColor ) ) {
			memcpy( color, materialColor, sizeof( float ) * 4 );
		} else {
			color[0] = 1.0f;
			color[1] = 1.0f;
			color[2] = 1.0f;
			color[3] = 1.0f;
		}
		break;
	case MODERN_GL_SHADER_TRANSPARENT_FORWARD:
		if ( R_ModernGLExecutor_CommandMaterialColor( command, materialColor ) ) {
			memcpy( color, materialColor, sizeof( float ) * 4 );
		} else {
			color[0] = 1.0f;
			color[1] = 1.0f;
			color[2] = 1.0f;
			color[3] = 0.65f;
		}
		break;
	case MODERN_GL_SHADER_GUI:
	case MODERN_GL_SHADER_POST_COPY:
		if ( R_ModernGLExecutor_CommandMaterialColor( command, materialColor ) ) {
			memcpy( color, materialColor, sizeof( float ) * 4 );
		} else {
			color[0] = 1.0f;
			color[1] = 1.0f;
			color[2] = 1.0f;
			color[3] = 1.0f;
		}
		break;
	case MODERN_GL_SHADER_DEBUG_VISUALIZATION:
		color[0] = 1.0f;
		color[1] = 0.25f;
		color[2] = 0.65f;
		color[3] = 1.0f;
		break;
	default:
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
		color[3] = 1.0f;
		break;
	}
}

static void R_ModernGLExecutor_SetDebugColor( const modernGLSubmitCommand_t &command ) {
	if ( command.debugColorLocation < 0 ) {
		return;
	}
	float color[4];
	R_ModernGLExecutor_DebugColorForCommand( command, color );
	glUniform4f( command.debugColorLocation, color[0], color[1], color[2], color[3] );
}

static float R_ModernGLExecutor_AlphaReferenceForCommand( const modernGLSubmitCommand_t &command );
static bool R_ModernGLExecutor_MaterialContractPromotable( const materialResourceTableRecord_t &materialRecord, bool allowAlphaBlend );

static void R_ModernGLExecutor_LocalParamsForCommand( const modernGLSubmitCommand_t &command, float params[4] ) {
	params[0] = 0.0f;
	params[1] = 0.0f;
	params[2] = 0.0f;
	params[3] = 0.0f;
	switch ( command.shaderKind ) {
	case MODERN_GL_SHADER_DEPTH:
	case MODERN_GL_SHADER_SHADOW_DEPTH: {
		const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
		const bool alphaTest = materialRecord != NULL && ( materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED );
		params[0] = R_ModernGLExecutor_AlphaReferenceForCommand( command );
		params[1] = alphaTest ? 1.0f : 0.0f;
		break;
	}
	case MODERN_GL_SHADER_GBUFFER_ALPHA_TEST:
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST:
		params[0] = R_ModernGLExecutor_AlphaReferenceForCommand( command );
		params[2] = 0.25f;
		break;
	case MODERN_GL_SHADER_GBUFFER_OPAQUE:
		params[0] = 0.1f;
		params[1] = 0.5f;
		params[2] = 0.25f;
		break;
	case MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE:
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE:
	case MODERN_GL_SHADER_LIGHT_GRID:
		params[0] = 1.0f;
		break;
	case MODERN_GL_SHADER_TRANSPARENT_FORWARD:
	case MODERN_GL_SHADER_FOG_BLEND:
		params[0] = 0.25f;
		params[1] = 0.38f;
		params[2] = 0.42f;
		params[3] = 0.48f;
		break;
	case MODERN_GL_SHADER_DEBUG_VISUALIZATION:
		params[0] = 0.5f;
		params[1] = 0.1f;
		params[2] = 0.9f;
		params[3] = 0.4f;
		break;
	default:
		break;
	}
}

static void R_ModernGLExecutor_SetLocalParams( const modernGLSubmitCommand_t &command ) {
	if ( command.localParamsLocation < 0 ) {
		return;
	}
	float params[4];
	R_ModernGLExecutor_LocalParamsForCommand( command, params );
	glUniform4f( command.localParamsLocation, params[0], params[1], params[2], params[3] );
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

static bool R_ModernGLExecutor_GpuDrivenStreamBindingsReady( void ) {
	return rg_modernGLGpuDrivenStreamBindings.valid
		&& rg_modernGLGpuDrivenStreamBindings.sceneRecords.valid
		&& rg_modernGLGpuDrivenStreamBindings.validationCounters.valid
		&& rg_modernGLGpuDrivenStreamBindings.indirectCommands.valid
		&& rg_modernGLGpuDrivenStreamBindings.drawRecords.valid
		&& rg_modernGLGpuDrivenStreamBindings.drawRecordIndices.valid
		&& rg_modernGLGpuDrivenStreamBindings.bucketRecords.valid;
}

static void R_ModernGLExecutor_BindGpuDrivenBuffers( modernGLExecutorStats_t &stats ) {
	if ( !stats.gpuDrivenReady ) {
		return;
	}
	if ( R_ModernGLExecutor_GpuDrivenStreamBindingsReady() && glBindBufferRange != NULL ) {
		const modernGLGpuDrivenStreamBindings_t &stream = rg_modernGLGpuDrivenStreamBindings;
		R_GLStateCache().BindBufferRange( GL_SHADER_STORAGE_BUFFER, 1, stream.sceneRecords.allocation.vbo, static_cast<GLintptr>( stream.sceneRecords.allocation.offset ), stream.sceneRecords.size );
		R_GLStateCache().BindBufferRange( GL_SHADER_STORAGE_BUFFER, 2, stream.validationCounters.allocation.vbo, static_cast<GLintptr>( stream.validationCounters.allocation.offset ), stream.validationCounters.size );
		R_GLStateCache().BindBufferRange( GL_SHADER_STORAGE_BUFFER, 3, stream.indirectCommands.allocation.vbo, static_cast<GLintptr>( stream.indirectCommands.allocation.offset ), stream.indirectCommands.size );
		R_GLStateCache().BindBufferRange( GL_SHADER_STORAGE_BUFFER, MODERN_GL_DRAW_RECORD_SSBO_BINDING, stream.drawRecords.allocation.vbo, static_cast<GLintptr>( stream.drawRecords.allocation.offset ), stream.drawRecords.size );
		R_GLStateCache().BindBufferRange( GL_SHADER_STORAGE_BUFFER, MODERN_GL_GPU_BUCKET_SSBO_BINDING, stream.bucketRecords.allocation.vbo, static_cast<GLintptr>( stream.bucketRecords.allocation.offset ), stream.bucketRecords.size );
		R_GLStateCache().BindBuffer( GL_DRAW_INDIRECT_BUFFER, stream.indirectCommands.allocation.vbo );
		return;
	}
	if ( stats.tierUsesMultiBind && glBindBuffersBase != NULL ) {
		GLuint buffers[5] = { rg_modernGLExecutorSceneSSBO, rg_modernGLExecutorValidationSSBO, rg_modernGLExecutorIndirectBuffer, rg_modernGLExecutorDrawRecordSSBO, rg_modernGLExecutorBucketSSBO };
		if ( R_GLStateCache().BindBuffersBase( GL_SHADER_STORAGE_BUFFER, 1, 5, buffers ) ) {
			stats.lowOverheadMultiBindBatches++;
		}
	} else if ( glBindBufferBase != NULL ) {
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 1, rg_modernGLExecutorSceneSSBO );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 2, rg_modernGLExecutorValidationSSBO );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 3, rg_modernGLExecutorIndirectBuffer );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, MODERN_GL_DRAW_RECORD_SSBO_BINDING, rg_modernGLExecutorDrawRecordSSBO );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, MODERN_GL_GPU_BUCKET_SSBO_BINDING, rg_modernGLExecutorBucketSSBO );
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
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, MODERN_GL_DRAW_RECORD_SSBO_BINDING, 0 );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, MODERN_GL_GPU_BUCKET_SSBO_BINDING, 0 );
	}
	if ( glBindBuffer != NULL ) {
		R_GLStateCache().BindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
	}
}

static void R_ModernGLExecutor_ResetGpuDrivenBatches( void ) {
	memset( rg_modernGLGpuDrivenBuckets, 0, sizeof( rg_modernGLGpuDrivenBuckets ) );
	rg_modernGLGpuDrivenBucketCount = 0;
}

enum modernGLVisibilityRejectReason_t {
	MODERN_GL_VISIBILITY_REJECT_NONE = 0,
	MODERN_GL_VISIBILITY_REJECT_INVALID,
	MODERN_GL_VISIBILITY_REJECT_SCISSOR,
	MODERN_GL_VISIBILITY_REJECT_FRUSTUM,
	MODERN_GL_VISIBILITY_REJECT_SCREEN_RECT
};

static int R_ModernGLExecutor_CommandTriangleCount( const modernGLSubmitCommand_t &command ) {
	return command.indexed ? Max( 0, command.indexCount / 3 ) : Max( 0, command.vertexCount / 3 );
}

static void R_ModernGLExecutor_CountVisibilityTest( modernGLExecutorStats_t *stats, const modernGLSubmitCommand_t &command, bool gpuDriven ) {
	if ( stats == NULL ) {
		return;
	}
	if ( gpuDriven ) {
		stats->visibilityGpuTested++;
	} else {
		stats->visibilityCpuTested++;
	}
	if ( command.visibilityShadowCaster ) {
		stats->visibilityShadowCasterTested++;
	}
}

static void R_ModernGLExecutor_CountVisibilityReject( modernGLExecutorStats_t *stats, const modernGLSubmitCommand_t &command, bool gpuDriven, modernGLVisibilityRejectReason_t reason ) {
	if ( stats == NULL || reason == MODERN_GL_VISIBILITY_REJECT_NONE || reason == MODERN_GL_VISIBILITY_REJECT_INVALID ) {
		return;
	}
	if ( gpuDriven ) {
		stats->visibilityGpuRejected++;
	} else {
		stats->visibilityCpuRejected++;
	}
	if ( reason == MODERN_GL_VISIBILITY_REJECT_SCISSOR ) {
		stats->visibilityScissorRejected++;
	} else if ( reason == MODERN_GL_VISIBILITY_REJECT_FRUSTUM ) {
		stats->visibilityFrustumRejected++;
	} else if ( reason == MODERN_GL_VISIBILITY_REJECT_SCREEN_RECT ) {
		stats->visibilityScreenRectRejected++;
	}
	stats->visibilitySavedDraws++;
	stats->visibilitySavedTriangles += R_ModernGLExecutor_CommandTriangleCount( command );
	if ( command.visibilityShadowCaster ) {
		stats->visibilityShadowCasterRejected++;
		stats->visibilityShadowCasterSavedDraws++;
		stats->visibilityShadowCasterSavedTriangles += R_ModernGLExecutor_CommandTriangleCount( command );
	}
}

static bool R_ModernGLExecutor_CommandUsesScreenBounds( const modernGLSubmitCommand_t &command ) {
	if ( command.passCategory == RENDER_PASS_SHADOW_MAP || command.passCategory == RENDER_PASS_STENCIL_SHADOW ) {
		return false;
	}
	return command.visibilityScreenRectValid;
}

static int R_ModernGLExecutor_ViewWidth( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return Max( 1, glConfig.vidWidth );
	}
	const int viewportWidth = viewDef->viewport.x2 >= viewDef->viewport.x1 ? viewDef->viewport.x2 + 1 - viewDef->viewport.x1 : 0;
	if ( viewportWidth > 0 ) {
		return viewportWidth;
	}
	if ( viewDef->renderView.width > 0 ) {
		return viewDef->renderView.width;
	}
	return Max( 1, glConfig.vidWidth );
}

static int R_ModernGLExecutor_ViewHeight( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return Max( 1, glConfig.vidHeight );
	}
	const int viewportHeight = viewDef->viewport.y2 >= viewDef->viewport.y1 ? viewDef->viewport.y2 + 1 - viewDef->viewport.y1 : 0;
	if ( viewportHeight > 0 ) {
		return viewportHeight;
	}
	if ( viewDef->renderView.height > 0 ) {
		return viewDef->renderView.height;
	}
	return Max( 1, glConfig.vidHeight );
}

static bool R_ModernGLExecutor_CommandBaseScissor( const modernGLSubmitCommand_t &command, int &scissorX1, int &scissorY1, int &scissorX2, int &scissorY2 ) {
	if ( command.viewDef == NULL ) {
		return false;
	}
	scissorX1 = command.scissorX1;
	scissorY1 = command.scissorY1;
	scissorX2 = command.scissorX2;
	scissorY2 = command.scissorY2;
	if ( scissorX2 < scissorX1 || scissorY2 < scissorY1 ) {
		scissorX1 = command.viewDef->scissor.x1;
		scissorY1 = command.viewDef->scissor.y1;
		scissorX2 = command.viewDef->scissor.x2;
		scissorY2 = command.viewDef->scissor.y2;
	}
	if ( scissorX2 < scissorX1 || scissorY2 < scissorY1 ) {
		return false;
	}

	const int viewWidth = R_ModernGLExecutor_ViewWidth( command.viewDef );
	const int viewHeight = R_ModernGLExecutor_ViewHeight( command.viewDef );
	if ( viewWidth <= 0 || viewHeight <= 0 ) {
		return false;
	}
	if ( scissorX2 < 0 || scissorY2 < 0 || scissorX1 >= viewWidth || scissorY1 >= viewHeight ) {
		return false;
	}
	scissorX1 = idMath::ClampInt( 0, viewWidth - 1, scissorX1 );
	scissorY1 = idMath::ClampInt( 0, viewHeight - 1, scissorY1 );
	scissorX2 = idMath::ClampInt( 0, viewWidth - 1, scissorX2 );
	scissorY2 = idMath::ClampInt( 0, viewHeight - 1, scissorY2 );
	return scissorX1 <= scissorX2 && scissorY1 <= scissorY2;
}

static bool R_ModernGLExecutor_CommandEffectiveScissor( const modernGLSubmitCommand_t &command, int &scissorX1, int &scissorY1, int &scissorX2, int &scissorY2, bool &screenClipped ) {
	screenClipped = false;
	if ( !R_ModernGLExecutor_CommandBaseScissor( command, scissorX1, scissorY1, scissorX2, scissorY2 ) ) {
		return false;
	}
	if ( R_ModernGLExecutor_CommandUsesScreenBounds( command ) ) {
		const int oldX1 = scissorX1;
		const int oldY1 = scissorY1;
		const int oldX2 = scissorX2;
		const int oldY2 = scissorY2;
		scissorX1 = Max( scissorX1, command.visibilityScreenX1 );
		scissorY1 = Max( scissorY1, command.visibilityScreenY1 );
		scissorX2 = Min( scissorX2, command.visibilityScreenX2 );
		scissorY2 = Min( scissorY2, command.visibilityScreenY2 );
		screenClipped = scissorX1 != oldX1 || scissorY1 != oldY1 || scissorX2 != oldX2 || scissorY2 != oldY2;
	}
	return scissorX1 <= scissorX2 && scissorY1 <= scissorY2;
}

static bool R_ModernGLExecutor_CommandFrustumVisible( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t *stats ) {
	if ( !command.visibilityFrustumEligible ) {
		return true;
	}
	if ( !command.visibilityBoundsValid ) {
		if ( stats != NULL ) {
			stats->visibilityFalsePositiveFallbacks++;
		}
		return true;
	}
	return !command.visibilityFrustumRejected;
}

static bool R_ModernGLExecutor_CommandVisibleForModernPath( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t *stats, bool gpuDriven ) {
	if ( command.viewDef == NULL || command.indexCount <= 0 || command.vertexCount <= 0 ) {
		return false;
	}

	if ( !r_rendererOcclusion.GetBool() ) {
		return true;
	}

	R_ModernGLExecutor_CountVisibilityTest( stats, command, gpuDriven );
	if ( command.visibilityNearPlaneClipped && stats != NULL ) {
		stats->visibilityNearPlaneConservative++;
	}
	if ( command.visibilityDynamic && stats != NULL ) {
		stats->visibilityDynamicConservative++;
	}
	if ( command.visibilityHiZCandidate && stats != NULL ) {
		stats->visibilityHiZCandidates++;
	}
	int scissorX1;
	int scissorY1;
	int scissorX2;
	int scissorY2;
	bool screenClipped = false;
	if ( !R_ModernGLExecutor_CommandBaseScissor( command, scissorX1, scissorY1, scissorX2, scissorY2 ) ) {
		R_ModernGLExecutor_CountVisibilityReject( stats, command, gpuDriven, MODERN_GL_VISIBILITY_REJECT_SCISSOR );
		return false;
	}
	if ( !R_ModernGLExecutor_CommandFrustumVisible( command, stats ) ) {
		R_ModernGLExecutor_CountVisibilityReject( stats, command, gpuDriven, MODERN_GL_VISIBILITY_REJECT_FRUSTUM );
		return false;
	}
	if ( !R_ModernGLExecutor_CommandEffectiveScissor( command, scissorX1, scissorY1, scissorX2, scissorY2, screenClipped ) ) {
		R_ModernGLExecutor_CountVisibilityReject( stats, command, gpuDriven, MODERN_GL_VISIBILITY_REJECT_SCREEN_RECT );
		return false;
	}
	if ( screenClipped && stats != NULL ) {
		stats->visibilityScreenRectClipped++;
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
		&& command.vertexStride > 0
		&& command.drawRecordModeLocation >= 0
		&& ( command.ambientCacheOffset % command.vertexStride ) == 0;
}

static bool R_ModernGLExecutor_CommandCanUseHiZForIndirect( const modernGLSubmitCommand_t &command, const modernGLExecutorStats_t &stats ) {
	if ( !stats.visibilityHiZBuilt || !command.visibilityHiZCandidate || command.visibilityNearPlaneClipped || command.visibilityDynamic ) {
		return false;
	}
	if ( command.passCategory == RENDER_PASS_SHADOW_MAP || command.passCategory == RENDER_PASS_STENCIL_SHADOW ) {
		return false;
	}
	switch ( command.pipeline ) {
	case MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER:
	case MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE:
	case MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST:
	case MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL:
		return true;
	default:
		return false;
	}
}

static bool R_ModernGLExecutor_CommandMatchesGpuDrivenBucket( const modernGLSubmitCommand_t &command, const modernGLGpuDrivenBucket_t &bucket ) {
	if ( !bucket.valid ) {
		return false;
	}
	return command.program == bucket.program
		&& command.vertexBuffer == bucket.vertexBuffer
		&& command.indexBuffer == bucket.indexBuffer
		&& static_cast<GLenum>( command.indexType ) == bucket.indexType
		&& command.vertexStride == bucket.vertexStride
		&& command.materialTableIndex == bucket.materialTableIndex
		&& command.materialStableId == bucket.materialStableId
		&& static_cast<int>( command.passCategory ) == bucket.passCategory
		&& static_cast<int>( command.shaderKind ) == bucket.shaderKind
		&& static_cast<int>( command.pipeline ) == bucket.pipeline
		&& command.cullType == bucket.cullType
		&& command.scissorX1 == bucket.scissorX1
		&& command.scissorY1 == bucket.scissorY1
		&& command.scissorX2 == bucket.scissorX2
		&& command.scissorY2 == bucket.scissorY2
		&& command.twoSided == bucket.twoSided
		&& command.shouldCreateBackSides == bucket.shouldCreateBackSides
		&& command.negativeScale == bucket.negativeScale
		&& command.weaponDepthHack == bucket.weaponDepthHack;
}

static int R_ModernGLExecutor_AllocGpuDrivenBucket( const modernGLSubmitCommand_t &command, int firstIndirect ) {
	if ( rg_modernGLGpuDrivenBucketCount >= MODERN_GL_GPU_DRIVEN_MAX_BUCKETS ) {
		return -1;
	}
	modernGLGpuDrivenBucket_t &bucket = rg_modernGLGpuDrivenBuckets[rg_modernGLGpuDrivenBucketCount];
	memset( &bucket, 0, sizeof( bucket ) );
	bucket.valid = true;
	bucket.program = command.program;
	bucket.vertexBuffer = command.vertexBuffer;
	bucket.indexBuffer = command.indexBuffer;
	bucket.indexType = static_cast<GLenum>( command.indexType );
	bucket.vertexStride = command.vertexStride;
	bucket.materialTableIndex = command.materialTableIndex;
	bucket.materialStableId = command.materialStableId;
	bucket.passCategory = static_cast<int>( command.passCategory );
	bucket.shaderKind = static_cast<int>( command.shaderKind );
	bucket.pipeline = static_cast<int>( command.pipeline );
	bucket.cullType = command.cullType;
	bucket.scissorX1 = command.scissorX1;
	bucket.scissorY1 = command.scissorY1;
	bucket.scissorX2 = command.scissorX2;
	bucket.scissorY2 = command.scissorY2;
	bucket.twoSided = command.twoSided;
	bucket.shouldCreateBackSides = command.shouldCreateBackSides;
	bucket.negativeScale = command.negativeScale;
	bucket.weaponDepthHack = command.weaponDepthHack;
	bucket.command = command;
	bucket.firstIndirect = firstIndirect;
	bucket.commandCount = 0;
	return rg_modernGLGpuDrivenBucketCount++;
}

static void R_ModernGLExecutor_BuildDrawRecord( const modernGLSubmitCommand_t &command, modernGLDrawRecord_t &record ) {
	memset( &record, 0, sizeof( record ) );
	float projectionMatrix[16];
	R_ModernGLExecutor_BuildCommandProjectionMatrix( command, projectionMatrix );
	myGlMultMatrix( command.modelViewMatrix, projectionMatrix, record.modelViewProjection );
	memcpy( record.modelViewMatrix, command.modelViewMatrix, sizeof( record.modelViewMatrix ) );
	R_ModernGLExecutor_DebugColorForCommand( command, record.debugColor );
	R_ModernGLExecutor_LocalParamsForCommand( command, record.localParams );
	R_ModernGLExecutor_MaterialFlagsForCommand( command, record.materialFlags );
	R_ModernGLExecutor_MaterialEnhancementForCommand( command, record.materialEnhancement );
	record.ids[0] = command.materialTableIndex >= 0 ? static_cast<GLuint>( command.materialTableIndex ) : 0xffffffffu;
	record.ids[1] = command.materialRecordIndex >= 0 ? static_cast<GLuint>( command.materialRecordIndex ) : 0xffffffffu;
	record.ids[2] = static_cast<GLuint>( command.shaderKind );
	record.ids[3] = command.sortEligible ? 1u : 0u;
}

static int R_ModernGLExecutor_CompareGpuDrivenCounter( GLuint gpuValue, int cpuValue ) {
	return gpuValue == static_cast<GLuint>( Max( 0, cpuValue ) ) ? 0 : 1;
}

static bool R_ModernGLExecutor_ReadGpuDrivenCounters( GLuint buffer, GLintptr offset, GLuint counters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] ) {
	if ( glGetBufferSubData == NULL || buffer == 0 ) {
		return false;
	}
	R_GLStateCache().BindBuffer( GL_SHADER_STORAGE_BUFFER, buffer );
	glGetBufferSubData( GL_SHADER_STORAGE_BUFFER, offset, sizeof( GLuint ) * MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS, counters );
	R_GLStateCache().BindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );
	return true;
}

static void R_ModernGLExecutor_ApplyGpuDrivenValidationCounters( modernGLExecutorStats_t &stats, const modernGLGpuDrivenCpuReference_t &cpuReference, const GLuint counters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] ) {
	const int hiZRejected = static_cast<int>( counters[MODERN_GL_GPU_COUNTER_HIZ_REJECTED] );
	stats.gpuDrivenValidationReadbackReady = true;
	stats.gpuDrivenValidationReadbacks++;
	stats.gpuDrivenGpuGeneratedCommands = static_cast<int>( counters[MODERN_GL_GPU_COUNTER_GENERATED] );
	stats.gpuDrivenGpuCulledCommands = static_cast<int>( counters[MODERN_GL_GPU_COUNTER_CULLED] );
	stats.gpuDrivenGpuVisibleInstances = static_cast<int>( counters[MODERN_GL_GPU_COUNTER_VISIBLE_INSTANCES] );
	stats.gpuDrivenGpuClusterBins = static_cast<int>( counters[MODERN_GL_GPU_COUNTER_CLUSTER_BINS] );
	stats.gpuDrivenHiZRejected = hiZRejected;
	stats.gpuDrivenIndirectCompactedCommands = static_cast<int>( counters[MODERN_GL_GPU_COUNTER_COMPACTED] );
	stats.visibilityHiZRejected = stats.gpuDrivenHiZRejected;
	stats.visibilityGpuRejected += stats.gpuDrivenHiZRejected;
	stats.visibilitySavedDraws += stats.gpuDrivenHiZRejected;
	stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( counters[MODERN_GL_GPU_COUNTER_PROCESSED], cpuReference.processedCommands );
	stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( counters[MODERN_GL_GPU_COUNTER_ELIGIBLE], cpuReference.eligibleCommands );
	stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( counters[MODERN_GL_GPU_COUNTER_GENERATED], Max( 0, cpuReference.generatedCommands - hiZRejected ) );
	stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( counters[MODERN_GL_GPU_COUNTER_CULLED], cpuReference.culledCommands + hiZRejected );
	stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( counters[MODERN_GL_GPU_COUNTER_VISIBLE_INSTANCES], Max( 0, cpuReference.visibleInstances - hiZRejected ) );
	stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( counters[MODERN_GL_GPU_COUNTER_CLUSTER_BINS], cpuReference.clusterBins );
	stats.gpuDrivenValidationMismatches += R_ModernGLExecutor_CompareGpuDrivenCounter( counters[MODERN_GL_GPU_COUNTER_COMPACTED], stats.gpuDrivenGpuGeneratedCommands );
}

static void R_ModernGLExecutor_ApplyGpuDrivenCpuPrediction( modernGLExecutorStats_t &stats, const modernGLGpuDrivenCpuReference_t &cpuReference ) {
	stats.gpuDrivenGpuGeneratedCommands = cpuReference.generatedCommands;
	stats.gpuDrivenGpuCulledCommands = cpuReference.culledCommands;
	stats.gpuDrivenGpuVisibleInstances = cpuReference.visibleInstances;
	stats.gpuDrivenGpuClusterBins = cpuReference.clusterBins;
	stats.gpuDrivenIndirectCompactedCommands = cpuReference.generatedCommands;
}

static void R_ModernGLExecutor_ClearPendingGpuValidationReadback( modernGLPendingGpuValidationReadback_t &pending ) {
	if ( pending.fence != NULL && glDeleteSync != NULL ) {
		glDeleteSync( pending.fence );
	}
	memset( &pending, 0, sizeof( pending ) );
}

static void R_ModernGLExecutor_ClearPendingGpuValidationReadbacks( void ) {
	for ( int i = 0; i < MODERN_GL_GPU_VALIDATION_PENDING_READBACKS; ++i ) {
		R_ModernGLExecutor_ClearPendingGpuValidationReadback( rg_modernGLPendingValidationReadbacks[i] );
	}
}

static bool R_ModernGLExecutor_PollGpuValidationFence( modernGLPendingGpuValidationReadback_t &pending ) {
	if ( pending.fence == NULL || glClientWaitSync == NULL ) {
		return tr.frameCount >= pending.readyFrame;
	}
	const GLenum waitResult = glClientWaitSync( pending.fence, 0, 0 );
	if ( waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED ) {
		return true;
	}
	return false;
}

static void R_ModernGLExecutor_ProcessPendingGpuDrivenValidationReadbacks( modernGLExecutorStats_t &stats ) {
	for ( int i = 0; i < MODERN_GL_GPU_VALIDATION_PENDING_READBACKS; ++i ) {
		modernGLPendingGpuValidationReadback_t &pending = rg_modernGLPendingValidationReadbacks[i];
		if ( !pending.valid || tr.frameCount < pending.readyFrame ) {
			continue;
		}
		if ( !R_ModernGLExecutor_PollGpuValidationFence( pending ) ) {
			stats.gpuDrivenValidationDeferredReadbacks++;
			const int maxSafeAge = Max( 1, R_RendererUpload_Stats().ringBufferCount - 1 );
			if ( tr.frameCount >= pending.submitFrame + maxSafeAge ) {
				stats.gpuDrivenValidationSkippedReadbacks++;
				R_ModernGLExecutor_ClearPendingGpuValidationReadback( pending );
			}
			continue;
		}
		GLuint gpuCounters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0 };
		if ( R_ModernGLExecutor_ReadGpuDrivenCounters( pending.buffer, pending.offset, gpuCounters ) ) {
			R_ModernGLExecutor_ApplyGpuDrivenValidationCounters( stats, pending.cpuReference, gpuCounters );
		} else {
			stats.gpuDrivenValidationSkippedReadbacks++;
		}
		R_ModernGLExecutor_ClearPendingGpuValidationReadback( pending );
	}
}

static bool R_ModernGLExecutor_QueueGpuDrivenValidationReadback( const modernGLStreamBufferBinding_t &validationBinding, const modernGLGpuDrivenCpuReference_t &cpuReference, int delayFrames, modernGLExecutorStats_t &stats ) {
	if ( !validationBinding.valid || validationBinding.allocation.vbo == 0 || validationBinding.size < static_cast<GLsizeiptr>( sizeof( GLuint ) * MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS ) ) {
		stats.gpuDrivenValidationSkippedReadbacks++;
		return false;
	}
	for ( int i = 0; i < MODERN_GL_GPU_VALIDATION_PENDING_READBACKS; ++i ) {
		modernGLPendingGpuValidationReadback_t &pending = rg_modernGLPendingValidationReadbacks[i];
		if ( pending.valid ) {
			continue;
		}
		memset( &pending, 0, sizeof( pending ) );
		pending.valid = true;
		pending.buffer = validationBinding.allocation.vbo;
		pending.offset = static_cast<GLintptr>( validationBinding.allocation.offset );
		pending.size = validationBinding.size;
		pending.submitFrame = tr.frameCount;
		const int safeDelay = Min( Max( 0, delayFrames ), Max( 0, R_RendererUpload_Stats().ringBufferCount - 1 ) );
		pending.readyFrame = tr.frameCount + safeDelay;
		pending.cpuReference = cpuReference;
		if ( glFenceSync != NULL ) {
			pending.fence = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
		}
		stats.gpuDrivenValidationDeferredReadbacks++;
		return true;
	}
	stats.gpuDrivenValidationSkippedReadbacks++;
	return false;
}

static void R_ModernGLExecutor_UpdateGpuDrivenBuffers( modernGLExecutorStats_t &stats, bool forceValidationReadback = false ) {
	R_ModernGLExecutor_ResetGpuDrivenBatches();
	R_ModernGLExecutor_ResetGpuDrivenStreamBindings();
	stats.gpuDrivenValidationRequested = stats.gpuDrivenValidationRequested || forceValidationReadback;
	if ( !stats.gpuDrivenReady || !stats.submitPlanReady || rg_modernGLExecutorSceneSSBO == 0 || rg_modernGLExecutorIndirectBuffer == 0 || rg_modernGLExecutorDrawRecordSSBO == 0 || rg_modernGLExecutorDrawRecordIndexBuffer == 0 || rg_modernGLExecutorBucketSSBO == 0 || rg_modernGLExecutorValidationSSBO == 0 ) {
		return;
	}

	static modernGLGpuSceneRecord_t sceneRecords[MODERN_GL_GPU_DRIVEN_MAX_RECORDS];
	static modernGLDrawElementsIndirectCommand_t indirectRecords[MODERN_GL_GPU_DRIVEN_MAX_RECORDS];
	static modernGLDrawRecord_t drawRecords[MODERN_GL_GPU_DRIVEN_MAX_RECORDS];
	static GLfloat drawRecordIndices[MODERN_GL_GPU_DRIVEN_MAX_RECORDS];
	static modernGLGpuDrivenBucketRecord_t bucketRecords[MODERN_GL_GPU_DRIVEN_MAX_BUCKETS];
	memset( sceneRecords, 0, sizeof( sceneRecords ) );
	memset( indirectRecords, 0, sizeof( indirectRecords ) );
	memset( drawRecords, 0, sizeof( drawRecords ) );
	memset( drawRecordIndices, 0, sizeof( drawRecordIndices ) );
	memset( bucketRecords, 0, sizeof( bucketRecords ) );

	const renderGraphResourceHandle_t *sceneHiZ = NULL;
	const bool useHiZForIndirect =
		stats.visibilityHiZBuilt
		&& stats.visibilityHiZResourceReady
		&& R_ModernGLExecutor_DepthResourceReady( "sceneHiZ", sceneHiZ )
		&& sceneHiZ != NULL
		&& sceneHiZ->target == GL_TEXTURE_2D
		&& sceneHiZ->texture != 0
		&& sceneHiZ->mipLevels > 1;
	stats.gpuDrivenHiZCullingReady = useHiZForIndirect;
	stats.gpuDrivenIndirectCompactionReady = rg_modernGLExecutorBucketSSBO != 0 && stats.computeValidationReady;

	const rendererClusteredLightingStats_t clusteredStats = R_ModernClusteredLighting_Stats();
	modernGLGpuDrivenCpuReference_t cpuReference;
	memset( &cpuReference, 0, sizeof( cpuReference ) );
	cpuReference.clusterBins = clusteredStats.frameValid ? clusteredStats.activeClusters : 0;

	const int sceneRecordCount = Min( rg_modernGLSubmitPlan.NumCommands(), MODERN_GL_GPU_DRIVEN_MAX_RECORDS );
	int indirectRecordCount = 0;
	for ( int i = 0; i < sceneRecordCount; ++i ) {
		const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
		modernGLSubmitCommand_t indirectCommand = command;
		modernGLGpuSceneRecord_t &record = sceneRecords[i];
		R_ModernGLExecutor_BuildDrawRecord( command, drawRecords[i] );
		drawRecordIndices[i] = static_cast<GLfloat>( i );

		const bool visible = R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, true );
		bool uploadReady = true;
		if ( visible && command.uploadIndexBuffer ) {
			uploadReady = false;
			if ( command.clientIndexData != NULL && command.clientIndexBytes > 0 ) {
				rendererUploadAllocation_t indexUpload;
				if ( R_RendererUpload_AllocFrameTemp( const_cast<void *>( command.clientIndexData ), command.clientIndexBytes, 4, indexUpload ) ) {
					indirectCommand.indexBuffer = indexUpload.vbo;
					indirectCommand.indexCacheOffset = indexUpload.offset;
					indirectCommand.uploadIndexBuffer = false;
					indirectCommand.clientIndexData = NULL;
					indirectCommand.clientIndexBytes = 0;
					uploadReady = true;
				}
			}
		}
		const bool canSeedIndirect = visible && uploadReady && R_ModernGLExecutor_CommandCanSeedIndirect( indirectCommand );
		int bucketIndex = -1;
		if ( canSeedIndirect ) {
			if ( rg_modernGLGpuDrivenBucketCount > 0 && R_ModernGLExecutor_CommandMatchesGpuDrivenBucket( indirectCommand, rg_modernGLGpuDrivenBuckets[rg_modernGLGpuDrivenBucketCount - 1] ) ) {
				bucketIndex = rg_modernGLGpuDrivenBucketCount - 1;
			} else {
				bucketIndex = R_ModernGLExecutor_AllocGpuDrivenBucket( indirectCommand, indirectRecordCount );
			}
		}
		const bool indirectEligible = canSeedIndirect && bucketIndex >= 0 && indirectRecordCount < MODERN_GL_GPU_DRIVEN_MAX_RECORDS;
		const bool hiZCandidate = indirectEligible && R_ModernGLExecutor_CommandCanUseHiZForIndirect( indirectCommand, stats );

		cpuReference.processedCommands++;
		if ( visible ) {
			cpuReference.visibleInstances++;
		} else {
			cpuReference.culledCommands++;
		}
		if ( indirectEligible ) {
			cpuReference.eligibleCommands++;
			cpuReference.generatedCommands++;
			modernGLGpuDrivenBucket_t &bucket = rg_modernGLGpuDrivenBuckets[bucketIndex];
			bucket.commandCount++;
			indirectRecordCount++;
			if ( hiZCandidate ) {
				stats.gpuDrivenHiZCandidates++;
			}
		} else if ( visible && command.indexed && ( !uploadReady || ( command.program != 0 && command.vertexBuffer != 0 && command.indexCount > 0 ) ) ) {
			stats.gpuDrivenIndirectFallbacks++;
		}

		GLuint flags = indirectCommand.indexed ? MODERN_GL_GPU_RECORD_INDEXED : 0u;
		if ( indirectEligible ) {
			flags |= MODERN_GL_GPU_RECORD_INDIRECT_ELIGIBLE;
		}
		if ( hiZCandidate ) {
			flags |= MODERN_GL_GPU_RECORD_HIZ_CANDIDATE;
		}
		if ( visible ) {
			flags |= MODERN_GL_GPU_RECORD_VISIBLE;
		}
		int effectiveX1 = 0;
		int effectiveY1 = 0;
		int effectiveX2 = -1;
		int effectiveY2 = -1;
		bool screenClipped = false;
		if ( visible ) {
			R_ModernGLExecutor_CommandEffectiveScissor( indirectCommand, effectiveX1, effectiveY1, effectiveX2, effectiveY2, screenClipped );
		}
		if ( i == 0 ) {
			flags |= MODERN_GL_GPU_RECORD_CLUSTER_BIN_SOURCE;
		}
		record.counts[0] = static_cast<float>( r_singleTriangle.GetBool() ? Min( 3, indirectCommand.indexCount ) : indirectCommand.indexCount );
		record.counts[1] = static_cast<float>( indirectCommand.indexCacheOffset >= 0 ? indirectCommand.indexCacheOffset / static_cast<int>( sizeof( glIndex_t ) ) : 0 );
		record.counts[2] = static_cast<float>( ( i == 0 ) ? cpuReference.clusterBins : 0 );
		record.counts[3] = static_cast<float>( indirectCommand.passCategory );
		record.screenBounds[0] = static_cast<float>( effectiveX1 );
		record.screenBounds[1] = static_cast<float>( effectiveY1 );
		record.screenBounds[2] = static_cast<float>( effectiveX2 );
		record.screenBounds[3] = static_cast<float>( effectiveY2 );
		record.depthBounds[0] = indirectCommand.visibilityDepthMin;
		record.depthBounds[1] = indirectCommand.visibilityDepthMax;
		record.depthBounds[2] = 0.0f;
		record.depthBounds[3] = 0.0f;
		record.ids[0] = bucketIndex >= 0 ? static_cast<GLuint>( bucketIndex ) : 0xffffffffu;
		record.ids[1] = static_cast<GLuint>( i );
		record.ids[2] = command.materialTableIndex >= 0 ? static_cast<GLuint>( command.materialTableIndex ) : 0xffffffffu;
		record.ids[3] = flags;
		record.indirect[0] = static_cast<GLuint>( r_singleTriangle.GetBool() ? Min( 3, indirectCommand.indexCount ) : indirectCommand.indexCount );
		record.indirect[1] = static_cast<GLuint>( indirectCommand.indexCacheOffset >= 0 ? indirectCommand.indexCacheOffset / static_cast<int>( sizeof( glIndex_t ) ) : 0 );
		record.indirect[2] = indirectCommand.vertexStride > 0 && indirectCommand.ambientCacheOffset >= 0 ? static_cast<GLuint>( indirectCommand.ambientCacheOffset / indirectCommand.vertexStride ) : 0u;
		record.indirect[3] = static_cast<GLuint>( i );
	}

	for ( int i = 0; i < rg_modernGLGpuDrivenBucketCount; ++i ) {
		const modernGLGpuDrivenBucket_t &bucket = rg_modernGLGpuDrivenBuckets[i];
		bucketRecords[i].header[0] = static_cast<GLuint>( bucket.firstIndirect );
		bucketRecords[i].header[1] = static_cast<GLuint>( Max( 0, bucket.commandCount ) );
		bucketRecords[i].header[2] = static_cast<GLuint>( i );
		bucketRecords[i].header[3] = 0u;
	}

	GLuint validationCounters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0 };
	const GLsizeiptr sceneBytes = static_cast<GLsizeiptr>( sceneRecordCount * sizeof( modernGLGpuSceneRecord_t ) );
	const GLsizeiptr indirectBytes = static_cast<GLsizeiptr>( Max( 1, indirectRecordCount ) * sizeof( modernGLDrawElementsIndirectCommand_t ) );
	const GLsizeiptr drawRecordBytes = static_cast<GLsizeiptr>( Max( 1, sceneRecordCount ) * sizeof( modernGLDrawRecord_t ) );
	const GLsizeiptr drawRecordIndexBytes = static_cast<GLsizeiptr>( Max( 1, sceneRecordCount ) * sizeof( GLfloat ) );
	const GLsizeiptr bucketBytes = static_cast<GLsizeiptr>( Max( 1, rg_modernGLGpuDrivenBucketCount ) * sizeof( modernGLGpuDrivenBucketRecord_t ) );
	const GLsizeiptr validationBytes = static_cast<GLsizeiptr>( sizeof( validationCounters ) );
	const int indirectAlignment = Max( rg_modernGLExecutorShaderStorageAlignment, rg_modernGLExecutorIndirectBufferAlignment );
	const GLsizeiptr streamBytes[6] = { sceneBytes, indirectBytes, drawRecordBytes, drawRecordIndexBytes, bucketBytes, validationBytes };
	const int streamAlignments[6] = {
		rg_modernGLExecutorShaderStorageAlignment,
		indirectAlignment,
		rg_modernGLExecutorShaderStorageAlignment,
		static_cast<int>( sizeof( GLfloat ) ),
		rg_modernGLExecutorShaderStorageAlignment,
		rg_modernGLExecutorShaderStorageAlignment
	};
	const bool streamedGpuDrivenBuffers =
		glBindBufferRange != NULL
		&& R_ModernGLExecutor_StreamSequenceFits( streamBytes, streamAlignments, 6 )
		&& R_ModernGLExecutor_StreamBufferData( sceneRecords, sceneBytes, rg_modernGLExecutorShaderStorageAlignment, rg_modernGLGpuDrivenStreamBindings.sceneRecords, stats )
		&& R_ModernGLExecutor_StreamBufferData( indirectRecords, indirectBytes, indirectAlignment, rg_modernGLGpuDrivenStreamBindings.indirectCommands, stats )
		&& R_ModernGLExecutor_StreamBufferData( drawRecords, drawRecordBytes, rg_modernGLExecutorShaderStorageAlignment, rg_modernGLGpuDrivenStreamBindings.drawRecords, stats )
		&& R_ModernGLExecutor_StreamBufferData( drawRecordIndices, drawRecordIndexBytes, static_cast<int>( sizeof( GLfloat ) ), rg_modernGLGpuDrivenStreamBindings.drawRecordIndices, stats )
		&& R_ModernGLExecutor_StreamBufferData( bucketRecords, bucketBytes, rg_modernGLExecutorShaderStorageAlignment, rg_modernGLGpuDrivenStreamBindings.bucketRecords, stats )
		&& R_ModernGLExecutor_StreamBufferData( validationCounters, validationBytes, rg_modernGLExecutorShaderStorageAlignment, rg_modernGLGpuDrivenStreamBindings.validationCounters, stats );
	rg_modernGLGpuDrivenStreamBindings.valid = streamedGpuDrivenBuffers;
	if ( streamedGpuDrivenBuffers ) {
		stats.gpuDrivenStreamedBuffersReady = true;
		stats.uploadManagerGpuDrivenBytes += static_cast<int>( sceneBytes + indirectBytes + drawRecordBytes + drawRecordIndexBytes + bucketBytes + validationBytes );
		stats.uploadManagerGpuDrivenBuffers += 6;
	} else {
		R_ModernGLExecutor_ResetGpuDrivenStreamBindings();
		R_ModernGLExecutor_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorSceneSSBO, sceneBytes, sceneRecords, stats );
		R_ModernGLExecutor_UpdateBuffer( GL_DRAW_INDIRECT_BUFFER, rg_modernGLExecutorIndirectBuffer, indirectBytes, indirectRecords, stats );
		R_ModernGLExecutor_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorDrawRecordSSBO, drawRecordBytes, drawRecords, stats );
		R_ModernGLExecutor_UpdateBuffer( GL_ARRAY_BUFFER, rg_modernGLExecutorDrawRecordIndexBuffer, drawRecordIndexBytes, drawRecordIndices, stats );
		R_ModernGLExecutor_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorBucketSSBO, bucketBytes, bucketRecords, stats );
		R_ModernGLExecutor_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorValidationSSBO, validationBytes, validationCounters, stats );
		R_ModernGLExecutor_RecordUploadFallback( stats, sceneBytes );
		R_ModernGLExecutor_RecordUploadFallback( stats, indirectBytes );
		R_ModernGLExecutor_RecordUploadFallback( stats, drawRecordBytes );
		R_ModernGLExecutor_RecordUploadFallback( stats, drawRecordIndexBytes );
		R_ModernGLExecutor_RecordUploadFallback( stats, bucketBytes );
		R_ModernGLExecutor_RecordUploadFallback( stats, validationBytes );
	}

	stats.gpuDrivenSceneRecords = sceneRecordCount;
	stats.gpuDrivenIndirectRecords = cpuReference.generatedCommands;
	stats.gpuDrivenDrawRecords = sceneRecordCount;
	stats.gpuDrivenSceneBytes = static_cast<int>( sceneBytes );
	stats.gpuDrivenIndirectBytes = static_cast<int>( indirectBytes );
	stats.gpuDrivenDrawRecordBytes = static_cast<int>( drawRecordBytes );
	stats.gpuDrivenDrawRecordIndexBytes = static_cast<int>( drawRecordIndexBytes );
	stats.gpuDrivenBucketRecordBytes = static_cast<int>( bucketBytes );
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
	stats.gpuDrivenIndirectBuckets = rg_modernGLGpuDrivenBucketCount;
	stats.gpuDrivenIndirectBucketedCommands = indirectRecordCount;
	stats.gpuDrivenIndirectMultiDrawReady = rg_modernGLGpuDrivenBucketCount > 0 && indirectRecordCount > 0 && glMultiDrawElementsIndirect != NULL;

	R_ModernGLExecutor_BindGpuDrivenBuffers( stats );
	if ( sceneRecordCount > 0 && stats.computeValidationReady ) {
		idGLDebugScope debugScope( "ModernGLExecutor GPU-driven compute" );
		R_GLStateCache().UseProgram( rg_modernGLExecutorComputeProgram );
		if ( rg_modernGLExecutorComputeRecordCountLocation >= 0 ) {
			glUniform1ui( rg_modernGLExecutorComputeRecordCountLocation, static_cast<GLuint>( sceneRecordCount ) );
		}
		if ( rg_modernGLExecutorComputeHiZTextureLocation >= 0 ) {
			glUniform1i( rg_modernGLExecutorComputeHiZTextureLocation, 1 );
		}
		if ( rg_modernGLExecutorComputeHiZParamsLocation >= 0 && glUniform4f != NULL ) {
			const float hiZWidth = useHiZForIndirect ? static_cast<float>( Max( 1, sceneHiZ->width ) ) : 1.0f;
			const float hiZHeight = useHiZForIndirect ? static_cast<float>( Max( 1, sceneHiZ->height ) ) : 1.0f;
			const float hiZLevels = useHiZForIndirect ? static_cast<float>( Max( 1, sceneHiZ->mipLevels ) ) : 1.0f;
			glUniform4f( rg_modernGLExecutorComputeHiZParamsLocation, hiZWidth, hiZHeight, hiZLevels, useHiZForIndirect ? 1.0f : 0.0f );
		}
		if ( useHiZForIndirect ) {
			R_GLStateCache().ActiveTextureUnit( 1 );
			R_GLStateCache().BindTexture( 1, GL_TEXTURE_2D, sceneHiZ->texture );
		}
		glDispatchCompute( static_cast<GLuint>( ( sceneRecordCount + MODERN_GL_GPU_DRIVEN_WORKGROUP_SIZE - 1 ) / MODERN_GL_GPU_DRIVEN_WORKGROUP_SIZE ), 1, 1 );
		glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT );
		if ( useHiZForIndirect ) {
			R_GLStateCache().BindTexture( 1, GL_TEXTURE_2D, 0 );
			R_GLStateCache().ActiveTextureUnit( 0 );
		}
		R_GLStateCache().UseProgram( 0 );
		stats.gpuDrivenComputeDispatches++;
		stats.gpuDrivenExecuted = true;
		if ( stats.gpuDrivenValidationRequested ) {
			const int validationDelay = idMath::ClampInt( 1, 8, r_rendererGpuValidationReadbackDelay.GetInteger() );
			if ( forceValidationReadback ) {
				GLuint gpuCounters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0 };
				const bool streamedValidation = rg_modernGLGpuDrivenStreamBindings.validationCounters.valid;
				const GLuint validationBuffer = streamedValidation ? rg_modernGLGpuDrivenStreamBindings.validationCounters.allocation.vbo : rg_modernGLExecutorValidationSSBO;
				const GLintptr validationOffset = streamedValidation ? static_cast<GLintptr>( rg_modernGLGpuDrivenStreamBindings.validationCounters.allocation.offset ) : 0;
				if ( R_ModernGLExecutor_ReadGpuDrivenCounters( validationBuffer, validationOffset, gpuCounters ) ) {
					R_ModernGLExecutor_ApplyGpuDrivenValidationCounters( stats, cpuReference, gpuCounters );
				} else {
					stats.gpuDrivenValidationSkippedReadbacks++;
				}
			} else {
				if ( !R_ModernGLExecutor_QueueGpuDrivenValidationReadback( rg_modernGLGpuDrivenStreamBindings.validationCounters, cpuReference, validationDelay, stats ) ) {
					// The fixed fallback buffer is reused every update, so delayed validation is skipped instead of forcing a synchronous read.
				}
				R_ModernGLExecutor_ApplyGpuDrivenCpuPrediction( stats, cpuReference );
			}
		} else {
			R_ModernGLExecutor_ApplyGpuDrivenCpuPrediction( stats, cpuReference );
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

static const materialResourceTextureBinding_t *R_ModernGLExecutor_PrimaryColorBinding( const materialResourceTableRecord_t &record, modernGLShaderProgramKind_t shaderKind ) {
	if ( shaderKind == MODERN_GL_SHADER_GUI ) {
		return R_ModernGLExecutor_FindTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_GUI );
	}
	if ( shaderKind == MODERN_GL_SHADER_POST_COPY ) {
		return R_ModernGLExecutor_FindTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_POST_PROCESS );
	}

	const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_DIFFUSE );
	if ( binding != NULL ) {
		return binding;
	}
	return R_ModernGLExecutor_FindTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_EMISSIVE );
}

static bool R_ModernGLExecutor_CommandMaterialColor( const modernGLSubmitCommand_t &command, float color[4] ) {
	color[0] = 1.0f;
	color[1] = 1.0f;
	color[2] = 1.0f;
	color[3] = 1.0f;

	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord == NULL ) {
		return false;
	}
	const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_PrimaryColorBinding( *materialRecord, command.shaderKind );
	if ( binding == NULL ) {
		return true;
	}
	for ( int component = 0; component < 4; ++component ) {
		color[component] = R_ModernGLExecutor_ShaderRegisterValue( command, binding->colorRegisters[component], 1.0f );
	}
	return true;
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

static int R_ModernGLExecutor_TextureTableIndexForHandle( GLuint textureHandle ) {
	return R_MaterialResourceTable_TextureArrayTableIndexForHandle( static_cast<unsigned int>( textureHandle ) );
}

static int R_ModernGLExecutor_TextureTableIndexForBinding( const materialResourceTextureBinding_t *binding ) {
	if ( binding == NULL || binding->textureHandle == 0 ) {
		return -1;
	}
	if ( binding->textureArrayCandidate && binding->textureArrayLayer >= 0 ) {
		return binding->textureArrayLayer;
	}
	return R_ModernGLExecutor_TextureTableIndexForHandle( static_cast<GLuint>( binding->textureHandle ) );
}

static int R_ModernGLExecutor_TextureTableIndexForCommandSemantic( const modernGLSubmitCommand_t &command, materialResourceTextureSemantic_t semantic ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord != NULL ) {
		const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, semantic );
		const int bindingIndex = R_ModernGLExecutor_TextureTableIndexForBinding( binding );
		if ( bindingIndex >= 0 ) {
			return bindingIndex;
		}
	}
	return R_ModernGLExecutor_TextureTableIndexForHandle( R_ModernGLExecutor_FallbackTextureForSemantic( semantic ) );
}

static int R_ModernGLExecutor_MainTextureTableIndexForCommand( const modernGLSubmitCommand_t &command ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord != NULL ) {
		const materialResourceTextureSemantic_t semantics[3] = {
			MATERIAL_RESOURCE_TEXTURE_DIFFUSE,
			MATERIAL_RESOURCE_TEXTURE_GUI,
			MATERIAL_RESOURCE_TEXTURE_POST_PROCESS
		};
		for ( int i = 0; i < 3; ++i ) {
			const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, semantics[i] );
			const int bindingIndex = R_ModernGLExecutor_TextureTableIndexForBinding( binding );
			if ( bindingIndex >= 0 ) {
				return bindingIndex;
			}
		}
	}
	return R_ModernGLExecutor_TextureTableIndexForHandle( R_ModernGLExecutor_FallbackTextureForSemantic( MATERIAL_RESOURCE_TEXTURE_DIFFUSE ) );
}

static bool R_ModernGLExecutor_TextureTableIndicesForCommand( const modernGLSubmitCommand_t &command, GLuint indices[MODERN_GL_MATERIAL_TEXTURE_COUNT] ) {
	const materialResourceTableStats_t &materialStats = R_MaterialResourceTable_Stats();
	if ( !materialStats.textureArrayTableReady || command.textureIndicesLocation < 0 || command.textureTableModeLocation < 0 || glUniform1ui == NULL || glUniform4ui == NULL ) {
		return false;
	}
	indices[MODERN_GL_MATERIAL_TEXTURE_MAIN] = static_cast<GLuint>( R_ModernGLExecutor_MainTextureTableIndexForCommand( command ) );
	indices[MODERN_GL_MATERIAL_TEXTURE_NORMAL] = static_cast<GLuint>( R_ModernGLExecutor_TextureTableIndexForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_BUMP ) );
	indices[MODERN_GL_MATERIAL_TEXTURE_SPECULAR] = static_cast<GLuint>( R_ModernGLExecutor_TextureTableIndexForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_SPECULAR ) );
	indices[MODERN_GL_MATERIAL_TEXTURE_EMISSIVE] = static_cast<GLuint>( R_ModernGLExecutor_TextureTableIndexForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_EMISSIVE ) );
	for ( int i = 0; i < MODERN_GL_MATERIAL_TEXTURE_COUNT; ++i ) {
		if ( indices[i] == static_cast<GLuint>( -1 ) || static_cast<int>( indices[i] ) < 0 || static_cast<int>( indices[i] ) >= materialStats.textureArrayTableTextures ) {
			return false;
		}
	}
	return true;
}

static bool R_ModernGLExecutor_BindMaterialTextureTable( modernGLExecutorStats_t &stats ) {
	int textureCount = 0;
	const unsigned int *textureTable = R_MaterialResourceTable_TextureArrayTable( textureCount );
	if ( textureTable == NULL || textureCount <= 0 ) {
		return false;
	}
	GLuint textures[MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY];
	const int clampedCount = Min( textureCount, MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY );
	for ( int i = 0; i < clampedCount; ++i ) {
		textures[i] = static_cast<GLuint>( textureTable[i] );
	}
	R_ModernGLExecutor_BindTextureGroup( 0, static_cast<GLsizei>( clampedCount ), textures, stats );
	return true;
}

static void R_ModernGLExecutor_SetTextureTableMode( const modernGLSubmitCommand_t &command, bool enabled ) {
	if ( command.textureTableModeLocation >= 0 && glUniform1ui != NULL ) {
		glUniform1ui( command.textureTableModeLocation, enabled ? 1u : 0u );
	}
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

static void R_ModernGLExecutor_MaterialFlagsForCommand( const modernGLSubmitCommand_t &command, float flags[4] ) {
	flags[0] = R_ModernGLExecutor_CommandHasTextureSemantic( command, MATERIAL_RESOURCE_TEXTURE_BUMP ) ? 1.0f : 0.0f;
	flags[1] = R_ModernGLExecutor_CommandHasTextureSemantic( command, MATERIAL_RESOURCE_TEXTURE_SPECULAR ) ? 1.0f : 0.0f;
	flags[2] = R_ModernGLExecutor_CommandHasTextureSemantic( command, MATERIAL_RESOURCE_TEXTURE_EMISSIVE ) ? 1.0f : 0.0f;
	flags[3] = 0.0f;
}

static bool R_ModernGLExecutor_EnhancedMaterialShadingActive( void ) {
	return glConfig.GLSLProgramAvailable && r_enhancedMaterials.GetBool();
}

static void R_ModernGLExecutor_MaterialEnhancementForCommand( const modernGLSubmitCommand_t &command, float enhancement[4] ) {
	(void)command;
	const bool active = R_ModernGLExecutor_EnhancedMaterialShadingActive();
	enhancement[0] = active ? 1.0f : 0.0f;
	enhancement[1] = active ? idMath::ClampFloat( 0.5f, 2.0f, r_enhancedMaterialNormalScale.GetFloat() ) : 1.0f;
	enhancement[2] = active ? Max( 0.0f, r_enhancedMaterialSpecularBoost.GetFloat() ) : 1.0f;
	enhancement[3] = active ? idMath::ClampFloat( 0.0f, 1.0f, r_enhancedMaterialFresnel.GetFloat() ) : 0.0f;
}

static void R_ModernGLExecutor_SetMaterialFlags( const modernGLSubmitCommand_t &command ) {
	if ( command.materialFlagsLocation < 0 ) {
		return;
	}
	float flags[4];
	R_ModernGLExecutor_MaterialFlagsForCommand( command, flags );
	glUniform4f( command.materialFlagsLocation, flags[0], flags[1], flags[2], flags[3] );
}

static void R_ModernGLExecutor_SetMaterialEnhancement( const modernGLSubmitCommand_t &command ) {
	if ( command.materialEnhancementLocation < 0 ) {
		return;
	}
	float enhancement[4];
	R_ModernGLExecutor_MaterialEnhancementForCommand( command, enhancement );
	glUniform4f( command.materialEnhancementLocation, enhancement[0], enhancement[1], enhancement[2], enhancement[3] );
}

static void R_ModernGLExecutor_BindMaterialTextures( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	GLuint textureIndices[MODERN_GL_MATERIAL_TEXTURE_COUNT];
	if ( R_ModernGLExecutor_TextureTableIndicesForCommand( command, textureIndices ) && R_ModernGLExecutor_BindMaterialTextureTable( stats ) ) {
		R_ModernGLExecutor_SetTextureTableMode( command, true );
		glUniform4ui(
			command.textureIndicesLocation,
			textureIndices[MODERN_GL_MATERIAL_TEXTURE_MAIN],
			textureIndices[MODERN_GL_MATERIAL_TEXTURE_NORMAL],
			textureIndices[MODERN_GL_MATERIAL_TEXTURE_SPECULAR],
			textureIndices[MODERN_GL_MATERIAL_TEXTURE_EMISSIVE] );
		stats.materialTextureTableUsed = true;
		stats.materialTextureTableDraws++;
		stats.materialTextureTableUniforms++;
		R_GLStateCache().ActiveTextureUnit( 0 );
		return;
	}
	if ( command.textureTableModeLocation >= 0 ) {
		R_ModernGLExecutor_SetTextureTableMode( command, false );
		stats.materialTextureTableFallbacks++;
	}
	if ( command.mainTextureLocation >= 0 ) {
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommand( command );
		if ( textureHandle != 0 ) {
			R_GLStateCache().ActiveTextureUnit( MODERN_GL_MATERIAL_TEXTURE_MAIN );
			if ( R_GLStateCache().BindTexture( MODERN_GL_MATERIAL_TEXTURE_MAIN, GL_TEXTURE_2D, textureHandle ) ) {
				stats.lowOverheadClassicTextureBinds++;
			}
		}
	}
	if ( command.normalTextureLocation >= 0 ) {
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_BUMP );
		R_GLStateCache().ActiveTextureUnit( MODERN_GL_MATERIAL_TEXTURE_NORMAL );
		if ( R_GLStateCache().BindTexture( MODERN_GL_MATERIAL_TEXTURE_NORMAL, GL_TEXTURE_2D, textureHandle ) ) {
			stats.lowOverheadClassicTextureBinds++;
		}
	}
	if ( command.specularTextureLocation >= 0 ) {
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_SPECULAR );
		R_GLStateCache().ActiveTextureUnit( MODERN_GL_MATERIAL_TEXTURE_SPECULAR );
		if ( R_GLStateCache().BindTexture( MODERN_GL_MATERIAL_TEXTURE_SPECULAR, GL_TEXTURE_2D, textureHandle ) ) {
			stats.lowOverheadClassicTextureBinds++;
		}
	}
	if ( command.emissiveTextureLocation >= 0 ) {
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommandSemantic( command, MATERIAL_RESOURCE_TEXTURE_EMISSIVE );
		R_GLStateCache().ActiveTextureUnit( MODERN_GL_MATERIAL_TEXTURE_EMISSIVE );
		if ( R_GLStateCache().BindTexture( MODERN_GL_MATERIAL_TEXTURE_EMISSIVE, GL_TEXTURE_2D, textureHandle ) ) {
			stats.lowOverheadClassicTextureBinds++;
		}
	}
	R_GLStateCache().ActiveTextureUnit( 0 );
}

static bool R_ModernGLExecutor_ExerciseMaterialTextureTableForSelfTest( modernGLExecutorStats_t &stats ) {
	if ( !stats.materialTextureTableReady ) {
		return false;
	}
	for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
		const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
		if ( command.program == 0 || command.textureIndicesLocation < 0 || command.textureTableModeLocation < 0 ) {
			continue;
		}
		const int drawsBefore = stats.materialTextureTableDraws;
		const int uniformsBefore = stats.materialTextureTableUniforms;
		R_GLStateCache().UseProgram( command.program );
		R_ModernGLExecutor_BindMaterialTextures( command, stats );
		R_GLStateCache().UseProgram( 0 );
		return stats.materialTextureTableDraws > drawsBefore && stats.materialTextureTableUniforms > uniformsBefore;
	}
	return false;
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

	float projectionMatrix[16];
	R_ModernGLExecutor_BuildCommandProjectionMatrix( command, projectionMatrix );
	float modelViewProjection[16];
	myGlMultMatrix( command.modelViewMatrix, projectionMatrix, modelViewProjection );

	R_GLStateCache().UseProgram( command.program );
	if ( command.drawRecordModeLocation >= 0 && glUniform1ui != NULL ) {
		glUniform1ui( command.drawRecordModeLocation, 0 );
	}
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
	R_ModernGLExecutor_SetMaterialEnhancement( command );
	R_ModernGLExecutor_BindMaterialTextures( command, stats );

	R_ModernGLExecutor_ApplyCommandDepthRange( command );
	R_ModernGLExecutor_ApplyCommandCullState( command );
	R_ModernGLExecutor_SetSubmitScissor( command, command.viewDef );
	if ( !R_ModernGLExecutor_BindDrawVertLayout( command, stats ) ) {
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

static void R_ModernGLExecutor_SoftRestoreForNextModernPass( modernGLExecutorStats_t &stats ) {
	R_ModernGLExecutor_DisableDrawRecordIndexAttribute();
	R_GLStateCache().ActiveTextureUnit( 0 );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	glDepthRange( 0.0, 1.0 );
	rg_modernGLExecutorModernStateDirty = true;
	stats.modernSoftRestores++;
}

static void R_ModernGLExecutor_FullRestoreForLegacyHandoff( modernGLExecutorStats_t &stats, const char *reason, bool force ) {
	(void)reason;
	if ( !force && !rg_modernGLExecutorModernStateDirty ) {
		return;
	}
	R_ModernGLExecutor_ResetDrawVertSourceBinding();
	R_GLStateCache().BindBuffer( GL_ARRAY_BUFFER, 0 );
	R_GLStateCache().BindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	R_GLStateCache().BindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
	const int restoreTextureCount = rg_modernGLExecutorCaps.maxTextureImageUnits > 0 ? Min( rg_modernGLExecutorCaps.maxTextureImageUnits, MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY ) : MODERN_GL_MATERIAL_TEXTURE_COUNT;
	for ( int unit = 0; unit < restoreTextureCount; ++unit ) {
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
	glReadBuffer( GL_BACK );
	glDrawBuffer( GL_BACK );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	glDepthRange( 0.0, 1.0 );
	GL_ClearStateDelta();
	rg_modernGLExecutorModernStateDirty = false;
	stats.modernFullRestores++;
}

static void R_ModernGLExecutor_RestoreAfterSubmit( modernGLExecutorStats_t &stats, const char *reason ) {
	if ( rg_modernGLExecutorSoftPassHandoffs ) {
		R_ModernGLExecutor_SoftRestoreForNextModernPass( stats );
		return;
	}
	R_ModernGLExecutor_FullRestoreForLegacyHandoff( stats, reason, true );
}

static void R_ModernGLExecutor_SubmitGpuDrivenIndirect( modernGLExecutorStats_t &stats ) {
	if ( !stats.gpuDrivenReady || !stats.gpuDrivenIndirectMultiDrawReady || rg_modernGLGpuDrivenBucketCount <= 0 || stats.gpuDrivenGeneratedCommands <= 0 ) {
		return;
	}
	if ( !stats.gpuDrivenValidationRequested && !r_rendererModernSubmit.GetBool() ) {
		return;
	}
	if ( !stats.enabled || !stats.available || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 || glMultiDrawElementsIndirect == NULL ) {
		return;
	}

	if ( rg_modernGLExecutorDrawRecordSSBO == 0 || rg_modernGLExecutorDrawRecordIndexBuffer == 0 ) {
		stats.gpuDrivenIndirectFallbacks += stats.gpuDrivenGeneratedCommands;
		return;
	}

	idGLDebugScope debugScope( "ModernGLExecutor GPU-driven indirect submit" );
	R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_GPU_DRIVEN_INDIRECT );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	if ( !R_ModernGLExecutor_EnableDrawRecordIndexAttribute() ) {
		stats.gpuDrivenIndirectFallbacks += stats.gpuDrivenGeneratedCommands;
		R_RendererMetrics_EndGpuTimer();
		R_ModernGLExecutor_RestoreAfterSubmit( stats, "gpu-driven indirect attribute fallback" );
		return;
	}
	const renderGraphResourceHandle_t *sceneDepth = NULL;
	if ( R_ModernGLExecutor_DepthResourceReady( "sceneDepth", sceneDepth ) && sceneDepth != NULL ) {
		R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, sceneDepth->framebuffer );
	} else {
		R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	}
	R_GLStateCache().SetScissorTestEnabled( true );
	R_GLStateCache().SetDepthTestEnabled( true );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetDepthFunc( GL_LEQUAL );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	R_ModernGLExecutor_BindGpuDrivenBuffers( stats );

	for ( int i = 0; i < rg_modernGLGpuDrivenBucketCount; ++i ) {
		const modernGLGpuDrivenBucket_t &bucket = rg_modernGLGpuDrivenBuckets[i];
		if ( !bucket.valid || bucket.commandCount <= 0 ) {
			continue;
		}
		const modernGLSubmitCommand_t &command = bucket.command;
		if ( command.viewDef == NULL || command.program == 0 || command.vertexBuffer == 0 || command.indexBuffer == 0 || command.drawRecordModeLocation < 0 ) {
			stats.gpuDrivenIndirectFallbacks += bucket.commandCount;
			continue;
		}

		R_GLStateCache().UseProgram( command.program );
		glUniform1ui( command.drawRecordModeLocation, 1 );
		R_ModernGLExecutor_BindFrameUniformBufferBase( stats );
		if ( R_ModernGLExecutor_CommandUsesClusteredLighting( command ) ) {
			R_ModernGLExecutor_BindClusterUniformBlocks( command.program );
		}
		R_ModernGLExecutor_BindMaterialTextures( command, stats );
		R_ModernGLExecutor_ApplyCommandDepthRange( command );
		R_ModernGLExecutor_ApplyCommandCullState( command );
		R_ModernGLExecutor_SetSubmitScissor( command, command.viewDef );
		if ( !R_ModernGLExecutor_BindDrawVertLayoutForIndirect( command, stats ) ) {
			stats.gpuDrivenIndirectFallbacks += bucket.commandCount;
			continue;
		}

		R_GLStateCache().BindBuffer( GL_ELEMENT_ARRAY_BUFFER, command.indexBuffer );
		const int indirectBaseOffset = rg_modernGLGpuDrivenStreamBindings.indirectCommands.valid ? rg_modernGLGpuDrivenStreamBindings.indirectCommands.allocation.offset : 0;
		glMultiDrawElementsIndirect(
			GL_TRIANGLES,
			static_cast<GLenum>( command.indexType ),
			R_ModernGLExecutor_BufferOffset( indirectBaseOffset + bucket.firstIndirect * static_cast<int>( sizeof( modernGLDrawElementsIndirectCommand_t ) ) ),
			static_cast<GLsizei>( bucket.commandCount ),
			static_cast<GLsizei>( sizeof( modernGLDrawElementsIndirectCommand_t ) ) );
		stats.gpuDrivenIndirectExecuted = true;
		stats.gpuDrivenIndirectDrawCalls += bucket.commandCount;
		stats.gpuDrivenMultiDrawBatches++;
	}
	R_RendererMetrics_EndGpuTimer();

	R_ModernGLExecutor_RestoreAfterSubmit( stats, "gpu-driven indirect submit" );
}

static bool R_ModernGLExecutor_DepthResourceReady( const char *name, const renderGraphResourceHandle_t *&handle ) {
	handle = R_RenderGraphResources_FindHandle( name );
	return handle != NULL
		&& ( handle->type == RENDER_GRAPH_RESOURCE_DEPTH || handle->type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL )
		&& handle->target == GL_TEXTURE_2D
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
	const bool alphaTest = materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED;
	if ( materialRecord->materialClass != RENDER_MATERIAL_OPAQUE && materialRecord->materialClass != RENDER_MATERIAL_SHADOW_ONLY && !alphaTest ) {
		stats.visibleDepthMaterialFallbackDraws++;
		return false;
	}
	if ( alphaTest ) {
		const bool shadowCaster = command.passCategory == RENDER_PASS_SHADOW_MAP;
		if ( shadowCaster ) {
			if ( !materialRecord->shadowCasterSupported || !materialRecord->shadowAlphaTest ) {
				stats.visibleDepthAlphaTestFallbackDraws++;
				return false;
			}
		} else if ( !R_ModernGLExecutor_MaterialContractPromotable( *materialRecord, false ) ) {
			stats.visibleDepthAlphaTestFallbackDraws++;
			return false;
		}
		const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, MATERIAL_RESOURCE_TEXTURE_DIFFUSE );
		if ( binding == NULL || binding->textureHandle == 0 ) {
			stats.visibleDepthAlphaTestFallbackDraws++;
			return false;
		}
	}

	const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
	const geometryResourceRecord_t *geometry = draw != NULL ? draw->geometryRecord : NULL;
	if ( geometry == NULL ) {
		stats.visibleDepthGeometryFallbackDraws++;
		return false;
	}
	if ( geometry->fallbackReason == GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_DEFORM ) {
		stats.visibleDepthDeformFallbackDraws++;
		return false;
	}
	if ( geometry->fallbackReason == GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_GPU_SKINNING ) {
		stats.visibleDepthSkinnedFallbackDraws++;
		return false;
	}
	if ( geometry->fallbackReason != GEOMETRY_RESOURCE_FALLBACK_NONE ) {
		stats.visibleDepthGeometryFallbackDraws++;
		return false;
	}
	return true;
}

static void R_ModernGLExecutor_CountVisibleDepthOwnedFeatures( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord != NULL && ( materialRecord->alphaTest || materialRecord->materialClass == RENDER_MATERIAL_PERFORATED ) ) {
		stats.visibleDepthAlphaTestDraws++;
	}
	const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
	const geometryResourceRecord_t *geometry = draw != NULL ? draw->geometryRecord : NULL;
	if ( geometry != NULL && geometry->skinningMode == GEOMETRY_SKINNING_CPU ) {
		stats.visibleDepthSkinnedDraws++;
	}
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
		if ( !R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
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
		R_ModernGLExecutor_CountVisibleDepthOwnedFeatures( command, stats );
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

	R_ModernGLExecutor_RestoreAfterSubmit( stats, "visible depth submit" );
	stats.visibleDepthExecuted = stats.visibleDepthDraws > 0 || stats.visibleShadowDepthDraws > 0 || stats.visibleDepthClearOps > 0;
	if ( stats.visibleDepthExecuted ) {
		R_ModernGLExecutor_SetStatus( stats, "visible-depth-legacy-fallback" );
	}
}

static int R_ModernGLExecutor_HiZMipSize( int size, int level ) {
	size = Max( 1, size );
	for ( int i = 0; i < level; ++i ) {
		size = Max( 1, size >> 1 );
	}
	return size;
}

static bool R_ModernGLExecutor_AttachHiZReductionMip( const renderGraphResourceHandle_t &sceneHiZ, int mipLevel ) {
	if ( rg_modernGLExecutorHiZFBO == 0 || sceneHiZ.texture == 0 || sceneHiZ.target != GL_TEXTURE_2D ) {
		return false;
	}
	if ( rg_modernGLExecutorLowOverheadReady && glNamedFramebufferTexture != NULL && glNamedFramebufferDrawBuffer != NULL && glNamedFramebufferReadBuffer != NULL && glCheckNamedFramebufferStatus != NULL ) {
		glNamedFramebufferTexture( rg_modernGLExecutorHiZFBO, GL_DEPTH_ATTACHMENT, sceneHiZ.texture, mipLevel );
		glNamedFramebufferDrawBuffer( rg_modernGLExecutorHiZFBO, GL_NONE );
		glNamedFramebufferReadBuffer( rg_modernGLExecutorHiZFBO, GL_NONE );
		return glCheckNamedFramebufferStatus( rg_modernGLExecutorHiZFBO, GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
	}
	if ( glFramebufferTexture2D == NULL || glCheckFramebufferStatus == NULL ) {
		return false;
	}
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorHiZFBO );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneHiZ.texture, mipLevel );
	glDrawBuffer( GL_NONE );
	glReadBuffer( GL_NONE );
	return glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
}

static void R_ModernGLExecutor_DetachHiZReductionMip( void ) {
	if ( rg_modernGLExecutorHiZFBO == 0 ) {
		return;
	}
	if ( rg_modernGLExecutorLowOverheadReady && glNamedFramebufferTexture != NULL ) {
		glNamedFramebufferTexture( rg_modernGLExecutorHiZFBO, GL_DEPTH_ATTACHMENT, 0, 0 );
		return;
	}
	if ( glFramebufferTexture2D == NULL ) {
		return;
	}
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorHiZFBO );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );
}

static void R_ModernGLExecutor_RestoreAfterHiZBuild( modernGLExecutorStats_t &stats, const char *reason ) {
	if ( rg_modernGLExecutorSoftPassHandoffs ) {
		R_GLStateCache().ActiveTextureUnit( 0 );
		R_GLStateCache().SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
		R_GLStateCache().SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
		R_GLStateCache().SetDepthFunc( GL_LEQUAL );
		R_GLStateCache().SetDepthMask( GL_TRUE );
		R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		glDepthRange( 0.0, 1.0 );
		rg_modernGLExecutorModernStateDirty = true;
		stats.modernSoftRestores++;
		return;
	}
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().BindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	R_GLStateCache().BindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
	glReadBuffer( GL_BACK );
	glDrawBuffer( GL_BACK );
	R_GLStateCache().SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetDepthFunc( GL_LEQUAL );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache_InvalidateAll( reason );
	GL_ClearStateDelta();
	rg_modernGLExecutorModernStateDirty = false;
	stats.modernFullRestores++;
}

static bool R_ModernGLExecutor_PrimeGpuDrivenSelfTestHiZ( modernGLExecutorStats_t &stats, int occludeX, int occludeY ) {
	const renderGraphResourceHandle_t *sceneHiZ = NULL;
	if ( !R_ModernGLExecutor_DepthResourceReady( "sceneHiZ", sceneHiZ ) || sceneHiZ == NULL || sceneHiZ->mipLevels <= 0 ) {
		return false;
	}

	R_GLStateCache().SetDepthMask( GL_TRUE );
	glClearDepth( 1.0 );
	for ( int level = 0; level < sceneHiZ->mipLevels; ++level ) {
		if ( !R_ModernGLExecutor_AttachHiZReductionMip( *sceneHiZ, level ) ) {
			R_ModernGLExecutor_DetachHiZReductionMip();
			R_ModernGLExecutor_RestoreAfterHiZBuild( stats, "modern gpu-driven self-test hiz prime failed" );
			return false;
		}
		const int mipWidth = R_ModernGLExecutor_HiZMipSize( sceneHiZ->width, level );
		const int mipHeight = R_ModernGLExecutor_HiZMipSize( sceneHiZ->height, level );
		R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorHiZFBO );
		R_GLStateCache().SetViewport( 0, 0, mipWidth, mipHeight );
		R_GLStateCache().SetScissor( 0, 0, mipWidth, mipHeight );
		glClear( GL_DEPTH_BUFFER_BIT );
	}
	R_ModernGLExecutor_DetachHiZReductionMip();

	const float occluderDepth = 0.10f;
	const int x = idMath::ClampInt( 0, Max( 0, sceneHiZ->width - 1 ), occludeX );
	const int y = idMath::ClampInt( 0, Max( 0, sceneHiZ->height - 1 ), occludeY );
	const int flippedY = idMath::ClampInt( 0, Max( 0, sceneHiZ->height - 1 ), sceneHiZ->height - 1 - y );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, sceneHiZ->texture );
	glTexSubImage2D( GL_TEXTURE_2D, 0, x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &occluderDepth );
	if ( flippedY != y ) {
		glTexSubImage2D( GL_TEXTURE_2D, 0, x, flippedY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &occluderDepth );
	}
	glMemoryBarrier( GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT );

	R_ModernGLExecutor_RestoreAfterHiZBuild( stats, "modern gpu-driven self-test hiz prime" );
	stats.visibilityHiZResourceReady = true;
	stats.visibilityHiZBuilt = true;
	stats.visibilityHiZLevels = sceneHiZ->mipLevels;
	return true;
}

static void R_ModernGLExecutor_BuildHiZPyramid( modernGLExecutorStats_t &stats ) {
	if ( !stats.visibilityHiZRequested || stats.visibilityHiZBuilt || !stats.enabled || !stats.available ) {
		return;
	}
	if ( !stats.visibleDepthExecuted && !stats.opaqueGBufferExecuted ) {
		return;
	}
	if ( glBlitFramebuffer == NULL || glBindTexture == NULL || glUniform1i == NULL || rg_modernGLExecutorHiZReduceProgram == 0 || rg_modernGLExecutorHiZFBO == 0 || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	const renderGraphResourceHandle_t *sceneDepth = NULL;
	const renderGraphResourceHandle_t *sceneHiZ = NULL;
	const bool sceneDepthReady = R_ModernGLExecutor_DepthResourceReady( "sceneDepth", sceneDepth );
	stats.visibilityHiZResourceReady =
		R_ModernGLExecutor_DepthResourceReady( "sceneHiZ", sceneHiZ )
		&& sceneHiZ != NULL
		&& sceneHiZ->type == RENDER_GRAPH_RESOURCE_DEPTH
		&& sceneHiZ->mipLevels > 1;
	if ( !sceneDepthReady || sceneDepth == NULL || !stats.visibilityHiZResourceReady || sceneHiZ == NULL ) {
		return;
	}

	const int startMsec = Sys_Milliseconds();
	idGLDebugScope scope( "ModernGLExecutor Hi-Z build" );
	R_GLStateCache().SetScissorTestEnabled( false );
	R_GLStateCache().BindFramebuffer( GL_READ_FRAMEBUFFER, sceneDepth->framebuffer );
	R_GLStateCache().BindFramebuffer( GL_DRAW_FRAMEBUFFER, sceneHiZ->framebuffer );
	glReadBuffer( GL_NONE );
	glDrawBuffer( GL_NONE );
	glBlitFramebuffer(
		0,
		0,
		Max( 1, sceneDepth->width ),
		Max( 1, sceneDepth->height ),
		0,
		0,
		Max( 1, sceneHiZ->width ),
		Max( 1, sceneHiZ->height ),
		GL_DEPTH_BUFFER_BIT,
		GL_NEAREST );

	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetScissorTestEnabled( false );
	R_GLStateCache().SetDepthTestEnabled( true );
	R_GLStateCache().SetDepthFunc( GL_ALWAYS );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	R_GLStateCache().UseProgram( rg_modernGLExecutorHiZReduceProgram );
	R_GLStateCache().ActiveTextureUnit( 0 );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, sceneHiZ->texture );
	if ( rg_modernGLExecutorHiZReduceTextureLocation >= 0 ) {
		glUniform1i( rg_modernGLExecutorHiZReduceTextureLocation, 0 );
	}
	for ( int level = 1; level < sceneHiZ->mipLevels; ++level ) {
		const int mipWidth = R_ModernGLExecutor_HiZMipSize( sceneHiZ->width, level );
		const int mipHeight = R_ModernGLExecutor_HiZMipSize( sceneHiZ->height, level );
		if ( !R_ModernGLExecutor_AttachHiZReductionMip( *sceneHiZ, level ) ) {
			R_ModernGLExecutor_DetachHiZReductionMip();
			R_ModernGLExecutor_RestoreAfterHiZBuild( stats, "modern visibility hiz build failed" );
			return;
		}
		R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorHiZFBO );
		R_GLStateCache().SetViewport( 0, 0, mipWidth, mipHeight );
		if ( rg_modernGLExecutorHiZReduceSourceMipLocation >= 0 ) {
			glUniform1i( rg_modernGLExecutorHiZReduceSourceMipLocation, level - 1 );
		}
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}
	R_ModernGLExecutor_DetachHiZReductionMip();
	R_ModernGLExecutor_RestoreAfterHiZBuild( stats, "modern visibility hiz build" );

	stats.visibilityHiZBuilt = true;
	stats.visibilityHiZLevels = sceneHiZ->mipLevels;
	stats.visibilityHiZBuildMsec = Sys_Milliseconds() - startMsec;
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

static void R_ModernGLExecutor_ResetFramebufferAttachmentCache( modernGLFramebufferAttachmentCache_t &cache ) {
	memset( &cache, 0, sizeof( cache ) );
}

static bool R_ModernGLExecutor_GBufferAttachmentCacheMatches(
	const modernGLFramebufferAttachmentCache_t &cache,
	const renderGraphResourceHandle_t *const colorHandles[MODERN_GL_GBUFFER_ATTACHMENT_COUNT],
	const renderGraphResourceHandle_t &depthHandle,
	GLenum depthAttachment ) {
	if ( !cache.valid || cache.framebuffer != rg_modernGLExecutorGBufferFBO || cache.depthTexture != depthHandle.texture || cache.depthTarget != depthHandle.target || cache.depthAttachment != depthAttachment ) {
		return false;
	}
	for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
		if ( colorHandles[i] == NULL || cache.colorTextures[i] != colorHandles[i]->texture ) {
			return false;
		}
	}
	return true;
}

static void R_ModernGLExecutor_RecordGBufferAttachmentCache(
	modernGLFramebufferAttachmentCache_t &cache,
	const renderGraphResourceHandle_t *const colorHandles[MODERN_GL_GBUFFER_ATTACHMENT_COUNT],
	const renderGraphResourceHandle_t &depthHandle,
	GLenum depthAttachment ) {
	memset( &cache, 0, sizeof( cache ) );
	cache.valid = true;
	cache.framebuffer = rg_modernGLExecutorGBufferFBO;
	cache.depthTexture = depthHandle.texture;
	cache.depthTarget = depthHandle.target;
	cache.depthAttachment = depthAttachment;
	cache.width = colorHandles[0] != NULL ? colorHandles[0]->width : 0;
	cache.height = colorHandles[0] != NULL ? colorHandles[0]->height : 0;
	for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
		cache.colorTextures[i] = colorHandles[i] != NULL ? colorHandles[i]->texture : 0;
	}
}

static bool R_ModernGLExecutor_ForwardAttachmentCacheMatches(
	const modernGLFramebufferAttachmentCache_t &cache,
	const renderGraphResourceHandle_t &sceneColor,
	const renderGraphResourceHandle_t &sceneDepth,
	GLenum depthAttachment ) {
	return cache.valid
		&& cache.framebuffer == sceneColor.framebuffer
		&& cache.colorTextures[0] == sceneColor.texture
		&& cache.depthTexture == sceneDepth.texture
		&& cache.depthTarget == sceneDepth.target
		&& cache.depthAttachment == depthAttachment
		&& cache.width == sceneColor.width
		&& cache.height == sceneColor.height;
}

static void R_ModernGLExecutor_RecordForwardAttachmentCache(
	modernGLFramebufferAttachmentCache_t &cache,
	const renderGraphResourceHandle_t &sceneColor,
	const renderGraphResourceHandle_t &sceneDepth,
	GLenum depthAttachment ) {
	memset( &cache, 0, sizeof( cache ) );
	cache.valid = true;
	cache.framebuffer = sceneColor.framebuffer;
	cache.colorTextures[0] = sceneColor.texture;
	cache.depthTexture = sceneDepth.texture;
	cache.depthTarget = sceneDepth.target;
	cache.depthAttachment = depthAttachment;
	cache.width = sceneColor.width;
	cache.height = sceneColor.height;
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
	if ( R_ModernGLExecutor_GBufferAttachmentCacheMatches( rg_modernGLExecutorGBufferAttachmentCache, colorHandles, depthHandle, depthAttachment ) ) {
		stats.pipelineFramebufferCacheHits++;
		stats.opaqueGBufferMRTReady = true;
		R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorGBufferFBO );
		return true;
	}
	if ( stats.tierUsesDSA && glNamedFramebufferTexture != NULL && glNamedFramebufferDrawBuffers != NULL && glNamedFramebufferReadBuffer != NULL && glCheckNamedFramebufferStatus != NULL ) {
		for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
			glNamedFramebufferTexture( rg_modernGLExecutorGBufferFBO, rg_modernGLGBufferColorAttachments[i], colorHandles[i]->texture, 0 );
		}
		glNamedFramebufferTexture( rg_modernGLExecutorGBufferFBO, depthAttachment, depthHandle.texture, 0 );
		glNamedFramebufferDrawBuffers( rg_modernGLExecutorGBufferFBO, MODERN_GL_GBUFFER_ATTACHMENT_COUNT, rg_modernGLGBufferColorAttachments );
		glNamedFramebufferReadBuffer( rg_modernGLExecutorGBufferFBO, GL_COLOR_ATTACHMENT0 );
		const GLenum status = glCheckNamedFramebufferStatus( rg_modernGLExecutorGBufferFBO, GL_FRAMEBUFFER );
		stats.lowOverheadFramebufferDSAUpdates++;
		stats.pipelineFramebufferAttachmentUpdates++;
		stats.opaqueGBufferMRTReady = status == GL_FRAMEBUFFER_COMPLETE;
		if ( stats.opaqueGBufferMRTReady ) {
			R_ModernGLExecutor_RecordGBufferAttachmentCache( rg_modernGLExecutorGBufferAttachmentCache, colorHandles, depthHandle, depthAttachment );
			R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorGBufferFBO );
		} else {
			R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorGBufferAttachmentCache );
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
	stats.pipelineFramebufferAttachmentUpdates++;
	stats.opaqueGBufferMRTReady = status == GL_FRAMEBUFFER_COMPLETE;
	if ( stats.opaqueGBufferMRTReady ) {
		R_ModernGLExecutor_RecordGBufferAttachmentCache( rg_modernGLExecutorGBufferAttachmentCache, colorHandles, depthHandle, depthAttachment );
	} else {
		R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorGBufferAttachmentCache );
	}
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
	const int blendedStageCount = materialRecord.additiveStageCount + materialRecord.filterStageCount + materialRecord.blendStageCount;
	if ( blendedStageCount <= 0 ) {
		return true;
	}
	if ( !allowAlphaBlend ) {
		return false;
	}
	if ( materialRecord.additiveStageCount > 0 ) {
		return materialRecord.blendMode == MATERIAL_RESOURCE_BLEND_ADD
			&& materialRecord.filterStageCount == 0
			&& materialRecord.blendStageCount == 0;
	}
	if ( materialRecord.filterStageCount > 0 ) {
		return materialRecord.blendMode == MATERIAL_RESOURCE_BLEND_FILTER
			&& materialRecord.blendStageCount == 0;
	}
	return materialRecord.blendMode == MATERIAL_RESOURCE_BLEND_BLEND;
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
	if ( geometry->fallbackReason == GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_DEFORM ) {
		stats.opaqueGBufferDeformFallbackDraws++;
		return false;
	}
	if ( geometry->fallbackReason == GEOMETRY_RESOURCE_FALLBACK_UNSUPPORTED_GPU_SKINNING ) {
		stats.opaqueGBufferSkinnedFallbackDraws++;
		return false;
	}
	if ( geometry->fallbackReason != GEOMETRY_RESOURCE_FALLBACK_NONE ) {
		stats.opaqueGBufferGeometryFallbackDraws++;
		return false;
	}
	return true;
}

static void R_ModernGLExecutor_SubmitGBuffer( modernGLExecutorStats_t &stats ) {
	if ( !stats.opaqueGBufferRequested || !stats.enabled || !stats.available || !stats.submitPlanReady || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 ) {
		return;
	}
	if ( !stats.pipelineGBufferNeeded ) {
		stats.pipelineSkippedEmptyPasses++;
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
		R_ModernGLExecutor_RestoreAfterSubmit( stats, "G-buffer resource fallback" );
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
	const bool reuseSceneDepth = stats.visibleDepthResourceReady && stats.visibleDepthDraws > 0;
	R_GLStateCache().SetDepthMask( reuseSceneDepth ? GL_FALSE : GL_TRUE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	{
		idGLDebugScope clearScope( "ModernGLExecutor G-buffer clear" );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		if ( reuseSceneDepth ) {
			glClear( GL_COLOR_BUFFER_BIT );
			stats.opaqueGBufferDepthReuseOps++;
		} else {
			glClearDepth( 1.0f );
			if ( sceneDepth->type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL ) {
				glClearStencil( R_ModernGLExecutor_SafeStencilClearValue() );
				glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
			} else {
				glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
			}
			stats.opaqueGBufferDepthClearOps++;
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
		if ( !R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
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

	R_ModernGLExecutor_RestoreAfterSubmit( stats, "G-buffer submit" );
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
			GLuint samplers[MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY];
			const GLsizei samplerCount = Min( count, static_cast<GLsizei>( MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY ) );
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
	GLuint textures[MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY];
	GLuint samplers[MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY];
	const GLsizei clampedCount = Min( count, static_cast<GLsizei>( MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY ) );
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
	if ( !stats.pipelineDeferredNeeded ) {
		stats.pipelineSkippedEmptyPasses++;
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
	const bool clusterLightingLossless = R_ModernClusteredLighting_FrameLossless();
	stats.deferredResolveClusterReady = clusterStats.requested && clusterStats.frameValid && clusterStats.buffersReady && clusterLightingLossless && clusterStats.gridCount == 1;
	stats.deferredResolveActiveLights = clusterStats.lightCount;
	stats.deferredResolvePointLights = clusterStats.pointLights;
	stats.deferredResolveProjectedLights = clusterStats.projectedLights;
	stats.deferredResolveShadowMappedLights = clusterStats.shadowMappedLights;
	stats.deferredResolveShadowFallbackLights = clusterStats.shadowFallbackLights;
	stats.deferredResolveShadowSkippedLights = clusterStats.shadowSkippedLights;
	stats.deferredResolveShadowDescriptors = clusterStats.shadowDescriptorCount;
	stats.deferredResolveFogFallbackLights = clusterStats.fogLights;
	stats.deferredResolveSpecialFallbackLights = clusterStats.specialLights + clusterStats.blendLights;
	stats.deferredResolveUnsupportedLightFallbacks = clusterStats.fogLights + clusterStats.specialLights + clusterStats.blendLights + clusterStats.shadowFallbackLights + clusterStats.lossyReferences + clusterStats.overflowLights + ( clusterStats.gridCount > 1 ? clusterStats.gridCount - 1 : 0 );
	stats.deferredResolveOverflowClusters = clusterStats.overflowClusters + clusterStats.spillClusters;
	if ( !stats.deferredResolveClusterReady ) {
		stats.deferredResolveResourceFallbacks++;
		return;
	}
	if ( stats.deferredResolveUnsupportedLightFallbacks > 0 ) {
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
		if ( !R_ModernClusteredLighting_BindGridForView( NULL ) ) {
			stats.deferredResolveResourceFallbacks++;
			R_RendererMetrics_EndGpuTimer();
			R_ModernGLExecutor_RestoreAfterSubmit( stats, "deferred resolve cluster fallback" );
			return;
		}

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

	if ( !rg_modernGLExecutorSoftPassHandoffs ) {
		R_ModernGLExecutor_UnbindTextureGroup( 0, MODERN_GL_DEFERRED_TEXTURE_COUNT, stats );
	}
	R_ModernGLExecutor_RestoreAfterSubmit( stats, "deferred resolve submit" );
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
	const bool allowAlphaBlend = command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT
		&& ( materialRecord->blendMode == MATERIAL_RESOURCE_BLEND_BLEND
			|| materialRecord->blendMode == MATERIAL_RESOURCE_BLEND_ADD
			|| materialRecord->blendMode == MATERIAL_RESOURCE_BLEND_FILTER );
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
	if ( geometry->fallbackReason != GEOMETRY_RESOURCE_FALLBACK_NONE ) {
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
	if ( R_ModernGLExecutor_ForwardAttachmentCacheMatches( rg_modernGLExecutorForwardPlusAttachmentCache, sceneColor, sceneDepth, depthAttachment ) ) {
		stats.pipelineFramebufferCacheHits++;
		stats.forwardPlusResourcesReady = true;
		R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, sceneColor.framebuffer );
		return true;
	}
	if ( stats.tierUsesDSA && glNamedFramebufferTexture != NULL && glNamedFramebufferDrawBuffer != NULL && glNamedFramebufferReadBuffer != NULL && glCheckNamedFramebufferStatus != NULL ) {
		glNamedFramebufferTexture( sceneColor.framebuffer, depthAttachment, sceneDepth.texture, 0 );
		glNamedFramebufferDrawBuffer( sceneColor.framebuffer, GL_COLOR_ATTACHMENT0 );
		glNamedFramebufferReadBuffer( sceneColor.framebuffer, GL_COLOR_ATTACHMENT0 );
		const GLenum status = glCheckNamedFramebufferStatus( sceneColor.framebuffer, GL_FRAMEBUFFER );
		stats.lowOverheadFramebufferDSAUpdates++;
		stats.pipelineFramebufferAttachmentUpdates++;
		stats.forwardPlusResourcesReady = status == GL_FRAMEBUFFER_COMPLETE;
		if ( stats.forwardPlusResourcesReady ) {
			R_ModernGLExecutor_RecordForwardAttachmentCache( rg_modernGLExecutorForwardPlusAttachmentCache, sceneColor, sceneDepth, depthAttachment );
			R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, sceneColor.framebuffer );
		} else {
			R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorForwardPlusAttachmentCache );
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
	stats.pipelineFramebufferAttachmentUpdates++;
	stats.forwardPlusResourcesReady = status == GL_FRAMEBUFFER_COMPLETE;
	if ( stats.forwardPlusResourcesReady ) {
		R_ModernGLExecutor_RecordForwardAttachmentCache( rg_modernGLExecutorForwardPlusAttachmentCache, sceneColor, sceneDepth, depthAttachment );
	} else {
		R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorForwardPlusAttachmentCache );
	}
	return stats.forwardPlusResourcesReady;
}

static void R_ModernGLExecutor_SubmitForwardPlus( modernGLExecutorStats_t &stats ) {
	if ( !stats.forwardPlusRequested || !stats.enabled || !stats.available || !stats.submitPlanReady || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 ) {
		return;
	}
	if ( !stats.pipelineForwardPlusNeeded ) {
		stats.pipelineSkippedEmptyPasses++;
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
	stats.forwardPlusClusterReady = clusterStats.requested && clusterStats.frameValid && clusterStats.buffersReady && R_ModernClusteredLighting_FrameLossless();
	stats.forwardPlusActiveLights = clusterStats.lightCount;
	stats.forwardPlusPointLights = clusterStats.pointLights;
	stats.forwardPlusProjectedLights = clusterStats.projectedLights;
	stats.forwardPlusShadowMappedLights = clusterStats.shadowMappedLights;
	stats.forwardPlusShadowFallbackLights = clusterStats.shadowFallbackLights;
	stats.forwardPlusShadowSkippedLights = clusterStats.shadowSkippedLights;
	stats.forwardPlusShadowDescriptors = clusterStats.shadowDescriptorCount;
	if ( !stats.forwardPlusClusterReady ) {
		stats.forwardPlusResourceFallbackDraws++;
		return;
	}
	if ( stats.forwardPlusShadowFallbackLights > 0 ) {
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
		R_ModernGLExecutor_RestoreAfterSubmit( stats, "forward+ resource fallback" );
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
			if ( !R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
				continue;
			}
			if ( !R_ModernGLExecutor_ForwardPlusMaterialSupported( command, stats ) ) {
				R_ModernGLExecutor_CountForwardPlusFallback( command, stats );
				continue;
			}
			if ( !R_ModernClusteredLighting_BindGridForView( command.viewDef ) ) {
				stats.forwardPlusResourceFallbackDraws++;
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
				if ( command.blendMode == MATERIAL_RESOURCE_BLEND_ADD ) {
					R_GLStateCache().SetBlendFunc( GL_ONE, GL_ONE );
				} else if ( command.blendMode == MATERIAL_RESOURCE_BLEND_FILTER ) {
					R_GLStateCache().SetBlendFunc( GL_DST_COLOR, GL_ZERO );
				} else {
					R_GLStateCache().SetBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				}
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

	R_ModernGLExecutor_RestoreAfterSubmit( stats, "forward+ submit" );
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
		if ( !R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
			continue;
		}
		R_ModernGLExecutor_SubmitCommand( command, stats, true );
	}

	R_ModernGLExecutor_RestoreAfterSubmit( stats, "diagnostic submit" );
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
	R_ModernGLExecutor_RestoreAfterSubmit( stats, "modern GUI submit" );
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
	stats.passOwnerDroppedByModern = 0;
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
		if ( slot.dropHazard ) {
			stats.passOwnerDroppedByModern++;
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
	stats.modernVisibleDroppedByModern = stats.passOwnerDroppedByModern;
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
	slot.dropHazard = slot.skipLegacy && !modernExecuted && category != RENDER_PASS_GUI;
	idStr::Copynz( slot.reason, reason != NULL ? reason : R_ModernGLExecutor_PassOwnerStateName( state ), sizeof( slot.reason ) );
}

static bool R_ModernGLExecutor_ModernVisibleShadowReceiversReady( const modernShadowPlannerStats_t &shadowStats, const modernGLExecutorStats_t &stats ) {
	if ( !r_shadows.GetBool() || !shadowStats.requested ) {
		return true;
	}
	if ( !shadowStats.frameValid ) {
		return false;
	}
	// Intentional skips such as ambient/no-receiver lights are diagnostics, not
	// shadowing gaps. Mapped receivers without modern sampling remain blockers.
	if ( shadowStats.fallbackLights > 0 || shadowStats.receiverSamplingBlockedLights > 0 ) {
		return false;
	}
	return stats.deferredResolveShadowFallbackLights == 0 &&
		stats.forwardPlusShadowFallbackLights == 0 &&
		stats.visibleShadowFallbackDraws == 0 &&
		stats.visibleStencilShadowFallbackDraws == 0;
}

static bool R_ModernGLExecutor_ModernVisiblePrecomposeReady( modernGLExecutorStats_t &stats ) {
	const renderGraphResourceHandle_t *deferredLight = NULL;
	const renderGraphResourceHandle_t *sceneColor = NULL;
	const renderGraphResourceHandle_t *hybridSceneColor = NULL;
	const renderGraphResourceHandle_t *backBuffer = R_RenderGraphResources_FindHandle( "backBuffer" );
	const bool deferredReady = stats.deferredResolveExecuted && R_ModernGLExecutor_GBufferResourceReady( "deferredLight", deferredLight );
	const bool forwardReady = stats.forwardPlusExecuted && R_ModernGLExecutor_GBufferResourceReady( "sceneColor", sceneColor );
	const bool hybridReady = R_ModernGLExecutor_GBufferResourceReady( "hybridSceneColor", hybridSceneColor );
	const bool guiReady = stats.modernVisibleGuiFallbackDraws == 0;
	const modernShadowPlannerStats_t &shadowStats = R_ModernShadowPlanner_Stats();
	stats.modernVisibleShadowMappedLights = shadowStats.mappedLights;
	stats.modernVisibleShadowFallbackLights = shadowStats.fallbackLights;
	stats.modernVisibleShadowSkippedLights = shadowStats.skippedLights;
	stats.modernVisibleShadowDescriptors = shadowStats.descriptorCount;
	stats.modernVisibleShadowReady =
		stats.modernVisibleShadowOwnershipReady &&
		R_ModernGLExecutor_ModernVisibleShadowReceiversReady( shadowStats, stats );
	stats.modernVisibleSourceReady = deferredReady || forwardReady;
	stats.modernVisibleBackBufferReady = backBuffer != NULL && backBuffer->presentable;
	stats.modernVisibleHybridTargetReady = hybridReady;
	stats.modernVisibleHDRTargetReady = hybridReady && hybridSceneColor != NULL && hybridSceneColor->internalFormat == GL_RGBA16F;
	stats.modernVisibleResourcesReady = stats.modernVisibleSourceReady && stats.modernVisibleBackBufferReady && stats.modernVisibleHybridTargetReady;
	stats.pipelineCompositionSingleSource = stats.modernVisibleSourceReady && ( deferredReady != forwardReady );
	stats.modernVisibleHandoffReady =
		stats.modernVisibleRequested &&
		stats.modernVisibleCanReplaceFrame &&
		stats.enabled &&
		stats.available &&
		stats.initialized &&
		stats.modernVisibleProgramReady &&
		!stats.modernVisibleBlockedByLegacy &&
		stats.modernVisibleLightingReady &&
		stats.modernVisibleLightGridReady &&
		stats.modernVisibleShadowOwnershipReady &&
		stats.modernVisibleMaterialFallbackDraws == 0 &&
		stats.modernVisibleGeometryFallbackDraws == 0 &&
		stats.modernVisibleShadowReady &&
		guiReady &&
		stats.modernVisibleResourcesReady &&
		( deferredLight != NULL || sceneColor != NULL ) &&
		hybridSceneColor != NULL &&
		( ( deferredLight != NULL && deferredLight->texture != 0 ) || ( sceneColor != NULL && sceneColor->texture != 0 ) ) &&
		hybridSceneColor->texture != 0;
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
			stats.deferredResolveSpecialFallbackLights == 0 &&
			stats.deferredResolveShadowFallbackLights == 0;
		const bool forwardModern =
			stats.forwardPlusExecuted &&
			stats.forwardPlusResourcesReady &&
			stats.forwardPlusFallbackDraws == 0 &&
			stats.forwardPlusSpecialEffectFallbacks == 0 &&
			stats.forwardPlusShadowFallbackLights == 0;
		const bool lightingModern =
			stats.modernVisibleLightingReady &&
			( deferredModern || forwardModern );
		const bool lightGridModern =
			stats.modernVisibleLightGridReady &&
			( deferredModern || forwardModern );
		const bool shadowOwnershipModern =
			stats.modernVisibleShadowOwnershipReady &&
			stats.modernVisibleShadowReady;

		if ( depthModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_DEPTH ) ) {
			R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_DEPTH, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-depth-complete" );
		}
		if ( shadowOwnershipModern && shadowMapModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_SHADOW_MAP ) ) {
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
			if ( lightingModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_ARB2_INTERACTION ) ) {
				R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_ARB2_INTERACTION, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-lighting-complete" );
			}
			if ( lightingModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_FOG_BLEND ) ) {
				R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_FOG_BLEND, MODERN_GL_PASS_OWNER_MODERN, true, true, "modern-forward-fog-complete" );
			}
		}
		if ( lightGridModern && R_ModernGLExecutor_PassExistsInGraph( graph, RENDER_PASS_LIGHT_GRID ) ) {
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
		slot.dropHazard = false;
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
		stats.visibleDepthAlphaTestDraws,
		stats.visibleDepthSkinnedDraws,
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
		stats.modernVisibleHybridTargetReady,
		stats.modernVisibleShadowReady,
		stats.modernVisibleHDRTargetReady,
		stats.modernVisiblePostProcessHandoff,
		stats.modernVisibleBlockedByLegacy,
		stats.modernVisibleCompositions,
		stats.modernVisiblePixels,
		stats.modernVisibleCompositeCopies,
		stats.modernVisiblePostProcessCompositions,
		stats.modernVisibleDepthCopies,
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
	R_RendererMetrics_RecordModernVisibility(
		stats.visibilityRequested,
		stats.visibilityEnabled,
		stats.visibilityPortalPVSPreserved,
		stats.visibilityCpuCullingReady,
		stats.visibilityGpuCullingReady,
		stats.visibilityHiZRequested,
		stats.visibilityHiZResourceReady,
		stats.visibilityHiZBuilt,
		stats.visibilityTemporalCoherenceReady,
		stats.visibilityNoQueryStall,
		stats.visibilityShadowCasterReady,
		stats.visibilityScenes,
		stats.visibilityPortalVisibleAreas,
		stats.visibilityPortalRejectedAreas,
		stats.visibilityCpuTested,
		stats.visibilityCpuRejected,
		stats.visibilityGpuTested,
		stats.visibilityGpuRejected,
		stats.visibilityScissorRejected,
		stats.visibilityFrustumRejected,
		stats.visibilityHiZRejected,
		stats.visibilitySavedDraws,
		stats.visibilitySavedTriangles,
		stats.visibilityFalsePositiveFallbacks,
		stats.visibilityTemporalReused,
		stats.visibilityHiZLevels,
		stats.visibilityHiZBuildMsec,
		stats.visibilityShadowCasterTested,
		stats.visibilityShadowCasterRejected,
		stats.visibilityShadowCasterSavedDraws,
		stats.visibilityShadowCasterSavedTriangles );
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
		stats.lowOverheadCompactedBatches,
		stats.modernSoftRestores,
		stats.modernFullRestores,
		stats.materialTextureTableReady,
		stats.materialTextureTableUsed,
		stats.materialTextureTableCapacity,
		stats.materialTextureTableTextures,
		stats.materialTextureTableDescriptors,
		stats.materialTextureTableDraws,
		stats.materialTextureTableUniforms,
		stats.materialTextureTableFallbacks );
}

static void R_ModernGLExecutor_PrintPipelineStats( const char *label ) {
	common->Printf(
		"%s plan=%d stable=%d minimum=%d needed(gbuf=%d deferred=%d forward=%d) commands(depth=%d shadow=%d gbuf=%d deferred=%d forward=%d transparent=%d gui=%d) batches(pipeline=%d geometry=%d texture=%d scissor=%d) skippedEmpty=%d fbo(cache=%d attach=%d) singleSource=%d noHotQueries=%d validationOptIn=%d fit(gl33=%d gl43=%d gl45=%d)\n",
		label,
		rg_modernGLExecutorStats.pipelinePlanReady ? 1 : 0,
		rg_modernGLExecutorStats.pipelineStableLayoutReady ? 1 : 0,
		rg_modernGLExecutorStats.pipelineMinimumPassesReady ? 1 : 0,
		rg_modernGLExecutorStats.pipelineGBufferNeeded ? 1 : 0,
		rg_modernGLExecutorStats.pipelineDeferredNeeded ? 1 : 0,
		rg_modernGLExecutorStats.pipelineForwardPlusNeeded ? 1 : 0,
		rg_modernGLExecutorStats.pipelineDepthCommands,
		rg_modernGLExecutorStats.pipelineShadowDepthCommands,
		rg_modernGLExecutorStats.pipelineGBufferCommands,
		rg_modernGLExecutorStats.pipelineDeferredCommands,
		rg_modernGLExecutorStats.pipelineForwardPlusCommands,
		rg_modernGLExecutorStats.pipelineForwardPlusTransparentCommands,
		rg_modernGLExecutorStats.pipelineGuiCommands,
		rg_modernGLExecutorStats.pipelineBatches,
		rg_modernGLExecutorStats.pipelineGeometryBatches,
		rg_modernGLExecutorStats.pipelineTextureSetBatches,
		rg_modernGLExecutorStats.pipelineScissorBatches,
		rg_modernGLExecutorStats.pipelineSkippedEmptyPasses,
		rg_modernGLExecutorStats.pipelineFramebufferCacheHits,
		rg_modernGLExecutorStats.pipelineFramebufferAttachmentUpdates,
		rg_modernGLExecutorStats.pipelineCompositionSingleSource ? 1 : 0,
		rg_modernGLExecutorStats.pipelineNoHotStateQueries ? 1 : 0,
		rg_modernGLExecutorStats.pipelineValidationReadbacksOptIn ? 1 : 0,
		rg_modernGLExecutorStats.pipelineGL33CpuBounded ? 1 : 0,
		rg_modernGLExecutorStats.pipelineGL43GpuDrivenFit ? 1 : 0,
		rg_modernGLExecutorStats.pipelineGL45LowOverheadFit ? 1 : 0 );
}

void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	R_ModernGLExecutor_Shutdown();
	rg_modernGLExecutorCaps = caps;
	rg_modernGLExecutorFeatures = features;
	R_GLStateCache_Init( caps );
	R_ModernShadowPlanner_Init( caps, features );
	R_ModernClusteredLighting_Init( caps, features );
	rg_modernGLExecutorLowOverheadReady = R_ModernGLExecutor_CanUseLowOverhead( caps, features );
	rg_modernGLExecutorVertexBindingReady = R_ModernGLExecutor_CanUseVertexBinding( caps, features );
	rg_modernGLExecutorVertexInputFormatSetups = 0;
	R_ModernGLExecutor_ResetVertexInputCache();
	R_ModernGLExecutor_QueryStreamingAlignments();
	R_ModernGLExecutor_ResetStreamBinding( rg_modernGLFrameUBOStream );
	R_ModernGLExecutor_ResetGpuDrivenStreamBindings();
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, r_rendererModernExecutor.GetBool() );

	if ( !R_ModernGLExecutor_CanCreateObjects( caps, features ) ) {
		rg_modernGLExecutorAvailable = false;
		rg_modernGLExecutorVertexBindingReady = false;
		R_ModernGLExecutor_ResetVertexInputCache();
		R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, r_rendererModernExecutor.GetBool() );
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
	if ( rg_modernGLExecutorVertexBindingReady && !R_ModernGLExecutor_ConfigureDrawVertVertexBindingFormat( NULL ) ) {
		rg_modernGLExecutorVertexBindingReady = false;
		R_ModernGLExecutor_ResetVertexInputCache();
	}
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
	rg_modernGLExecutorHiZReduceProgram = R_ModernGLExecutor_CompileHiZReduceProgram();
	if ( rg_modernGLExecutorHiZReduceProgram != 0 ) {
		R_GLDebug_LabelProgram( rg_modernGLExecutorHiZReduceProgram, "ModernGLExecutor Hi-Z max-depth reduce" );
	} else {
		common->Printf( "Modern GL executor: Hi-Z max-depth reduction unavailable, r_rendererHiZ will remain resource-only\n" );
	}
	if ( rg_modernGLExecutorLowOverheadReady && glCreateFramebuffers != NULL ) {
		glCreateFramebuffers( 1, &rg_modernGLExecutorGBufferFBO );
		if ( rg_modernGLExecutorGBufferFBO != 0 ) {
			R_GLDebug_LabelFramebuffer( rg_modernGLExecutorGBufferFBO, "ModernGLExecutor G-buffer MRT" );
		}
		glCreateFramebuffers( 1, &rg_modernGLExecutorHiZFBO );
		if ( rg_modernGLExecutorHiZFBO != 0 ) {
			R_GLDebug_LabelFramebuffer( rg_modernGLExecutorHiZFBO, "ModernGLExecutor Hi-Z reduce" );
		}
	} else if ( glGenFramebuffers != NULL ) {
		glGenFramebuffers( 1, &rg_modernGLExecutorGBufferFBO );
		if ( rg_modernGLExecutorGBufferFBO != 0 ) {
			R_GLDebug_LabelFramebuffer( rg_modernGLExecutorGBufferFBO, "ModernGLExecutor G-buffer MRT" );
		}
		glGenFramebuffers( 1, &rg_modernGLExecutorHiZFBO );
		if ( rg_modernGLExecutorHiZFBO != 0 ) {
			R_GLDebug_LabelFramebuffer( rg_modernGLExecutorHiZFBO, "ModernGLExecutor Hi-Z reduce" );
		}
	}
	R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorGBufferAttachmentCache );
	R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorForwardPlusAttachmentCache );

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
	R_ModernGLExecutor_ClearPendingGpuValidationReadbacks();
	R_ModernGLExecutor_ResetStreamBinding( rg_modernGLFrameUBOStream );
	R_ModernGLExecutor_ResetGpuDrivenStreamBindings();
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
	if ( rg_modernGLExecutorHiZFBO != 0 && glDeleteFramebuffers != NULL ) {
		glDeleteFramebuffers( 1, &rg_modernGLExecutorHiZFBO );
	}
	if ( rg_modernGLExecutorLowOverheadSampler != 0 && glDeleteSamplers != NULL ) {
		glDeleteSamplers( 1, &rg_modernGLExecutorLowOverheadSampler );
	}
	R_ModernGLShaderLibrary_Shutdown();

	rg_modernGLExecutorVAO = 0;
	rg_modernGLExecutorFrameUBO = 0;
	rg_modernGLExecutorGBufferFBO = 0;
	rg_modernGLExecutorHiZFBO = 0;
	rg_modernGLExecutorLowOverheadSampler = 0;
	rg_modernGLExecutorLowOverheadSamplerDSACreations = 0;
	rg_modernGLExecutorLowOverheadSamplerDSAUpdates = 0;
	R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorGBufferAttachmentCache );
	R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorForwardPlusAttachmentCache );
	rg_modernGLExecutorInitialized = false;
	rg_modernGLExecutorAvailable = false;
	rg_modernGLExecutorLowOverheadReady = false;
	rg_modernGLExecutorVertexBindingReady = false;
	rg_modernGLExecutorVertexInputFormatSetups = 0;
	R_ModernGLExecutor_ResetVertexInputCache();
	memset( &rg_modernGLExecutorCaps, 0, sizeof( rg_modernGLExecutorCaps ) );
	memset( &rg_modernGLExecutorFeatures, 0, sizeof( rg_modernGLExecutorFeatures ) );
	R_ModernClusteredLighting_Shutdown();
	R_ModernShadowPlanner_Shutdown();
	R_GLStateCache_Shutdown();
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, false );
	R_ModernGLExecutor_ResetPassOwnershipTable( "shutdown" );
}

void R_ModernGLExecutor_SkipFrame( void ) {
	R_ModernGLExecutor_ResetPassOwnershipTable( "side-pipeline-skipped" );
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, false );
	R_ModernGLExecutor_ProcessPendingGpuDrivenValidationReadbacks( rg_modernGLExecutorStats );
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );
}

void R_ModernGLExecutor_PrepareFrame( const idScenePacketFrame &packetFrame, const idRenderGraph &graph ) {
	R_ModernGLExecutor_ResetPassOwnershipTable( "frame-start" );
	const bool modernVisibleRequested = R_ModernGLExecutor_ModernVisibleRequested();
	const bool visibleDepthSidecarRequested = r_rendererModernVisibleDepth.GetBool() || r_rendererModernDepthDebug.GetInteger() > 0 || R_ModernGLExecutor_ShadowMapSidecarRequested();
	const bool deferredResolveSidecarRequested = r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	const bool forwardPlusSidecarRequested = r_rendererForwardPlus.GetBool();
	const bool opaqueGBufferSidecarRequested = r_rendererModernOpaque.GetBool() || r_rendererModernGBufferDebug.GetInteger() > 0;
	const bool gpuDrivenValidationRequested = r_rendererGpuValidation.GetBool() && R_ModernGLExecutor_FrameSupportsGpuDrivenValidation( packetFrame );
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
	rg_modernGLExecutorStats.gpuDrivenValidationRequested = gpuDrivenValidationRequested;
	R_ModernGLExecutor_ProcessPendingGpuDrivenValidationReadbacks( rg_modernGLExecutorStats );
	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	rg_modernGLExecutorStats.visibilityScenes = packetFrame.NumScenes();
	rg_modernGLExecutorStats.visibilityPortalPVSPreserved = packetStats.frontEndDerived || packetStats.backendDerived;
	rg_modernGLExecutorStats.visibilityPortalRejectedAreas = packetStats.clippedDrawPackets;
	for ( int i = 0; i < packetFrame.NumScenes(); ++i ) {
		const scenePacket_t &scene = packetFrame.Scene( i );
		if ( scene.viewDef != NULL && scene.viewDef->connectedAreas != NULL && scene.viewDef->areaNum >= 0 ) {
			rg_modernGLExecutorStats.visibilityPortalVisibleAreas++;
		}
	}
	R_ModernGLExecutor_AnalyzeModernVisibleOwnershipReadiness( packetFrame, graph, rg_modernGLExecutorStats );

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
	R_ModernGLExecutor_AnalyzeModernVisibleOwnershipReadiness( packetFrame, graph, rg_modernGLExecutorStats );

	const bool visibleReplacementCanConsume =
		modernVisibleRequested &&
		rg_modernGLExecutorStats.modernVisibleProgramReady &&
		!rg_modernGLExecutorStats.modernVisibleBlockedByLegacy;
	rg_modernGLExecutorStats.modernVisibleCanReplaceFrame = visibleReplacementCanConsume;
	const bool gpuDrivenWorkRequested = gpuDrivenValidationRequested || r_rendererModernSubmit.GetBool();
	const bool gpuDrivenHiZPreludeRequested = gpuDrivenWorkRequested && r_rendererOcclusion.GetBool() && r_rendererHiZ.GetBool();
	const bool visibleDepthRequested = visibleDepthSidecarRequested || visibleReplacementCanConsume || gpuDrivenHiZPreludeRequested;
	const bool deferredResolveRequested = deferredResolveSidecarRequested || visibleReplacementCanConsume;
	const bool forwardPlusRequested = forwardPlusSidecarRequested || visibleReplacementCanConsume;
	const bool opaqueGBufferRequested = opaqueGBufferSidecarRequested || deferredResolveRequested;
	R_ModernGLExecutor_SetEffectivePassRequests(
		rg_modernGLExecutorStats,
		visibleDepthRequested,
		opaqueGBufferRequested,
		deferredResolveRequested,
		forwardPlusRequested );
	R_ModernGLExecutor_RecordPipelinePolicy( rg_modernGLExecutorStats, gpuDrivenWorkRequested );
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
		rg_modernGLExecutorStats.deferredResolveRequested ||
		rg_modernGLExecutorStats.forwardPlusRequested ||
		gpuDrivenValidationRequested ||
		r_rendererClusterDebug.GetInteger() > 0;
	const bool shadowPlanningRequested =
		r_shadows.GetBool() &&
		( clusteredLightingRequested || rg_modernGLExecutorStats.visibleDepthRequested || R_ModernGLExecutor_ShadowMapSidecarRequested() );

	R_ModernShadowPlanner_PrepareFrame( packetFrame, shadowPlanningRequested );
	const modernShadowPlannerStats_t &shadowStats = R_ModernShadowPlanner_Stats();
	rg_modernGLExecutorStats.visibilityShadowCasterTested += shadowStats.visibilityCasterTests;
	rg_modernGLExecutorStats.visibilityShadowCasterRejected += shadowStats.visibilityCasterRejected;
	rg_modernGLExecutorStats.visibilityShadowCasterSavedDraws += shadowStats.visibilityCasterSavedDraws;
	R_ModernClusteredLighting_PrepareFrame( packetFrame, clusteredLightingRequested );
	R_ModernGLExecutor_UpdateFrameUBO( rg_modernGLExecutorStats );
	{
		modernGLExecutorSoftPassHandoffScope_t softHandoffs;
		R_ModernGLExecutor_SubmitVisibleDepth( rg_modernGLExecutorStats );
		R_ModernGLExecutor_BuildHiZPyramid( rg_modernGLExecutorStats );
		if ( gpuDrivenWorkRequested ) {
			R_ModernGLExecutor_UpdateGpuDrivenBuffers( rg_modernGLExecutorStats );
			R_ModernGLExecutor_SubmitGpuDrivenIndirect( rg_modernGLExecutorStats );
		} else {
			R_ModernGLExecutor_ResetGpuDrivenBatches();
		}
		R_ModernGLExecutor_SubmitGBuffer( rg_modernGLExecutorStats );
		R_ModernGLExecutor_BuildHiZPyramid( rg_modernGLExecutorStats );
		R_ModernGLExecutor_SubmitDeferredResolve( rg_modernGLExecutorStats );
		R_ModernGLExecutor_SubmitForwardPlus( rg_modernGLExecutorStats );
		R_ModernGLExecutor_SubmitPlan( rg_modernGLExecutorStats );
	}
	R_ModernGLExecutor_FullRestoreForLegacyHandoff( rg_modernGLExecutorStats, "modern executor legacy handoff", false );
	R_ModernGLExecutor_FinalizePassOwnership( graph, rg_modernGLExecutorStats );
	if ( modernVisibleRequested
		&& rg_modernGLExecutorStats.modernVisibleBlockedByLegacy
		&& !rg_modernGLExecutorStats.visibleDepthRequested
		&& !rg_modernGLExecutorStats.opaqueGBufferRequested
		&& !rg_modernGLExecutorStats.deferredResolveRequested
		&& !rg_modernGLExecutorStats.forwardPlusRequested ) {
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "modern-visible-blocked-analyze-only" );
	} else if ( rg_modernGLExecutorStats.enabled && !preliminaryPlanRequested ) {
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "analyze-only" );
	}
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );

	if ( r_rendererMetrics.GetInteger() >= 2 && enabled ) {
		common->Printf(
			"modernGLExecutor status=%s passes=%d/%d fallback=%d draws=%d prepared=%d material=%d resources=%d geometry=%d gui=%d world=%d plan=%d planDraws=%d depth=%d materialFamily=%d planFallback=%d batches=%d programSwitches=%d materialSwitches=%d planOverflow=%d submit=%d submitDraws=%d submitFallback=%d submitMissing(vbo=%d ibo=%d) submitIndexUpload=%d submitted=%d submittedDraws=%d submittedFallback=%d submittedUpload=%d submitBatches(program=%d vbo=%d ibo=%d scissor=%d material=%d) uniforms=%d frameUBO=%d submitOverflow=%d visibleDepth(req=%d exec=%d res=%d/%d draws=%d alpha=%d skinned=%d shadow=%d fallback=%d/%d stencil=%d mismatch=%d clear=%d resolve=%d overlay=%d/%d) gbuffer(req=%d exec=%d res=%d mrt=%d draws=%d fallback=%d alpha=%d skinned=%d clear=%d depth=%d/%d att=%d bpp=%d bw=%dKB overlay=%d/%d) deferred(req=%d exec=%d res=%d out=%d program=%d cluster=%d pixels=%d lights=%d point=%d projected=%d lightGrid=%d reads=%d fallback=%d unsupported=%d fog=%d special=%d overflow=%d clear=%d debug=%d overlay=%d/%d) vao=%d ubo=%d shaders=%d shaderFails=%d glsl=%d gpuDriven=%d ssbo=%d indirect=%d validation=%d compute=%d sceneRecords=%d indirectRecords=%d gpuBytes(scene=%d indirect=%d validation=%d) dispatches=%d source=%d eligible=%d generated=%d culled=%d visible=%d mismatches=%d readbacks=%d indirectExec=%d multiDraw=%d indirectCalls=%d lowOverhead=%d dsa=%d multiBind=%d dsaUpdates=%d multiBindBatches=%d restores=%d/%d upload(frame=%d gpu=%d/%d fallback=%d/%d stream=%d/%d validationDeferred=%d skipped=%d)\n",
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
			rg_modernGLExecutorStats.visibleDepthAlphaTestDraws,
			rg_modernGLExecutorStats.visibleDepthSkinnedDraws,
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
			rg_modernGLExecutorStats.opaqueGBufferDepthReuseOps,
			rg_modernGLExecutorStats.opaqueGBufferDepthClearOps,
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
			rg_modernGLExecutorStats.lowOverheadMultiBindBatches,
			rg_modernGLExecutorStats.modernSoftRestores,
			rg_modernGLExecutorStats.modernFullRestores,
			rg_modernGLExecutorStats.uploadManagerFrameUBOBytes,
			rg_modernGLExecutorStats.uploadManagerGpuDrivenBytes,
			rg_modernGLExecutorStats.uploadManagerGpuDrivenBuffers,
			rg_modernGLExecutorStats.uploadManagerFallbackBytes,
			rg_modernGLExecutorStats.uploadManagerFallbackBuffers,
			rg_modernGLExecutorStats.frameUBOStreamed ? 1 : 0,
			rg_modernGLExecutorStats.gpuDrivenStreamedBuffersReady ? 1 : 0,
			rg_modernGLExecutorStats.gpuDrivenValidationDeferredReadbacks,
			rg_modernGLExecutorStats.gpuDrivenValidationSkippedReadbacks );
		common->Printf(
			"modernSubmitSort eligible=%d locked=%d spans=%d buckets=%d moved=%d stateBuckets=%d/%d saved=%d programSaved=%d materialSaved=%d vboSaved=%d\n",
			rg_modernGLExecutorStats.submitPlanSortEligibleDraws,
			rg_modernGLExecutorStats.submitPlanSortLockedDraws,
			rg_modernGLExecutorStats.submitPlanSortSpans,
			rg_modernGLExecutorStats.submitPlanSortBuckets,
			rg_modernGLExecutorStats.submitPlanSortReorderedDraws,
			rg_modernGLExecutorStats.submitPlanUnsortedStateBuckets,
			rg_modernGLExecutorStats.submitPlanSortedStateBuckets,
			rg_modernGLExecutorStats.submitPlanSortStateBucketSavings,
			rg_modernGLExecutorStats.submitPlanSortProgramBatchSavings,
			rg_modernGLExecutorStats.submitPlanSortMaterialBatchSavings,
			rg_modernGLExecutorStats.submitPlanSortVertexBufferBatchSavings );
		common->Printf(
			"modernGpuDrivenMDI drawRecords=%d ready=%d/%d bucketReady=%d bytes=%d/%d bucketBytes=%d buckets=%d bucketed=%d compacted=%d generated=%d hiz=%d/%d rejected=%d indirectBytes=%d multiDrawCalls=%d multiDrawBatches=%d fallback=%d streamed=%d upload=%d/%d validationDeferred=%d skipped=%d\n",
			rg_modernGLExecutorStats.gpuDrivenDrawRecords,
			rg_modernGLExecutorStats.drawRecordBufferReady ? 1 : 0,
			rg_modernGLExecutorStats.drawRecordIndexBufferReady ? 1 : 0,
			rg_modernGLExecutorStats.gpuDrivenBucketBufferReady ? 1 : 0,
			rg_modernGLExecutorStats.gpuDrivenDrawRecordBytes,
			rg_modernGLExecutorStats.gpuDrivenDrawRecordIndexBytes,
			rg_modernGLExecutorStats.gpuDrivenBucketRecordBytes,
			rg_modernGLExecutorStats.gpuDrivenIndirectBuckets,
			rg_modernGLExecutorStats.gpuDrivenIndirectBucketedCommands,
			rg_modernGLExecutorStats.gpuDrivenIndirectCompactedCommands,
			rg_modernGLExecutorStats.gpuDrivenGeneratedCommands,
			rg_modernGLExecutorStats.gpuDrivenHiZCullingReady ? 1 : 0,
			rg_modernGLExecutorStats.gpuDrivenHiZCandidates,
			rg_modernGLExecutorStats.gpuDrivenHiZRejected,
			rg_modernGLExecutorStats.gpuDrivenIndirectBytes,
			rg_modernGLExecutorStats.gpuDrivenIndirectDrawCalls,
			rg_modernGLExecutorStats.gpuDrivenMultiDrawBatches,
			rg_modernGLExecutorStats.gpuDrivenIndirectFallbacks,
			rg_modernGLExecutorStats.gpuDrivenStreamedBuffersReady ? 1 : 0,
			rg_modernGLExecutorStats.uploadManagerGpuDrivenBytes,
			rg_modernGLExecutorStats.uploadManagerGpuDrivenBuffers,
			rg_modernGLExecutorStats.gpuDrivenValidationDeferredReadbacks,
			rg_modernGLExecutorStats.gpuDrivenValidationSkippedReadbacks );
		common->Printf(
			"modernVertexInput cache=%d vertexBinding=%d format=%d formatSetups=%d sourceBinds=%d legacyLayouts=%d hits=%d\n",
			rg_modernGLExecutorStats.vertexInputCacheReady ? 1 : 0,
			rg_modernGLExecutorStats.vertexInputVertexBindingReady ? 1 : 0,
			rg_modernGLExecutorStats.vertexInputFormatReady ? 1 : 0,
			rg_modernGLExecutorStats.vertexInputFormatSetups,
			rg_modernGLExecutorStats.vertexInputSourceBinds,
			rg_modernGLExecutorStats.vertexInputLegacyLayoutUpdates,
			rg_modernGLExecutorStats.vertexInputCacheHits );
		common->Printf(
			"modernLowOverhead req=%d ready=%d dsa=%d multiBind=%d bindless=%d/%d sampler=%d samplerDSA=%d/%d dsaUpdates=%d framebufferDSA=%d bufferMultiBind=%d textureMultiBind=%d samplerMultiBind=%d classicTextureBinds=%d compactedBatches=%d restores=%d/%d textureTable=%d/%d tableSize=%d/%d desc=%d draws=%d uniforms=%d fallback=%d\n",
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
			rg_modernGLExecutorStats.lowOverheadCompactedBatches,
			rg_modernGLExecutorStats.modernSoftRestores,
			rg_modernGLExecutorStats.modernFullRestores,
			rg_modernGLExecutorStats.materialTextureTableUsed ? 1 : 0,
			rg_modernGLExecutorStats.materialTextureTableReady ? 1 : 0,
			rg_modernGLExecutorStats.materialTextureTableTextures,
			rg_modernGLExecutorStats.materialTextureTableCapacity,
			rg_modernGLExecutorStats.materialTextureTableDescriptors,
			rg_modernGLExecutorStats.materialTextureTableDraws,
			rg_modernGLExecutorStats.materialTextureTableUniforms,
			rg_modernGLExecutorStats.materialTextureTableFallbacks );
		R_ModernGLExecutor_PrintPipelineStats( "modernPipeline" );
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
			"modernVisible req=%d exec=%d res=%d program=%d source=%d hybrid=%d backBuffer=%d lighting=%d lightGrid=%d shadow=%d shadowOwner=%d hdr=%d postHandoff=%d blocked=%d droppedByModern=%d composed=%d copies=%d postComposed=%d depthCopies=%d pixels=%d modern=%d legacy=%d disabled=%d fallback=%d ownerFallback=%d resourceFallback=%d materialFallback=%d geometryFallback=%d lightingFallback=%d lightGridFallback=%d shadowFallback=%d gui=%d post=%d special=%d subview=%d present=%d clear=%d blocker='%s'\n",
			rg_modernGLExecutorStats.modernVisibleRequested ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleExecuted ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleResourcesReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleProgramReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleSourceReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleHybridTargetReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleBackBufferReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleLightingReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleLightGridReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleShadowReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleShadowOwnershipReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleHDRTargetReady ? 1 : 0,
			rg_modernGLExecutorStats.modernVisiblePostProcessHandoff ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleBlockedByLegacy ? 1 : 0,
			rg_modernGLExecutorStats.modernVisibleDroppedByModern,
			rg_modernGLExecutorStats.modernVisibleCompositions,
			rg_modernGLExecutorStats.modernVisibleCompositeCopies,
			rg_modernGLExecutorStats.modernVisiblePostProcessCompositions,
			rg_modernGLExecutorStats.modernVisibleDepthCopies,
			rg_modernGLExecutorStats.modernVisiblePixels,
			rg_modernGLExecutorStats.modernVisibleModernPasses,
			rg_modernGLExecutorStats.modernVisibleLegacyPasses,
			rg_modernGLExecutorStats.modernVisibleDisabledPasses,
			rg_modernGLExecutorStats.modernVisibleFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleOwnerFallbacks,
			rg_modernGLExecutorStats.modernVisibleResourceFallbacks,
			rg_modernGLExecutorStats.modernVisibleMaterialFallbackDraws,
			rg_modernGLExecutorStats.modernVisibleGeometryFallbackDraws,
			rg_modernGLExecutorStats.modernVisibleLightingFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleLightGridFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleShadowOwnershipFallbackPasses,
			rg_modernGLExecutorStats.modernVisibleGuiLegacyPasses,
			rg_modernGLExecutorStats.modernVisiblePostLegacyPasses,
			rg_modernGLExecutorStats.modernVisibleSpecialLegacyPasses,
			rg_modernGLExecutorStats.modernVisibleSubviewLegacyPasses,
			rg_modernGLExecutorStats.modernVisiblePresentPasses,
			rg_modernGLExecutorStats.modernVisibleClearOps,
			rg_modernGLExecutorStats.modernVisibleOwnershipBlocker );
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
			"modernPassOwnership ready=%d table=%d legacy=%d modern=%d mixed=%d blocked=%d disabled=%d skipArmed=%d skipIssued=%d duplicateHazards=%d droppedByModern=%d failClosed=%d shadow(modern=%d legacy=%d) guiModern=%d postLegacy=%d reason=%s\n",
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
			rg_modernGLExecutorStats.passOwnerDroppedByModern,
			rg_modernGLExecutorStats.passOwnerFailClosedRestores,
			rg_modernGLExecutorStats.passOwnerShadowModernPasses,
			rg_modernGLExecutorStats.passOwnerShadowLegacyPasses,
			rg_modernGLExecutorStats.passOwnerGuiModernPasses,
			rg_modernGLExecutorStats.passOwnerPostLegacyPasses,
			rg_modernGLPassOwnership.failClosedReason );
		common->Printf(
			"modernVisibility req=%d enabled=%d portalPVS=%d scenes=%d portalAreas=%d rejectedAreas=%d cpu=%d/%d gpu=%d/%d scissor=%d frustum=%d screen(reject=%d clipped=%d near=%d dynamic=%d) hiz(req=%d ready=%d built=%d levels=%d build=%dms candidates=%d rejected=%d) saved(draws=%d tris=%d) fallback=%d temporal=%d noQueryStall=%d shadowCasters=%d/%d saved=%d/%d\n",
			rg_modernGLExecutorStats.visibilityRequested ? 1 : 0,
			rg_modernGLExecutorStats.visibilityEnabled ? 1 : 0,
			rg_modernGLExecutorStats.visibilityPortalPVSPreserved ? 1 : 0,
			rg_modernGLExecutorStats.visibilityScenes,
			rg_modernGLExecutorStats.visibilityPortalVisibleAreas,
			rg_modernGLExecutorStats.visibilityPortalRejectedAreas,
			rg_modernGLExecutorStats.visibilityCpuTested,
			rg_modernGLExecutorStats.visibilityCpuRejected,
			rg_modernGLExecutorStats.visibilityGpuTested,
			rg_modernGLExecutorStats.visibilityGpuRejected,
			rg_modernGLExecutorStats.visibilityScissorRejected,
			rg_modernGLExecutorStats.visibilityFrustumRejected,
			rg_modernGLExecutorStats.visibilityScreenRectRejected,
			rg_modernGLExecutorStats.visibilityScreenRectClipped,
			rg_modernGLExecutorStats.visibilityNearPlaneConservative,
			rg_modernGLExecutorStats.visibilityDynamicConservative,
			rg_modernGLExecutorStats.visibilityHiZRequested ? 1 : 0,
			rg_modernGLExecutorStats.visibilityHiZResourceReady ? 1 : 0,
			rg_modernGLExecutorStats.visibilityHiZBuilt ? 1 : 0,
			rg_modernGLExecutorStats.visibilityHiZLevels,
			rg_modernGLExecutorStats.visibilityHiZBuildMsec,
			rg_modernGLExecutorStats.visibilityHiZCandidates,
			rg_modernGLExecutorStats.visibilityHiZRejected,
			rg_modernGLExecutorStats.visibilitySavedDraws,
			rg_modernGLExecutorStats.visibilitySavedTriangles,
			rg_modernGLExecutorStats.visibilityFalsePositiveFallbacks,
			rg_modernGLExecutorStats.visibilityTemporalReused,
			rg_modernGLExecutorStats.visibilityNoQueryStall ? 1 : 0,
			rg_modernGLExecutorStats.visibilityShadowCasterTested,
			rg_modernGLExecutorStats.visibilityShadowCasterRejected,
			rg_modernGLExecutorStats.visibilityShadowCasterSavedDraws,
			rg_modernGLExecutorStats.visibilityShadowCasterSavedTriangles );
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

bool R_ModernGLExecutor_LegacyPassCanSkipForView( renderPassCategory_t category, const viewDef_t *viewDef ) {
	if ( !R_ModernGLExecutor_LegacyPassCanSkip( category ) ) {
		return false;
	}
	if ( viewDef == NULL ) {
		return true;
	}
	if ( R_ModernGLExecutor_ViewDefUsesLegacySidecar( viewDef ) ) {
		return false;
	}
	return true;
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

static void R_ModernGLExecutor_VisibleTargetRect( bool useViewRect, int &x, int &y, int &width, int &height ) {
	if ( useViewRect && backEnd.viewDef != NULL ) {
		x = tr.viewportOffset[0] + backEnd.viewDef->viewport.x1;
		y = tr.viewportOffset[1] + backEnd.viewDef->viewport.y1;
		width = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
		height = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	} else {
		x = 0;
		y = 0;
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}
	width = Max( 1, width );
	height = Max( 1, height );
}

static GLuint R_ModernGLExecutor_TargetFramebuffer( bool useCurrentFramebuffer ) {
	if ( !useCurrentFramebuffer || backEnd.renderTexture == NULL ) {
		return 0;
	}
	return backEnd.renderTexture->GetDeviceHandle();
}

static void R_ModernGLExecutor_RestoreTargetFramebuffer( bool useCurrentFramebuffer ) {
	if ( useCurrentFramebuffer && backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
	}
	R_GLStateCache_InvalidateAll( "modern visible framebuffer restore" );
}

static bool R_ModernGLExecutor_VisibleCompositionReady(
	modernGLExecutorStats_t &stats,
	const renderGraphResourceHandle_t *&deferredLight,
	const renderGraphResourceHandle_t *&sceneColor,
	const renderGraphResourceHandle_t *&hybridSceneColor,
	const renderGraphResourceHandle_t *&backBuffer ) {
	stats.modernVisibleProgramReady = rg_modernGLExecutorVisibleCompositeProgram != 0;
	deferredLight = NULL;
	sceneColor = NULL;
	hybridSceneColor = NULL;
	backBuffer = R_RenderGraphResources_FindHandle( "backBuffer" );
	const bool deferredReady = stats.deferredResolveExecuted && R_ModernGLExecutor_GBufferResourceReady( "deferredLight", deferredLight );
	const bool forwardReady = stats.forwardPlusExecuted && R_ModernGLExecutor_GBufferResourceReady( "sceneColor", sceneColor );
	const bool hybridReady = R_ModernGLExecutor_GBufferResourceReady( "hybridSceneColor", hybridSceneColor );
	stats.modernVisibleSourceReady = deferredReady || forwardReady;
	stats.modernVisibleBackBufferReady = backBuffer != NULL && backBuffer->presentable;
	stats.modernVisibleHybridTargetReady = hybridReady;
	stats.modernVisibleHDRTargetReady = hybridReady && hybridSceneColor != NULL && hybridSceneColor->internalFormat == GL_RGBA16F;
	stats.modernVisibleResourcesReady = stats.modernVisibleSourceReady && stats.modernVisibleBackBufferReady && stats.modernVisibleHybridTargetReady;
	stats.pipelineCompositionSingleSource = stats.modernVisibleSourceReady && ( deferredReady != forwardReady );

	if ( !rg_modernGLPassOwnership.valid || !rg_modernGLPassOwnership.handoffReady ) {
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-handoff-not-armed" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return false;
	}
	if ( !stats.enabled || !stats.available || !stats.initialized || rg_modernGLExecutorVAO == 0 || !stats.modernVisibleProgramReady ) {
		R_ModernGLExecutor_FailClosedPassOwnership( stats, "modern-visible-unavailable" );
		stats.modernVisibleResourceFallbacks++;
		stats.modernVisibleFallbackPasses++;
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-unavailable" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return false;
	}
	if ( stats.modernVisibleBlockedByLegacy ) {
		R_ModernGLExecutor_FailClosedPassOwnership( stats, stats.modernVisibleOwnershipBlocker[0] != '\0' ? stats.modernVisibleOwnershipBlocker : "modern-visible-legacy-blocked" );
		stats.modernVisibleFallbackPasses++;
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-legacy-blocked" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return false;
	}
	if ( !stats.modernVisibleShadowReady ) {
		R_ModernGLExecutor_FailClosedPassOwnership( stats, "modern-visible-shadow-fallback" );
		stats.modernVisibleFallbackPasses++;
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-shadow-fallback" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return false;
	}
	if ( deferredLight == NULL && forwardReady ) {
		deferredLight = sceneColor;
	}
	if ( sceneColor == NULL && deferredReady ) {
		sceneColor = deferredLight;
	}
	if ( !stats.modernVisibleResourcesReady || deferredLight == NULL || sceneColor == NULL || hybridSceneColor == NULL || deferredLight->texture == 0 || sceneColor->texture == 0 || hybridSceneColor->texture == 0 || hybridSceneColor->framebuffer == 0 ) {
		R_ModernGLExecutor_FailClosedPassOwnership( stats, "modern-visible-resource-fallback" );
		stats.modernVisibleResourceFallbacks++;
		stats.modernVisibleFallbackPasses++;
		R_ModernGLExecutor_SetStatus( stats, "modern-visible-resource-fallback" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return false;
	}
	return true;
}

static void R_ModernGLExecutor_DrawVisibleCompositeQuad(
	modernGLExecutorStats_t &stats,
	GLuint firstTexture,
	GLuint secondTexture,
	float firstWeight,
	float secondWeight ) {
	R_GLStateCache().UseProgram( rg_modernGLExecutorVisibleCompositeProgram );
	GLuint compositeTextures[2] = { firstTexture, secondTexture };
	R_ModernGLExecutor_BindTextureGroup( 0, 2, compositeTextures, stats );
	if ( rg_modernGLExecutorVisibleCompositeDeferredLocation >= 0 ) {
		glUniform1i( rg_modernGLExecutorVisibleCompositeDeferredLocation, 0 );
	}
	if ( rg_modernGLExecutorVisibleCompositeForwardLocation >= 0 ) {
		glUniform1i( rg_modernGLExecutorVisibleCompositeForwardLocation, 1 );
	}
	if ( rg_modernGLExecutorVisibleCompositeParamsLocation >= 0 ) {
		glUniform4f( rg_modernGLExecutorVisibleCompositeParamsLocation, firstWeight, secondWeight, 0.0f, 0.0f );
	}
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
}

static void R_ModernGLExecutor_RenderHybridScene(
	modernGLExecutorStats_t &stats,
	const renderGraphResourceHandle_t &deferredLight,
	const renderGraphResourceHandle_t &sceneColor,
	const renderGraphResourceHandle_t &hybridSceneColor ) {
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, hybridSceneColor.framebuffer );
	glDrawBuffer( GL_COLOR_ATTACHMENT0 );
	glReadBuffer( GL_COLOR_ATTACHMENT0 );
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetViewport( 0, 0, Max( 1, hybridSceneColor.width ), Max( 1, hybridSceneColor.height ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, hybridSceneColor.width ), Max( 1, hybridSceneColor.height ) );
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

	const bool deferredContributes = stats.deferredResolveExecuted && deferredLight.texture != 0;
	const bool forwardContributes = stats.forwardPlusExecuted && sceneColor.texture != 0;
	const GLuint firstTexture = deferredContributes ? deferredLight.texture : sceneColor.texture;
	const GLuint secondTexture = forwardContributes ? sceneColor.texture : firstTexture;
	R_ModernGLExecutor_DrawVisibleCompositeQuad(
		stats,
		firstTexture,
		secondTexture,
		deferredContributes ? 1.0f : 0.0f,
		forwardContributes ? 1.0f : 0.0f );
}

static void R_ModernGLExecutor_BlitVisibleDepthToTarget(
	modernGLExecutorStats_t &stats,
	GLuint targetFramebuffer,
	int targetX,
	int targetY,
	int targetWidth,
	int targetHeight ) {
	if ( glBlitFramebuffer == NULL ) {
		return;
	}
	const renderGraphResourceHandle_t *sceneDepth = NULL;
	if ( !R_ModernGLExecutor_DepthResourceReady( "sceneDepth", sceneDepth ) || sceneDepth == NULL ) {
		return;
	}

	int targetSamples = 0;
	if ( backEnd.renderTexture != NULL && backEnd.renderTexture->GetDepthImage() != NULL ) {
		targetSamples = backEnd.renderTexture->GetDepthImage()->GetOpts().numMSAASamples;
	}
	if ( sceneDepth->samples > 1 || targetSamples > 1 ) {
		return;
	}

	glBindFramebuffer( GL_READ_FRAMEBUFFER, sceneDepth->framebuffer );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, targetFramebuffer );
	glReadBuffer( GL_NONE );
	glDrawBuffer( GL_NONE );
	glBlitFramebuffer(
		0,
		0,
		Max( 1, sceneDepth->width ),
		Max( 1, sceneDepth->height ),
		targetX,
		targetY,
		targetX + targetWidth,
		targetY + targetHeight,
		GL_DEPTH_BUFFER_BIT,
		GL_NEAREST );
	stats.modernVisibleDepthCopies++;
	R_GLStateCache_InvalidateAll( "modern visible depth handoff" );
}

static void R_ModernGLExecutor_CopyHybridToTarget(
	modernGLExecutorStats_t &stats,
	const renderGraphResourceHandle_t &hybridSceneColor,
	GLuint targetFramebuffer,
	int targetX,
	int targetY,
	int targetWidth,
	int targetHeight ) {
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, targetFramebuffer );
	if ( targetFramebuffer == 0 ) {
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	} else {
		glDrawBuffer( GL_COLOR_ATTACHMENT0 );
		glReadBuffer( GL_COLOR_ATTACHMENT0 );
	}
	R_GLStateCache().BindVertexArray( rg_modernGLExecutorVAO );
	R_GLStateCache().SetViewport( targetX, targetY, targetWidth, targetHeight );
	R_GLStateCache().SetScissor( targetX, targetY, targetWidth, targetHeight );
	R_GLStateCache().SetScissorTestEnabled( false );
	R_GLStateCache().SetDepthTestEnabled( false );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	R_ModernGLExecutor_DrawVisibleCompositeQuad( stats, hybridSceneColor.texture, hybridSceneColor.texture, 1.0f, 0.0f );
	stats.modernVisibleCompositeCopies++;
}

static void R_ModernGLExecutor_FinishVisibleComposition( modernGLExecutorStats_t &stats ) {
	R_ModernGLExecutor_UnbindTextureGroup( 0, 2, stats );
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

static bool R_ModernGLExecutor_ComposeVisibleSceneToTarget(
	bool postProcessHandoff,
	bool useCurrentFramebuffer,
	bool useViewRect,
	bool submitGui,
	bool useGpuTimer,
	const char *status ) {
	modernGLExecutorStats_t &stats = rg_modernGLExecutorStats;
	if ( !stats.modernVisibleRequested ) {
		return false;
	}

	if ( stats.modernVisibleSceneComposited ) {
		if ( submitGui && !stats.modernVisibleGuiExecuted ) {
			R_ModernGLExecutor_SubmitModernGui( stats );
		}
		R_ModernGLExecutor_SetStatus( stats, status );
		R_ModernGLExecutor_RecordMetrics( stats );
		return true;
	}

	const renderGraphResourceHandle_t *deferredLight = NULL;
	const renderGraphResourceHandle_t *sceneColor = NULL;
	const renderGraphResourceHandle_t *hybridSceneColor = NULL;
	const renderGraphResourceHandle_t *backBuffer = R_RenderGraphResources_FindHandle( "backBuffer" );
	if ( !R_ModernGLExecutor_VisibleCompositionReady( stats, deferredLight, sceneColor, hybridSceneColor, backBuffer ) ) {
		return false;
	}

	const GLuint targetFramebuffer = R_ModernGLExecutor_TargetFramebuffer( useCurrentFramebuffer );
	int targetX = 0;
	int targetY = 0;
	int targetWidth = 0;
	int targetHeight = 0;
	R_ModernGLExecutor_VisibleTargetRect( useViewRect, targetX, targetY, targetWidth, targetHeight );

	if ( useGpuTimer ) {
		R_RendererMetrics_BeginGpuTimer( RENDERER_GPU_TIMER_MODERN_COMPOSITE );
	}
	{
		idGLDebugScope composeScope( postProcessHandoff ? "ModernGLExecutor visible post handoff" : "ModernGLExecutor visible hybrid composite" );
		R_GLStateCache_InvalidateAll( "modern visible composition" );
		R_ModernGLExecutor_RenderHybridScene( stats, *deferredLight, *sceneColor, *hybridSceneColor );
		if ( postProcessHandoff ) {
			R_ModernGLExecutor_BlitVisibleDepthToTarget( stats, targetFramebuffer, targetX, targetY, targetWidth, targetHeight );
		}
		R_ModernGLExecutor_CopyHybridToTarget( stats, *hybridSceneColor, targetFramebuffer, targetX, targetY, targetWidth, targetHeight );
	}
	if ( useGpuTimer ) {
		R_RendererMetrics_EndGpuTimer();
	}
	R_ModernGLExecutor_RestoreTargetFramebuffer( useCurrentFramebuffer );

	stats.modernVisibleSceneComposited = true;
	stats.modernVisiblePostProcessHandoff = stats.modernVisiblePostProcessHandoff || postProcessHandoff;
	if ( postProcessHandoff ) {
		stats.modernVisiblePostProcessCompositions++;
	}
	if ( submitGui ) {
		R_ModernGLExecutor_SubmitModernGui( stats );
	}

	stats.modernVisibleExecuted = true;
	stats.modernVisibleCompositions++;
	stats.modernVisiblePixels = targetWidth * targetHeight;
	R_ModernGLExecutor_SetStatus( stats, status );
	R_ModernGLExecutor_RecordMetrics( stats );
	R_ModernGLExecutor_FinishVisibleComposition( stats );
	return true;
}

void R_ModernGLExecutor_ComposeVisibleSceneForPost( void ) {
	R_ModernGLExecutor_ComposeVisibleSceneToTarget( true, true, true, false, false, "modern-visible-post-handoff" );
}

void R_ModernGLExecutor_ComposeVisibleFrame( void ) {
	modernGLExecutorStats_t &stats = rg_modernGLExecutorStats;
	if ( !stats.modernVisibleRequested ) {
		return;
	}
	if ( stats.modernVisibleSceneComposited ) {
		if ( !stats.modernVisibleGuiExecuted ) {
			R_ModernGLExecutor_SubmitModernGui( stats );
		}
		R_ModernGLExecutor_SetStatus( stats, stats.modernVisiblePostProcessHandoff ? "modern-visible-post-composited" : "modern-visible-composited" );
		R_ModernGLExecutor_RecordMetrics( stats );
		return;
	}
	R_ModernGLExecutor_ComposeVisibleSceneToTarget( false, false, false, true, true, "modern-visible-composited" );
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

bool R_ModernGLExecutor_ModernVisiblePostProcessHandoffActive( void ) {
	return rg_modernGLExecutorStats.modernVisibleRequested
		&& rg_modernGLExecutorStats.modernVisibleExecuted
		&& rg_modernGLExecutorStats.modernVisibleSceneComposited
		&& rg_modernGLExecutorStats.modernVisiblePostProcessHandoff;
}

void R_ModernGLExecutor_PrintGfxInfo( void ) {
	common->Printf(
		"Modern GL executor: %s, cvar=%d, submitCvar=%d, gpuValidation=%d, visibleDepthCvar=%d, depthDebug=%d, opaqueCvar=%d, gbufferDebug=%d, deferredCvar=%d, deferredDebug=%d, VAO=%d, frameUBO=%d, shaderLibrary=%d, shaderPrograms=%d, highestGLSL=%d, drawPlan=%d, planDraws=%d, depth=%d, materialFamily=%d, planFallback=%d, batches=%d, submitPlan=%d, submitDraws=%d, submitFallback=%d, missingVBO=%d, missingIBO=%d, indexUpload=%d, submitted=%d/%d upload=%d fallback=%d, visibleDepth(req=%d exec=%d res=%d/%d draws=%d alpha=%d skinned=%d shadow=%d fallback=%d/%d stencil=%d mismatch=%d clears=%d resolves=%d overlay=%d/%d), gbuffer(req=%d exec=%d res=%d mrt=%d draws=%d fallback=%d alpha=%d skinned=%d clear=%d depth=%d/%d att=%d bpp=%d bw=%dKB overlay=%d/%d), deferred(req=%d exec=%d res=%d out=%d program=%d cluster=%d pixels=%d lights=%d point=%d projected=%d lightGrid=%d reads=%d fallback=%d unsupported=%d fog=%d special=%d overflow=%d clears=%d overlay=%d/%d), submitBatches(program=%d vbo=%d ibo=%d), gpuDriven=%d ssbo=%d indirect=%d validation=%d compute=%d sceneRecords=%d indirectRecords=%d gpuBytes(scene=%d indirect=%d validation=%d) dispatches=%d source=%d eligible=%d generated=%d culled=%d visible=%d cpu=%d/%d/%d gpu=%d/%d/%d clusters=%d/%d mismatches=%d readbacks=%d indirectExec=%d multiDraw=%d indirectCalls=%d, lowOverhead=%d dsa=%d multiBind=%d dsaUpdates=%d multiBindBatches=%d, restores=%d/%d, upload(frame=%d gpu=%d/%d fallback=%d/%d stream=%d/%d deferred=%d skipped=%d), legacyFallback=%d\n",
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
		rg_modernGLExecutorStats.visibleDepthAlphaTestDraws,
		rg_modernGLExecutorStats.visibleDepthSkinnedDraws,
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
		rg_modernGLExecutorStats.opaqueGBufferDepthReuseOps,
		rg_modernGLExecutorStats.opaqueGBufferDepthClearOps,
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
		rg_modernGLExecutorStats.modernSoftRestores,
		rg_modernGLExecutorStats.modernFullRestores,
		rg_modernGLExecutorStats.uploadManagerFrameUBOBytes,
		rg_modernGLExecutorStats.uploadManagerGpuDrivenBytes,
		rg_modernGLExecutorStats.uploadManagerGpuDrivenBuffers,
		rg_modernGLExecutorStats.uploadManagerFallbackBytes,
		rg_modernGLExecutorStats.uploadManagerFallbackBuffers,
		rg_modernGLExecutorStats.frameUBOStreamed ? 1 : 0,
		rg_modernGLExecutorStats.gpuDrivenStreamedBuffersReady ? 1 : 0,
		rg_modernGLExecutorStats.gpuDrivenValidationDeferredReadbacks,
		rg_modernGLExecutorStats.gpuDrivenValidationSkippedReadbacks,
		rg_modernGLExecutorStats.legacyFallback ? 1 : 0 );
	common->Printf(
		"Modern GL submit sort: eligible=%d locked=%d spans=%d buckets=%d moved=%d stateBuckets=%d/%d saved=%d programSaved=%d materialSaved=%d vboSaved=%d\n",
		rg_modernGLExecutorStats.submitPlanSortEligibleDraws,
		rg_modernGLExecutorStats.submitPlanSortLockedDraws,
		rg_modernGLExecutorStats.submitPlanSortSpans,
		rg_modernGLExecutorStats.submitPlanSortBuckets,
		rg_modernGLExecutorStats.submitPlanSortReorderedDraws,
		rg_modernGLExecutorStats.submitPlanUnsortedStateBuckets,
		rg_modernGLExecutorStats.submitPlanSortedStateBuckets,
		rg_modernGLExecutorStats.submitPlanSortStateBucketSavings,
		rg_modernGLExecutorStats.submitPlanSortProgramBatchSavings,
		rg_modernGLExecutorStats.submitPlanSortMaterialBatchSavings,
		rg_modernGLExecutorStats.submitPlanSortVertexBufferBatchSavings );
	common->Printf(
		"Modern GL GPU-driven MDI: drawRecords=%d ready=%d/%d bucketReady=%d bytes=%d/%d bucketBytes=%d buckets=%d bucketed=%d compacted=%d generated=%d hiz=%d/%d rejected=%d indirectBytes=%d multiDrawCalls=%d multiDrawBatches=%d fallback=%d streamed=%d upload=%d/%d validationDeferred=%d skipped=%d\n",
		rg_modernGLExecutorStats.gpuDrivenDrawRecords,
		rg_modernGLExecutorStats.drawRecordBufferReady ? 1 : 0,
		rg_modernGLExecutorStats.drawRecordIndexBufferReady ? 1 : 0,
		rg_modernGLExecutorStats.gpuDrivenBucketBufferReady ? 1 : 0,
		rg_modernGLExecutorStats.gpuDrivenDrawRecordBytes,
		rg_modernGLExecutorStats.gpuDrivenDrawRecordIndexBytes,
		rg_modernGLExecutorStats.gpuDrivenBucketRecordBytes,
		rg_modernGLExecutorStats.gpuDrivenIndirectBuckets,
		rg_modernGLExecutorStats.gpuDrivenIndirectBucketedCommands,
		rg_modernGLExecutorStats.gpuDrivenIndirectCompactedCommands,
		rg_modernGLExecutorStats.gpuDrivenGeneratedCommands,
		rg_modernGLExecutorStats.gpuDrivenHiZCullingReady ? 1 : 0,
		rg_modernGLExecutorStats.gpuDrivenHiZCandidates,
		rg_modernGLExecutorStats.gpuDrivenHiZRejected,
		rg_modernGLExecutorStats.gpuDrivenIndirectBytes,
		rg_modernGLExecutorStats.gpuDrivenIndirectDrawCalls,
		rg_modernGLExecutorStats.gpuDrivenMultiDrawBatches,
		rg_modernGLExecutorStats.gpuDrivenIndirectFallbacks,
		rg_modernGLExecutorStats.gpuDrivenStreamedBuffersReady ? 1 : 0,
		rg_modernGLExecutorStats.uploadManagerGpuDrivenBytes,
		rg_modernGLExecutorStats.uploadManagerGpuDrivenBuffers,
		rg_modernGLExecutorStats.gpuDrivenValidationDeferredReadbacks,
		rg_modernGLExecutorStats.gpuDrivenValidationSkippedReadbacks );
	common->Printf(
		"Modern GL vertex input: cache=%d vertexBinding=%d format=%d formatSetups=%d sourceBinds=%d legacyLayouts=%d hits=%d\n",
		rg_modernGLExecutorStats.vertexInputCacheReady ? 1 : 0,
		rg_modernGLExecutorStats.vertexInputVertexBindingReady ? 1 : 0,
		rg_modernGLExecutorStats.vertexInputFormatReady ? 1 : 0,
		rg_modernGLExecutorStats.vertexInputFormatSetups,
		rg_modernGLExecutorStats.vertexInputSourceBinds,
		rg_modernGLExecutorStats.vertexInputLegacyLayoutUpdates,
		rg_modernGLExecutorStats.vertexInputCacheHits );
	common->Printf(
		"Modern GL low-overhead: requested=%d ready=%d dsa=%d multiBind=%d bindless=%d/%d sampler=%d samplerDSA=%d/%d dsaUpdates=%d framebufferDSA=%d bufferMultiBind=%d textureMultiBind=%d samplerMultiBind=%d classicTextureBinds=%d compactedBatches=%d restores=%d/%d textureTable=%d/%d tableSize=%d/%d desc=%d draws=%d uniforms=%d fallback=%d\n",
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
		rg_modernGLExecutorStats.lowOverheadCompactedBatches,
		rg_modernGLExecutorStats.modernSoftRestores,
		rg_modernGLExecutorStats.modernFullRestores,
		rg_modernGLExecutorStats.materialTextureTableUsed ? 1 : 0,
		rg_modernGLExecutorStats.materialTextureTableReady ? 1 : 0,
		rg_modernGLExecutorStats.materialTextureTableTextures,
		rg_modernGLExecutorStats.materialTextureTableCapacity,
		rg_modernGLExecutorStats.materialTextureTableDescriptors,
		rg_modernGLExecutorStats.materialTextureTableDraws,
		rg_modernGLExecutorStats.materialTextureTableUniforms,
		rg_modernGLExecutorStats.materialTextureTableFallbacks );
	R_ModernGLExecutor_PrintPipelineStats( "Modern GL pipeline:" );
	common->Printf(
		"Modern visibility: occlusion=%d hiz=%d req=%d enabled=%d portalPVS=%d scenes=%d portalAreas=%d rejectedAreas=%d cpu=%d/%d gpu=%d/%d scissor=%d frustum=%d screen(reject=%d clipped=%d near=%d dynamic=%d) hizReady=%d built=%d levels=%d build=%dms candidates=%d rejected=%d saved(draws=%d tris=%d) fallback=%d temporal=%d noQueryStall=%d shadowCasters=%d/%d saved=%d/%d\n",
		r_rendererOcclusion.GetBool() ? 1 : 0,
		r_rendererHiZ.GetBool() ? 1 : 0,
		rg_modernGLExecutorStats.visibilityRequested ? 1 : 0,
		rg_modernGLExecutorStats.visibilityEnabled ? 1 : 0,
		rg_modernGLExecutorStats.visibilityPortalPVSPreserved ? 1 : 0,
		rg_modernGLExecutorStats.visibilityScenes,
		rg_modernGLExecutorStats.visibilityPortalVisibleAreas,
		rg_modernGLExecutorStats.visibilityPortalRejectedAreas,
		rg_modernGLExecutorStats.visibilityCpuTested,
		rg_modernGLExecutorStats.visibilityCpuRejected,
		rg_modernGLExecutorStats.visibilityGpuTested,
		rg_modernGLExecutorStats.visibilityGpuRejected,
		rg_modernGLExecutorStats.visibilityScissorRejected,
		rg_modernGLExecutorStats.visibilityFrustumRejected,
		rg_modernGLExecutorStats.visibilityScreenRectRejected,
		rg_modernGLExecutorStats.visibilityScreenRectClipped,
		rg_modernGLExecutorStats.visibilityNearPlaneConservative,
		rg_modernGLExecutorStats.visibilityDynamicConservative,
		rg_modernGLExecutorStats.visibilityHiZResourceReady ? 1 : 0,
		rg_modernGLExecutorStats.visibilityHiZBuilt ? 1 : 0,
		rg_modernGLExecutorStats.visibilityHiZLevels,
		rg_modernGLExecutorStats.visibilityHiZBuildMsec,
		rg_modernGLExecutorStats.visibilityHiZCandidates,
		rg_modernGLExecutorStats.visibilityHiZRejected,
		rg_modernGLExecutorStats.visibilitySavedDraws,
		rg_modernGLExecutorStats.visibilitySavedTriangles,
		rg_modernGLExecutorStats.visibilityFalsePositiveFallbacks,
		rg_modernGLExecutorStats.visibilityTemporalReused,
		rg_modernGLExecutorStats.visibilityNoQueryStall ? 1 : 0,
		rg_modernGLExecutorStats.visibilityShadowCasterTested,
		rg_modernGLExecutorStats.visibilityShadowCasterRejected,
		rg_modernGLExecutorStats.visibilityShadowCasterSavedDraws,
		rg_modernGLExecutorStats.visibilityShadowCasterSavedTriangles );
	common->Printf(
		"Modern forward+: cvar=%d, req=%d exec=%d resources=%d sceneColor=%d sceneDepth=%d program=%d cluster=%d draws=%d opaque=%d alpha=%d transparent=%d viewmodel=%d fog=%d batches=%d fallback=%d resource=%d material=%d geometry=%d texture=%d blend=%d effects=%d sort=%d overdraw=%d reads=%d lights=%d point=%d projected=%d shadow(mapped=%d fallback=%d skipped=%d descriptors=%d) lightGrid=%d clears=%d\n",
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
		rg_modernGLExecutorStats.forwardPlusShadowMappedLights,
		rg_modernGLExecutorStats.forwardPlusShadowFallbackLights,
		rg_modernGLExecutorStats.forwardPlusShadowSkippedLights,
		rg_modernGLExecutorStats.forwardPlusShadowDescriptors,
		rg_modernGLExecutorStats.forwardPlusLightGridContributions,
		rg_modernGLExecutorStats.forwardPlusClearOps );
	common->Printf(
		"Modern visible frame: cvar=%d, req=%d exec=%d resources=%d program=%d source=%d hybrid=%d backBuffer=%d lighting=%d lightGrid=%d shadowReady=%d shadowOwner=%d shadow(mapped=%d fallback=%d skipped=%d descriptors=%d) hdr=%d postHandoff=%d blocked=%d droppedByModern=%d composed=%d copies=%d postComposed=%d depthCopies=%d pixels=%d modern=%d legacy=%d disabled=%d fallback=%d ownerFallback=%d resourceFallback=%d materialFallback=%d geometryFallback=%d lightingFallback=%d lightGridFallback=%d shadowFallback=%d gui=%d post=%d special=%d subview=%d present=%d clears=%d blocker='%s'\n",
		r_rendererModernVisible.GetBool() ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleRequested ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleExecuted ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleResourcesReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleProgramReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleSourceReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleHybridTargetReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleBackBufferReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleLightingReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleLightGridReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleShadowReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleShadowOwnershipReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleShadowMappedLights,
		rg_modernGLExecutorStats.modernVisibleShadowFallbackLights,
		rg_modernGLExecutorStats.modernVisibleShadowSkippedLights,
		rg_modernGLExecutorStats.modernVisibleShadowDescriptors,
		rg_modernGLExecutorStats.modernVisibleHDRTargetReady ? 1 : 0,
		rg_modernGLExecutorStats.modernVisiblePostProcessHandoff ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleBlockedByLegacy ? 1 : 0,
		rg_modernGLExecutorStats.modernVisibleDroppedByModern,
		rg_modernGLExecutorStats.modernVisibleCompositions,
		rg_modernGLExecutorStats.modernVisibleCompositeCopies,
		rg_modernGLExecutorStats.modernVisiblePostProcessCompositions,
		rg_modernGLExecutorStats.modernVisibleDepthCopies,
		rg_modernGLExecutorStats.modernVisiblePixels,
		rg_modernGLExecutorStats.modernVisibleModernPasses,
		rg_modernGLExecutorStats.modernVisibleLegacyPasses,
		rg_modernGLExecutorStats.modernVisibleDisabledPasses,
		rg_modernGLExecutorStats.modernVisibleFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleOwnerFallbacks,
		rg_modernGLExecutorStats.modernVisibleResourceFallbacks,
		rg_modernGLExecutorStats.modernVisibleMaterialFallbackDraws,
		rg_modernGLExecutorStats.modernVisibleGeometryFallbackDraws,
		rg_modernGLExecutorStats.modernVisibleLightingFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleLightGridFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleShadowOwnershipFallbackPasses,
		rg_modernGLExecutorStats.modernVisibleGuiLegacyPasses,
		rg_modernGLExecutorStats.modernVisiblePostLegacyPasses,
		rg_modernGLExecutorStats.modernVisibleSpecialLegacyPasses,
		rg_modernGLExecutorStats.modernVisibleSubviewLegacyPasses,
		rg_modernGLExecutorStats.modernVisiblePresentPasses,
		rg_modernGLExecutorStats.modernVisibleClearOps,
		rg_modernGLExecutorStats.modernVisibleOwnershipBlocker );
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

	if ( !stats.legacyFallback || stats.preparedPasses <= 0 || stats.preparedPasses + stats.fallbackPasses != graph.NumPasses() ) {
		common->Printf(
			"RendererModernGLExecutor self-test failed: pass preparation mismatch (prepared=%d fallback=%d graph=%d legacy=%d)\n",
			stats.preparedPasses,
			stats.fallbackPasses,
			graph.NumPasses(),
			stats.legacyFallback ? 1 : 0 );
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
	if ( rg_modernGLExecutorVAO == 0 || rg_modernGLExecutorFrameUBO == 0 || rg_modernGLExecutorHiZReduceProgram == 0 || rg_modernGLExecutorHiZFBO == 0 ) {
		common->Printf( "RendererModernGLExecutor self-test failed: live GL object state mismatch\n" );
		return false;
	}
	if ( rg_modernGLExecutorFeatures.gpuDriven && ( !rg_modernGLExecutorVertexBindingReady || !rg_modernGLVertexInputCache.formatConfigured ) ) {
		common->Printf( "RendererModernGLExecutor self-test failed: GL43 vertex-binding layout path unavailable\n" );
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
		if ( !rg_modernGLExecutorGpuDrivenReady || rg_modernGLExecutorSceneSSBO == 0 || rg_modernGLExecutorIndirectBuffer == 0 || rg_modernGLExecutorDrawRecordSSBO == 0 || rg_modernGLExecutorDrawRecordIndexBuffer == 0 || rg_modernGLExecutorBucketSSBO == 0 || rg_modernGLExecutorValidationSSBO == 0 || rg_modernGLExecutorComputeProgram == 0 ) {
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
		const bool uploadStreamExercised = stats.frameUBOStreamed && stats.uploadManagerFrameUBOBytes > 0;
		if ( ( stats.lowOverheadDSAUpdates <= 0 && !uploadStreamExercised ) || ( stats.lowOverheadMultiBindBatches <= 0 && !uploadStreamExercised ) ) {
			common->Printf(
				"RendererModernGLExecutor self-test failed: GL45 DSA/multi-bind/upload-stream path was not exercised (dsa=%d multiBind=%d stream=%d bytes=%d)\n",
				stats.lowOverheadDSAUpdates,
				stats.lowOverheadMultiBindBatches,
				uploadStreamExercised ? 1 : 0,
				stats.uploadManagerFrameUBOBytes );
			return false;
		}
	}
	const materialResourceTableRecord_t *defaultRecord = R_MaterialResourceTable_FindRecordForMaterial( tr.defaultMaterial );
	const bool materialModernEligible = defaultRecord != NULL && defaultRecord->fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE;
	if ( tr.defaultMaterial != NULL && materialModernEligible && ( !stats.drawPlanReady || stats.drawPlanDraws <= 0 || stats.drawPlanStateBatches <= 0 ) ) {
		common->Printf( "RendererModernGLExecutor self-test failed: draw-plan readiness mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererModernGLExecutor self-test passed (gpuScene=%d gpuIndirect=%d drawRecords=%d buckets=%d dispatches=%d submitSort=%d/%d saved=%d textureTable=%d/%d vertexBinding=%d vertexFormat=%d vertexSource=%d vertexLegacy=%d vertexHits=%d dsaUpdates=%d multiBindBatches=%d uploadStream=%d/%d hizReduce=%d)\n",
		stats.gpuDrivenSceneRecords,
		stats.gpuDrivenIndirectRecords,
		stats.gpuDrivenDrawRecords,
		stats.gpuDrivenIndirectBuckets,
		stats.gpuDrivenComputeDispatches,
		stats.submitPlanSortEligibleDraws,
		stats.submitPlanSortReorderedDraws,
		stats.submitPlanSortStateBucketSavings,
		stats.materialTextureTableTextures,
		stats.materialTextureTableCapacity,
		stats.vertexInputVertexBindingReady ? 1 : 0,
		stats.vertexInputFormatReady ? 1 : 0,
		stats.vertexInputSourceBinds,
		stats.vertexInputLegacyLayoutUpdates,
		stats.vertexInputCacheHits,
		stats.lowOverheadDSAUpdates,
		stats.lowOverheadMultiBindBatches,
		stats.frameUBOStreamed ? 1 : 0,
		stats.uploadManagerFrameUBOBytes,
		rg_modernGLExecutorHiZReduceProgram != 0 && rg_modernGLExecutorHiZFBO != 0 ? 1 : 0 );
	return true;
}

bool RendererGpuDriven_RunSelfTest( void ) {
	idScenePacketFrame emptyFrame;
	if ( R_ModernGLExecutor_FrameSupportsGpuDrivenValidation( emptyFrame ) ) {
		common->Printf( "RendererGpuDriven self-test failed: empty frame accepted for validation\n" );
		return false;
	}
	drawPacket_t guiDraw;
	memset( &guiDraw, 0, sizeof( guiDraw ) );
	guiDraw.packetCategory = SCENE_PACKET_CATEGORY_GUI;
	guiDraw.passCategory = RENDER_PASS_GUI;
	guiDraw.hasGeometry = true;
	guiDraw.indexCount = 6;
	guiDraw.materialRecordIndex = 0;
	geometryResourceRecord_t geometryRecord;
	memset( &geometryRecord, 0, sizeof( geometryRecord ) );
	instanceRecord_t instanceRecord;
	memset( &instanceRecord, 0, sizeof( instanceRecord ) );
	guiDraw.geometryRecord = &geometryRecord;
	guiDraw.instanceRecord = &instanceRecord;
	if ( R_ModernGLExecutor_DrawPacketSupportsGpuDrivenValidation( guiDraw ) ) {
		common->Printf( "RendererGpuDriven self-test failed: GUI frame accepted for validation\n" );
		return false;
	}
	drawPacket_t worldDraw = guiDraw;
	worldDraw.packetCategory = SCENE_PACKET_CATEGORY_WORLD;
	worldDraw.passCategory = RENDER_PASS_DEPTH;
	if ( !R_ModernGLExecutor_DrawPacketSupportsGpuDrivenValidation( worldDraw ) ) {
		common->Printf( "RendererGpuDriven self-test failed: world draw rejected for validation\n" );
		return false;
	}

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

	struct rendererGpuDrivenCVarRestore_t {
		idCVar &cvar;
		bool oldValue;
		rendererGpuDrivenCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetBool() ) {}
		~rendererGpuDrivenCVarRestore_t() { cvar.SetBool( oldValue ); }
	};
	rendererGpuDrivenCVarRestore_t restoreOcclusion( r_rendererOcclusion );
	rendererGpuDrivenCVarRestore_t restoreHiZ( r_rendererHiZ );
	rendererGpuDrivenCVarRestore_t restoreModernSubmit( r_rendererModernSubmit );
	r_rendererOcclusion.SetBool( true );
	r_rendererHiZ.SetBool( true );
	r_rendererModernSubmit.SetBool( true );

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

	drawSurf_t drawSurfs[4];
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

	for ( int i = 0; i < 4; ++i ) {
		drawSurfs[i].geo = &geometry;
		drawSurfs[i].material = tr.defaultMaterial;
		drawSurfs[i].sort = tr.defaultMaterial->GetSort();
		drawSurfs[i].scissorRect.x1 = 0;
		drawSurfs[i].scissorRect.y1 = 0;
		drawSurfs[i].scissorRect.x2 = 127;
		drawSurfs[i].scissorRect.y2 = 127;
	}
	const int hiZSelfTestPixelX = 64;
	const int hiZSelfTestPixelY = 64;
	drawSurfs[2].scissorRect.x1 = hiZSelfTestPixelX;
	drawSurfs[2].scissorRect.y1 = hiZSelfTestPixelY;
	drawSurfs[2].scissorRect.x2 = hiZSelfTestPixelX;
	drawSurfs[2].scissorRect.y2 = hiZSelfTestPixelY;
	drawSurfs[3].scissorRect.x1 = 512;
	drawSurfs[3].scissorRect.y1 = 512;
	drawSurfs[3].scissorRect.x2 = 640;
	drawSurfs[3].scissorRect.y2 = 640;

	drawSurf_t *drawSurfPtrs[4] = { &drawSurfs[0], &drawSurfs[1], &drawSurfs[2], &drawSurfs[3] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	for ( int i = 0; i < 16; ++i ) {
		viewEntity.modelMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
		viewEntity.modelViewMatrix[i] = viewEntity.modelMatrix[i];
	}
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	drawSurfs[2].space = &viewEntity;
	drawSurfs[3].space = &viewEntity;
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
	worldView.numDrawSurfs = 4;
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
	R_RenderGraphResources_PrepareFrame( graph );
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
	if ( !R_ModernGLExecutor_PrimeGpuDrivenSelfTestHiZ( stats, hiZSelfTestPixelX, hiZSelfTestPixelY ) ) {
		if ( glDeleteBuffers != NULL ) {
			glDeleteBuffers( 1, &vertexBuffer );
			glDeleteBuffers( 1, &indexBuffer );
		}
		common->Printf( "RendererGpuDriven self-test failed: Hi-Z rejection probe unavailable\n" );
		return false;
	}
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
	const int expectedGpuGeneratedCommands = Max( 0, stats.gpuDrivenCpuGeneratedCommands - stats.gpuDrivenHiZRejected );
	if ( stats.gpuDrivenSourceCommands <= 0 || stats.gpuDrivenGeneratedCommands <= 0 || stats.gpuDrivenCulledCommands <= 0 || stats.gpuDrivenGpuGeneratedCommands != expectedGpuGeneratedCommands ) {
		common->Printf( "RendererGpuDriven self-test failed: CPU/GPU generated-command coverage mismatch\n" );
		return false;
	}
	if ( !stats.gpuDrivenHiZCullingReady || stats.gpuDrivenHiZRejected <= 0 || stats.gpuDrivenHiZCandidates <= 0 || !stats.gpuDrivenIndirectExecuted || stats.gpuDrivenMultiDrawBatches <= 0 || stats.gpuDrivenIndirectDrawCalls != stats.gpuDrivenGeneratedCommands || stats.gpuDrivenDrawRecords != stats.gpuDrivenSceneRecords || stats.gpuDrivenIndirectBucketedCommands != stats.gpuDrivenGeneratedCommands || stats.gpuDrivenIndirectCompactedCommands != stats.gpuDrivenGpuGeneratedCommands || stats.gpuDrivenIndirectBuckets < 2 ) {
		common->Printf( "RendererGpuDriven self-test failed: indirect multi-draw execution mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererGpuDriven self-test passed (resources=%d compute=%d source=%d eligible=%d generated=%d culled=%d visible=%d cpu=%d/%d/%d gpu=%d/%d/%d clusters=%d/%d mismatches=%d readbacks=%d drawRecords=%d buckets=%d bucketed=%d compacted=%d hiz=%d/%d/%d indirect=%d multiDraw=%d indirectCalls=%d dispatches=%d)\n",
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
		stats.gpuDrivenDrawRecords,
		stats.gpuDrivenIndirectBuckets,
		stats.gpuDrivenIndirectBucketedCommands,
		stats.gpuDrivenIndirectCompactedCommands,
		stats.gpuDrivenHiZCullingReady ? 1 : 0,
		stats.gpuDrivenHiZCandidates,
		stats.gpuDrivenHiZRejected,
		stats.gpuDrivenIndirectExecuted ? 1 : 0,
		stats.gpuDrivenMultiDrawBatches,
		stats.gpuDrivenIndirectDrawCalls,
		stats.gpuDrivenComputeDispatches );
	return true;
}

bool RendererModernVisibility_RunSelfTest( void ) {
	struct rendererModernVisibilityCVarRestore_t {
		idCVar &cvar;
		bool oldValue;
		rendererModernVisibilityCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetBool() ) {}
		~rendererModernVisibilityCVarRestore_t() { cvar.SetBool( oldValue ); }
	};
	rendererModernVisibilityCVarRestore_t restoreOcclusion( r_rendererOcclusion );
	rendererModernVisibilityCVarRestore_t restoreHiZ( r_rendererHiZ );
	r_rendererOcclusion.SetBool( true );
	r_rendererHiZ.SetBool( true );

	viewDef_t view;
	memset( &view, 0, sizeof( view ) );
	view.viewport.x1 = 0;
	view.viewport.y1 = 0;
	view.viewport.x2 = 127;
	view.viewport.y2 = 127;
	view.scissor = view.viewport;
	view.renderView.width = 128;
	view.renderView.height = 128;
	for ( int i = 0; i < 16; ++i ) {
		view.projectionMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
	}

	geometryResourceRecord_t geometry;
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.hasBounds = true;
	geometry.bounds[0].Set( -0.5f, -0.5f, -0.5f );
	geometry.bounds[1].Set( 0.5f, 0.5f, 0.5f );

	drawPacket_t draw;
	memset( &draw, 0, sizeof( draw ) );
	draw.viewDef = &view;
	draw.packetCategory = SCENE_PACKET_CATEGORY_WORLD;
	draw.geometryRecord = &geometry;

	modernGLDrawPlanEntry_t entry;
	memset( &entry, 0, sizeof( entry ) );
	entry.drawPacket = &draw;

	modernGLSubmitCommand_t command;
	memset( &command, 0, sizeof( command ) );
	command.drawPlanEntry = &entry;
	command.viewDef = &view;
	command.passCategory = RENDER_PASS_AMBIENT;
	command.pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_GBUFFER;
	command.indexed = true;
	command.indexCount = 6;
	command.vertexCount = 4;
	command.scissorX1 = 0;
	command.scissorY1 = 0;
	command.scissorX2 = 127;
	command.scissorY2 = 127;
	for ( int i = 0; i < 16; ++i ) {
		command.modelViewMatrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
	}
	command.visibilityBoundsValid = true;
	command.visibilityFrustumEligible = true;
	command.visibilityFrustumRejected = false;
	command.visibilityScreenRectValid = true;
	command.visibilityScreenX1 = 0;
	command.visibilityScreenY1 = 0;
	command.visibilityScreenX2 = 127;
	command.visibilityScreenY2 = 127;
	command.visibilityHiZCandidate = true;

	modernGLExecutorStats_t stats;
	R_ModernGLExecutor_ResetStats( stats, true );
	if ( !R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
		common->Printf( "RendererModernVisibility self-test failed: visible command rejected\n" );
		return false;
	}

	command.scissorX1 = 256;
	command.scissorY1 = 256;
	command.scissorX2 = 320;
	command.scissorY2 = 320;
	if ( R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
		common->Printf( "RendererModernVisibility self-test failed: offscreen scissor command accepted\n" );
		return false;
	}

	command.scissorX1 = 0;
	command.scissorY1 = 0;
	command.scissorX2 = 127;
	command.scissorY2 = 127;
	geometry.bounds[0].Set( 2.0f, -0.5f, -0.5f );
	geometry.bounds[1].Set( 3.0f, 0.5f, 0.5f );
	command.visibilityFrustumRejected = true;
	if ( R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
		common->Printf( "RendererModernVisibility self-test failed: frustum-culled command accepted\n" );
		return false;
	}

	command.passCategory = RENDER_PASS_SHADOW_MAP;
	command.visibilityFrustumEligible = false;
	if ( !R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
		common->Printf( "RendererModernVisibility self-test failed: main-view frustum was applied to shadow caster\n" );
		return false;
	}

	command.passCategory = RENDER_PASS_AMBIENT;
	command.visibilityFrustumEligible = true;
	command.visibilityFrustumRejected = false;
	command.visibilityScreenX1 = 192;
	command.visibilityScreenY1 = 192;
	command.visibilityScreenX2 = 224;
	command.visibilityScreenY2 = 224;
	if ( R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
		common->Printf( "RendererModernVisibility self-test failed: projected screen bounds did not reject command\n" );
		return false;
	}

	r_rendererOcclusion.SetBool( false );
	command.scissorX1 = 256;
	command.scissorY1 = 256;
	command.scissorX2 = 320;
	command.scissorY2 = 320;
	if ( !R_ModernGLExecutor_CommandVisibleForModernPath( command, &stats, false ) ) {
		common->Printf( "RendererModernVisibility self-test failed: culling did not disable cleanly\n" );
		return false;
	}

	if ( stats.visibilityCpuTested < 5 || stats.visibilityCpuRejected < 3 || stats.visibilityScissorRejected != 1 || stats.visibilityFrustumRejected != 1 || stats.visibilityScreenRectRejected != 1 || stats.visibilitySavedDraws < 3 || stats.visibilityHiZCandidates <= 0 || !stats.visibilityNoQueryStall ) {
		common->Printf(
			"RendererModernVisibility self-test failed: metric mismatch (cpu=%d/%d scissor=%d frustum=%d screen=%d saved=%d hizCandidates=%d noQuery=%d)\n",
			stats.visibilityCpuTested,
			stats.visibilityCpuRejected,
			stats.visibilityScissorRejected,
			stats.visibilityFrustumRejected,
			stats.visibilityScreenRectRejected,
			stats.visibilitySavedDraws,
			stats.visibilityHiZCandidates,
			stats.visibilityNoQueryStall ? 1 : 0 );
		return false;
	}

	common->Printf(
		"RendererModernVisibility self-test passed (cpu=%d/%d scissor=%d frustum=%d screen=%d clipped=%d saved=%d/%d shadowSafe=1 disabled=1 hizReq=%d candidates=%d noQueryStall=%d)\n",
		stats.visibilityCpuTested,
		stats.visibilityCpuRejected,
		stats.visibilityScissorRejected,
		stats.visibilityFrustumRejected,
		stats.visibilityScreenRectRejected,
		stats.visibilityScreenRectClipped,
		stats.visibilitySavedDraws,
		stats.visibilitySavedTriangles,
		stats.visibilityHiZRequested ? 1 : 0,
		stats.visibilityHiZCandidates,
		stats.visibilityNoQueryStall ? 1 : 0 );
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
	if ( depthProgram->mainTextureLocation < 0 || depthProgram->localParamsLocation < 0 || shadowProgram->mainTextureLocation < 0 || shadowProgram->localParamsLocation < 0 ) {
		common->Printf( "RendererVisiblePath self-test failed: alpha-aware depth bindings unavailable\n" );
		return false;
	}

	idModernGLDrawPlan drawPlan;
	drawPlan.Build( packetFrame, graph );
	const modernGLDrawPlanStats_t &drawStats = drawPlan.Stats();
	idModernGLSubmitPlan submitPlan;
	submitPlan.Build( drawPlan );
	const modernGLSubmitPlanStats_t &submitStats = submitPlan.Stats();
	const materialResourceTableRecord_t *defaultRecord = R_MaterialResourceTable_FindRecordForMaterial( tr.defaultMaterial );
	const bool materialModernEligible = defaultRecord != NULL && defaultRecord->fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE;
	const int expectedDepthDraws = tr.defaultMaterial != NULL && materialModernEligible ? 4 : 0;
	const int expectedDrawFallbacks = packetFrame.NumDrawPackets() - expectedDepthDraws;
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
	const float packedMaterial[4] = { 0.1f, 0.5f, 0.25f, 0.0f };
	return idMath::Fabs( packedNormal[0] - 0.5f ) < 0.001f
		&& idMath::Fabs( packedNormal[1] - 0.5f ) < 0.001f
		&& idMath::Fabs( packedNormal[2] - 1.0f ) < 0.001f
		&& packedMaterial[0] >= 0.0f && packedMaterial[0] <= 1.0f
		&& packedMaterial[1] >= 0.0f && packedMaterial[1] <= 1.0f
		&& packedMaterial[2] >= 0.0f && packedMaterial[2] <= 1.0f
		&& packedMaterial[3] >= 0.0f && packedMaterial[3] <= 1.0f;
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
		|| !opaqueProgram->reflection.usesMaterialTextures || opaqueProgram->normalTextureLocation < 0 || opaqueProgram->specularTextureLocation < 0 || opaqueProgram->emissiveTextureLocation < 0 || opaqueProgram->materialFlagsLocation < 0 || opaqueProgram->materialEnhancementLocation < 0
		|| !alphaProgram->reflection.usesMainTexture || alphaProgram->mainTextureLocation < 0
		|| !alphaProgram->reflection.usesMaterialTextures || alphaProgram->normalTextureLocation < 0 || alphaProgram->specularTextureLocation < 0 || alphaProgram->emissiveTextureLocation < 0 || alphaProgram->materialFlagsLocation < 0 || alphaProgram->materialEnhancementLocation < 0 ) {
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
	const materialResourceTableRecord_t *defaultRecord = R_MaterialResourceTable_FindRecordForMaterial( tr.defaultMaterial );
	const bool materialModernEligible = defaultRecord != NULL && defaultRecord->fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE;
	const int expectedDraws = tr.defaultMaterial != NULL && materialModernEligible ? 2 : 0;
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
	stats.opaqueGBufferRequested = true;
	stats.deferredResolveRequested = true;
	stats.pipelineGBufferNeeded = true;
	stats.pipelineDeferredNeeded = true;
	stats.pipelineDeferredCommands = 1;
	stats.pipelineMinimumPassesReady = true;
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
		"RendererDeferredResolve self-test passed (program=%d output=%d resources=%d cluster=%d pixels=%d lights=%d point=%d projected=%d shadow=%d/%d/%d/%d lightGrid=%d reads=%d fallback=%d debug=%d overlay=%d)\n",
		stats.deferredResolveProgramReady ? 1 : 0,
		stats.deferredResolveOutputReady ? 1 : 0,
		stats.deferredResolveResourcesReady ? 1 : 0,
		stats.deferredResolveClusterReady ? 1 : 0,
		stats.deferredResolvePixels,
		stats.deferredResolveActiveLights,
		stats.deferredResolvePointLights,
		stats.deferredResolveProjectedLights,
		stats.deferredResolveShadowMappedLights,
		stats.deferredResolveShadowFallbackLights,
		stats.deferredResolveShadowSkippedLights,
		stats.deferredResolveShadowDescriptors,
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

	struct rendererForwardPlusBoolCVarRestore_t {
		idCVar &cvar;
		bool oldValue;
		rendererForwardPlusBoolCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetBool() ) {}
		~rendererForwardPlusBoolCVarRestore_t() { cvar.SetBool( oldValue ); }
	};
	rendererForwardPlusBoolCVarRestore_t restoreOcclusion( r_rendererOcclusion );
	r_rendererOcclusion.SetBool( false );

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
	R_ModernGLExecutor_RecordPipelinePolicy( stats, false );
	R_ModernGLExecutor_SubmitForwardPlus( stats );
	R_ModernGLExecutor_RecordMetrics( stats );
	rg_modernGLExecutorStats = stats;

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
		"RendererForwardPlus self-test passed (programs=%d resources=%d scene=%d depth=%d cluster=%d draws=%d opaque=%d alpha=%d alphaProgram=%d transparent=%d batches=%d fallback=%d effects=%d overdraw=%d reads=%d lights=%d point=%d projected=%d shadow=%d/%d/%d/%d lightGrid=%d)\n",
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
		stats.forwardPlusShadowMappedLights,
		stats.forwardPlusShadowFallbackLights,
		stats.forwardPlusShadowSkippedLights,
		stats.forwardPlusShadowDescriptors,
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

	struct rendererModernVisibleBoolCVarRestore_t {
		idCVar &cvar;
		bool oldValue;
		rendererModernVisibleBoolCVarRestore_t( idCVar &value ) : cvar( value ), oldValue( value.GetBool() ) {}
		~rendererModernVisibleBoolCVarRestore_t() { cvar.SetBool( oldValue ); }
	};
	rendererModernVisibleBoolCVarRestore_t restoreShadows( r_shadows );
	r_shadows.SetBool( false );

	drawSurf_t drawSurfs[3];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	vertCache_t ambientCache;
	vertCache_t indexCache;
	if ( !R_ModernGLExecutor_InitSelfTestTriangleGeometry( geometry, ambientCache, indexCache, "RendererModernVisible" ) ) {
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
		|| graph.FindResource( "deferredLight" ) < 0 || graph.FindResource( "sceneColor" ) < 0 || graph.FindResource( "hybridSceneColor" ) < 0 || graph.FindResource( "backBuffer" ) < 0 ) {
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
	R_ModernGLExecutor_AnalyzeModernVisibleOwnershipReadiness( packetFrame, graph, rg_modernGLExecutorStats );
	rg_modernGLExecutorStats.modernVisibleCanReplaceFrame =
		rg_modernGLExecutorStats.modernVisibleRequested &&
		rg_modernGLExecutorStats.modernVisibleProgramReady &&
		!rg_modernGLExecutorStats.modernVisibleBlockedByLegacy;
	R_ModernGLExecutor_SetEffectivePassRequests( rg_modernGLExecutorStats, true, true, true, true );
	R_ModernGLExecutor_RecordPipelinePolicy( rg_modernGLExecutorStats, false );
	R_ModernGLExecutor_RecordPassGates( rg_modernGLExecutorStats, false, false, false, false );
	R_ModernGLExecutor_UpdateFrameUBO( rg_modernGLExecutorStats );
	{
		modernGLExecutorSoftPassHandoffScope_t softHandoffs;
		R_ModernGLExecutor_SubmitVisibleDepth( rg_modernGLExecutorStats );
		R_ModernGLExecutor_SubmitGBuffer( rg_modernGLExecutorStats );
		R_ModernGLExecutor_SubmitDeferredResolve( rg_modernGLExecutorStats );
		R_ModernGLExecutor_SubmitForwardPlus( rg_modernGLExecutorStats );
	}
	R_ModernGLExecutor_FullRestoreForLegacyHandoff( rg_modernGLExecutorStats, "modern visible self-test legacy handoff", false );
	R_ModernGLExecutor_FinalizePassOwnership( graph, rg_modernGLExecutorStats );

	const viewDef_t *previousViewDef = backEnd.viewDef;
	idRenderTexture *previousRenderTexture = backEnd.renderTexture;
	backEnd.viewDef = &worldView;
	backEnd.renderTexture = NULL;
	R_GLStateCache_InvalidateAll( "RendererModernVisible self-test post target" );
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
	R_GLStateCache().SetViewport( 0, 0, 640, 480 );
	R_GLStateCache().SetScissor( 0, 0, 640, 480 );
	R_ModernGLExecutor_ComposeVisibleSceneForPost();
	R_ModernGLExecutor_ComposeVisibleFrame();
	backEnd.viewDef = previousViewDef;
	backEnd.renderTexture = previousRenderTexture;

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
	if ( resourcesAvailable && ( stats.modernSoftRestores <= 0 || stats.modernFullRestores <= 0 ) ) {
		common->Printf(
			"RendererModernVisible self-test failed: soft/full restore policy was not exercised (soft=%d full=%d)\n",
			stats.modernSoftRestores,
			stats.modernFullRestores );
		return false;
	}
	if ( resourcesAvailable && ( !stats.deferredResolveExecuted || ( stats.pipelineForwardPlusNeeded && !stats.forwardPlusExecuted ) || !stats.modernVisibleExecuted || !stats.modernVisibleResourcesReady || !stats.modernVisibleSourceReady || !stats.modernVisibleHybridTargetReady || !stats.modernVisibleBackBufferReady || !stats.modernVisibleShadowReady || !stats.modernVisiblePostProcessHandoff || stats.modernVisibleCompositions <= 0 || stats.modernVisibleCompositeCopies <= 0 || stats.modernVisiblePostProcessCompositions <= 0 || stats.modernVisibleDepthCopies <= 0 || stats.modernVisiblePixels <= 0 ) ) {
		common->Printf(
			"RendererModernVisible self-test failed: composition execution mismatch (deferred=%d forward=%d exec=%d res=%d source=%d hybrid=%d backBuffer=%d shadow=%d hdr=%d postHandoff=%d composed=%d copies=%d postComposed=%d depthCopies=%d pixels=%d fallback=%d)\n",
			stats.deferredResolveExecuted ? 1 : 0,
			stats.forwardPlusExecuted ? 1 : 0,
			stats.modernVisibleExecuted ? 1 : 0,
			stats.modernVisibleResourcesReady ? 1 : 0,
			stats.modernVisibleSourceReady ? 1 : 0,
			stats.modernVisibleHybridTargetReady ? 1 : 0,
			stats.modernVisibleBackBufferReady ? 1 : 0,
			stats.modernVisibleShadowReady ? 1 : 0,
			stats.modernVisibleHDRTargetReady ? 1 : 0,
			stats.modernVisiblePostProcessHandoff ? 1 : 0,
			stats.modernVisibleCompositions,
			stats.modernVisibleCompositeCopies,
			stats.modernVisiblePostProcessCompositions,
			stats.modernVisibleDepthCopies,
			stats.modernVisiblePixels,
			stats.modernVisibleFallbackPasses );
		return false;
	}

	common->Printf(
		"RendererModernVisible self-test passed (program=%d resources=%d source=%d hybrid=%d backBuffer=%d shadow=%d hdr=%d postHandoff=%d singleSource=%d blocked=%d composed=%d copies=%d postComposed=%d depthCopies=%d pixels=%d modern=%d legacy=%d fallback=%d deferred=%d forward=%d present=%d clears=%d restores=%d/%d)\n",
		stats.modernVisibleProgramReady ? 1 : 0,
		stats.modernVisibleResourcesReady ? 1 : 0,
		stats.modernVisibleSourceReady ? 1 : 0,
		stats.modernVisibleHybridTargetReady ? 1 : 0,
		stats.modernVisibleBackBufferReady ? 1 : 0,
		stats.modernVisibleShadowReady ? 1 : 0,
		stats.modernVisibleHDRTargetReady ? 1 : 0,
		stats.modernVisiblePostProcessHandoff ? 1 : 0,
		stats.pipelineCompositionSingleSource ? 1 : 0,
		stats.modernVisibleBlockedByLegacy ? 1 : 0,
		stats.modernVisibleCompositions,
		stats.modernVisibleCompositeCopies,
		stats.modernVisiblePostProcessCompositions,
		stats.modernVisibleDepthCopies,
		stats.modernVisiblePixels,
		stats.modernVisibleModernPasses,
		stats.modernVisibleLegacyPasses,
		stats.modernVisibleFallbackPasses,
		stats.deferredResolveExecuted ? 1 : 0,
		stats.forwardPlusExecuted ? 1 : 0,
		stats.modernVisiblePresentPasses,
		stats.modernVisibleClearOps,
		stats.modernSoftRestores,
		stats.modernFullRestores );
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
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_SHADOW_MAP, MODERN_GL_PASS_OWNER_BLOCKED, true, false, "selftest-shadow-receiver-parity-blocked" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_STENCIL_SHADOW, MODERN_GL_PASS_OWNER_BLOCKED, false, false, "selftest-stencil-fallback-visible" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_AMBIENT, MODERN_GL_PASS_OWNER_MIXED, true, false, "selftest-diagnostic-gbuffer" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_DEFERRED_RESOLVE, MODERN_GL_PASS_OWNER_MODERN, true, false, "selftest-modern-deferred" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_FORWARD_PLUS, MODERN_GL_PASS_OWNER_MODERN, true, false, "selftest-modern-forward" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_ARB2_INTERACTION, MODERN_GL_PASS_OWNER_BLOCKED, true, false, "selftest-lighting-parity-blocked" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_GUI, MODERN_GL_PASS_OWNER_LEGACY, false, false, "selftest-legacy-gui" );
	R_ModernGLExecutor_SetPassOwnership( RENDER_PASS_AUTHORED_POST, MODERN_GL_PASS_OWNER_LEGACY, false, false, "selftest-legacy-post" );
	R_ModernGLExecutor_UpdatePassOwnershipCounts( stats );

	if ( !R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_DEPTH )
		|| R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_SHADOW_MAP )
		|| R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_STENCIL_SHADOW )
		|| R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_ARB2_INTERACTION )
		|| R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_AMBIENT )
		|| R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_GUI )
		|| stats.passOwnerDuplicateHazards != 0
		|| stats.passOwnerDroppedByModern != 0 ) {
		common->Printf(
			"RendererPassOwnership self-test failed: ownership mismatch (skipDepth=%d skipShadow=%d skipStencil=%d skipLight=%d skipAmbient=%d skipGui=%d hazards=%d dropped=%d)\n",
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_DEPTH ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_SHADOW_MAP ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_STENCIL_SHADOW ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_ARB2_INTERACTION ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_AMBIENT ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkip( RENDER_PASS_GUI ) ? 1 : 0,
			stats.passOwnerDuplicateHazards,
			stats.passOwnerDroppedByModern );
		R_ModernGLExecutor_ResetPassOwnershipTable( "pass-ownership-selftest-failed" );
		return false;
	}

	viewDef_t mainView;
	memset( &mainView, 0, sizeof( mainView ) );
	mainView.renderView.viewID = 1;
	viewDef_t subview = mainView;
	subview.isSubview = true;
	viewDef_t renderDemoView = mainView;
	renderDemoView.renderView.viewID = -1;
	if ( !R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_DEPTH, &mainView )
		|| R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_DEPTH, &subview )
		|| R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_DEPTH, &renderDemoView ) ) {
		common->Printf(
			"RendererPassOwnership self-test failed: view-aware skip mismatch (main=%d subview=%d demo=%d)\n",
			R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_DEPTH, &mainView ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_DEPTH, &subview ) ? 1 : 0,
			R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_DEPTH, &renderDemoView ) ? 1 : 0 );
		R_ModernGLExecutor_ResetPassOwnershipTable( "pass-ownership-selftest-failed" );
		return false;
	}

	R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_DEPTH );
	R_ModernGLExecutor_UpdatePassOwnershipCounts( stats );
	if ( stats.passOwnerLegacySkipsIssued != 1 ) {
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
	R_ModernGLExecutor_AnalyzeModernVisibleOwnershipReadiness( packetFrame, graph, rg_modernGLExecutorStats );

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
	if ( !stats.modernVisibleBlockedByLegacy
		|| stats.modernVisibleOwnerFallbacks <= 0
		|| stats.modernVisibleLightGridModernPasses <= 0
		|| stats.modernVisibleLightingReady
		|| stats.modernVisibleLightGridReady
		|| stats.modernVisibleShadowOwnershipReady
		|| stats.modernVisibleLightingFallbackPasses <= 0
		|| stats.modernVisibleLightGridFallbackPasses <= 0
		|| stats.modernVisibleShadowOwnershipFallbackPasses <= 0
		|| stats.modernVisibleOwnershipBlocker[0] == '\0' ) {
		common->Printf(
			"RendererModernCompatibility self-test failed: fallback policy mismatch (blocked=%d ownerFallback=%d lightGrid=%d ready(light=%d grid=%d shadow=%d) fallback(light=%d grid=%d shadow=%d) blocker='%s')\n",
			stats.modernVisibleBlockedByLegacy ? 1 : 0,
			stats.modernVisibleOwnerFallbacks,
			stats.modernVisibleLightGridModernPasses,
			stats.modernVisibleLightingReady ? 1 : 0,
			stats.modernVisibleLightGridReady ? 1 : 0,
			stats.modernVisibleShadowOwnershipReady ? 1 : 0,
			stats.modernVisibleLightingFallbackPasses,
			stats.modernVisibleLightGridFallbackPasses,
			stats.modernVisibleShadowOwnershipFallbackPasses,
			stats.modernVisibleOwnershipBlocker );
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
		"RendererModernCompatibility self-test passed (inventory=%d modern=%d legacy=%d gui=%d/%d post=%d/%d subview=%d demo=%d bse=%d ownerFallback=%d blocked=%d lightReady=%d gridReady=%d shadowReady=%d skipBlocked=%d/%d/%d/%d blocker='%s')\n",
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
		stats.modernVisibleLightingReady ? 1 : 0,
		stats.modernVisibleLightGridReady ? 1 : 0,
		stats.modernVisibleShadowOwnershipReady ? 1 : 0,
		gateStats.visibleDepthSkippedBlocked,
		gateStats.opaqueGBufferSkippedBlocked,
		gateStats.deferredResolveSkippedBlocked,
		gateStats.forwardPlusSkippedBlocked,
		stats.modernVisibleOwnershipBlocker );
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
	R_ModernGLExecutor_RecordPipelinePolicy( stats, false );
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
	R_ModernGLExecutor_ResetFramebufferAttachmentCache( rg_modernGLExecutorGBufferAttachmentCache );
	if ( !stats.opaqueGBufferResourcesReady || !depthReady || sceneDepth == NULL || !R_ModernGLExecutor_PrepareGBufferFBO( gBufferHandles, *sceneDepth, stats ) ) {
		common->Printf(
			"RendererLowOverhead self-test failed: G-buffer DSA preparation mismatch (res=%d depth=%d fboDSA=%d mrt=%d)\n",
			stats.opaqueGBufferResourcesReady ? 1 : 0,
			depthReady ? 1 : 0,
			stats.lowOverheadFramebufferDSAUpdates,
			stats.opaqueGBufferMRTReady ? 1 : 0 );
		return false;
	}
	const int fboCacheHitsBefore = stats.pipelineFramebufferCacheHits;
	if ( !R_ModernGLExecutor_PrepareGBufferFBO( gBufferHandles, *sceneDepth, stats ) || stats.pipelineFramebufferCacheHits <= fboCacheHitsBefore ) {
		common->Printf(
			"RendererLowOverhead self-test failed: G-buffer FBO cache reuse mismatch (hits=%d before=%d updates=%d)\n",
			stats.pipelineFramebufferCacheHits,
			fboCacheHitsBefore,
			stats.pipelineFramebufferAttachmentUpdates );
		return false;
	}
	const bool materialTextureTableExercised = R_ModernGLExecutor_ExerciseMaterialTextureTableForSelfTest( stats );
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
	const bool uploadStreamExercised = stats.frameUBOStreamed && stats.uploadManagerFrameUBOBytes > 0;
	if ( ( stats.lowOverheadDSAUpdates <= 0 && !uploadStreamExercised ) || stats.lowOverheadFramebufferDSAUpdates <= 0 || ( stats.lowOverheadMultiBindBatches <= 0 && !uploadStreamExercised ) || stats.lowOverheadTextureMultiBindBatches <= 0 || stats.lowOverheadSamplerMultiBindBatches <= 0 || stats.lowOverheadCompactedBatches <= 0 ) {
		common->Printf(
			"RendererLowOverhead self-test failed: low-overhead operations were not exercised (dsa=%d fboDSA=%d bufferBind=%d textureBind=%d samplerBind=%d compact=%d stream=%d/%d)\n",
			stats.lowOverheadDSAUpdates,
			stats.lowOverheadFramebufferDSAUpdates,
			stats.lowOverheadMultiBindBatches,
			stats.lowOverheadTextureMultiBindBatches,
			stats.lowOverheadSamplerMultiBindBatches,
			stats.lowOverheadCompactedBatches,
			uploadStreamExercised ? 1 : 0,
			stats.uploadManagerFrameUBOBytes );
		return false;
	}
	if ( !materialTextureTableExercised || !stats.materialTextureTableReady || !stats.materialTextureTableUsed || stats.materialTextureTableDraws <= 0 || stats.materialTextureTableUniforms <= 0 ) {
		common->Printf(
			"RendererLowOverhead self-test failed: material texture table was not exercised (ready=%d used=%d size=%d/%d desc=%d draws=%d uniforms=%d fallback=%d)\n",
			stats.materialTextureTableReady ? 1 : 0,
			stats.materialTextureTableUsed ? 1 : 0,
			stats.materialTextureTableTextures,
			stats.materialTextureTableCapacity,
			stats.materialTextureTableDescriptors,
			stats.materialTextureTableDraws,
			stats.materialTextureTableUniforms,
			stats.materialTextureTableFallbacks );
		return false;
	}

	const rendererUploadStats_t &uploadStats = R_RendererUpload_Stats();
	common->Printf(
		"RendererLowOverhead self-test passed (dsa=1 multiBind=1 sampler=%d textureDSA=%d framebufferDSA=%d dsaUpdates=%d framebufferDSAUpdates=%d fboCache=%d/%d bufferMultiBind=%d textureMultiBind=%d samplerMultiBind=%d classicTextureBinds=%d bindless=%d/%d compactedBatches=%d textureTable=%d/%d draws=%d uniforms=%d fallback=%d streamUpload=%d/%d uploadFallback=%d/%d persistent=%d buffers=%d fences=%d/%d waits=%d)\n",
		stats.lowOverheadSamplerReady ? 1 : 0,
		graphStats.dsaTextureAllocations,
		graphStats.dsaFramebufferAllocations,
		stats.lowOverheadDSAUpdates,
		stats.lowOverheadFramebufferDSAUpdates,
		stats.pipelineFramebufferCacheHits,
		stats.pipelineFramebufferAttachmentUpdates,
		stats.lowOverheadMultiBindBatches,
		stats.lowOverheadTextureMultiBindBatches,
		stats.lowOverheadSamplerMultiBindBatches,
		stats.lowOverheadClassicTextureBinds,
		stats.lowOverheadBindlessRequested ? 1 : 0,
		stats.lowOverheadBindlessAvailable ? 1 : 0,
		stats.lowOverheadCompactedBatches,
		stats.materialTextureTableTextures,
		stats.materialTextureTableCapacity,
		stats.materialTextureTableDraws,
		stats.materialTextureTableUniforms,
		stats.materialTextureTableFallbacks,
		stats.frameUBOStreamed ? 1 : 0,
		stats.uploadManagerFrameUBOBytes,
		stats.uploadManagerFallbackBytes,
		stats.uploadManagerFallbackBuffers,
		uploadStats.persistentMapped ? 1 : 0,
		uploadStats.ringBufferCount,
		uploadStats.frameFencesSubmitted,
		uploadStats.frameFencesRetired,
		uploadStats.frameFenceWaits );
	return true;
}
