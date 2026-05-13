// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLShaderLibrary.h"

static modernGLShaderLibraryStats_t rg_modernGLShaderLibraryStats;
static modernGLShaderProgramInfo_t rg_modernGLShaderPrograms[MODERN_GL_SHADER_MAX_PROGRAMS];
static int rg_modernGLShaderProgramCount = 0;

typedef struct modernGLShaderProgramDescriptor_s {
	modernGLShaderProgramKind_t	kind;
	renderPassCategory_t		passCategory;
	rendererMaterialClass_t		materialClass;
	unsigned int				lightingMode;
	unsigned int				shadowMode;
	unsigned int				alphaMode;
	const char					*name;
} modernGLShaderProgramDescriptor_t;

static const modernGLShaderProgramDescriptor_t rg_modernGLShaderProgramDescriptors[MODERN_GL_SHADER_PROGRAM_KIND_COUNT] = {
	{ MODERN_GL_SHADER_DEPTH, RENDER_PASS_DEPTH, RENDER_MATERIAL_OPAQUE, 0, 0, 0, "depth" },
	{ MODERN_GL_SHADER_SHADOW_DEPTH, RENDER_PASS_SHADOW_MAP, RENDER_MATERIAL_SHADOW_ONLY, 0, 1, 0, "shadowDepth" },
	{ MODERN_GL_SHADER_FLAT_MATERIAL, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_OPAQUE, 1, 0, 0, "flatMaterial" },
	{ MODERN_GL_SHADER_LIGHT_GRID, RENDER_PASS_LIGHT_GRID, RENDER_MATERIAL_OPAQUE, 2, 0, 0, "lightGrid" },
	{ MODERN_GL_SHADER_FOG_BLEND, RENDER_PASS_FOG_BLEND, RENDER_MATERIAL_TRANSLUCENT, 3, 0, 1, "fogBlend" },
	{ MODERN_GL_SHADER_GUI, RENDER_PASS_GUI, RENDER_MATERIAL_GUI, 0, 0, 1, "gui" },
	{ MODERN_GL_SHADER_POST_COPY, RENDER_PASS_AUTHORED_POST, RENDER_MATERIAL_POST_PROCESS, 0, 0, 1, "postCopy" }
};

const char *ModernGLShaderProgramKind_Name( modernGLShaderProgramKind_t kind ) {
	switch ( kind ) {
	case MODERN_GL_SHADER_DEPTH:
		return "depth";
	case MODERN_GL_SHADER_SHADOW_DEPTH:
		return "shadowDepth";
	case MODERN_GL_SHADER_FLAT_MATERIAL:
		return "flatMaterial";
	case MODERN_GL_SHADER_LIGHT_GRID:
		return "lightGrid";
	case MODERN_GL_SHADER_FOG_BLEND:
		return "fogBlend";
	case MODERN_GL_SHADER_GUI:
		return "gui";
	case MODERN_GL_SHADER_POST_COPY:
		return "postCopy";
	default:
		return "unknown";
	}
}

static const modernGLShaderProgramDescriptor_t *R_ModernGLShaderLibrary_DescriptorForKind( modernGLShaderProgramKind_t kind ) {
	for ( int i = 0; i < MODERN_GL_SHADER_PROGRAM_KIND_COUNT; ++i ) {
		if ( rg_modernGLShaderProgramDescriptors[i].kind == kind ) {
			return &rg_modernGLShaderProgramDescriptors[i];
		}
	}
	return NULL;
}

static void R_ModernGLShaderLibrary_SetStatus( const char *status ) {
	idStr::snPrintf(
		rg_modernGLShaderLibraryStats.status,
		sizeof( rg_modernGLShaderLibraryStats.status ),
		"%s",
		status ? status : "unknown" );
}

static void R_ModernGLShaderLibrary_ResetStats( void ) {
	memset( &rg_modernGLShaderLibraryStats, 0, sizeof( rg_modernGLShaderLibraryStats ) );
	rg_modernGLShaderLibraryStats.programKindCount = MODERN_GL_SHADER_PROGRAM_KIND_COUNT;
	R_ModernGLShaderLibrary_SetStatus( "unavailable" );
}

static bool R_ModernGLShaderLibrary_HasCoreEntrypoints( void ) {
	return glCreateShader != NULL
		&& glShaderSource != NULL
		&& glCompileShader != NULL
		&& glGetShaderiv != NULL
		&& glGetShaderInfoLog != NULL
		&& glCreateProgram != NULL
		&& glAttachShader != NULL
		&& glDetachShader != NULL
		&& glDeleteShader != NULL
		&& glDeleteProgram != NULL
		&& glBindAttribLocation != NULL
		&& glLinkProgram != NULL
		&& glGetProgramiv != NULL
		&& glGetProgramInfoLog != NULL
		&& glGetUniformLocation != NULL
		&& glGetUniformBlockIndex != NULL
		&& glUniformBlockBinding != NULL
		&& glUseProgram != NULL
		&& glUniform1i != NULL;
}

static bool R_ModernGLShaderLibrary_CanCompile( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !features.shaderLibrary || !features.modernBaseline ) {
		return false;
	}
	if ( !caps.hasGLSL || !caps.hasUBO ) {
		return false;
	}
	if ( !R_ModernGLShaderLibrary_HasCoreEntrypoints() ) {
		return false;
	}
	return true;
}

static int R_ModernGLShaderLibrary_BuildVersionList( const renderBackendCaps_t &caps, const renderFeatureSet_t &features, int versions[4] ) {
	int count = 0;
	if ( caps.glMajor > 3 || ( caps.glMajor == 3 && caps.glMinor >= 3 ) ) {
		versions[count++] = 330;
	}
	if ( features.modernGL41 && ( caps.glMajor > 4 || ( caps.glMajor == 4 && caps.glMinor >= 1 ) ) ) {
		versions[count++] = 410;
	}
	if ( features.gpuDriven && ( caps.glMajor > 4 || ( caps.glMajor == 4 && caps.glMinor >= 3 ) ) ) {
		versions[count++] = 430;
	}
	if ( features.lowOverhead && ( caps.glMajor > 4 || ( caps.glMajor == 4 && caps.glMinor >= 5 ) ) ) {
		versions[count++] = 450;
	}
	return count;
}

static void R_ModernGLShaderLibrary_BuildVertexSource( int glslVersion, char *buffer, int bufferSize ) {
	idStr::snPrintf(
		buffer,
		bufferSize,
		"#version %d\n"
		"layout(location = 0) in vec3 attr_Position;\n"
		"layout(location = 8) in vec2 attr_TexCoord0;\n"
		"layout(std140) uniform ModernFrameConstants {\n"
		"    vec4 viewport;\n"
		"    vec4 frame;\n"
		"    vec4 capabilities;\n"
		"    vec4 reserved;\n"
		"} uFrame;\n"
		"uniform mat4 uModelViewProjection;\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"    vec4 frameJitter = vec4(uFrame.reserved.xy, 0.0, 0.0);\n"
		"    vTexCoord = attr_TexCoord0;\n"
		"    gl_Position = uModelViewProjection * vec4(attr_Position, 1.0) + frameJitter;\n"
		"}\n",
		glslVersion );
}

static void R_ModernGLShaderLibrary_BuildFragmentSource( int glslVersion, modernGLShaderProgramKind_t kind, char *buffer, int bufferSize ) {
	if ( kind == MODERN_GL_SHADER_DEPTH || kind == MODERN_GL_SHADER_SHADOW_DEPTH ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"void main() {\n"
			"}\n",
			glslVersion );
		return;
	}

	if ( kind == MODERN_GL_SHADER_LIGHT_GRID ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"layout(location = 0) out vec4 out_Color;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"void main() {\n"
			"    float lightScale = clamp(uLocalParams.x + 1.0, 0.25, 2.0);\n"
			"    out_Color = vec4(uDebugColor.rgb * lightScale, uDebugColor.a);\n"
			"}\n",
			glslVersion );
		return;
	}

	if ( kind == MODERN_GL_SHADER_FOG_BLEND ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"layout(location = 0) out vec4 out_Color;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"void main() {\n"
			"    float fogAmount = clamp(uLocalParams.x, 0.0, 1.0);\n"
			"    vec3 fogColor = vec3(uLocalParams.y, uLocalParams.z, uLocalParams.w);\n"
			"    out_Color = vec4(mix(uDebugColor.rgb, fogColor, fogAmount), uDebugColor.a);\n"
			"}\n",
			glslVersion );
		return;
	}

	if ( kind == MODERN_GL_SHADER_GUI ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"in vec2 vTexCoord;\n"
			"layout(location = 0) out vec4 out_Color;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform sampler2D uMainTexture;\n"
			"void main() {\n"
			"    out_Color = texture(uMainTexture, vTexCoord) * uDebugColor;\n"
			"}\n",
			glslVersion );
		return;
	}

	if ( kind == MODERN_GL_SHADER_POST_COPY ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"in vec2 vTexCoord;\n"
			"layout(location = 0) out vec4 out_Color;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"uniform sampler2D uMainTexture;\n"
			"void main() {\n"
			"    vec2 uv = clamp(vTexCoord + uLocalParams.xy, vec2(0.0), vec2(1.0));\n"
			"    vec4 texel = texture(uMainTexture, uv);\n"
			"    out_Color = vec4(texel.rgb * max(uDebugColor.rgb, vec3(0.0)), texel.a * uDebugColor.a);\n"
			"}\n",
			glslVersion );
		return;
	}

	idStr::snPrintf(
		buffer,
		bufferSize,
		"#version %d\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform vec4 uDebugColor;\n"
		"void main() {\n"
		"    out_Color = uDebugColor;\n"
		"}\n",
		glslVersion );
}

static void R_ModernGLShaderLibrary_PrintShaderLog( GLuint shader, const char *label ) {
	char logBuffer[4096];
	GLsizei length = 0;
	logBuffer[0] = '\0';
	glGetShaderInfoLog( shader, sizeof( logBuffer ) - 1, &length, logBuffer );
	logBuffer[sizeof( logBuffer ) - 1] = '\0';
	common->Warning( "Modern GL shader compile failed for '%s':\n%s", label, logBuffer[0] ? logBuffer : "<no info log>" );
}

static void R_ModernGLShaderLibrary_PrintProgramLog( GLuint program, const char *label ) {
	char logBuffer[4096];
	GLsizei length = 0;
	logBuffer[0] = '\0';
	glGetProgramInfoLog( program, sizeof( logBuffer ) - 1, &length, logBuffer );
	logBuffer[sizeof( logBuffer ) - 1] = '\0';
	common->Warning( "Modern GL program link failed for '%s':\n%s", label, logBuffer[0] ? logBuffer : "<no info log>" );
}

static GLuint R_ModernGLShaderLibrary_CompileShader( GLenum shaderType, const char *source, const char *label ) {
	GLuint shader = glCreateShader( shaderType );
	if ( shader == 0 ) {
		common->Warning( "Modern GL shader compile failed for '%s': glCreateShader returned 0", label );
		return 0;
	}

	const GLchar *sources[1] = { source };
	glShaderSource( shader, 1, sources, NULL );
	glCompileShader( shader );

	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( compiled != GL_TRUE ) {
		R_ModernGLShaderLibrary_PrintShaderLog( shader, label );
		glDeleteShader( shader );
		return 0;
	}

	return shader;
}

static bool R_ModernGLShaderLibrary_KindUsesDebugColor( modernGLShaderProgramKind_t kind ) {
	return kind != MODERN_GL_SHADER_DEPTH && kind != MODERN_GL_SHADER_SHADOW_DEPTH;
}

static bool R_ModernGLShaderLibrary_KindUsesLocalParams( modernGLShaderProgramKind_t kind ) {
	return kind == MODERN_GL_SHADER_LIGHT_GRID || kind == MODERN_GL_SHADER_FOG_BLEND || kind == MODERN_GL_SHADER_POST_COPY;
}

static bool R_ModernGLShaderLibrary_KindUsesMainTexture( modernGLShaderProgramKind_t kind ) {
	return kind == MODERN_GL_SHADER_GUI || kind == MODERN_GL_SHADER_POST_COPY;
}

static bool R_ModernGLShaderLibrary_ReflectProgram( modernGLShaderProgramInfo_t &info ) {
	memset( &info.reflection, 0, sizeof( info.reflection ) );
	info.reflection.positionAttribute = 0;
	info.reflection.texCoordAttribute = 8;
	info.reflection.usesFrameConstants = true;
	info.reflection.usesModelViewProjection = true;
	info.reflection.usesDebugColor = R_ModernGLShaderLibrary_KindUsesDebugColor( info.kind );
	info.reflection.usesLocalParams = R_ModernGLShaderLibrary_KindUsesLocalParams( info.kind );
	info.reflection.usesMainTexture = R_ModernGLShaderLibrary_KindUsesMainTexture( info.kind );
	info.reflection.usesTexCoord = info.reflection.usesMainTexture;

	const GLuint frameBlockIndex = glGetUniformBlockIndex( info.program, "ModernFrameConstants" );
	info.reflection.frameBlockIndex = frameBlockIndex == GL_INVALID_INDEX ? -1 : static_cast<int>( frameBlockIndex );
	info.reflection.modelViewProjectionLocation = glGetUniformLocation( info.program, "uModelViewProjection" );
	info.reflection.debugColorLocation = glGetUniformLocation( info.program, "uDebugColor" );
	info.reflection.localParamsLocation = glGetUniformLocation( info.program, "uLocalParams" );
	info.reflection.mainTextureLocation = glGetUniformLocation( info.program, "uMainTexture" );

	info.frameBlockIndex = info.reflection.frameBlockIndex;
	info.modelViewProjectionLocation = info.reflection.modelViewProjectionLocation;
	info.debugColorLocation = info.reflection.debugColorLocation;
	info.localParamsLocation = info.reflection.localParamsLocation;
	info.mainTextureLocation = info.reflection.mainTextureLocation;

	if ( info.frameBlockIndex < 0 || info.modelViewProjectionLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing required reflected bindings", info.name );
		return false;
	}
	if ( info.reflection.usesDebugColor && info.debugColorLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uDebugColor", info.name );
		return false;
	}
	if ( info.reflection.usesLocalParams && info.localParamsLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uLocalParams", info.name );
		return false;
	}
	if ( info.reflection.usesMainTexture && info.mainTextureLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uMainTexture", info.name );
		return false;
	}

	glUniformBlockBinding( info.program, static_cast<GLuint>( info.frameBlockIndex ), 0 );
	if ( info.reflection.usesMainTexture ) {
		glUseProgram( info.program );
		glUniform1i( info.mainTextureLocation, 0 );
		glUseProgram( 0 );
	}
	return true;
}

static void R_ModernGLShaderLibrary_MarkKindReady( modernGLShaderProgramKind_t kind ) {
	bool *readyFlag = NULL;
	switch ( kind ) {
	case MODERN_GL_SHADER_DEPTH:
		readyFlag = &rg_modernGLShaderLibraryStats.depthProgramReady;
		break;
	case MODERN_GL_SHADER_SHADOW_DEPTH:
		readyFlag = &rg_modernGLShaderLibraryStats.shadowDepthProgramReady;
		break;
	case MODERN_GL_SHADER_FLAT_MATERIAL:
		readyFlag = &rg_modernGLShaderLibraryStats.flatMaterialProgramReady;
		break;
	case MODERN_GL_SHADER_LIGHT_GRID:
		readyFlag = &rg_modernGLShaderLibraryStats.lightGridProgramReady;
		break;
	case MODERN_GL_SHADER_FOG_BLEND:
		readyFlag = &rg_modernGLShaderLibraryStats.fogBlendProgramReady;
		break;
	case MODERN_GL_SHADER_GUI:
		readyFlag = &rg_modernGLShaderLibraryStats.guiProgramReady;
		break;
	case MODERN_GL_SHADER_POST_COPY:
		readyFlag = &rg_modernGLShaderLibraryStats.postCopyProgramReady;
		break;
	default:
		break;
	}
	if ( readyFlag != NULL && !*readyFlag ) {
		*readyFlag = true;
		rg_modernGLShaderLibraryStats.readyProgramKindCount++;
	}
}

static bool R_ModernGLShaderLibrary_CreateProgram( int glslVersion, modernGLShaderProgramKind_t kind ) {
	if ( rg_modernGLShaderProgramCount >= MODERN_GL_SHADER_MAX_PROGRAMS ) {
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}
	const modernGLShaderProgramDescriptor_t *descriptor = R_ModernGLShaderLibrary_DescriptorForKind( kind );
	if ( descriptor == NULL ) {
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	modernGLShaderProgramInfo_t &info = rg_modernGLShaderPrograms[rg_modernGLShaderProgramCount];
	memset( &info, 0, sizeof( info ) );
	info.kind = kind;
	info.passCategory = descriptor->passCategory;
	info.materialClass = descriptor->materialClass;
	info.permutation.materialClass = descriptor->materialClass;
	info.permutation.lightingMode = descriptor->lightingMode;
	info.permutation.shadowMode = descriptor->shadowMode;
	info.permutation.alphaMode = descriptor->alphaMode;
	info.permutation.skinningMode = 0;
	info.permutation.tier = static_cast<unsigned int>( glslVersion );
	info.glslVersion = glslVersion;
	info.frameBlockIndex = -1;
	info.modelViewProjectionLocation = -1;
	info.debugColorLocation = -1;
	info.localParamsLocation = -1;
	info.mainTextureLocation = -1;
	idStr::snPrintf(
		info.name,
		sizeof( info.name ),
		"modern_%s_%d",
		ModernGLShaderProgramKind_Name( kind ),
		glslVersion );

	char vertexSource[4096];
	char fragmentSource[4096];
	R_ModernGLShaderLibrary_BuildVertexSource( glslVersion, vertexSource, sizeof( vertexSource ) );
	R_ModernGLShaderLibrary_BuildFragmentSource( glslVersion, kind, fragmentSource, sizeof( fragmentSource ) );

	GLuint vertexShader = R_ModernGLShaderLibrary_CompileShader( GL_VERTEX_SHADER, vertexSource, info.name );
	if ( vertexShader == 0 ) {
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}
	GLuint fragmentShader = R_ModernGLShaderLibrary_CompileShader( GL_FRAGMENT_SHADER, fragmentSource, info.name );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	info.program = glCreateProgram();
	if ( info.program == 0 ) {
		common->Warning( "Modern GL program link failed for '%s': glCreateProgram returned 0", info.name );
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	glAttachShader( info.program, vertexShader );
	glAttachShader( info.program, fragmentShader );
	glBindAttribLocation( info.program, 0, "attr_Position" );
	glBindAttribLocation( info.program, 8, "attr_TexCoord0" );
	glLinkProgram( info.program );

	GLint linked = GL_FALSE;
	glGetProgramiv( info.program, GL_LINK_STATUS, &linked );
	glDetachShader( info.program, vertexShader );
	glDetachShader( info.program, fragmentShader );
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );

	if ( linked != GL_TRUE ) {
		R_ModernGLShaderLibrary_PrintProgramLog( info.program, info.name );
		glDeleteProgram( info.program );
		info.program = 0;
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	info.linked = true;
	if ( !R_ModernGLShaderLibrary_ReflectProgram( info ) ) {
		glDeleteProgram( info.program );
		info.program = 0;
		info.linked = false;
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	rg_modernGLShaderProgramCount++;
	rg_modernGLShaderLibraryStats.programCount = rg_modernGLShaderProgramCount;
	rg_modernGLShaderLibraryStats.permutationCount++;
	rg_modernGLShaderLibraryStats.reflectedUniformCount++;
	if ( info.debugColorLocation >= 0 ) {
		rg_modernGLShaderLibraryStats.reflectedUniformCount++;
	}
	if ( info.localParamsLocation >= 0 ) {
		rg_modernGLShaderLibraryStats.reflectedUniformCount++;
	}
	if ( info.mainTextureLocation >= 0 ) {
		rg_modernGLShaderLibraryStats.reflectedSamplerCount++;
	}
	if ( info.reflection.usesMainTexture ) {
		rg_modernGLShaderLibraryStats.textureProgramCount++;
	}
	if ( glslVersion > rg_modernGLShaderLibraryStats.highestGLSLVersion ) {
		rg_modernGLShaderLibraryStats.highestGLSLVersion = glslVersion;
	}
	R_ModernGLShaderLibrary_MarkKindReady( kind );
	rg_modernGLShaderLibraryStats.frameConstantsReady = true;
	return true;
}

void R_ModernGLShaderLibrary_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	R_ModernGLShaderLibrary_Shutdown();
	R_ModernGLShaderLibrary_ResetStats();

	if ( !R_ModernGLShaderLibrary_CanCompile( caps, features ) ) {
		R_ModernGLShaderLibrary_SetStatus( "unavailable" );
		return;
	}

	int versions[4];
	const int versionCount = R_ModernGLShaderLibrary_BuildVersionList( caps, features, versions );
	if ( versionCount <= 0 ) {
		R_ModernGLShaderLibrary_SetStatus( "no-supported-glsl-version" );
		return;
	}

	rg_modernGLShaderLibraryStats.initialized = true;
	for ( int i = 0; i < versionCount; ++i ) {
		for ( int kind = 0; kind < MODERN_GL_SHADER_PROGRAM_KIND_COUNT; ++kind ) {
			R_ModernGLShaderLibrary_CreateProgram( versions[i], static_cast<modernGLShaderProgramKind_t>( kind ) );
		}
	}

	if ( rg_modernGLShaderLibraryStats.programCount > 0
		&& rg_modernGLShaderLibraryStats.failedProgramCount == 0
		&& rg_modernGLShaderLibraryStats.readyProgramKindCount == MODERN_GL_SHADER_PROGRAM_KIND_COUNT
		&& rg_modernGLShaderLibraryStats.frameConstantsReady ) {
		rg_modernGLShaderLibraryStats.available = true;
		R_ModernGLShaderLibrary_SetStatus( "available" );
		return;
	}

	R_ModernGLShaderLibrary_SetStatus( "incomplete" );
}

void R_ModernGLShaderLibrary_Shutdown( void ) {
	for ( int i = 0; i < rg_modernGLShaderProgramCount; ++i ) {
		if ( rg_modernGLShaderPrograms[i].program != 0 && glDeleteProgram != NULL ) {
			glDeleteProgram( rg_modernGLShaderPrograms[i].program );
		}
	}
	memset( rg_modernGLShaderPrograms, 0, sizeof( rg_modernGLShaderPrograms ) );
	rg_modernGLShaderProgramCount = 0;
	R_ModernGLShaderLibrary_ResetStats();
}

const modernGLShaderLibraryStats_t &R_ModernGLShaderLibrary_Stats( void ) {
	return rg_modernGLShaderLibraryStats;
}

const modernGLShaderProgramInfo_t *R_ModernGLShaderLibrary_FindProgram( modernGLShaderProgramKind_t kind, int preferredGLSLVersion ) {
	const modernGLShaderProgramInfo_t *best = NULL;
	for ( int i = 0; i < rg_modernGLShaderProgramCount; ++i ) {
		const modernGLShaderProgramInfo_t &info = rg_modernGLShaderPrograms[i];
		if ( info.kind != kind || !info.linked ) {
			continue;
		}
		if ( info.glslVersion == preferredGLSLVersion ) {
			return &info;
		}
		if ( info.glslVersion <= preferredGLSLVersion ) {
			if ( best == NULL || info.glslVersion > best->glslVersion ) {
				best = &info;
			}
		} else if ( best == NULL ) {
			best = &info;
		}
	}
	return best;
}

void R_ModernGLShaderLibrary_PrintGfxInfo( void ) {
	common->Printf(
		"Modern GL shader library: %s, programs=%d, kinds=%d/%d, permutations=%d, failed=%d, highestGLSL=%d, frameUBOBlock=%d, uniforms=%d, samplers=%d, texturePrograms=%d, depth=%d, shadowDepth=%d, flatMaterial=%d, lightGrid=%d, fogBlend=%d, gui=%d, postCopy=%d\n",
		rg_modernGLShaderLibraryStats.available ? "available" : rg_modernGLShaderLibraryStats.status,
		rg_modernGLShaderLibraryStats.programCount,
		rg_modernGLShaderLibraryStats.readyProgramKindCount,
		rg_modernGLShaderLibraryStats.programKindCount,
		rg_modernGLShaderLibraryStats.permutationCount,
		rg_modernGLShaderLibraryStats.failedProgramCount,
		rg_modernGLShaderLibraryStats.highestGLSLVersion,
		rg_modernGLShaderLibraryStats.frameConstantsReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.reflectedUniformCount,
		rg_modernGLShaderLibraryStats.reflectedSamplerCount,
		rg_modernGLShaderLibraryStats.textureProgramCount,
		rg_modernGLShaderLibraryStats.depthProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.shadowDepthProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.flatMaterialProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.lightGridProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.fogBlendProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.guiProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.postCopyProgramReady ? 1 : 0 );
}

bool RendererModernGLShaderLibrary_RunSelfTest( void ) {
	const modernGLShaderLibraryStats_t &stats = R_ModernGLShaderLibrary_Stats();
	if ( !stats.available ) {
		common->Printf( "RendererModernGLShaderLibrary self-test passed (%s)\n", stats.status );
		return true;
	}

	if ( !stats.initialized || stats.programCount <= 0 || stats.failedProgramCount != 0 ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: library stats mismatch\n" );
		return false;
	}
	if ( !stats.frameConstantsReady
		|| stats.programKindCount != MODERN_GL_SHADER_PROGRAM_KIND_COUNT
		|| stats.readyProgramKindCount != MODERN_GL_SHADER_PROGRAM_KIND_COUNT
		|| !stats.depthProgramReady
		|| !stats.shadowDepthProgramReady
		|| !stats.flatMaterialProgramReady
		|| !stats.lightGridProgramReady
		|| !stats.fogBlendProgramReady
		|| !stats.guiProgramReady
		|| !stats.postCopyProgramReady ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: required variant missing\n" );
		return false;
	}
	if ( stats.permutationCount != stats.programCount || stats.reflectedUniformCount <= 0 || stats.reflectedSamplerCount <= 0 || stats.textureProgramCount <= 0 ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: reflection/permutation stats mismatch\n" );
		return false;
	}

	for ( int kind = 0; kind < MODERN_GL_SHADER_PROGRAM_KIND_COUNT; ++kind ) {
		const modernGLShaderProgramInfo_t *program = R_ModernGLShaderLibrary_FindProgram( static_cast<modernGLShaderProgramKind_t>( kind ), stats.highestGLSLVersion );
		if ( program == NULL || program->program == 0 || !program->linked ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: lookup/object mismatch for %s\n", ModernGLShaderProgramKind_Name( static_cast<modernGLShaderProgramKind_t>( kind ) ) );
			return false;
		}
		if ( program->frameBlockIndex < 0 || program->modelViewProjectionLocation < 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: frame/MVP reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesDebugColor && program->debugColorLocation < 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: debug-color reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesLocalParams && program->localParamsLocation < 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: local-param reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesMainTexture && program->mainTextureLocation < 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: sampler reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->permutation.materialClass != static_cast<unsigned int>( program->materialClass ) || program->permutation.tier != static_cast<unsigned int>( program->glslVersion ) ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: permutation metadata mismatch for %s\n", program->name );
			return false;
		}
	}

	common->Printf(
		"RendererModernGLShaderLibrary self-test passed (%d programs, %d kinds, %d permutations, GLSL %d)\n",
		stats.programCount,
		stats.readyProgramKindCount,
		stats.permutationCount,
		stats.highestGLSLVersion );
	return true;
}
