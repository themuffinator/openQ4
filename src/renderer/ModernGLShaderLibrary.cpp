// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "GLDebugScope.h"
#include "ModernGLShaderLibrary.h"

static modernGLShaderLibraryStats_t rg_modernGLShaderLibraryStats;
static modernGLShaderProgramInfo_t rg_modernGLShaderPrograms[MODERN_GL_SHADER_MAX_PROGRAMS];
static int rg_modernGLShaderProgramCount = 0;
static renderBackendCaps_t rg_modernGLShaderLibraryLastCaps;
static renderFeatureSet_t rg_modernGLShaderLibraryLastFeatures;
static bool rg_modernGLShaderLibraryHasInitContext = false;
static int rg_modernGLShaderLibraryReloadCount = 0;

typedef struct modernGLShaderProgramDescriptor_s {
	modernGLShaderProgramKind_t	kind;
	renderPassCategory_t		passCategory;
	rendererMaterialClass_t		materialClass;
	unsigned int				lightingMode;
	unsigned int				shadowMode;
	unsigned int				alphaMode;
	unsigned int				skinningMode;
	unsigned int				deformMode;
	unsigned int				lightGridMode;
	unsigned int				fogMode;
	unsigned int				debugMode;
	bool						usesTexture;
	bool						usesLocalParams;
	bool						usesShaderStorage;
	bool						usesImage;
	const char					*name;
} modernGLShaderProgramDescriptor_t;

static const modernGLShaderProgramDescriptor_t rg_modernGLShaderProgramDescriptors[MODERN_GL_SHADER_PROGRAM_KIND_COUNT] = {
	{ MODERN_GL_SHADER_DEPTH, RENDER_PASS_DEPTH, RENDER_MATERIAL_OPAQUE, 0, 0, 0, 0, 0, 0, 0, 0, false, false, false, false, "depth" },
	{ MODERN_GL_SHADER_SHADOW_DEPTH, RENDER_PASS_SHADOW_MAP, RENDER_MATERIAL_SHADOW_ONLY, 0, 1, 0, 0, 0, 0, 0, 0, false, false, false, false, "shadowDepth" },
	{ MODERN_GL_SHADER_FLAT_MATERIAL, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_OPAQUE, 1, 0, 0, 0, 0, 0, 0, 0, false, false, false, false, "flatMaterial" },
	{ MODERN_GL_SHADER_LIGHT_GRID, RENDER_PASS_LIGHT_GRID, RENDER_MATERIAL_OPAQUE, 2, 0, 0, 0, 0, 1, 0, 0, false, true, false, false, "lightGrid" },
	{ MODERN_GL_SHADER_FOG_BLEND, RENDER_PASS_FOG_BLEND, RENDER_MATERIAL_TRANSLUCENT, 3, 0, 1, 0, 0, 0, 1, 0, true, true, false, false, "fogBlend" },
	{ MODERN_GL_SHADER_GBUFFER_OPAQUE, RENDER_PASS_AMBIENT, RENDER_MATERIAL_OPAQUE, 4, 0, 0, 0, 0, 0, 0, 0, true, true, false, false, "gbufferOpaque" },
	{ MODERN_GL_SHADER_GBUFFER_ALPHA_TEST, RENDER_PASS_AMBIENT, RENDER_MATERIAL_PERFORATED, 4, 0, 1, 0, 0, 0, 0, 0, true, true, false, false, "gbufferAlphaTest" },
	{ MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_OPAQUE, 5, 2, 0, 0, 0, 1, 0, 0, true, true, true, false, "deferredLightResolve" },
	{ MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_OPAQUE, 6, 2, 0, 0, 0, 1, 0, 0, true, true, true, false, "clusteredForwardOpaque" },
	{ MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_PERFORATED, 6, 2, 1, 0, 0, 1, 0, 0, true, true, true, false, "clusteredForwardAlphaTest" },
	{ MODERN_GL_SHADER_TRANSPARENT_FORWARD, RENDER_PASS_FOG_BLEND, RENDER_MATERIAL_TRANSLUCENT, 7, 0, 2, 0, 0, 0, 1, 0, true, true, false, false, "transparentForward" },
	{ MODERN_GL_SHADER_GUI, RENDER_PASS_GUI, RENDER_MATERIAL_GUI, 0, 0, 2, 0, 0, 0, 0, 0, true, false, false, false, "gui" },
	{ MODERN_GL_SHADER_POST_COPY, RENDER_PASS_AUTHORED_POST, RENDER_MATERIAL_POST_PROCESS, 0, 0, 2, 0, 0, 0, 0, 0, true, true, false, false, "postCopy" },
	{ MODERN_GL_SHADER_DEBUG_VISUALIZATION, RENDER_PASS_AUTHORED_POST, RENDER_MATERIAL_POST_PROCESS, 0, 0, 0, 0, 0, 0, 0, 1, false, true, false, true, "debugVisualization" }
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
	case MODERN_GL_SHADER_GBUFFER_OPAQUE:
		return "gbufferOpaque";
	case MODERN_GL_SHADER_GBUFFER_ALPHA_TEST:
		return "gbufferAlphaTest";
	case MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE:
		return "deferredLightResolve";
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE:
		return "clusteredForwardOpaque";
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST:
		return "clusteredForwardAlphaTest";
	case MODERN_GL_SHADER_TRANSPARENT_FORWARD:
		return "transparentForward";
	case MODERN_GL_SHADER_GUI:
		return "gui";
	case MODERN_GL_SHADER_POST_COPY:
		return "postCopy";
	case MODERN_GL_SHADER_DEBUG_VISUALIZATION:
		return "debugVisualization";
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
	rg_modernGLShaderLibraryStats.reloadCount = rg_modernGLShaderLibraryReloadCount;
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

static void R_ModernGLShaderLibrary_BuildVertexSource( int glslVersion, modernGLShaderProgramKind_t kind, char *buffer, int bufferSize ) {
	if ( kind == MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"layout(std140) uniform ModernFrameConstants {\n"
			"    vec4 viewport;\n"
			"    vec4 frame;\n"
			"    vec4 capabilities;\n"
			"    vec4 reserved;\n"
			"} uFrame;\n"
			"uniform mat4 uModelViewProjection;\n"
			"out vec2 vTexCoord;\n"
			"void main() {\n"
			"    vec2 positions[4] = vec2[](vec2(-1.0, 1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, -1.0));\n"
			"    vec2 texcoords[4] = vec2[](vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0));\n"
			"    vTexCoord = texcoords[gl_VertexID];\n"
			"    vec4 clip = vec4(positions[gl_VertexID] + uFrame.reserved.xy, 0.0, 1.0);\n"
			"    gl_Position = uModelViewProjection * clip;\n"
			"}\n",
			glslVersion );
		return;
	}

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
	const int hasShaderStorage = glslVersion >= 430 ? 1 : 0;
	const int hasImageLoadStore = glslVersion >= 430 ? 1 : 0;

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

	const char *sharedHeader =
		"in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform vec4 uDebugColor;\n"
		"uniform vec4 uLocalParams;\n"
		"uniform sampler2D uMainTexture;\n";

	if ( kind == MODERN_GL_SHADER_GBUFFER_OPAQUE ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"in vec2 vTexCoord;\n"
			"layout(location = 0) out vec4 out_Albedo;\n"
			"layout(location = 1) out vec4 out_Normal;\n"
			"layout(location = 2) out vec4 out_Material;\n"
			"layout(location = 3) out vec4 out_Emissive;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"uniform sampler2D uMainTexture;\n"
			"void main() {\n"
			"    vec4 texel = texture(uMainTexture, vTexCoord);\n"
			"    vec3 baseColor = texel.rgb * max(uDebugColor.rgb, vec3(0.0));\n"
			"    out_Albedo = vec4(baseColor, 1.0);\n"
			"    out_Normal = vec4(0.5, 0.5, 1.0, 1.0);\n"
			"    out_Material = vec4(uLocalParams.xyz, 1.0);\n"
			"    out_Emissive = vec4(vec3(uLocalParams.w), 1.0);\n"
			"}\n",
			glslVersion );
		return;
	}

	if ( kind == MODERN_GL_SHADER_GBUFFER_ALPHA_TEST ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"in vec2 vTexCoord;\n"
			"layout(location = 0) out vec4 out_Albedo;\n"
			"layout(location = 1) out vec4 out_Normal;\n"
			"layout(location = 2) out vec4 out_Material;\n"
			"layout(location = 3) out vec4 out_Emissive;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"uniform sampler2D uMainTexture;\n"
			"void main() {\n"
			"    vec4 texel = texture(uMainTexture, vTexCoord);\n"
			"    if (texel.a < max(uLocalParams.x, 0.001)) { discard; }\n"
			"    out_Albedo = vec4(texel.rgb * max(uDebugColor.rgb, vec3(0.0)), 1.0);\n"
			"    out_Normal = vec4(0.5, 0.5, 1.0, 1.0);\n"
			"    out_Material = vec4(uLocalParams.yzw, 1.0);\n"
			"    out_Emissive = vec4(0.0, 0.0, 0.0, 1.0);\n"
			"}\n",
			glslVersion );
		return;
	}

	if ( kind == MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"in vec2 vTexCoord;\n"
			"layout(location = 0) out vec4 out_Color;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"uniform sampler2D uMainTexture;\n"
			"uniform sampler2D uGBufferNormal;\n"
			"uniform sampler2D uGBufferMaterial;\n"
			"uniform sampler2D uGBufferEmissive;\n"
			"uniform sampler2D uSceneDepth;\n"
			"struct ModernClusterLightRecord {\n"
			"    vec4 positionRadius;\n"
			"    vec4 colorType;\n"
			"    vec4 scissorDepth;\n"
			"    vec4 flags;\n"
			"};\n"
			"layout(std140) uniform ModernClusterGridParams {\n"
			"    vec4 grid;\n"
			"    vec4 depth;\n"
			"    vec4 viewport;\n"
			"    vec4 counts;\n"
			"} uClusterGrid;\n"
			"layout(std140) uniform ModernClusterLightRecords {\n"
			"    ModernClusterLightRecord lights[128];\n"
			"} uClusterLights;\n"
			"layout(std140) uniform ModernClusterIndexRecords {\n"
			"    uvec4 indices[768];\n"
			"} uClusterIndices;\n"
			"void main() {\n"
			"    vec4 albedo = texture(uMainTexture, vTexCoord);\n"
			"    vec3 normal = normalize(texture(uGBufferNormal, vTexCoord).xyz * 2.0 - 1.0);\n"
			"    vec4 material = texture(uGBufferMaterial, vTexCoord);\n"
			"    vec3 lightGrid = texture(uGBufferEmissive, vTexCoord).rgb;\n"
			"    float rawDepth = texture(uSceneDepth, vTexCoord).r;\n"
			"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
			"    int maxLights = int(max(uClusterGrid.grid.w, 1.0));\n"
			"    int tileX = clamp(int(floor(vTexCoord.x * float(grid.x))), 0, grid.x - 1);\n"
			"    int tileY = clamp(int(floor((1.0 - vTexCoord.y) * float(grid.y))), 0, grid.y - 1);\n"
			"    int sliceZ = clamp(int(floor(rawDepth * float(grid.z))), 0, grid.z - 1);\n"
			"    int clusterIndex = (sliceZ * grid.y + tileY) * grid.x + tileX;\n"
			"    uvec4 lightIndices = uClusterIndices.indices[clusterIndex];\n"
			"    vec3 lightAccum = vec3(0.0);\n"
			"    int contributingLights = 0;\n"
			"    int scannedLights = 0;\n"
			"    for (int i = 0; i < 4; ++i) {\n"
			"        if (i >= maxLights) { break; }\n"
			"        uint lightIndex = lightIndices[i];\n"
			"        if (lightIndex == 0xffffffffu || lightIndex >= uint(max(uClusterGrid.counts.x, 0.0))) { continue; }\n"
			"        ModernClusterLightRecord light = uClusterLights.lights[int(lightIndex)];\n"
			"        int type = int(floor(light.colorType.w + 0.5));\n"
			"        bool supported = type == 0 || type == 1;\n"
			"        vec2 pixel = gl_FragCoord.xy;\n"
			"        float inX = step(light.scissorDepth.x, pixel.x) * step(pixel.x, light.scissorDepth.z);\n"
			"        float inY = step(light.scissorDepth.y, pixel.y) * step(pixel.y, light.scissorDepth.w);\n"
			"        vec3 lightDir = normalize(vec3(0.25, 0.35, 1.0));\n"
			"        float ndotl = clamp(dot(normal, lightDir) * 0.5 + 0.5, 0.18, 1.0);\n"
			"        float projectedScale = type == 1 ? 0.85 : 1.0;\n"
			"        float attenuation = supported ? inX * inY * ndotl * projectedScale : 0.0;\n"
			"        lightAccum += light.colorType.rgb * attenuation;\n"
			"        contributingLights += attenuation > 0.001 ? 1 : 0;\n"
			"        scannedLights++;\n"
			"    }\n"
			"    float exposure = max(uLocalParams.x, 0.25);\n"
			"    float debugMode = floor(uLocalParams.y + 0.5);\n"
			"    float overflowPressure = clamp(uLocalParams.w, 0.0, 1.0);\n"
			"    vec3 lit = albedo.rgb * (vec3(0.12) + lightGrid + lightAccum * (0.35 + material.g)) * max(uDebugColor.rgb, vec3(0.0)) * exposure;\n"
			"    if (debugMode == 1.0) {\n"
			"        out_Color = vec4(clamp(lightAccum, vec3(0.0), vec3(1.0)), 1.0);\n"
			"    } else if (debugMode == 2.0) {\n"
			"        out_Color = vec4(fract(vec3(float(tileX) * 0.173, float(tileY) * 0.271, float(sliceZ) * 0.067)), 1.0);\n"
			"    } else if (debugMode == 3.0) {\n"
			"        float heat = clamp(float(scannedLights) / max(float(maxLights), 1.0), 0.0, 1.0);\n"
			"        out_Color = vec4(heat, float(contributingLights) * 0.25, 1.0 - heat, 1.0);\n"
			"    } else if (debugMode == 4.0) {\n"
			"        out_Color = vec4(overflowPressure, uLocalParams.z, 0.1, 1.0);\n"
			"    } else {\n"
			"        out_Color = vec4(clamp(lit, vec3(0.0), vec3(1.0)), albedo.a);\n"
			"    }\n"
			"}\n",
			glslVersion );
		return;
	}

	if ( kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE || kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"#define MODERN_HAS_SHADER_STORAGE %d\n"
			"%s"
			"#if MODERN_HAS_SHADER_STORAGE\n"
			"layout(std430, binding = 1) readonly buffer ModernLightRecords { vec4 lightRecords[]; };\n"
			"#endif\n"
			"struct ModernClusterLightRecord {\n"
			"    vec4 positionRadius;\n"
			"    vec4 colorType;\n"
			"    vec4 scissorDepth;\n"
			"    vec4 flags;\n"
			"};\n"
			"layout(std140) uniform ModernClusterGridParams {\n"
			"    vec4 grid;\n"
			"    vec4 depth;\n"
			"    vec4 viewport;\n"
			"    vec4 counts;\n"
			"} uClusterGrid;\n"
			"layout(std140) uniform ModernClusterLightRecords {\n"
			"    ModernClusterLightRecord lights[128];\n"
			"} uClusterLights;\n"
			"layout(std140) uniform ModernClusterIndexRecords {\n"
			"    uvec4 indices[768];\n"
			"} uClusterIndices;\n"
			"void main() {\n"
			"    vec4 texel = texture(uMainTexture, vTexCoord);\n"
			"    if (%d != 0 && texel.a < max(uLocalParams.x, 0.001)) { discard; }\n"
			"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
			"    int maxLights = int(max(uClusterGrid.grid.w, 1.0));\n"
			"    vec2 viewport = max(uClusterGrid.viewport.xy, vec2(1.0));\n"
			"    vec2 normalizedPixel = clamp(gl_FragCoord.xy / viewport, vec2(0.0), vec2(0.999));\n"
			"    int tileX = clamp(int(floor(normalizedPixel.x * float(grid.x))), 0, grid.x - 1);\n"
			"    int tileY = clamp(int(floor((1.0 - normalizedPixel.y) * float(grid.y))), 0, grid.y - 1);\n"
			"    int sliceZ = clamp(int(floor(gl_FragCoord.z * float(grid.z))), 0, grid.z - 1);\n"
			"    int clusterIndex = (sliceZ * grid.y + tileY) * grid.x + tileX;\n"
			"    uvec4 lightIndices = uClusterIndices.indices[clusterIndex];\n"
			"    vec3 lightAccum = vec3(0.0);\n"
			"    int scannedLights = 0;\n"
			"    for (int i = 0; i < 4; ++i) {\n"
			"        if (i >= maxLights) { break; }\n"
			"        uint lightIndex = lightIndices[i];\n"
			"        if (lightIndex == 0xffffffffu || lightIndex >= uint(max(uClusterGrid.counts.x, 0.0))) { continue; }\n"
			"        ModernClusterLightRecord light = uClusterLights.lights[int(lightIndex)];\n"
			"        int type = int(floor(light.colorType.w + 0.5));\n"
			"        bool supported = type == 0 || type == 1;\n"
			"        float inX = step(light.scissorDepth.x, gl_FragCoord.x) * step(gl_FragCoord.x, light.scissorDepth.z);\n"
			"        float inY = step(light.scissorDepth.y, gl_FragCoord.y) * step(gl_FragCoord.y, light.scissorDepth.w);\n"
			"        float projectedScale = type == 1 ? 0.85 : 1.0;\n"
			"        lightAccum += supported ? light.colorType.rgb * inX * inY * projectedScale : vec3(0.0);\n"
			"        scannedLights++;\n"
			"    }\n"
			"    float lightScale = clamp(0.18 + uLocalParams.y + float(scannedLights) * 0.05, 0.18, 2.5);\n"
			"    vec3 lit = texel.rgb * max(uDebugColor.rgb, vec3(0.0)) * (lightScale + lightAccum * 0.35);\n"
			"    out_Color = vec4(clamp(lit, vec3(0.0), vec3(1.0)), texel.a * uDebugColor.a);\n"
			"}\n",
			glslVersion,
			hasShaderStorage,
			sharedHeader,
			kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST ? 1 : 0 );
		return;
	}

	if ( kind == MODERN_GL_SHADER_TRANSPARENT_FORWARD || kind == MODERN_GL_SHADER_FOG_BLEND ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"%s"
			"struct ModernClusterLightRecord {\n"
			"    vec4 positionRadius;\n"
			"    vec4 colorType;\n"
			"    vec4 scissorDepth;\n"
			"    vec4 flags;\n"
			"};\n"
			"layout(std140) uniform ModernClusterGridParams {\n"
			"    vec4 grid;\n"
			"    vec4 depth;\n"
			"    vec4 viewport;\n"
			"    vec4 counts;\n"
			"} uClusterGrid;\n"
			"layout(std140) uniform ModernClusterLightRecords {\n"
			"    ModernClusterLightRecord lights[128];\n"
			"} uClusterLights;\n"
			"layout(std140) uniform ModernClusterIndexRecords {\n"
			"    uvec4 indices[768];\n"
			"} uClusterIndices;\n"
			"void main() {\n"
			"    vec4 texel = texture(uMainTexture, vTexCoord);\n"
			"    float blendAmount = clamp(uLocalParams.x, 0.0, 1.0);\n"
			"    vec3 blendColor = vec3(uLocalParams.y, uLocalParams.z, uLocalParams.w);\n"
			"    vec3 baseColor = texel.rgb * max(uDebugColor.rgb, vec3(0.0));\n"
			"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
			"    int maxLights = int(max(uClusterGrid.grid.w, 1.0));\n"
			"    vec2 viewport = max(uClusterGrid.viewport.xy, vec2(1.0));\n"
			"    vec2 normalizedPixel = clamp(gl_FragCoord.xy / viewport, vec2(0.0), vec2(0.999));\n"
			"    int tileX = clamp(int(floor(normalizedPixel.x * float(grid.x))), 0, grid.x - 1);\n"
			"    int tileY = clamp(int(floor((1.0 - normalizedPixel.y) * float(grid.y))), 0, grid.y - 1);\n"
			"    int sliceZ = clamp(int(floor(gl_FragCoord.z * float(grid.z))), 0, grid.z - 1);\n"
			"    int clusterIndex = (sliceZ * grid.y + tileY) * grid.x + tileX;\n"
			"    uvec4 lightIndices = uClusterIndices.indices[clusterIndex];\n"
			"    vec3 lightAccum = vec3(0.0);\n"
			"    for (int i = 0; i < 4; ++i) {\n"
			"        if (i >= maxLights) { break; }\n"
			"        uint lightIndex = lightIndices[i];\n"
			"        if (lightIndex == 0xffffffffu || lightIndex >= uint(max(uClusterGrid.counts.x, 0.0))) { continue; }\n"
			"        ModernClusterLightRecord light = uClusterLights.lights[int(lightIndex)];\n"
			"        int type = int(floor(light.colorType.w + 0.5));\n"
			"        if (type == 0 || type == 1) { lightAccum += light.colorType.rgb * 0.18; }\n"
			"    }\n"
			"    out_Color = vec4(clamp(mix(baseColor, blendColor, blendAmount) + lightAccum, vec3(0.0), vec3(1.0)), texel.a * uDebugColor.a);\n"
			"}\n",
			glslVersion,
			sharedHeader );
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

	if ( kind == MODERN_GL_SHADER_DEBUG_VISUALIZATION ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"#define MODERN_HAS_IMAGE_LOAD_STORE %d\n"
			"layout(location = 0) out vec4 out_Color;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"#if MODERN_HAS_IMAGE_LOAD_STORE\n"
			"layout(binding = 2, rgba16f) uniform readonly image2D uDebugImage;\n"
			"#endif\n"
			"void main() {\n"
			"    vec3 color = mix(uDebugColor.rgb, uLocalParams.yzw, clamp(uLocalParams.x, 0.0, 1.0));\n"
			"#if MODERN_HAS_IMAGE_LOAD_STORE\n"
			"    color += imageLoad(uDebugImage, ivec2(0, 0)).rgb * 0.0;\n"
			"#endif\n"
			"    out_Color = vec4(color, uDebugColor.a);\n"
			"}\n",
			glslVersion,
			hasImageLoadStore );
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
	const modernGLShaderProgramDescriptor_t *descriptor = R_ModernGLShaderLibrary_DescriptorForKind( kind );
	return descriptor != NULL && descriptor->usesLocalParams;
}

static bool R_ModernGLShaderLibrary_KindUsesMainTexture( modernGLShaderProgramKind_t kind ) {
	const modernGLShaderProgramDescriptor_t *descriptor = R_ModernGLShaderLibrary_DescriptorForKind( kind );
	return descriptor != NULL && descriptor->usesTexture;
}

static bool R_ModernGLShaderLibrary_KindUsesShaderStorage( modernGLShaderProgramKind_t kind, int glslVersion ) {
	const modernGLShaderProgramDescriptor_t *descriptor = R_ModernGLShaderLibrary_DescriptorForKind( kind );
	return descriptor != NULL && descriptor->usesShaderStorage && glslVersion >= 430;
}

static bool R_ModernGLShaderLibrary_KindUsesImage( modernGLShaderProgramKind_t kind, int glslVersion ) {
	const modernGLShaderProgramDescriptor_t *descriptor = R_ModernGLShaderLibrary_DescriptorForKind( kind );
	return descriptor != NULL && descriptor->usesImage && glslVersion >= 430;
}

static void R_ModernGLShaderLibrary_AddReflectionRecord(
	modernGLShaderResourceReflection_t *records,
	int &count,
	const char *name,
	modernGLShaderResourceType_t resourceType,
	int index,
	int location,
	int binding,
	int size,
	unsigned int glType,
	bool required,
	bool present ) {
	if ( count >= MODERN_GL_SHADER_MAX_REFLECTION_RECORDS ) {
		return;
	}
	modernGLShaderResourceReflection_t &record = records[count++];
	memset( &record, 0, sizeof( record ) );
	idStr::Copynz( record.name, name != NULL ? name : "<unnamed>", sizeof( record.name ) );
	record.index = index;
	record.location = location;
	record.binding = binding;
	record.size = size;
	record.type = glType | ( static_cast<unsigned int>( resourceType ) << 24 );
	record.required = required;
	record.present = present;
}

static int R_ModernGLShaderLibrary_ProgramResourceIndex( GLuint program, GLenum interfaceType, const char *name ) {
#if defined( GL_SHADER_STORAGE_BLOCK )
	if ( glGetProgramResourceIndex != NULL ) {
		const GLuint index = glGetProgramResourceIndex( program, interfaceType, name );
		return index == GL_INVALID_INDEX ? -1 : static_cast<int>( index );
	}
#endif
	(void)program;
	(void)interfaceType;
	(void)name;
	return -1;
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
	info.reflection.usesShaderStorage = R_ModernGLShaderLibrary_KindUsesShaderStorage( info.kind, info.glslVersion );
	info.reflection.usesImage = R_ModernGLShaderLibrary_KindUsesImage( info.kind, info.glslVersion );

	const GLuint frameBlockIndex = glGetUniformBlockIndex( info.program, "ModernFrameConstants" );
	info.reflection.frameBlockIndex = frameBlockIndex == GL_INVALID_INDEX ? -1 : static_cast<int>( frameBlockIndex );
	info.reflection.modelViewProjectionLocation = glGetUniformLocation( info.program, "uModelViewProjection" );
	info.reflection.debugColorLocation = glGetUniformLocation( info.program, "uDebugColor" );
	info.reflection.localParamsLocation = glGetUniformLocation( info.program, "uLocalParams" );
	info.reflection.mainTextureLocation = glGetUniformLocation( info.program, "uMainTexture" );

	R_ModernGLShaderLibrary_AddReflectionRecord(
		info.reflection.uniformBlocks,
		info.reflection.uniformBlockCount,
		"ModernFrameConstants",
		MODERN_GL_SHADER_RESOURCE_UNIFORM_BLOCK,
		info.reflection.frameBlockIndex,
		-1,
		0,
		1,
		GL_UNIFORM_BLOCK,
		true,
		info.reflection.frameBlockIndex >= 0 );
	R_ModernGLShaderLibrary_AddReflectionRecord(
		info.reflection.uniforms,
		info.reflection.uniformCount,
		"uModelViewProjection",
		MODERN_GL_SHADER_RESOURCE_UNIFORM,
		-1,
		info.reflection.modelViewProjectionLocation,
		-1,
		1,
		GL_FLOAT_MAT4,
		true,
		info.reflection.modelViewProjectionLocation >= 0 );
	if ( info.reflection.usesDebugColor ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uDebugColor",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.debugColorLocation,
			-1,
			1,
			GL_FLOAT_VEC4,
			true,
			info.reflection.debugColorLocation >= 0 );
	}
	if ( info.reflection.usesLocalParams ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uLocalParams",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.localParamsLocation,
			-1,
			1,
			GL_FLOAT_VEC4,
			true,
			info.reflection.localParamsLocation >= 0 );
	}
	if ( info.reflection.usesMainTexture ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uMainTexture",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			info.reflection.mainTextureLocation,
			0,
			1,
			GL_SAMPLER_2D,
			true,
			info.reflection.mainTextureLocation >= 0 );
	}
	if ( info.reflection.usesShaderStorage ) {
		const int ssboIndex = R_ModernGLShaderLibrary_ProgramResourceIndex( info.program, GL_SHADER_STORAGE_BLOCK, "ModernLightRecords" );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.shaderStorageBlocks,
			info.reflection.shaderStorageBlockCount,
			"ModernLightRecords",
			MODERN_GL_SHADER_RESOURCE_SHADER_STORAGE_BLOCK,
			ssboIndex,
			-1,
			1,
			1,
			GL_SHADER_STORAGE_BLOCK,
			false,
			ssboIndex >= 0 || info.glslVersion >= 430 );
	}
	if ( info.reflection.usesImage ) {
		const int imageLocation = glGetUniformLocation( info.program, "uDebugImage" );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.images,
			info.reflection.imageCount,
			"uDebugImage",
			MODERN_GL_SHADER_RESOURCE_IMAGE,
			-1,
			imageLocation,
			2,
			1,
			GL_IMAGE_2D,
			false,
			imageLocation >= 0 || info.glslVersion >= 430 );
	}
	R_ModernGLShaderLibrary_AddReflectionRecord(
		info.reflection.attributes,
		info.reflection.attributeCount,
		"attr_Position",
		MODERN_GL_SHADER_RESOURCE_ATTRIBUTE,
		-1,
		0,
		-1,
		1,
		GL_FLOAT_VEC3,
		true,
		true );
	R_ModernGLShaderLibrary_AddReflectionRecord(
		info.reflection.attributes,
		info.reflection.attributeCount,
		"attr_TexCoord0",
		MODERN_GL_SHADER_RESOURCE_ATTRIBUTE,
		-1,
		8,
		-1,
		1,
		GL_FLOAT_VEC2,
		info.reflection.usesTexCoord,
		info.reflection.usesTexCoord );

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
	case MODERN_GL_SHADER_GBUFFER_OPAQUE:
		readyFlag = &rg_modernGLShaderLibraryStats.gbufferOpaqueProgramReady;
		break;
	case MODERN_GL_SHADER_GBUFFER_ALPHA_TEST:
		readyFlag = &rg_modernGLShaderLibraryStats.gbufferAlphaTestProgramReady;
		break;
	case MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE:
		readyFlag = &rg_modernGLShaderLibraryStats.deferredLightResolveProgramReady;
		break;
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE:
		readyFlag = &rg_modernGLShaderLibraryStats.clusteredForwardOpaqueProgramReady;
		break;
	case MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST:
		readyFlag = &rg_modernGLShaderLibraryStats.clusteredForwardAlphaTestProgramReady;
		break;
	case MODERN_GL_SHADER_TRANSPARENT_FORWARD:
		readyFlag = &rg_modernGLShaderLibraryStats.transparentForwardProgramReady;
		break;
	case MODERN_GL_SHADER_GUI:
		readyFlag = &rg_modernGLShaderLibraryStats.guiProgramReady;
		break;
	case MODERN_GL_SHADER_POST_COPY:
		readyFlag = &rg_modernGLShaderLibraryStats.postCopyProgramReady;
		break;
	case MODERN_GL_SHADER_DEBUG_VISUALIZATION:
		readyFlag = &rg_modernGLShaderLibraryStats.debugVisualizationProgramReady;
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
	info.permutation.skinningMode = descriptor->skinningMode;
	info.permutation.deformMode = descriptor->deformMode;
	info.permutation.lightGridMode = descriptor->lightGridMode;
	info.permutation.fogMode = descriptor->fogMode;
	info.permutation.debugMode = descriptor->debugMode;
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

	char programContext[256];
	idStr::snPrintf(
		programContext,
		sizeof( programContext ),
		"%s pass=%s material=%s glsl=%d key(mat=%u light=%u shadow=%u alpha=%u skin=%u deform=%u grid=%u fog=%u debug=%u tier=%u)",
		info.name,
		RenderPassCategory_Name( info.passCategory ),
		RendererMaterialClass_Name( info.materialClass ),
		info.glslVersion,
		info.permutation.materialClass,
		info.permutation.lightingMode,
		info.permutation.shadowMode,
		info.permutation.alphaMode,
		info.permutation.skinningMode,
		info.permutation.deformMode,
		info.permutation.lightGridMode,
		info.permutation.fogMode,
		info.permutation.debugMode,
		info.permutation.tier );

	char vertexSource[4096];
	char fragmentSource[8192];
	R_ModernGLShaderLibrary_BuildVertexSource( glslVersion, kind, vertexSource, sizeof( vertexSource ) );
	R_ModernGLShaderLibrary_BuildFragmentSource( glslVersion, kind, fragmentSource, sizeof( fragmentSource ) );

	GLuint vertexShader = R_ModernGLShaderLibrary_CompileShader( GL_VERTEX_SHADER, vertexSource, programContext );
	if ( vertexShader == 0 ) {
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}
	GLuint fragmentShader = R_ModernGLShaderLibrary_CompileShader( GL_FRAGMENT_SHADER, fragmentSource, programContext );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	info.program = glCreateProgram();
	if ( info.program == 0 ) {
		common->Warning( "Modern GL program link failed for '%s': glCreateProgram returned 0", programContext );
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
		R_ModernGLShaderLibrary_PrintProgramLog( info.program, programContext );
		glDeleteProgram( info.program );
		info.program = 0;
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	info.linked = true;
	R_GLDebug_LabelProgram( info.program, info.name );
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
	rg_modernGLShaderLibraryStats.reflectedUniformCount += info.reflection.uniformCount;
	rg_modernGLShaderLibraryStats.reflectedUniformBlockCount += info.reflection.uniformBlockCount;
	rg_modernGLShaderLibraryStats.reflectedShaderStorageBlockCount += info.reflection.shaderStorageBlockCount;
	rg_modernGLShaderLibraryStats.reflectedSamplerCount += info.reflection.samplerCount;
	rg_modernGLShaderLibraryStats.reflectedImageCount += info.reflection.imageCount;
	rg_modernGLShaderLibraryStats.reflectedAttributeCount += info.reflection.attributeCount;
	if ( info.reflection.usesMainTexture ) {
		rg_modernGLShaderLibraryStats.textureProgramCount++;
	}
	switch ( glslVersion ) {
	case 330:
		rg_modernGLShaderLibraryStats.glsl330ProgramCount++;
		break;
	case 410:
		rg_modernGLShaderLibraryStats.glsl410ProgramCount++;
		break;
	case 430:
		rg_modernGLShaderLibraryStats.glsl430ProgramCount++;
		break;
	case 450:
		rg_modernGLShaderLibraryStats.glsl450ProgramCount++;
		break;
	default:
		break;
	}
	if ( glslVersion > rg_modernGLShaderLibraryStats.highestGLSLVersion ) {
		rg_modernGLShaderLibraryStats.highestGLSLVersion = glslVersion;
	}
	R_ModernGLShaderLibrary_MarkKindReady( kind );
	rg_modernGLShaderLibraryStats.frameConstantsReady = true;
	return true;
}

void R_ModernGLShaderLibrary_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	rg_modernGLShaderLibraryLastCaps = caps;
	rg_modernGLShaderLibraryLastFeatures = features;
	rg_modernGLShaderLibraryHasInitContext = true;
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
	rg_modernGLShaderLibraryStats.validatedGLSLVersionCount = versionCount;
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

bool R_ModernGLShaderLibrary_Reload( void ) {
	if ( !r_rendererShaderReload.GetBool() ) {
		common->Printf( "Modern GL shader library reload skipped: r_rendererShaderReload is 0\n" );
		return false;
	}
	if ( !rg_modernGLShaderLibraryHasInitContext ) {
		common->Printf( "Modern GL shader library reload skipped: no previous initialization context\n" );
		return false;
	}
	rg_modernGLShaderLibraryReloadCount++;
	R_ModernGLShaderLibrary_Init( rg_modernGLShaderLibraryLastCaps, rg_modernGLShaderLibraryLastFeatures );
	common->Printf(
		"Modern GL shader library reload %s (%d programs, %d failures)\n",
		rg_modernGLShaderLibraryStats.available ? "passed" : "incomplete",
		rg_modernGLShaderLibraryStats.programCount,
		rg_modernGLShaderLibraryStats.failedProgramCount );
	return rg_modernGLShaderLibraryStats.available;
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
		"Modern GL shader library: %s, programs=%d, kinds=%d/%d, permutations=%d, failed=%d, versions=%d [330=%d 410=%d 430=%d 450=%d], highestGLSL=%d, reloads=%d, reflection(ubo=%d ssbo=%d uniforms=%d samplers=%d images=%d attrs=%d), texturePrograms=%d, ready(depth=%d shadow=%d flat=%d lightGrid=%d fog=%d gbuf=%d/%d deferred=%d clustered=%d/%d transparent=%d gui=%d post=%d debug=%d)\n",
		rg_modernGLShaderLibraryStats.available ? "available" : rg_modernGLShaderLibraryStats.status,
		rg_modernGLShaderLibraryStats.programCount,
		rg_modernGLShaderLibraryStats.readyProgramKindCount,
		rg_modernGLShaderLibraryStats.programKindCount,
		rg_modernGLShaderLibraryStats.permutationCount,
		rg_modernGLShaderLibraryStats.failedProgramCount,
		rg_modernGLShaderLibraryStats.validatedGLSLVersionCount,
		rg_modernGLShaderLibraryStats.glsl330ProgramCount,
		rg_modernGLShaderLibraryStats.glsl410ProgramCount,
		rg_modernGLShaderLibraryStats.glsl430ProgramCount,
		rg_modernGLShaderLibraryStats.glsl450ProgramCount,
		rg_modernGLShaderLibraryStats.highestGLSLVersion,
		rg_modernGLShaderLibraryStats.reloadCount,
		rg_modernGLShaderLibraryStats.reflectedUniformBlockCount,
		rg_modernGLShaderLibraryStats.reflectedShaderStorageBlockCount,
		rg_modernGLShaderLibraryStats.reflectedUniformCount,
		rg_modernGLShaderLibraryStats.reflectedSamplerCount,
		rg_modernGLShaderLibraryStats.reflectedImageCount,
		rg_modernGLShaderLibraryStats.reflectedAttributeCount,
		rg_modernGLShaderLibraryStats.textureProgramCount,
		rg_modernGLShaderLibraryStats.depthProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.shadowDepthProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.flatMaterialProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.lightGridProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.fogBlendProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.gbufferOpaqueProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.gbufferAlphaTestProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.deferredLightResolveProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.clusteredForwardOpaqueProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.clusteredForwardAlphaTestProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.transparentForwardProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.guiProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.postCopyProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.debugVisualizationProgramReady ? 1 : 0 );
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
		|| !stats.gbufferOpaqueProgramReady
		|| !stats.gbufferAlphaTestProgramReady
		|| !stats.deferredLightResolveProgramReady
		|| !stats.clusteredForwardOpaqueProgramReady
		|| !stats.clusteredForwardAlphaTestProgramReady
		|| !stats.transparentForwardProgramReady
		|| !stats.guiProgramReady
		|| !stats.postCopyProgramReady
		|| !stats.debugVisualizationProgramReady ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: required variant missing\n" );
		return false;
	}
	if ( stats.permutationCount != stats.programCount
		|| stats.reflectedUniformCount <= 0
		|| stats.reflectedUniformBlockCount < stats.programCount
		|| stats.reflectedSamplerCount <= 0
		|| stats.reflectedAttributeCount < stats.programCount
		|| stats.textureProgramCount <= 0
		|| stats.validatedGLSLVersionCount <= 0 ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: reflection/permutation stats mismatch\n" );
		return false;
	}
	if ( stats.highestGLSLVersion >= 430 && ( stats.reflectedShaderStorageBlockCount <= 0 || stats.reflectedImageCount <= 0 ) ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: GL430 resource reflection missing\n" );
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
		if ( program->reflection.uniformBlockCount <= 0 || program->reflection.uniformCount <= 0 || program->reflection.attributeCount <= 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: reflection record coverage mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesShaderStorage && program->reflection.shaderStorageBlockCount <= 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: SSBO reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesImage && program->reflection.imageCount <= 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: image reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->permutation.materialClass != static_cast<unsigned int>( program->materialClass ) || program->permutation.tier != static_cast<unsigned int>( program->glslVersion ) ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: permutation metadata mismatch for %s\n", program->name );
			return false;
		}
	}

	common->Printf(
		"RendererModernGLShaderLibrary self-test passed (%d programs, %d kinds, %d permutations, GLSL %d, reflection ubo=%d ssbo=%d samplers=%d images=%d)\n",
		stats.programCount,
		stats.readyProgramKindCount,
		stats.permutationCount,
		stats.highestGLSLVersion,
		stats.reflectedUniformBlockCount,
		stats.reflectedShaderStorageBlockCount,
		stats.reflectedSamplerCount,
		stats.reflectedImageCount );
	return true;
}
