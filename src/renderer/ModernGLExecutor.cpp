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
#include "RenderGraphResources.h"
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

const int MODERN_GL_GPU_DRIVEN_MAX_RECORDS = MODERN_GL_DRAW_PLAN_MAX_ENTRIES;
const int MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS = 4;
const int MODERN_GL_GBUFFER_ATTACHMENT_COUNT = 4;
const int MODERN_GL_DEFERRED_TEXTURE_COUNT = 5;
const int MODERN_GL_CLUSTER_UBO_BINDING_PARAMS = 3;
const int MODERN_GL_CLUSTER_UBO_BINDING_LIGHTS = 4;
const int MODERN_GL_CLUSTER_UBO_BINDING_INDICES = 5;

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
static GLuint rg_modernGLExecutorValidationSSBO = 0;
static GLuint rg_modernGLExecutorComputeProgram = 0;
static GLuint rg_modernGLExecutorDepthOverlayProgram = 0;
static GLuint rg_modernGLExecutorGBufferFBO = 0;
static GLuint rg_modernGLExecutorGBufferOverlayProgram = 0;
static GLuint rg_modernGLExecutorDeferredOverlayProgram = 0;
static GLint rg_modernGLExecutorComputeRecordCountLocation = -1;
static GLint rg_modernGLExecutorDepthOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorDepthOverlayParamsLocation = -1;
static GLint rg_modernGLExecutorGBufferOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorGBufferOverlayParamsLocation = -1;
static GLint rg_modernGLExecutorDeferredOverlayTextureLocation = -1;
static GLint rg_modernGLExecutorDeferredOverlayParamsLocation = -1;
static bool rg_modernGLExecutorInitialized = false;
static bool rg_modernGLExecutorAvailable = false;
static bool rg_modernGLExecutorGpuDrivenReady = false;
static bool rg_modernGLExecutorLowOverheadReady = false;

static ID_INLINE GLint R_ModernGLExecutor_SafeStencilClearValue( void ) {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

static void R_ModernGLExecutor_SetStatus( modernGLExecutorStats_t &stats, const char *status ) {
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", status ? status : "unknown" );
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
		&& glBindBuffersBase != NULL;
}

static bool R_ModernGLExecutor_CanCreateGpuDrivenObjects( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !features.gpuDriven ) {
		return false;
	}
	if ( !caps.hasSSBO || !caps.hasCompute || !caps.hasDrawIndirect || !caps.hasMultiDrawIndirect ) {
		return false;
	}
	if ( glBindBufferBase == NULL || glBufferData == NULL || glBufferSubData == NULL || glDispatchCompute == NULL || glMemoryBarrier == NULL || glUniform1ui == NULL ) {
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
		"uniform uint u_recordCount;\n"
		"layout(std430, binding = 1) readonly buffer ModernSceneRecords { SceneRecord records[]; };\n"
		"layout(std430, binding = 2) buffer ModernValidation { uint counters[4]; };\n"
		"void main() {\n"
		"	uint index = gl_GlobalInvocationID.x;\n"
		"	if ( index < u_recordCount ) {\n"
		"		atomicAdd( counters[0], 1u );\n"
		"		atomicAdd( counters[1], records[index].ids.x != 0xffffffffu ? 1u : 0u );\n"
		"	}\n"
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
	rg_modernGLExecutorComputeRecordCountLocation = -1;
	rg_modernGLExecutorDepthOverlayTextureLocation = -1;
	rg_modernGLExecutorDepthOverlayParamsLocation = -1;
	rg_modernGLExecutorGBufferOverlayTextureLocation = -1;
	rg_modernGLExecutorGBufferOverlayParamsLocation = -1;
	rg_modernGLExecutorDeferredOverlayTextureLocation = -1;
	rg_modernGLExecutorDeferredOverlayParamsLocation = -1;
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
	GLuint zeroValidation[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0, 0, 0, 0 };
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
	stats.visibleDepthRequested = r_rendererModernVisibleDepth.GetBool() || r_rendererModernDepthDebug.GetInteger() > 0;
	stats.visibleDepthDebugOverlayReady = rg_modernGLExecutorDepthOverlayProgram != 0;
	stats.opaqueGBufferRequested = r_rendererModernOpaque.GetBool() || r_rendererModernGBufferDebug.GetInteger() > 0 || r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	stats.opaqueGBufferDebugOverlayReady = rg_modernGLExecutorGBufferOverlayProgram != 0;
	stats.opaqueGBufferMRTReady = rg_modernGLExecutorGBufferFBO != 0 && rg_modernGLExecutorCaps.hasMRT && rg_modernGLExecutorCaps.maxDrawBuffers >= MODERN_GL_GBUFFER_ATTACHMENT_COUNT && glDrawBuffers != NULL;
	stats.deferredResolveRequested = r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	stats.deferredResolveDebugOverlayReady = rg_modernGLExecutorDeferredOverlayProgram != 0;
	stats.deferredResolveDebugMode = r_rendererModernDeferredDebug.GetInteger();
	stats.forwardPlusRequested = r_rendererForwardPlus.GetBool();
	stats.tierUsesDSA = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.directStateAccess;
	stats.tierUsesMultiBind = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.multiBind;
	stats.shaderProgramCount = shaderStats.programCount;
	stats.shaderFailureCount = shaderStats.failedProgramCount;
	stats.highestGLSLVersion = shaderStats.highestGLSLVersion;
	R_ModernGLExecutor_SetStatus( stats, enabled ? "unavailable" : "off" );
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
	stats.visibleDepthRequested = r_rendererModernVisibleDepth.GetBool() || r_rendererModernDepthDebug.GetInteger() > 0;
	stats.visibleDepthDebugOverlayReady = rg_modernGLExecutorDepthOverlayProgram != 0;
	stats.opaqueGBufferRequested = r_rendererModernOpaque.GetBool() || r_rendererModernGBufferDebug.GetInteger() > 0 || r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	stats.opaqueGBufferDebugOverlayReady = rg_modernGLExecutorGBufferOverlayProgram != 0;
	stats.opaqueGBufferMRTReady = rg_modernGLExecutorGBufferFBO != 0 && rg_modernGLExecutorCaps.hasMRT && rg_modernGLExecutorCaps.maxDrawBuffers >= MODERN_GL_GBUFFER_ATTACHMENT_COUNT && glDrawBuffers != NULL;
	stats.deferredResolveRequested = r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	stats.deferredResolveDebugOverlayReady = rg_modernGLExecutorDeferredOverlayProgram != 0;
	stats.deferredResolveDebugMode = r_rendererModernDeferredDebug.GetInteger();
	stats.forwardPlusRequested = r_rendererForwardPlus.GetBool();
	if ( stats.forwardPlusRequested ) {
		stats.forwardPlusSpecialEffectFallbacks = packetFrame.Stats().specialEffectPackets;
	}
	stats.tierUsesDSA = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.directStateAccess;
	stats.tierUsesMultiBind = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.multiBind;
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

static void R_ModernGLExecutor_SetLocalParams( const modernGLSubmitCommand_t &command ) {
	if ( command.localParamsLocation < 0 ) {
		return;
	}
	switch ( command.shaderKind ) {
	case MODERN_GL_SHADER_GBUFFER_ALPHA_TEST:
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST:
		glUniform4f( command.localParamsLocation, 0.5f, 0.0f, 0.0f, 0.0f );
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
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_OPAQUE
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_ALPHA_TEST
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT;
}

static void R_ModernGLExecutor_BindGpuDrivenBuffers( modernGLExecutorStats_t &stats ) {
	if ( !stats.gpuDrivenReady ) {
		return;
	}
	if ( stats.tierUsesMultiBind && glBindBuffersBase != NULL ) {
		GLuint buffers[2] = { rg_modernGLExecutorSceneSSBO, rg_modernGLExecutorValidationSSBO };
		if ( R_GLStateCache().BindBuffersBase( GL_SHADER_STORAGE_BUFFER, 1, 2, buffers ) ) {
			stats.lowOverheadMultiBindBatches++;
		}
	} else if ( glBindBufferBase != NULL ) {
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 1, rg_modernGLExecutorSceneSSBO );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 2, rg_modernGLExecutorValidationSSBO );
	}
	if ( rg_modernGLExecutorIndirectBuffer != 0 ) {
		R_GLStateCache().BindBuffer( GL_DRAW_INDIRECT_BUFFER, rg_modernGLExecutorIndirectBuffer );
	}
}

static void R_ModernGLExecutor_UnbindGpuDrivenBuffers( void ) {
	if ( glBindBufferBase != NULL ) {
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 1, 0 );
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 2, 0 );
	}
	if ( glBindBuffer != NULL ) {
		R_GLStateCache().BindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
	}
}

static void R_ModernGLExecutor_UpdateGpuDrivenBuffers( modernGLExecutorStats_t &stats ) {
	if ( !stats.gpuDrivenReady || !stats.drawPlanReady || rg_modernGLExecutorSceneSSBO == 0 || rg_modernGLExecutorIndirectBuffer == 0 || rg_modernGLExecutorValidationSSBO == 0 ) {
		return;
	}

	static modernGLGpuSceneRecord_t sceneRecords[MODERN_GL_GPU_DRIVEN_MAX_RECORDS];
	static modernGLDrawElementsIndirectCommand_t indirectRecords[MODERN_GL_GPU_DRIVEN_MAX_RECORDS];
	memset( sceneRecords, 0, sizeof( sceneRecords ) );
	memset( indirectRecords, 0, sizeof( indirectRecords ) );

	const int sceneRecordCount = Min( rg_modernGLDrawPlan.NumEntries(), MODERN_GL_GPU_DRIVEN_MAX_RECORDS );
	for ( int i = 0; i < sceneRecordCount; ++i ) {
		const modernGLDrawPlanEntry_t &entry = rg_modernGLDrawPlan.Entry( i );
		modernGLGpuSceneRecord_t &record = sceneRecords[i];
		record.counts[0] = static_cast<float>( entry.indexCount );
		record.counts[1] = static_cast<float>( entry.vertexCount );
		record.counts[2] = static_cast<float>( entry.materialTableIndex );
		record.counts[3] = static_cast<float>( entry.passCategory );
		record.ids[0] = static_cast<GLuint>( entry.shaderKind );
		record.ids[1] = static_cast<GLuint>( entry.drawPacketIndex );
		record.ids[2] = entry.materialTableIndex >= 0 ? static_cast<GLuint>( entry.materialTableIndex ) : 0xffffffffu;
		record.ids[3] = entry.indexed ? 1u : 0u;
	}

	int indirectRecordCount = 0;
	if ( stats.submitPlanReady ) {
		for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands() && indirectRecordCount < MODERN_GL_GPU_DRIVEN_MAX_RECORDS; ++i ) {
			const modernGLSubmitCommand_t &command = rg_modernGLSubmitPlan.Command( i );
			if ( !command.indexed || command.uploadIndexBuffer || command.indexBuffer == 0 || command.indexCount <= 0 || command.indexCacheOffset < 0 ) {
				continue;
			}
			modernGLDrawElementsIndirectCommand_t &indirect = indirectRecords[indirectRecordCount++];
			indirect.count = static_cast<GLuint>( r_singleTriangle.GetBool() ? Min( 3, command.indexCount ) : command.indexCount );
			indirect.instanceCount = 1;
			indirect.firstIndex = static_cast<GLuint>( command.indexCacheOffset / sizeof( glIndex_t ) );
			indirect.baseVertex = 0;
			indirect.baseInstance = command.materialTableIndex >= 0 ? static_cast<GLuint>( command.materialTableIndex ) : 0u;
		}
	}

	GLuint validationCounters[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0, 0, 0, 0 };
	const GLsizeiptr sceneBytes = static_cast<GLsizeiptr>( sceneRecordCount * sizeof( modernGLGpuSceneRecord_t ) );
	const GLsizeiptr indirectBytes = static_cast<GLsizeiptr>( indirectRecordCount * sizeof( modernGLDrawElementsIndirectCommand_t ) );
	const GLsizeiptr validationBytes = static_cast<GLsizeiptr>( sizeof( validationCounters ) );
	R_ModernGLExecutor_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorSceneSSBO, sceneBytes, sceneRecords, stats );
	if ( indirectRecordCount > 0 ) {
		R_ModernGLExecutor_UpdateBuffer( GL_DRAW_INDIRECT_BUFFER, rg_modernGLExecutorIndirectBuffer, indirectBytes, indirectRecords, stats );
	}
	R_ModernGLExecutor_UpdateBuffer( GL_SHADER_STORAGE_BUFFER, rg_modernGLExecutorValidationSSBO, validationBytes, validationCounters, stats );

	stats.gpuDrivenSceneRecords = sceneRecordCount;
	stats.gpuDrivenIndirectRecords = indirectRecordCount;
	stats.gpuDrivenSceneBytes = static_cast<int>( sceneBytes );
	stats.gpuDrivenIndirectBytes = static_cast<int>( indirectBytes );
	stats.gpuDrivenValidationBytes = static_cast<int>( validationBytes );

	R_ModernGLExecutor_BindGpuDrivenBuffers( stats );
	if ( sceneRecordCount > 0 && stats.computeValidationReady ) {
		idGLDebugScope debugScope( "ModernGLExecutor GPU validation" );
		R_GLStateCache().UseProgram( rg_modernGLExecutorComputeProgram );
		if ( rg_modernGLExecutorComputeRecordCountLocation >= 0 ) {
			glUniform1ui( rg_modernGLExecutorComputeRecordCountLocation, static_cast<GLuint>( sceneRecordCount ) );
		}
		glDispatchCompute( static_cast<GLuint>( ( sceneRecordCount + 63 ) / 64 ), 1, 1 );
		glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT );
		R_GLStateCache().UseProgram( 0 );
		stats.gpuDrivenComputeDispatches++;
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

static GLuint R_ModernGLExecutor_TextureForCommand( const modernGLSubmitCommand_t &command ) {
	const materialResourceTableRecord_t *materialRecord = R_MaterialResourceTable_RecordForIndex( command.materialTableIndex );
	if ( materialRecord != NULL ) {
		const materialResourceTextureBinding_t *binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, MATERIAL_RESOURCE_TEXTURE_DIFFUSE );
		if ( binding == NULL ) {
			binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, MATERIAL_RESOURCE_TEXTURE_GUI );
		}
		if ( binding == NULL ) {
			binding = R_ModernGLExecutor_FindTextureBinding( *materialRecord, MATERIAL_RESOURCE_TEXTURE_POST_PROCESS );
		}
		if ( binding != NULL && binding->textureHandle != 0 ) {
			return static_cast<GLuint>( binding->textureHandle );
		}
	}
	if ( globalImages != NULL && globalImages->whiteImage != NULL && globalImages->whiteImage->IsLoaded() ) {
		return globalImages->whiteImage->GetDeviceHandle();
	}
	if ( globalImages != NULL && globalImages->defaultImage != NULL && globalImages->defaultImage->IsLoaded() ) {
		return globalImages->defaultImage->GetDeviceHandle();
	}
	return 0;
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
	R_ModernGLExecutor_SetDebugColor( command );
	R_ModernGLExecutor_SetLocalParams( command );
	if ( command.mainTextureLocation >= 0 && glUniform1i != NULL ) {
		glUniform1i( command.mainTextureLocation, 0 );
		const GLuint textureHandle = R_ModernGLExecutor_TextureForCommand( command );
		if ( textureHandle != 0 ) {
			R_GLStateCache().ActiveTextureUnit( 0 );
			R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, textureHandle );
		}
	}

	R_ModernGLExecutor_SetSubmitScissor( command, command.viewDef );
	R_GLStateCache().BindBuffer( GL_ARRAY_BUFFER, command.vertexBuffer );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer(
		0,
		3,
		GL_FLOAT,
		GL_FALSE,
		command.vertexStride,
		R_ModernGLExecutor_BufferOffset( command.ambientCacheOffset + DRAWVERT_XYZ_OFFSET ) );
	if ( command.mainTextureLocation >= 0 ) {
		glEnableVertexAttribArray( 8 );
		glVertexAttribPointer(
			8,
			2,
			GL_FLOAT,
			GL_FALSE,
			command.vertexStride,
			R_ModernGLExecutor_BufferOffset( command.ambientCacheOffset + DRAWVERT_ST_OFFSET ) );
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
	glDisableVertexAttribArray( 0 );
	glDisableVertexAttribArray( 8 );
	R_GLStateCache().BindBuffer( GL_ARRAY_BUFFER, 0 );
	R_GLStateCache().BindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
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
	if ( rg_modernGLExecutorGBufferFBO == 0 || glFramebufferTexture2D == NULL || glCheckFramebufferStatus == NULL || glDrawBuffers == NULL ) {
		return false;
	}
	if ( !rg_modernGLExecutorCaps.hasMRT || rg_modernGLExecutorCaps.maxDrawBuffers < MODERN_GL_GBUFFER_ATTACHMENT_COUNT || rg_modernGLExecutorCaps.maxColorAttachments < MODERN_GL_GBUFFER_ATTACHMENT_COUNT ) {
		return false;
	}

	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, rg_modernGLExecutorGBufferFBO );
	for ( int i = 0; i < MODERN_GL_GBUFFER_ATTACHMENT_COUNT; ++i ) {
		if ( colorHandles[i] == NULL || colorHandles[i]->texture == 0 ) {
			return false;
		}
		glFramebufferTexture2D( GL_FRAMEBUFFER, rg_modernGLGBufferColorAttachments[i], GL_TEXTURE_2D, colorHandles[i]->texture, 0 );
	}
	const GLenum depthAttachment = depthHandle.type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
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

static void R_ModernGLExecutor_BindDeferredResolveTextures( const renderGraphResourceHandle_t *const handles[MODERN_GL_DEFERRED_TEXTURE_COUNT] ) {
	for ( int i = 0; i < MODERN_GL_DEFERRED_TEXTURE_COUNT; ++i ) {
		if ( handles[i] != NULL && handles[i]->texture != 0 ) {
			R_GLStateCache().ActiveTextureUnit( i );
			R_GLStateCache().BindTexture( i, GL_TEXTURE_2D, handles[i]->texture );
		}
	}
}

static void R_ModernGLExecutor_UnbindDeferredResolveTextures( void ) {
	for ( int i = 0; i < MODERN_GL_DEFERRED_TEXTURE_COUNT; ++i ) {
		R_GLStateCache().BindTexture( i, GL_TEXTURE_2D, 0 );
	}
	R_GLStateCache().ActiveTextureUnit( 0 );
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
	stats.deferredResolveClusterReady = clusterStats.requested && clusterStats.frameValid && clusterStats.uboFallbackReady;
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
		R_ModernGLExecutor_BindDeferredResolveTextures( textureHandles );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	}
	R_RendererMetrics_EndGpuTimer();

	R_ModernGLExecutor_UnbindDeferredResolveTextures();
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
	if ( sceneColor.framebuffer == 0 || sceneColor.texture == 0 || sceneDepth.texture == 0 || glFramebufferTexture2D == NULL || glCheckFramebufferStatus == NULL ) {
		return false;
	}
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, sceneColor.framebuffer );
	const GLenum depthAttachment = sceneDepth.type == RENDER_GRAPH_RESOURCE_DEPTH_STENCIL ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
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
	stats.forwardPlusClusterReady = clusterStats.requested && clusterStats.frameValid && clusterStats.uboFallbackReady;
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
}

void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	R_ModernGLExecutor_Shutdown();
	rg_modernGLExecutorCaps = caps;
	rg_modernGLExecutorFeatures = features;
	R_GLStateCache_Init( caps );
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
	if ( glGenFramebuffers != NULL ) {
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
	R_ModernGLShaderLibrary_Shutdown();

	rg_modernGLExecutorVAO = 0;
	rg_modernGLExecutorFrameUBO = 0;
	rg_modernGLExecutorGBufferFBO = 0;
	rg_modernGLExecutorInitialized = false;
	rg_modernGLExecutorAvailable = false;
	rg_modernGLExecutorLowOverheadReady = false;
	memset( &rg_modernGLExecutorCaps, 0, sizeof( rg_modernGLExecutorCaps ) );
	memset( &rg_modernGLExecutorFeatures, 0, sizeof( rg_modernGLExecutorFeatures ) );
	R_ModernClusteredLighting_Shutdown();
	R_GLStateCache_Shutdown();
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, false );
}

void R_ModernGLExecutor_PrepareFrame( const idScenePacketFrame &packetFrame, const idRenderGraph &graph ) {
	const bool visibleDepthRequested = r_rendererModernVisibleDepth.GetBool() || r_rendererModernDepthDebug.GetInteger() > 0;
	const bool deferredResolveRequested = r_rendererModernDeferred.GetBool() || r_rendererModernDeferredDebug.GetInteger() > 0;
	const bool forwardPlusRequested = r_rendererForwardPlus.GetBool();
	const bool opaqueGBufferRequested = r_rendererModernOpaque.GetBool() || r_rendererModernGBufferDebug.GetInteger() > 0 || deferredResolveRequested;
	const bool clusteredLightingRequested = r_rendererModernExecutor.GetBool() || opaqueGBufferRequested || deferredResolveRequested || forwardPlusRequested || r_rendererClusterDebug.GetInteger() > 0;
	const bool enabled = r_rendererModernExecutor.GetBool() || r_rendererModernSubmit.GetBool() || visibleDepthRequested || opaqueGBufferRequested || deferredResolveRequested || forwardPlusRequested || clusteredLightingRequested;
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		enabled,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		rg_modernGLExecutorStats );

	if ( rg_modernGLExecutorStats.enabled
		&& rg_modernGLExecutorStats.available
		&& rg_modernGLExecutorStats.initialized
		&& rg_modernGLExecutorStats.vaoReady
		&& rg_modernGLExecutorStats.frameUBOReady
		&& rg_modernGLExecutorStats.shaderLibraryReady ) {
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

	R_ModernGLExecutor_UpdateGpuDrivenBuffers( rg_modernGLExecutorStats );
	R_ModernGLExecutor_UpdateFrameUBO( rg_modernGLExecutorStats );
	R_ModernClusteredLighting_PrepareFrame( packetFrame, clusteredLightingRequested );
	R_ModernGLExecutor_SubmitVisibleDepth( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitGBuffer( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitDeferredResolve( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitForwardPlus( rg_modernGLExecutorStats );
	R_ModernGLExecutor_SubmitPlan( rg_modernGLExecutorStats );
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );

	if ( r_rendererMetrics.GetInteger() >= 2 && enabled ) {
		common->Printf(
			"modernGLExecutor status=%s passes=%d/%d fallback=%d draws=%d prepared=%d material=%d resources=%d geometry=%d gui=%d world=%d plan=%d planDraws=%d depth=%d materialFamily=%d planFallback=%d batches=%d programSwitches=%d materialSwitches=%d planOverflow=%d submit=%d submitDraws=%d submitFallback=%d submitMissing(vbo=%d ibo=%d) submitIndexUpload=%d submitted=%d submittedDraws=%d submittedFallback=%d submittedUpload=%d submitBatches(program=%d vbo=%d ibo=%d scissor=%d material=%d) uniforms=%d frameUBO=%d submitOverflow=%d visibleDepth(req=%d exec=%d res=%d/%d draws=%d shadow=%d fallback=%d/%d stencil=%d mismatch=%d clear=%d resolve=%d overlay=%d/%d) gbuffer(req=%d exec=%d res=%d mrt=%d draws=%d fallback=%d alpha=%d skinned=%d clear=%d att=%d bpp=%d bw=%dKB overlay=%d/%d) deferred(req=%d exec=%d res=%d out=%d program=%d cluster=%d pixels=%d lights=%d point=%d projected=%d lightGrid=%d reads=%d fallback=%d unsupported=%d fog=%d special=%d overflow=%d clear=%d debug=%d overlay=%d/%d) vao=%d ubo=%d shaders=%d shaderFails=%d glsl=%d gpuDriven=%d ssbo=%d indirect=%d validation=%d compute=%d sceneRecords=%d indirectRecords=%d gpuBytes(scene=%d indirect=%d validation=%d) dispatches=%d lowOverhead=%d dsa=%d multiBind=%d dsaUpdates=%d multiBindBatches=%d\n",
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
			rg_modernGLExecutorStats.lowOverheadReady ? 1 : 0,
			rg_modernGLExecutorStats.tierUsesDSA ? 1 : 0,
			rg_modernGLExecutorStats.tierUsesMultiBind ? 1 : 0,
			rg_modernGLExecutorStats.lowOverheadDSAUpdates,
			rg_modernGLExecutorStats.lowOverheadMultiBindBatches );
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
	}
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
		"Modern GL executor: %s, cvar=%d, submitCvar=%d, visibleDepthCvar=%d, depthDebug=%d, opaqueCvar=%d, gbufferDebug=%d, deferredCvar=%d, deferredDebug=%d, VAO=%d, frameUBO=%d, shaderLibrary=%d, shaderPrograms=%d, highestGLSL=%d, drawPlan=%d, planDraws=%d, depth=%d, materialFamily=%d, planFallback=%d, batches=%d, submitPlan=%d, submitDraws=%d, submitFallback=%d, missingVBO=%d, missingIBO=%d, indexUpload=%d, submitted=%d/%d upload=%d fallback=%d, visibleDepth(req=%d exec=%d res=%d/%d draws=%d shadow=%d fallback=%d/%d stencil=%d mismatch=%d clears=%d resolves=%d overlay=%d/%d), gbuffer(req=%d exec=%d res=%d mrt=%d draws=%d fallback=%d alpha=%d skinned=%d clear=%d att=%d bpp=%d bw=%dKB overlay=%d/%d), deferred(req=%d exec=%d res=%d out=%d program=%d cluster=%d pixels=%d lights=%d point=%d projected=%d lightGrid=%d reads=%d fallback=%d unsupported=%d fog=%d special=%d overflow=%d clears=%d overlay=%d/%d), submitBatches(program=%d vbo=%d ibo=%d), gpuDriven=%d ssbo=%d indirect=%d validation=%d compute=%d sceneRecords=%d indirectRecords=%d gpuBytes(scene=%d indirect=%d validation=%d), lowOverhead=%d dsa=%d multiBind=%d dsaUpdates=%d multiBindBatches=%d, legacyFallback=%d\n",
		rg_modernGLExecutorStats.available ? "available" : "unavailable",
		r_rendererModernExecutor.GetBool() ? 1 : 0,
		r_rendererModernSubmit.GetBool() ? 1 : 0,
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
		rg_modernGLExecutorStats.lowOverheadReady ? 1 : 0,
		rg_modernGLExecutorStats.tierUsesDSA ? 1 : 0,
		rg_modernGLExecutorStats.tierUsesMultiBind ? 1 : 0,
		rg_modernGLExecutorStats.lowOverheadDSAUpdates,
		rg_modernGLExecutorStats.lowOverheadMultiBindBatches,
		rg_modernGLExecutorStats.legacyFallback ? 1 : 0 );
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
	R_GLStateCache_PrintGfxInfo();
	R_ModernGLShaderLibrary_PrintGfxInfo();
}

bool RendererModernGLExecutor_RunSelfTest( void ) {
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

static bool R_ModernGLExecutor_BuildVisiblePathSelfTestFrame( idScenePacketFrame &packetFrame, idRenderGraph &graph ) {
	packetFrame.Clear();

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
	if ( !r_rendererModernOpaque.GetBool() && r_rendererModernGBufferDebug.GetInteger() <= 0 ) {
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
	if ( !opaqueProgram->reflection.usesMainTexture || opaqueProgram->mainTextureLocation < 0 || !alphaProgram->reflection.usesMainTexture || alphaProgram->mainTextureLocation < 0 ) {
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
	if ( !r_rendererModernDeferred.GetBool() && r_rendererModernDeferredDebug.GetInteger() <= 0 ) {
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
	if ( !r_rendererForwardPlus.GetBool() ) {
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
