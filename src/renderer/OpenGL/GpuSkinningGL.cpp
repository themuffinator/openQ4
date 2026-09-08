// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	OpenGL GPU animation adapter.

	The shared contract deliberately exposes no OpenGL object names.  This
	adapter uploads one immutable bind-pose stream, the exact four-weight stream,
	and the current joint palette into the renderer's bounded frame ring.  A
	compute dispatch writes an ordinary idDrawVert stream, so every existing
	ambient, interaction, subview, view-model, and shadow-map draw keeps using
	the established vertex ABI.  Stencil volumes continue to use their CPU
	shadow cache and never enter this path.

===============================================================================
*/

#include "../tr_local.h"
#include "../GLStateCache.h"
#include "../GpuSkinning.h"
#include "../RendererUpload.h"

#include <limits>

namespace {

static const int GPU_SKINNING_GL_WORKGROUP_SIZE = 64;

struct gpuSkinningGLState_t {
	GLuint	program;
	GLint	sourceWordOffset;
	GLint	skinWordOffset;
	GLint	jointWordOffset;
	GLint	outputWordOffset;
	GLint	vertexCount;
	GLint	jointCount;
	bool	available;
};

static gpuSkinningGLState_t gpuSkinningGL;

static GLuint R_GpuSkinningGL_CompileShader( const char *source, bool esProfile ) {
	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL
			|| glGetShaderiv == NULL || glDeleteShader == NULL ) {
		return 0;
	}

	GLuint shader = glCreateShader( GL_COMPUTE_SHADER );
	if ( shader == 0 ) {
		return 0;
	}
	// The compute body uses the common GL 4.3 / ES 3.1 feature set. Select the
	// language from the actual context, including explicit ES float precision.
	const char *sources[] = {
		esProfile ? "#version 310 es\nprecision highp float;\nprecision highp int;\n" : "#version 430\n",
		source
	};
	glShaderSource( shader, 2, sources, NULL );
	glCompileShader( shader );

	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( compiled != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetShaderInfoLog != NULL ) {
			glGetShaderInfoLog( shader, sizeof( log ) - 1, NULL, log );
		}
		common->Warning( "GPU skinning: OpenGL compute shader compilation failed: %s", log );
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

static GLuint R_GpuSkinningGL_CreateProgram( bool esProfile ) {
	static const char *computeSource =
		"layout(local_size_x = 64) in;\n"
		"layout(std430, binding = 0) readonly buffer SourceWords { uint words[]; } sourceData;\n"
		"layout(std430, binding = 1) readonly buffer SkinWords { uint words[]; } skinData;\n"
		"layout(std430, binding = 2) readonly buffer JointWords { uint words[]; } jointData;\n"
		"layout(std430, binding = 3) writeonly buffer OutputWords { uint words[]; } outputData;\n"
		"uniform uint uSourceWordOffset;\n"
		"uniform uint uSkinWordOffset;\n"
		"uniform uint uJointWordOffset;\n"
		"uniform uint uOutputWordOffset;\n"
		"uniform uint uVertexCount;\n"
		"uniform uint uJointCount;\n"
		"vec4 LoadJointRow(uint jointIndex, uint row) {\n"
		"  uint base = uJointWordOffset + jointIndex * 12u + row * 4u;\n"
		"  return vec4(uintBitsToFloat(jointData.words[base + 0u]),\n"
		"              uintBitsToFloat(jointData.words[base + 1u]),\n"
		"              uintBitsToFloat(jointData.words[base + 2u]),\n"
		"              uintBitsToFloat(jointData.words[base + 3u]));\n"
		"}\n"
		"vec3 TransformBasis(vec4 row0, vec4 row1, vec4 row2, vec3 value, float w) {\n"
		"  vec4 source = vec4(value, w);\n"
		"  return vec3(dot(row0, source), dot(row1, source), dot(row2, source));\n"
		"}\n"
		"void StoreVec3(uint base, uint member, vec3 value) {\n"
		"  outputData.words[base + member + 0u] = floatBitsToUint(value.x);\n"
		"  outputData.words[base + member + 1u] = floatBitsToUint(value.y);\n"
		"  outputData.words[base + member + 2u] = floatBitsToUint(value.z);\n"
		"}\n"
		"void main() {\n"
		"  uint vertex = gl_GlobalInvocationID.x;\n"
		"  if (vertex >= uVertexCount) { return; }\n"
		"  uint sourceBase = uSourceWordOffset + vertex * 16u;\n"
		"  uint outputBase = uOutputWordOffset + vertex * 16u;\n"
		"  for (uint word = 0u; word < 16u; ++word) {\n"
		"    outputData.words[outputBase + word] = sourceData.words[sourceBase + word];\n"
		"  }\n"
		"  uint skinBase = uSkinWordOffset + vertex * 8u;\n"
		"  vec4 row0 = vec4(0.0);\n"
		"  vec4 row1 = vec4(0.0);\n"
		"  vec4 row2 = vec4(0.0);\n"
		"  for (uint influence = 0u; influence < 4u; ++influence) {\n"
		"    float weight = uintBitsToFloat(skinData.words[skinBase + 4u + influence]);\n"
		"    if (weight == 0.0) { continue; }\n"
		"    uint jointIndex = skinData.words[skinBase + influence];\n"
		"    if (jointIndex >= uJointCount) { return; }\n"
		"    row0 += LoadJointRow(jointIndex, 0u) * weight;\n"
		"    row1 += LoadJointRow(jointIndex, 1u) * weight;\n"
		"    row2 += LoadJointRow(jointIndex, 2u) * weight;\n"
		"  }\n"
		"  vec3 position = vec3(uintBitsToFloat(sourceData.words[sourceBase + 0u]),\n"
		"                       uintBitsToFloat(sourceData.words[sourceBase + 1u]),\n"
		"                       uintBitsToFloat(sourceData.words[sourceBase + 2u]));\n"
		"  vec3 normal = vec3(uintBitsToFloat(sourceData.words[sourceBase + 4u]),\n"
		"                     uintBitsToFloat(sourceData.words[sourceBase + 5u]),\n"
		"                     uintBitsToFloat(sourceData.words[sourceBase + 6u]));\n"
		"  vec3 tangent0 = vec3(uintBitsToFloat(sourceData.words[sourceBase + 8u]),\n"
		"                       uintBitsToFloat(sourceData.words[sourceBase + 9u]),\n"
		"                       uintBitsToFloat(sourceData.words[sourceBase + 10u]));\n"
		"  vec3 tangent1 = vec3(uintBitsToFloat(sourceData.words[sourceBase + 11u]),\n"
		"                       uintBitsToFloat(sourceData.words[sourceBase + 12u]),\n"
		"                       uintBitsToFloat(sourceData.words[sourceBase + 13u]));\n"
		"  StoreVec3(outputBase, 0u, TransformBasis(row0, row1, row2, position, 1.0));\n"
		"  StoreVec3(outputBase, 4u, TransformBasis(row0, row1, row2, normal, 0.0));\n"
		"  StoreVec3(outputBase, 8u, TransformBasis(row0, row1, row2, tangent0, 0.0));\n"
		"  StoreVec3(outputBase, 11u, TransformBasis(row0, row1, row2, tangent1, 0.0));\n"
		"}\n";

	if ( glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL
			|| glGetProgramiv == NULL || glDeleteProgram == NULL ) {
		return 0;
	}

	GLuint shader = R_GpuSkinningGL_CompileShader( computeSource, esProfile );
	if ( shader == 0 ) {
		return 0;
	}
	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( shader );
		return 0;
	}
	glAttachShader( program, shader );
	glLinkProgram( program );
	if ( glDetachShader != NULL ) {
		glDetachShader( program, shader );
	}
	glDeleteShader( shader );

	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Warning( "GPU skinning: OpenGL compute program link failed: %s", log );
		glDeleteProgram( program );
		return 0;
	}
	return program;
}

static bool R_GpuSkinningGL_AllocationValid( const rendererUploadAllocation_t &allocation ) {
	return allocation.vbo != 0 && allocation.offset >= 0 && allocation.size > 0
			&& ( allocation.offset & 3 ) == 0;
}

} // namespace

void R_BackendGpuSkinning_Init( const renderBackendCaps_t &caps ) {
	memset( &gpuSkinningGL, 0, sizeof( gpuSkinningGL ) );
	if ( !caps.hasCompute || !caps.hasSSBO || glBindBufferBase == NULL
			|| glDispatchCompute == NULL || glMemoryBarrier == NULL
			|| glUniform1ui == NULL || glGetUniformLocation == NULL ) {
		return;
	}

	gpuSkinningGL.program = R_GpuSkinningGL_CreateProgram( caps.profile == RENDERER_CONTEXT_PROFILE_ES );
	if ( gpuSkinningGL.program == 0 ) {
		return;
	}
	gpuSkinningGL.sourceWordOffset = glGetUniformLocation( gpuSkinningGL.program, "uSourceWordOffset" );
	gpuSkinningGL.skinWordOffset = glGetUniformLocation( gpuSkinningGL.program, "uSkinWordOffset" );
	gpuSkinningGL.jointWordOffset = glGetUniformLocation( gpuSkinningGL.program, "uJointWordOffset" );
	gpuSkinningGL.outputWordOffset = glGetUniformLocation( gpuSkinningGL.program, "uOutputWordOffset" );
	gpuSkinningGL.vertexCount = glGetUniformLocation( gpuSkinningGL.program, "uVertexCount" );
	gpuSkinningGL.jointCount = glGetUniformLocation( gpuSkinningGL.program, "uJointCount" );
	gpuSkinningGL.available = gpuSkinningGL.sourceWordOffset >= 0
			&& gpuSkinningGL.skinWordOffset >= 0 && gpuSkinningGL.jointWordOffset >= 0
			&& gpuSkinningGL.outputWordOffset >= 0 && gpuSkinningGL.vertexCount >= 0
			&& gpuSkinningGL.jointCount >= 0;
	if ( !gpuSkinningGL.available ) {
		R_BackendGpuSkinning_Shutdown();
	}
}

void R_BackendGpuSkinning_Shutdown( void ) {
	if ( gpuSkinningGL.program != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( gpuSkinningGL.program );
	}
	memset( &gpuSkinningGL, 0, sizeof( gpuSkinningGL ) );
}

bool R_BackendGpuSkinning_PrepareAmbientCache( srfTriangles_t *tri, bool needsLighting ) {
	(void)needsLighting;
	if ( tri == NULL ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_VERTEX_COUNT );
		return false;
	}
	if ( tri->ambientCache != NULL ) {
		return true;
	}
	if ( !gpuSkinningGL.available ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_BACKEND_UNAVAILABLE );
		return false;
	}

	gpuSkinningSurface_t surface;
	if ( !R_GpuSkinning_GetSurface( tri, surface ) ) {
		R_GpuSkinning_RecordFallback( surface.fallbackReason );
		return false;
	}
	const gpuSkinningFallbackReason_t validation = R_GpuSkinning_ValidateSurface( surface );
	if ( validation != GPU_SKINNING_FALLBACK_NONE ) {
		R_GpuSkinning_RecordFallback( validation );
		return false;
	}
	if ( surface.numVerts <= 0 || surface.numVerts > ( std::numeric_limits<int>::max )() / static_cast<int>( sizeof( idDrawVert ) )
			|| surface.palette.matrixStrideFloats != GPU_SKINNING_JOINT_FLOATS ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_VERTEX_COUNT );
		return false;
	}
	if ( surface.palette.numJoints <= 0 ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_JOINT_COUNT );
		return false;
	}

	const int sourceBytes = surface.numVerts * static_cast<int>( sizeof( idDrawVert ) );
	const int skinBytes = surface.numVerts * static_cast<int>( sizeof( gpuSkinningVertex_t ) );
	if ( surface.palette.numJoints > ( std::numeric_limits<int>::max )() / ( GPU_SKINNING_JOINT_FLOATS * static_cast<int>( sizeof( float ) ) ) ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_JOINT_COUNT );
		return false;
	}
	const int jointBytes = surface.palette.numJoints * GPU_SKINNING_JOINT_FLOATS * static_cast<int>( sizeof( float ) );

	rendererUploadAllocation_t sourceAllocation;
	rendererUploadAllocation_t skinAllocation;
	rendererUploadAllocation_t jointAllocation;
	if ( !R_RendererUpload_AllocFrameTemp( const_cast<idDrawVert *>( surface.bindPoseVerts ), sourceBytes, 16, sourceAllocation )
			|| !R_RendererUpload_AllocFrameTemp( const_cast<gpuSkinningVertex_t *>( surface.skinVerts ), skinBytes, 16, skinAllocation )
			|| !R_RendererUpload_AllocFrameTemp( const_cast<float *>( surface.palette.matrices ), jointBytes, 16, jointAllocation )
			|| !R_GpuSkinningGL_AllocationValid( sourceAllocation )
			|| !R_GpuSkinningGL_AllocationValid( skinAllocation )
			|| !R_GpuSkinningGL_AllocationValid( jointAllocation ) ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_PALETTE_ALLOCATION );
		return false;
	}

	vertCache_t *outputCache = vertexCache.AllocFrameTemp(
			const_cast<idDrawVert *>( surface.bindPoseVerts ), sourceBytes );
	if ( outputCache == NULL || outputCache->vbo == 0 || outputCache->offset < 0
			|| ( outputCache->offset & 3 ) != 0 ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_PALETTE_ALLOCATION );
		return false;
	}

	R_GLStateCache().UseProgram( gpuSkinningGL.program );
	R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 0, sourceAllocation.vbo );
	R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 1, skinAllocation.vbo );
	R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 2, jointAllocation.vbo );
	R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, 3, outputCache->vbo );
	glUniform1ui( gpuSkinningGL.sourceWordOffset, static_cast<GLuint>( sourceAllocation.offset / 4 ) );
	glUniform1ui( gpuSkinningGL.skinWordOffset, static_cast<GLuint>( skinAllocation.offset / 4 ) );
	glUniform1ui( gpuSkinningGL.jointWordOffset, static_cast<GLuint>( jointAllocation.offset / 4 ) );
	glUniform1ui( gpuSkinningGL.outputWordOffset, static_cast<GLuint>( outputCache->offset / 4 ) );
	glUniform1ui( gpuSkinningGL.vertexCount, static_cast<GLuint>( surface.numVerts ) );
	glUniform1ui( gpuSkinningGL.jointCount, static_cast<GLuint>( surface.palette.numJoints ) );
	glDispatchCompute( static_cast<GLuint>(
			( surface.numVerts + GPU_SKINNING_GL_WORKGROUP_SIZE - 1 ) / GPU_SKINNING_GL_WORKGROUP_SIZE ), 1, 1 );
	glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
			| GL_BUFFER_UPDATE_BARRIER_BIT );
	for ( GLuint binding = 0; binding < 4; ++binding ) {
		R_GLStateCache().BindBufferBase( GL_SHADER_STORAGE_BUFFER, binding, 0 );
	}
	R_GLStateCache().UseProgram( 0 );

	tri->ambientCache = outputCache;
	tri->tempAmbientCache = true;
	return true;
}

void R_BackendGpuSkinning_PrintGfxInfo( void ) {
	common->Printf( "GPU skinning compute (OpenGL): %s\n",
			gpuSkinningGL.available ? "available" : "unavailable" );
}
