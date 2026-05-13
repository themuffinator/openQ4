// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLExecutor.h"
#include "ModernGLDrawPlan.h"
#include "ModernGLShaderLibrary.h"
#include "ModernGLSubmitPlan.h"
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
static GLint rg_modernGLExecutorComputeRecordCountLocation = -1;
static bool rg_modernGLExecutorInitialized = false;
static bool rg_modernGLExecutorAvailable = false;
static bool rg_modernGLExecutorGpuDrivenReady = false;
static bool rg_modernGLExecutorLowOverheadReady = false;
static unsigned int rg_modernGLExecutorCachedUniformBuffer = static_cast<unsigned int>( -1 );

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
	glBindBuffer( target, buffer );
	glBufferSubData( target, 0, bytes, data );
	glBindBuffer( target, 0 );
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

static void R_ModernGLExecutor_DestroyGpuDrivenObjects( void ) {
	if ( rg_modernGLExecutorComputeProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_modernGLExecutorComputeProgram );
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
	rg_modernGLExecutorComputeRecordCountLocation = -1;
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
	if ( !R_ModernGLExecutor_CreateBuffer( GL_DRAW_INDIRECT_BUFFER, indirectBytes, NULL, GL_DYNAMIC_DRAW, rg_modernGLExecutorIndirectBuffer ) ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
	GLuint zeroValidation[MODERN_GL_GPU_DRIVEN_VALIDATION_COUNTERS] = { 0, 0, 0, 0 };
	if ( !R_ModernGLExecutor_CreateBuffer( GL_SHADER_STORAGE_BUFFER, validationBytes, zeroValidation, GL_DYNAMIC_DRAW, rg_modernGLExecutorValidationSSBO ) ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}

	rg_modernGLExecutorComputeProgram = R_ModernGLExecutor_CompileGpuDrivenComputeProgram();
	if ( rg_modernGLExecutorComputeProgram == 0 ) {
		R_ModernGLExecutor_DestroyGpuDrivenObjects();
		return false;
	}
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
	stats.tierUsesDSA = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.directStateAccess;
	stats.tierUsesMultiBind = rg_modernGLExecutorLowOverheadReady && rg_modernGLExecutorFeatures.multiBind;
	stats.shaderProgramCount = shaderStats.programCount;
	stats.shaderFailureCount = shaderStats.failedProgramCount;
	stats.highestGLSLVersion = shaderStats.highestGLSLVersion;
	R_ModernGLExecutor_SetStatus( stats, enabled ? "unavailable" : "off" );
}

static bool R_ModernGLExecutor_IsWorldPass( renderPassCategory_t category ) {
	return category != RENDER_PASS_GUI && category != RENDER_PASS_PRESENT;
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
		if ( draw.hasGeometry ) {
			stats.geometryDrawPackets++;
		}
		if ( draw.materialRecord != NULL ) {
			stats.materialDrawPackets++;
		}
		if ( draw.materialRecordIndex >= 0 ) {
			stats.resourceDrawPackets++;
		}
		if ( draw.passCategory == RENDER_PASS_GUI ) {
			stats.guiDrawPackets++;
		}
		if ( R_ModernGLExecutor_IsWorldPass( draw.passCategory ) ) {
			stats.worldDrawPackets++;
		}
		if ( draw.hasGeometry && draw.materialRecordIndex >= 0 ) {
			stats.preparedDrawPackets++;
		}
	}

	stats.legacyFallback = true;
	R_ModernGLExecutor_SetStatus( stats, "prepared-legacy-fallback" );
}

static void R_ModernGLExecutor_BindUniformBuffer( GLuint buffer ) {
	if ( rg_modernGLExecutorCachedUniformBuffer == buffer ) {
		return;
	}
	glBindBuffer( GL_UNIFORM_BUFFER, buffer );
	rg_modernGLExecutorCachedUniformBuffer = buffer;
}

static void R_ModernGLExecutor_BindFrameUniformBufferBase( modernGLExecutorStats_t &stats ) {
	if ( rg_modernGLExecutorFrameUBO == 0 ) {
		return;
	}
	if ( rg_modernGLExecutorLowOverheadReady && glBindBuffersBase != NULL ) {
		GLuint buffers[1] = { rg_modernGLExecutorFrameUBO };
		glBindBuffersBase( GL_UNIFORM_BUFFER, 0, 1, buffers );
		stats.lowOverheadMultiBindBatches++;
		return;
	}
	if ( glBindBufferBase != NULL ) {
		glBindBufferBase( GL_UNIFORM_BUFFER, 0, rg_modernGLExecutorFrameUBO );
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
		glBindBufferBase( GL_UNIFORM_BUFFER, 0, 0 );
	}
}

static const GLvoid *R_ModernGLExecutor_BufferOffset( int offset ) {
	return reinterpret_cast<const GLvoid *>( static_cast<uintptr_t>( offset ) );
}

static void R_ModernGLExecutor_SetSubmitScissor( const modernGLSubmitCommand_t &command, const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		glViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		return;
	}

	glViewport(
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

	glScissor(
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
	case MODERN_GL_SHADER_GUI:
	case MODERN_GL_SHADER_POST_COPY:
		glUniform4f( command.debugColorLocation, 1.0f, 1.0f, 1.0f, 1.0f );
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
	case MODERN_GL_SHADER_LIGHT_GRID:
		glUniform4f( command.localParamsLocation, 1.0f, 0.0f, 0.0f, 0.0f );
		break;
	case MODERN_GL_SHADER_FOG_BLEND:
		glUniform4f( command.localParamsLocation, 0.25f, 0.38f, 0.42f, 0.48f );
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
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_LIGHT_GRID
		|| pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FOG_BLEND;
}

static void R_ModernGLExecutor_BindGpuDrivenBuffers( modernGLExecutorStats_t &stats ) {
	if ( !stats.gpuDrivenReady ) {
		return;
	}
	if ( stats.tierUsesMultiBind && glBindBuffersBase != NULL ) {
		GLuint buffers[2] = { rg_modernGLExecutorSceneSSBO, rg_modernGLExecutorValidationSSBO };
		glBindBuffersBase( GL_SHADER_STORAGE_BUFFER, 1, 2, buffers );
		stats.lowOverheadMultiBindBatches++;
	} else if ( glBindBufferBase != NULL ) {
		glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 1, rg_modernGLExecutorSceneSSBO );
		glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 2, rg_modernGLExecutorValidationSSBO );
	}
	if ( rg_modernGLExecutorIndirectBuffer != 0 ) {
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, rg_modernGLExecutorIndirectBuffer );
	}
}

static void R_ModernGLExecutor_UnbindGpuDrivenBuffers( void ) {
	if ( glBindBufferBase != NULL ) {
		glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 1, 0 );
		glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 2, 0 );
	}
	if ( glBindBuffer != NULL ) {
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
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
		record.counts[2] = static_cast<float>( entry.materialRecordIndex );
		record.counts[3] = static_cast<float>( entry.passCategory );
		record.ids[0] = static_cast<GLuint>( entry.shaderKind );
		record.ids[1] = static_cast<GLuint>( entry.drawPacketIndex );
		record.ids[2] = entry.materialRecordIndex >= 0 ? static_cast<GLuint>( entry.materialRecordIndex ) : 0xffffffffu;
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
			indirect.baseInstance = command.materialRecordIndex >= 0 ? static_cast<GLuint>( command.materialRecordIndex ) : 0u;
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
		glUseProgram( rg_modernGLExecutorComputeProgram );
		if ( rg_modernGLExecutorComputeRecordCountLocation >= 0 ) {
			glUniform1ui( rg_modernGLExecutorComputeRecordCountLocation, static_cast<GLuint>( sceneRecordCount ) );
		}
		glDispatchCompute( static_cast<GLuint>( ( sceneRecordCount + 63 ) / 64 ), 1, 1 );
		glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT );
		glUseProgram( 0 );
		stats.gpuDrivenComputeDispatches++;
	}
	R_ModernGLExecutor_UnbindGpuDrivenBuffers();
}

static bool R_ModernGLExecutor_SubmitCommand( const modernGLSubmitCommand_t &command, modernGLExecutorStats_t &stats ) {
	const drawPacket_t *draw = command.drawPlanEntry != NULL ? command.drawPlanEntry->drawPacket : NULL;
	const drawSurf_t *surf = draw != NULL ? draw->legacyDrawSurf : NULL;
	if ( draw == NULL || surf == NULL || surf->space == NULL || draw->viewDef == NULL ) {
		stats.submittedFallbackDraws++;
		return false;
	}
	if ( command.program == 0 || command.vertexBuffer == 0 || command.modelViewProjectionLocation < 0 ) {
		stats.submittedFallbackDraws++;
		return false;
	}

	GLuint indexBuffer = command.indexBuffer;
	int indexOffset = command.indexCacheOffset;
	if ( command.indexed ) {
		if ( command.uploadIndexBuffer ) {
			if ( command.clientIndexData == NULL || command.clientIndexBytes <= 0 ) {
				stats.submittedFallbackDraws++;
				return false;
			}
			rendererUploadAllocation_t indexUpload;
			if ( !R_RendererUpload_AllocFrameTemp( const_cast<void *>( command.clientIndexData ), command.clientIndexBytes, 4, indexUpload ) ) {
				stats.submittedFallbackDraws++;
				return false;
			}
			indexBuffer = indexUpload.vbo;
			indexOffset = indexUpload.offset;
		}
		if ( indexBuffer == 0 ) {
			stats.submittedFallbackDraws++;
			return false;
		}
	}

	float modelViewProjection[16];
	myGlMultMatrix( surf->space->modelViewMatrix, draw->viewDef->projectionMatrix, modelViewProjection );

	glUseProgram( command.program );
	R_ModernGLExecutor_BindFrameUniformBufferBase( stats );
	glUniformMatrix4fv( command.modelViewProjectionLocation, 1, GL_FALSE, modelViewProjection );
	R_ModernGLExecutor_SetDebugColor( command );
	R_ModernGLExecutor_SetLocalParams( command );
	if ( command.mainTextureLocation >= 0 && glUniform1i != NULL ) {
		glUniform1i( command.mainTextureLocation, 0 );
	}

	R_ModernGLExecutor_SetSubmitScissor( command, draw->viewDef );
	glBindBuffer( GL_ARRAY_BUFFER, command.vertexBuffer );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer(
		0,
		3,
		GL_FLOAT,
		GL_FALSE,
		command.vertexStride,
		R_ModernGLExecutor_BufferOffset( command.ambientCacheOffset + DRAWVERT_XYZ_OFFSET ) );

	if ( command.indexed ) {
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBuffer );
		glDrawElements(
			GL_TRIANGLES,
			r_singleTriangle.GetBool() ? Min( 3, command.indexCount ) : command.indexCount,
			static_cast<GLenum>( command.indexType ),
			R_ModernGLExecutor_BufferOffset( indexOffset ) );
	} else {
		glDrawArrays( GL_TRIANGLES, 0, command.vertexCount );
	}

	stats.submittedDraws++;
	if ( R_ModernGLExecutor_IsDepthPipeline( command.pipeline ) ) {
		stats.submittedDepthDraws++;
	} else if ( R_ModernGLExecutor_IsMaterialPipeline( command.pipeline ) ) {
		stats.submittedMaterialDraws++;
	}
	if ( command.uploadIndexBuffer ) {
		stats.submittedIndexUploadDraws++;
	}
	return true;
}

static void R_ModernGLExecutor_RestoreAfterSubmit( void ) {
	glDisableVertexAttribArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	if ( glBindBufferBase != NULL ) {
		glBindBufferBase( GL_UNIFORM_BUFFER, 0, 0 );
	}
	R_ModernGLExecutor_UnbindGpuDrivenBuffers();
	glUseProgram( 0 );
	glBindVertexArray( 0 );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

static void R_ModernGLExecutor_SubmitPlan( modernGLExecutorStats_t &stats ) {
	if ( !r_rendererModernSubmit.GetBool() || !stats.enabled || !stats.available || !stats.submitPlanReady || !rg_modernGLExecutorInitialized || rg_modernGLExecutorVAO == 0 ) {
		return;
	}

	glBindVertexArray( rg_modernGLExecutorVAO );
	glEnable( GL_SCISSOR_TEST );
	glEnable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_BLEND );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_FALSE );
	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );

	for ( int i = 0; i < rg_modernGLSubmitPlan.NumCommands(); ++i ) {
		R_ModernGLExecutor_SubmitCommand( rg_modernGLSubmitPlan.Command( i ), stats );
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
		stats.submitPlanFrameUBOBinds );
}

void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	R_ModernGLExecutor_Shutdown();
	rg_modernGLExecutorCaps = caps;
	rg_modernGLExecutorFeatures = features;
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
	rg_modernGLExecutorCachedUniformBuffer = 0;

	rg_modernGLExecutorGpuDrivenReady = R_ModernGLExecutor_InitGpuDrivenObjects( caps, features );
	if ( features.gpuDriven && !rg_modernGLExecutorGpuDrivenReady ) {
		common->Printf( "Modern GL executor: GL43 GPU-driven resources unavailable, keeping GL3 submit bridge active\n" );
	}
	if ( features.lowOverhead && !rg_modernGLExecutorLowOverheadReady ) {
		common->Printf( "Modern GL executor: GL45 low-overhead DSA/multi-bind path unavailable, keeping bind-based path active\n" );
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
	R_ModernGLShaderLibrary_Shutdown();

	rg_modernGLExecutorVAO = 0;
	rg_modernGLExecutorFrameUBO = 0;
	rg_modernGLExecutorInitialized = false;
	rg_modernGLExecutorAvailable = false;
	rg_modernGLExecutorLowOverheadReady = false;
	rg_modernGLExecutorCachedUniformBuffer = static_cast<unsigned int>( -1 );
	memset( &rg_modernGLExecutorCaps, 0, sizeof( rg_modernGLExecutorCaps ) );
	memset( &rg_modernGLExecutorFeatures, 0, sizeof( rg_modernGLExecutorFeatures ) );
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, false );
}

void R_ModernGLExecutor_PrepareFrame( const idScenePacketFrame &packetFrame, const idRenderGraph &graph ) {
	const bool enabled = r_rendererModernExecutor.GetBool();
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
	R_ModernGLExecutor_SubmitPlan( rg_modernGLExecutorStats );
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );

	if ( r_rendererMetrics.GetInteger() >= 2 && enabled ) {
		common->Printf(
			"modernGLExecutor status=%s passes=%d/%d fallback=%d draws=%d prepared=%d material=%d resources=%d geometry=%d gui=%d world=%d plan=%d planDraws=%d depth=%d materialFamily=%d planFallback=%d batches=%d programSwitches=%d materialSwitches=%d planOverflow=%d submit=%d submitDraws=%d submitFallback=%d submitMissing(vbo=%d ibo=%d) submitIndexUpload=%d submitted=%d submittedDraws=%d submittedFallback=%d submittedUpload=%d submitBatches(program=%d vbo=%d ibo=%d scissor=%d material=%d) uniforms=%d frameUBO=%d submitOverflow=%d vao=%d ubo=%d shaders=%d shaderFails=%d glsl=%d gpuDriven=%d ssbo=%d indirect=%d validation=%d compute=%d sceneRecords=%d indirectRecords=%d gpuBytes(scene=%d indirect=%d validation=%d) dispatches=%d lowOverhead=%d dsa=%d multiBind=%d dsaUpdates=%d multiBindBatches=%d\n",
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
	}
}

const modernGLExecutorStats_t &R_ModernGLExecutor_Stats( void ) {
	return rg_modernGLExecutorStats;
}

void R_ModernGLExecutor_PrintGfxInfo( void ) {
	common->Printf(
		"Modern GL executor: %s, cvar=%d, submitCvar=%d, VAO=%d, frameUBO=%d, shaderLibrary=%d, shaderPrograms=%d, highestGLSL=%d, drawPlan=%d, planDraws=%d, depth=%d, materialFamily=%d, planFallback=%d, batches=%d, submitPlan=%d, submitDraws=%d, submitFallback=%d, missingVBO=%d, missingIBO=%d, indexUpload=%d, submitted=%d/%d upload=%d fallback=%d, submitBatches(program=%d vbo=%d ibo=%d), gpuDriven=%d ssbo=%d indirect=%d validation=%d compute=%d sceneRecords=%d indirectRecords=%d gpuBytes(scene=%d indirect=%d validation=%d), lowOverhead=%d dsa=%d multiBind=%d dsaUpdates=%d multiBindBatches=%d, legacyFallback=%d\n",
		rg_modernGLExecutorStats.available ? "available" : "unavailable",
		r_rendererModernExecutor.GetBool() ? 1 : 0,
		r_rendererModernSubmit.GetBool() ? 1 : 0,
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
