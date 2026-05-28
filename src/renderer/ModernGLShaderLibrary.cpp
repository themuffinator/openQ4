// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "GLDebugScope.h"
#include "MaterialResourceTable.h"
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
	{ MODERN_GL_SHADER_DEPTH, RENDER_PASS_DEPTH, RENDER_MATERIAL_OPAQUE, 0, 0, 1, 0, 0, 0, 0, 0, true, true, false, false, "depth" },
	{ MODERN_GL_SHADER_SHADOW_DEPTH, RENDER_PASS_SHADOW_MAP, RENDER_MATERIAL_SHADOW_ONLY, 0, 1, 1, 0, 0, 0, 0, 0, true, true, false, false, "shadowDepth" },
	{ MODERN_GL_SHADER_FLAT_MATERIAL, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_OPAQUE, 1, 0, 0, 0, 0, 0, 0, 0, false, false, false, false, "flatMaterial" },
	{ MODERN_GL_SHADER_LIGHT_GRID, RENDER_PASS_LIGHT_GRID, RENDER_MATERIAL_OPAQUE, 2, 0, 0, 0, 0, 1, 0, 0, false, true, false, false, "lightGrid" },
	{ MODERN_GL_SHADER_FOG_BLEND, RENDER_PASS_FOG_BLEND, RENDER_MATERIAL_TRANSLUCENT, 3, 0, 1, 0, 0, 0, 1, 0, true, true, true, false, "fogBlend" },
	{ MODERN_GL_SHADER_GBUFFER_OPAQUE, RENDER_PASS_AMBIENT, RENDER_MATERIAL_OPAQUE, 4, 0, 0, 0, 0, 0, 0, 0, true, true, false, false, "gbufferOpaque" },
	{ MODERN_GL_SHADER_GBUFFER_ALPHA_TEST, RENDER_PASS_AMBIENT, RENDER_MATERIAL_PERFORATED, 4, 0, 1, 0, 0, 0, 0, 0, true, true, false, false, "gbufferAlphaTest" },
	{ MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_OPAQUE, 5, 2, 0, 0, 0, 1, 0, 0, true, true, true, false, "deferredLightResolve" },
	{ MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_OPAQUE, 6, 2, 0, 0, 0, 1, 0, 0, true, true, true, false, "clusteredForwardOpaque" },
	{ MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST, RENDER_PASS_ARB2_INTERACTION, RENDER_MATERIAL_PERFORATED, 6, 2, 1, 0, 0, 1, 0, 0, true, true, true, false, "clusteredForwardAlphaTest" },
	{ MODERN_GL_SHADER_TRANSPARENT_FORWARD, RENDER_PASS_FOG_BLEND, RENDER_MATERIAL_TRANSLUCENT, 7, 0, 2, 0, 0, 0, 1, 0, true, true, true, false, "transparentForward" },
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
	idStr::Copynz( rg_modernGLShaderLibraryStats.status, status ? status : "unknown", sizeof( rg_modernGLShaderLibraryStats.status ) );
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
	const int hasDrawRecords = ( glslVersion >= 430 && kind != MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE ) ? 1 : 0;

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
		"layout(location = 3) in vec4 attr_Color;\n"
		"layout(location = 8) in vec2 attr_TexCoord0;\n"
		"layout(location = 9) in vec3 attr_Tangent0;\n"
		"layout(location = 10) in vec3 attr_Tangent1;\n"
		"layout(location = 11) in vec3 attr_Normal;\n"
		"layout(std140) uniform ModernFrameConstants {\n"
		"    vec4 viewport;\n"
		"    vec4 frame;\n"
		"    vec4 capabilities;\n"
		"    vec4 reserved;\n"
		"} uFrame;\n"
		"uniform mat4 uModelViewProjection;\n"
		"uniform mat4 uModelViewMatrix;\n"
		"uniform vec4 uDebugColor;\n"
		"uniform vec4 uLocalParams;\n"
		"uniform vec4 uMaterialFlags;\n"
		"uniform vec4 uMaterialEnhancement;\n"
		"#define MODERN_HAS_DRAW_RECORDS %d\n"
		"struct ModernDrawRecord {\n"
		"    mat4 modelViewProjection;\n"
		"    mat4 modelViewMatrix;\n"
		"    vec4 debugColor;\n"
		"    vec4 localParams;\n"
		"    vec4 materialFlags;\n"
		"    vec4 materialEnhancement;\n"
		"    uvec4 ids;\n"
		"};\n"
		"#if MODERN_HAS_DRAW_RECORDS\n"
		"layout(location = 12) in float attr_DrawRecordIndex;\n"
		"layout(std430, binding = 4) readonly buffer ModernDrawRecords {\n"
		"    ModernDrawRecord records[];\n"
		"} uDrawRecords;\n"
		"uniform uint uDrawRecordMode;\n"
		"flat out vec4 vDrawDebugColor;\n"
		"flat out vec4 vDrawLocalParams;\n"
		"flat out vec4 vDrawMaterialFlags;\n"
		"flat out vec4 vDrawMaterialEnhancement;\n"
		"#endif\n"
		"ModernDrawRecord ModernUniformDrawRecord(void) {\n"
		"    ModernDrawRecord record;\n"
		"    record.modelViewProjection = uModelViewProjection;\n"
		"    record.modelViewMatrix = uModelViewMatrix;\n"
		"    record.debugColor = uDebugColor;\n"
		"    record.localParams = uLocalParams;\n"
		"    record.materialFlags = uMaterialFlags;\n"
		"    record.materialEnhancement = uMaterialEnhancement;\n"
		"    record.ids = uvec4(0u);\n"
		"    return record;\n"
		"}\n"
		"ModernDrawRecord ModernFetchDrawRecord(void) {\n"
		"#if MODERN_HAS_DRAW_RECORDS\n"
		"    if (uDrawRecordMode != 0u) {\n"
		"        uint drawRecordIndex = uint(attr_DrawRecordIndex + 0.5);\n"
		"        return uDrawRecords.records[int(drawRecordIndex)];\n"
		"    }\n"
		"#endif\n"
		"    return ModernUniformDrawRecord();\n"
		"}\n"
		"out vec2 vTexCoord;\n"
		"out vec4 vVertexColor;\n"
		"out vec3 vViewNormal;\n"
		"out vec3 vViewTangent;\n"
		"out vec3 vViewBitangent;\n"
		"out vec3 vViewPosition;\n"
		"flat out float vTangentSign;\n"
		"vec3 ModernSafeNormalize(vec3 value, vec3 fallback) {\n"
		"    float len2 = dot(value, value);\n"
		"    return len2 > 0.00000001 ? value * inversesqrt(len2) : fallback;\n"
		"}\n"
		"void main() {\n"
		"    ModernDrawRecord drawRecord = ModernFetchDrawRecord();\n"
		"#if MODERN_HAS_DRAW_RECORDS\n"
		"    vDrawDebugColor = drawRecord.debugColor;\n"
		"    vDrawLocalParams = drawRecord.localParams;\n"
		"    vDrawMaterialFlags = drawRecord.materialFlags;\n"
		"    vDrawMaterialEnhancement = drawRecord.materialEnhancement;\n"
		"#endif\n"
		"    vec4 frameJitter = vec4(uFrame.reserved.xy, 0.0, 0.0);\n"
		"    mat3 localToView = mat3(drawRecord.modelViewMatrix);\n"
		"    vec3 normal = ModernSafeNormalize(localToView * attr_Normal, vec3(0.0, 0.0, 1.0));\n"
		"    vec3 tangent = ModernSafeNormalize(localToView * attr_Tangent0, vec3(1.0, 0.0, 0.0));\n"
		"    vec3 rawBitangent = ModernSafeNormalize(localToView * attr_Tangent1, vec3(0.0, 1.0, 0.0));\n"
		"    tangent = ModernSafeNormalize(tangent - normal * dot(normal, tangent), vec3(1.0, 0.0, 0.0));\n"
		"    float signValue = dot(cross(normal, tangent), rawBitangent) < 0.0 ? -1.0 : 1.0;\n"
		"    vTexCoord = attr_TexCoord0;\n"
		"    vVertexColor = attr_Color;\n"
		"    vViewNormal = normal;\n"
		"    vViewTangent = tangent;\n"
		"    vViewBitangent = ModernSafeNormalize(cross(normal, tangent) * signValue, rawBitangent);\n"
		"    vViewPosition = (drawRecord.modelViewMatrix * vec4(attr_Position, 1.0)).xyz;\n"
		"    vTangentSign = signValue;\n"
		"    gl_Position = drawRecord.modelViewProjection * vec4(attr_Position, 1.0) + frameJitter;\n"
		"}\n",
		glslVersion,
		hasDrawRecords );
}

static void R_ModernGLShaderLibrary_BuildFragmentSource( int glslVersion, modernGLShaderProgramKind_t kind, char *buffer, int bufferSize ) {
	const int hasShaderStorage = glslVersion >= 430 ? 1 : 0;
	const int hasImageLoadStore = glslVersion >= 430 ? 1 : 0;
	const int hasDrawRecords = glslVersion >= 430 && kind != MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE ? 1 : 0;
	const int hasTextureTable = glslVersion >= 430 && kind != MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE ? 1 : 0;

	char materialTextureHeader[4096];
	idStr::snPrintf(
		materialTextureHeader,
		sizeof( materialTextureHeader ),
		"uniform sampler2D uMainTexture;\n"
		"uniform sampler2D uNormalTexture;\n"
		"uniform sampler2D uSpecularTexture;\n"
		"uniform sampler2D uEmissiveTexture;\n"
		"#define MODERN_HAS_TEXTURE_TABLE %d\n"
		"#define MODERN_MATERIAL_TEXTURE_TABLE_SIZE %d\n"
		"#if MODERN_HAS_TEXTURE_TABLE\n"
		"uniform sampler2D uMaterialTextures[%d];\n"
		"uniform uvec4 uTextureIndices;\n"
		"uniform uint uTextureTableMode;\n"
		"uint ModernTextureTableIndex(uint index) {\n"
		"    return min(index, uint(MODERN_MATERIAL_TEXTURE_TABLE_SIZE - 1));\n"
		"}\n"
		"#endif\n"
		"vec4 ModernSampleMainTexture(vec2 uv) {\n"
		"#if MODERN_HAS_TEXTURE_TABLE\n"
		"    if (uTextureTableMode != 0u) { return texture(uMaterialTextures[int(ModernTextureTableIndex(uTextureIndices.x))], uv); }\n"
		"#endif\n"
		"    return texture(uMainTexture, uv);\n"
		"}\n"
		"vec4 ModernSampleNormalTexture(vec2 uv) {\n"
		"#if MODERN_HAS_TEXTURE_TABLE\n"
		"    if (uTextureTableMode != 0u) { return texture(uMaterialTextures[int(ModernTextureTableIndex(uTextureIndices.y))], uv); }\n"
		"#endif\n"
		"    return texture(uNormalTexture, uv);\n"
		"}\n"
		"vec4 ModernSampleSpecularTexture(vec2 uv) {\n"
		"#if MODERN_HAS_TEXTURE_TABLE\n"
		"    if (uTextureTableMode != 0u) { return texture(uMaterialTextures[int(ModernTextureTableIndex(uTextureIndices.z))], uv); }\n"
		"#endif\n"
		"    return texture(uSpecularTexture, uv);\n"
		"}\n"
		"vec4 ModernSampleEmissiveTexture(vec2 uv) {\n"
		"#if MODERN_HAS_TEXTURE_TABLE\n"
		"    if (uTextureTableMode != 0u) { return texture(uMaterialTextures[int(ModernTextureTableIndex(uTextureIndices.w))], uv); }\n"
		"#endif\n"
		"    return texture(uEmissiveTexture, uv);\n"
		"}\n",
		hasTextureTable,
		MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY,
		MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY );

	if ( kind == MODERN_GL_SHADER_DEPTH || kind == MODERN_GL_SHADER_SHADOW_DEPTH ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"in vec2 vTexCoord;\n"
			"#define MODERN_HAS_DRAW_RECORDS %d\n"
			"#if MODERN_HAS_DRAW_RECORDS\n"
			"flat in vec4 vDrawLocalParams;\n"
			"#define uLocalParams vDrawLocalParams\n"
			"#else\n"
			"uniform vec4 uLocalParams;\n"
			"#endif\n"
			"%s"
			"void main() {\n"
			"    if (uLocalParams.y > 0.5 && ModernSampleMainTexture(vTexCoord).a < max(uLocalParams.x, 0.001)) { discard; }\n"
			"}\n",
			glslVersion,
			hasDrawRecords,
			materialTextureHeader );
		return;
	}

	char sharedHeader[12288];
	idStr::snPrintf(
		sharedHeader,
		sizeof( sharedHeader ),
		"in vec2 vTexCoord;\n"
		"in vec4 vVertexColor;\n"
		"in vec3 vViewNormal;\n"
		"in vec3 vViewTangent;\n"
		"in vec3 vViewBitangent;\n"
		"in vec3 vViewPosition;\n"
		"flat in float vTangentSign;\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"#define MODERN_HAS_DRAW_RECORDS %d\n"
		"#if MODERN_HAS_DRAW_RECORDS\n"
		"flat in vec4 vDrawDebugColor;\n"
		"flat in vec4 vDrawLocalParams;\n"
		"flat in vec4 vDrawMaterialFlags;\n"
		"flat in vec4 vDrawMaterialEnhancement;\n"
		"#define uDebugColor vDrawDebugColor\n"
		"#define uLocalParams vDrawLocalParams\n"
		"#define uMaterialFlags vDrawMaterialFlags\n"
		"#define uMaterialEnhancement vDrawMaterialEnhancement\n"
		"#else\n"
		"uniform vec4 uDebugColor;\n"
		"uniform vec4 uLocalParams;\n"
		"uniform vec4 uMaterialFlags;\n"
		"uniform vec4 uMaterialEnhancement;\n"
		"#endif\n"
		"%s"
		"vec3 ModernSafeNormal(vec3 normal) {\n"
		"    float len2 = dot(normal, normal);\n"
		"    return len2 > 0.00000001 ? normal * inversesqrt(len2) : vec3(0.0, 0.0, 1.0);\n"
		"}\n"
		"vec3 ModernDecodeClassicNormal(vec4 bumpSample) {\n"
		"    return ModernSafeNormal(vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0);\n"
		"}\n"
		"vec3 ModernDecodeEnhancedNormal(vec4 bumpSample) {\n"
		"    vec2 xy = vec2(bumpSample.a, bumpSample.g) * 2.0 - 1.0;\n"
		"    xy *= max(uMaterialEnhancement.y, 0.0);\n"
		"    float xyLengthSq = dot(xy, xy);\n"
		"    if (xyLengthSq > 1.0) { xy *= inversesqrt(xyLengthSq); xyLengthSq = 1.0; }\n"
		"    float encodedZ = max(bumpSample.b * 2.0 - 1.0, 0.0);\n"
		"    float reconstructedZ = sqrt(max(1.0 - xyLengthSq, 0.0));\n"
		"    return ModernSafeNormal(vec3(xy, mix(encodedZ, reconstructedZ, 0.75)));\n"
		"}\n"
		"vec3 ModernMaterialTangentNormal(void) {\n"
		"    if (uMaterialFlags.x <= 0.5) { return vec3(0.0, 0.0, 1.0); }\n"
		"    vec4 bumpSample = ModernSampleNormalTexture(vTexCoord);\n"
		"    return uMaterialEnhancement.x > 0.5 ? ModernDecodeEnhancedNormal(bumpSample) : ModernDecodeClassicNormal(bumpSample);\n"
		"}\n"
		"vec3 ModernMaterialNormal(void) {\n"
		"    vec3 tangentNormal = ModernMaterialTangentNormal();\n"
		"    vec3 tangent = ModernSafeNormal(vViewTangent);\n"
		"    vec3 bitangent = ModernSafeNormal(vViewBitangent);\n"
		"    vec3 normal = ModernSafeNormal(vViewNormal);\n"
		"    return ModernSafeNormal(mat3(tangent, bitangent, normal) * tangentNormal);\n"
		"}\n"
		"float ModernSpecularStrength(void) {\n"
		"    vec3 specular = uMaterialFlags.y > 0.5 ? ModernSampleSpecularTexture(vTexCoord).rgb : vec3(0.04);\n"
		"    float boost = uMaterialEnhancement.x > 0.5 ? max(uMaterialEnhancement.z, 0.0) : 1.0;\n"
		"    return clamp(dot(specular, vec3(0.333333)) * boost, 0.0, 4.0);\n"
		"}\n"
		"float ModernMaterialFresnel(void) {\n"
		"    return uMaterialEnhancement.x > 0.5 ? clamp(uMaterialEnhancement.w, 0.0, 1.0) : 0.0;\n"
		"}\n"
		"vec3 ModernEmissiveColor(void) {\n"
		"    return uMaterialFlags.z > 0.5 ? ModernSampleEmissiveTexture(vTexCoord).rgb : vec3(0.0);\n"
		"}\n"
		"float ModernNormalLightScale(void) {\n"
		"    vec3 lightDir = normalize(vec3(0.25, 0.35, 1.0));\n"
		"    return clamp(dot(ModernMaterialNormal(), lightDir) * 0.5 + 0.5, 0.18, 1.0);\n"
		"}\n",
		hasDrawRecords,
		materialTextureHeader );

	const char *clusterHeader =
		"#define MODERN_CLUSTER_UBO_MAX_LIGHTS 256\n"
		"#define MODERN_CLUSTER_UBO_MAX_INDEX_RECORDS 1024\n"
		"struct ModernClusterLightRecord {\n"
		"    vec4 positionRadius;\n"
		"    vec4 colorType;\n"
		"    vec4 scissorDepth;\n"
		"    vec4 flags;\n"
		"    vec4 depthRange;\n"
		"    vec4 falloff;\n"
		"    vec4 projectS;\n"
		"    vec4 projectT;\n"
		"    vec4 projectQ;\n"
		"};\n"
		"layout(std140) uniform ModernClusterGridParams {\n"
		"    vec4 grid;\n"
		"    vec4 depth;\n"
		"    vec4 viewport;\n"
		"    vec4 counts;\n"
		"} uClusterGrid;\n"
		"#if MODERN_HAS_SHADER_STORAGE\n"
		"layout(std430, binding = 6) readonly buffer ModernLightRecords {\n"
		"    ModernClusterLightRecord lights[];\n"
		"} uClusterLightsSSBO;\n"
		"layout(std430, binding = 7) readonly buffer ModernClusterIndexRecordsSSBO {\n"
		"    uvec4 indices[];\n"
		"} uClusterIndicesSSBO;\n"
		"#else\n"
		"layout(std140) uniform ModernClusterLightRecords {\n"
		"    ModernClusterLightRecord lights[MODERN_CLUSTER_UBO_MAX_LIGHTS];\n"
		"} uClusterLights;\n"
		"layout(std140) uniform ModernClusterIndexRecords {\n"
		"    uvec4 indices[MODERN_CLUSTER_UBO_MAX_INDEX_RECORDS];\n"
		"} uClusterIndices;\n"
		"#endif\n"
		"ModernClusterLightRecord ModernClusterEmptyLight() {\n"
		"    ModernClusterLightRecord light;\n"
		"    light.positionRadius = vec4(0.0);\n"
		"    light.colorType = vec4(0.0);\n"
		"    light.scissorDepth = vec4(0.0);\n"
		"    light.flags = vec4(0.0);\n"
		"    light.depthRange = vec4(0.0);\n"
		"    light.falloff = vec4(0.0);\n"
		"    light.projectS = vec4(0.0);\n"
		"    light.projectT = vec4(0.0);\n"
		"    light.projectQ = vec4(0.0, 0.0, 1.0, 0.0);\n"
		"    return light;\n"
		"}\n"
		"ModernClusterLightRecord ModernClusterFetchLight(uint lightIndex) {\n"
		"#if MODERN_HAS_SHADER_STORAGE\n"
		"    if (lightIndex >= uint(uClusterLightsSSBO.lights.length())) { return ModernClusterEmptyLight(); }\n"
		"    return uClusterLightsSSBO.lights[int(lightIndex)];\n"
		"#else\n"
		"    if (lightIndex >= uint(MODERN_CLUSTER_UBO_MAX_LIGHTS)) { return ModernClusterEmptyLight(); }\n"
		"    return uClusterLights.lights[int(lightIndex)];\n"
		"#endif\n"
		"}\n"
		"uvec4 ModernClusterFetchIndex(int indexRecord) {\n"
		"#if MODERN_HAS_SHADER_STORAGE\n"
		"    if (indexRecord < 0 || indexRecord >= uClusterIndicesSSBO.indices.length()) { return uvec4(0xffffffffu); }\n"
		"    return uClusterIndicesSSBO.indices[indexRecord];\n"
		"#else\n"
		"    if (indexRecord < 0 || indexRecord >= MODERN_CLUSTER_UBO_MAX_INDEX_RECORDS) { return uvec4(0xffffffffu); }\n"
		"    return uClusterIndices.indices[indexRecord];\n"
		"#endif\n"
		"}\n"
		"uvec4 ModernClusterFetchRange(int clusterIndex) {\n"
		"    int headerBase = int(max(uClusterGrid.viewport.w, 0.0));\n"
		"    return ModernClusterFetchIndex(headerBase + clusterIndex);\n"
		"}\n"
		"uint ModernClusterFetchLightIndex(uint scalarOffset) {\n"
		"    uint flatBase = uint(max(uClusterGrid.viewport.z, 0.0));\n"
		"    uvec4 word = ModernClusterFetchIndex(int(flatBase + (scalarOffset >> 2u)));\n"
		"    uint lane = scalarOffset & 3u;\n"
		"    if (lane == 0u) { return word.x; }\n"
		"    if (lane == 1u) { return word.y; }\n"
		"    if (lane == 2u) { return word.z; }\n"
		"    return word.w;\n"
		"}\n"
		"float ModernClusterLinearDepth(float rawDepth) {\n"
		"    float n = max(uClusterGrid.depth.x, 0.01);\n"
		"    float f = max(uClusterGrid.depth.y, n + 1.0);\n"
		"    return clamp((n * f) / max(f - rawDepth * (f - n), 0.0001), n, f);\n"
		"}\n"
		"int ModernClusterSliceForDepth(float viewDepth) {\n"
		"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
		"    float n = max(uClusterGrid.depth.x, 0.01);\n"
		"    float f = max(uClusterGrid.depth.y, n + 1.0);\n"
		"    float z = clamp(viewDepth, n, f);\n"
		"    float denom = max(uClusterGrid.depth.w, 0.0001);\n"
		"    float normalized = clamp(log(max(z, n) / n) / denom, 0.0, 0.999999);\n"
		"    return clamp(int(floor(normalized * float(grid.z))), 0, grid.z - 1);\n"
		"}\n"
		"vec3 ModernClusterViewPositionFromDepth(vec2 uv, float rawDepth) {\n"
		"    float z = ModernClusterLinearDepth(rawDepth);\n"
		"    vec2 ndc = uv * 2.0 - 1.0;\n"
		"    float aspect = uClusterGrid.viewport.y > 0.0 ? uClusterGrid.viewport.x / uClusterGrid.viewport.y : 1.0;\n"
		"    return vec3(ndc.x * z * aspect, ndc.y * z, z);\n"
		"}\n"
		"float ModernClusterProjectedMask(ModernClusterLightRecord light, vec3 viewPosition) {\n"
		"    vec4 p = vec4(viewPosition, 1.0);\n"
		"    float q = dot(light.projectQ, p);\n"
		"    if (abs(q) < 0.0001) { return 0.0; }\n"
		"    vec2 uv = vec2(dot(light.projectS, p), dot(light.projectT, p)) / q;\n"
		"    vec2 inside = step(vec2(0.0), uv) * step(uv, vec2(1.0));\n"
		"    return inside.x * inside.y;\n"
		"}\n"
		"vec3 ModernClusterEvaluateLight(ModernClusterLightRecord light, vec3 viewPosition, vec3 normal, float specular, float fresnelStrength, out float attenuation) {\n"
		"    int type = int(floor(light.colorType.w + 0.5));\n"
		"    bool projected = type == 1;\n"
		"    bool point = type == 0;\n"
		"    vec3 toLight = light.positionRadius.xyz - viewPosition;\n"
		"    float dist = length(toLight);\n"
		"    vec3 lightDir = dist > 0.0001 ? toLight / dist : vec3(0.0, 0.0, 1.0);\n"
		"    float radius = max(light.positionRadius.w, 1.0);\n"
		"    float radial = clamp(1.0 - dist / radius, 0.0, 1.0);\n"
		"    radial *= radial;\n"
		"    float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);\n"
		"    vec3 viewDir = length(viewPosition) > 0.0001 ? normalize(-viewPosition) : vec3(0.0, 0.0, -1.0);\n"
		"    vec3 halfDir = normalize(lightDir + viewDir);\n"
		"    float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);\n"
		"    float fresnel = 1.0 + pow(1.0 - ndotv, 5.0) * 2.0 * clamp(fresnelStrength, 0.0, 1.0);\n"
		"    float spec = pow(clamp(dot(normal, halfDir), 0.0, 1.0), 24.0) * specular * fresnel;\n"
		"    float projectMask = projected ? ModernClusterProjectedMask(light, viewPosition) : 1.0;\n"
		"    attenuation = (point || projected) ? radial * (ndotl + spec) * projectMask : 0.0;\n"
		"    return light.colorType.rgb * attenuation;\n"
		"}\n";
	const char *shadowPolicyHeader =
		"#define MODERN_SHADOW_POLICY_MAPPED 1.0\n"
		"#define MODERN_SHADOW_POLICY_STENCIL_FALLBACK 2.0\n"
		"#define MODERN_SHADOW_POLICY_SKIPPED 3.0\n"
		"float ModernClusterShadowVisibility(ModernClusterLightRecord light) {\n"
		"    float policy = floor(light.flags.w + 0.5);\n"
		"    float descriptorValid = step(0.0, light.flags.z);\n"
		"    if (policy == MODERN_SHADOW_POLICY_MAPPED) { return descriptorValid; }\n"
		"    return 1.0;\n"
		"}\n";

	if ( kind == MODERN_GL_SHADER_GBUFFER_OPAQUE ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"in vec2 vTexCoord;\n"
			"in vec4 vVertexColor;\n"
			"in vec3 vViewNormal;\n"
			"in vec3 vViewTangent;\n"
			"in vec3 vViewBitangent;\n"
			"flat in float vTangentSign;\n"
			"layout(location = 0) out vec4 out_Albedo;\n"
			"layout(location = 1) out vec4 out_Normal;\n"
			"layout(location = 2) out vec4 out_Material;\n"
			"layout(location = 3) out vec4 out_Emissive;\n"
			"#define MODERN_HAS_DRAW_RECORDS %d\n"
			"#if MODERN_HAS_DRAW_RECORDS\n"
			"flat in vec4 vDrawDebugColor;\n"
			"flat in vec4 vDrawLocalParams;\n"
			"flat in vec4 vDrawMaterialFlags;\n"
			"flat in vec4 vDrawMaterialEnhancement;\n"
			"#define uDebugColor vDrawDebugColor\n"
			"#define uLocalParams vDrawLocalParams\n"
			"#define uMaterialFlags vDrawMaterialFlags\n"
			"#define uMaterialEnhancement vDrawMaterialEnhancement\n"
			"#else\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"uniform vec4 uMaterialFlags;\n"
			"uniform vec4 uMaterialEnhancement;\n"
			"#endif\n"
			"%s"
			"vec3 ModernSafeNormal(vec3 normal) {\n"
			"    float len2 = dot(normal, normal);\n"
			"    return len2 > 0.00000001 ? normal * inversesqrt(len2) : vec3(0.0, 0.0, 1.0);\n"
			"}\n"
			"vec3 ModernDecodeClassicNormal(vec4 bumpSample) {\n"
			"    return ModernSafeNormal(vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0);\n"
			"}\n"
			"vec3 ModernDecodeEnhancedNormal(vec4 bumpSample) {\n"
			"    vec2 xy = vec2(bumpSample.a, bumpSample.g) * 2.0 - 1.0;\n"
			"    xy *= max(uMaterialEnhancement.y, 0.0);\n"
			"    float xyLengthSq = dot(xy, xy);\n"
			"    if (xyLengthSq > 1.0) { xy *= inversesqrt(xyLengthSq); xyLengthSq = 1.0; }\n"
			"    float encodedZ = max(bumpSample.b * 2.0 - 1.0, 0.0);\n"
			"    float reconstructedZ = sqrt(max(1.0 - xyLengthSq, 0.0));\n"
			"    return ModernSafeNormal(vec3(xy, mix(encodedZ, reconstructedZ, 0.75)));\n"
			"}\n"
			"vec3 ModernMaterialTangentNormal(void) {\n"
			"    if (uMaterialFlags.x <= 0.5) { return vec3(0.0, 0.0, 1.0); }\n"
			"    vec4 bumpSample = ModernSampleNormalTexture(vTexCoord);\n"
			"    return uMaterialEnhancement.x > 0.5 ? ModernDecodeEnhancedNormal(bumpSample) : ModernDecodeClassicNormal(bumpSample);\n"
			"}\n"
			"vec3 ModernMaterialNormal(void) {\n"
			"    vec3 tangentNormal = ModernMaterialTangentNormal();\n"
			"    return ModernSafeNormal(mat3(ModernSafeNormal(vViewTangent), ModernSafeNormal(vViewBitangent), ModernSafeNormal(vViewNormal)) * tangentNormal);\n"
			"}\n"
			"float ModernSpecularStrength(void) {\n"
			"    vec3 specular = uMaterialFlags.y > 0.5 ? ModernSampleSpecularTexture(vTexCoord).rgb : vec3(0.04);\n"
			"    float boost = uMaterialEnhancement.x > 0.5 ? max(uMaterialEnhancement.z, 0.0) : 1.0;\n"
			"    return clamp(dot(specular, vec3(0.333333)) * boost, 0.0, 4.0);\n"
			"}\n"
			"float ModernMaterialFresnel(void) {\n"
			"    return uMaterialEnhancement.x > 0.5 ? clamp(uMaterialEnhancement.w, 0.0, 1.0) : 0.0;\n"
			"}\n"
			"void main() {\n"
			"    vec4 texel = ModernSampleMainTexture(vTexCoord);\n"
			"    vec3 baseColor = texel.rgb * max(uDebugColor.rgb, vec3(0.0));\n"
			"    vec3 normal = ModernMaterialNormal();\n"
			"    float specular = ModernSpecularStrength();\n"
			"    vec3 emissive = uMaterialFlags.z > 0.5 ? ModernSampleEmissiveTexture(vTexCoord).rgb : vec3(uLocalParams.w);\n"
			"    out_Albedo = vec4(baseColor, 1.0);\n"
			"    out_Normal = vec4(normal * 0.5 + 0.5, vTangentSign * 0.5 + 0.5);\n"
			"    out_Material = vec4(0.04, specular, uLocalParams.z, ModernMaterialFresnel());\n"
			"    out_Emissive = vec4(emissive, 1.0);\n"
			"}\n",
			glslVersion,
			hasDrawRecords,
			materialTextureHeader );
		return;
	}

	if ( kind == MODERN_GL_SHADER_GBUFFER_ALPHA_TEST ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"in vec2 vTexCoord;\n"
			"in vec4 vVertexColor;\n"
			"in vec3 vViewNormal;\n"
			"in vec3 vViewTangent;\n"
			"in vec3 vViewBitangent;\n"
			"flat in float vTangentSign;\n"
			"layout(location = 0) out vec4 out_Albedo;\n"
			"layout(location = 1) out vec4 out_Normal;\n"
			"layout(location = 2) out vec4 out_Material;\n"
			"layout(location = 3) out vec4 out_Emissive;\n"
			"#define MODERN_HAS_DRAW_RECORDS %d\n"
			"#if MODERN_HAS_DRAW_RECORDS\n"
			"flat in vec4 vDrawDebugColor;\n"
			"flat in vec4 vDrawLocalParams;\n"
			"flat in vec4 vDrawMaterialFlags;\n"
			"flat in vec4 vDrawMaterialEnhancement;\n"
			"#define uDebugColor vDrawDebugColor\n"
			"#define uLocalParams vDrawLocalParams\n"
			"#define uMaterialFlags vDrawMaterialFlags\n"
			"#define uMaterialEnhancement vDrawMaterialEnhancement\n"
			"#else\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"uniform vec4 uMaterialFlags;\n"
			"uniform vec4 uMaterialEnhancement;\n"
			"#endif\n"
			"%s"
			"vec3 ModernSafeNormal(vec3 normal) {\n"
			"    float len2 = dot(normal, normal);\n"
			"    return len2 > 0.00000001 ? normal * inversesqrt(len2) : vec3(0.0, 0.0, 1.0);\n"
			"}\n"
			"vec3 ModernDecodeClassicNormal(vec4 bumpSample) {\n"
			"    return ModernSafeNormal(vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0);\n"
			"}\n"
			"vec3 ModernDecodeEnhancedNormal(vec4 bumpSample) {\n"
			"    vec2 xy = vec2(bumpSample.a, bumpSample.g) * 2.0 - 1.0;\n"
			"    xy *= max(uMaterialEnhancement.y, 0.0);\n"
			"    float xyLengthSq = dot(xy, xy);\n"
			"    if (xyLengthSq > 1.0) { xy *= inversesqrt(xyLengthSq); xyLengthSq = 1.0; }\n"
			"    float encodedZ = max(bumpSample.b * 2.0 - 1.0, 0.0);\n"
			"    float reconstructedZ = sqrt(max(1.0 - xyLengthSq, 0.0));\n"
			"    return ModernSafeNormal(vec3(xy, mix(encodedZ, reconstructedZ, 0.75)));\n"
			"}\n"
			"vec3 ModernMaterialTangentNormal(void) {\n"
			"    if (uMaterialFlags.x <= 0.5) { return vec3(0.0, 0.0, 1.0); }\n"
			"    vec4 bumpSample = ModernSampleNormalTexture(vTexCoord);\n"
			"    return uMaterialEnhancement.x > 0.5 ? ModernDecodeEnhancedNormal(bumpSample) : ModernDecodeClassicNormal(bumpSample);\n"
			"}\n"
			"vec3 ModernMaterialNormal(void) {\n"
			"    vec3 tangentNormal = ModernMaterialTangentNormal();\n"
			"    return ModernSafeNormal(mat3(ModernSafeNormal(vViewTangent), ModernSafeNormal(vViewBitangent), ModernSafeNormal(vViewNormal)) * tangentNormal);\n"
			"}\n"
			"float ModernSpecularStrength(void) {\n"
			"    vec3 specular = uMaterialFlags.y > 0.5 ? ModernSampleSpecularTexture(vTexCoord).rgb : vec3(0.04);\n"
			"    float boost = uMaterialEnhancement.x > 0.5 ? max(uMaterialEnhancement.z, 0.0) : 1.0;\n"
			"    return clamp(dot(specular, vec3(0.333333)) * boost, 0.0, 4.0);\n"
			"}\n"
			"float ModernMaterialFresnel(void) {\n"
			"    return uMaterialEnhancement.x > 0.5 ? clamp(uMaterialEnhancement.w, 0.0, 1.0) : 0.0;\n"
			"}\n"
			"void main() {\n"
			"    vec4 texel = ModernSampleMainTexture(vTexCoord);\n"
			"    if (texel.a < max(uLocalParams.x, 0.001)) { discard; }\n"
			"    vec3 normal = ModernMaterialNormal();\n"
			"    float specular = ModernSpecularStrength();\n"
			"    vec3 emissive = uMaterialFlags.z > 0.5 ? ModernSampleEmissiveTexture(vTexCoord).rgb : vec3(0.0);\n"
			"    out_Albedo = vec4(texel.rgb * max(uDebugColor.rgb, vec3(0.0)), 1.0);\n"
			"    out_Normal = vec4(normal * 0.5 + 0.5, vTangentSign * 0.5 + 0.5);\n"
			"    out_Material = vec4(0.04, specular, uLocalParams.z, ModernMaterialFresnel());\n"
			"    out_Emissive = vec4(emissive, 1.0);\n"
			"}\n",
			glslVersion,
			hasDrawRecords,
			materialTextureHeader );
		return;
	}

	if ( kind == MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"#define MODERN_HAS_SHADER_STORAGE %d\n"
			"in vec2 vTexCoord;\n"
			"layout(location = 0) out vec4 out_Color;\n"
			"uniform vec4 uDebugColor;\n"
			"uniform vec4 uLocalParams;\n"
			"uniform sampler2D uMainTexture;\n"
			"uniform sampler2D uGBufferNormal;\n"
			"uniform sampler2D uGBufferMaterial;\n"
			"uniform sampler2D uGBufferEmissive;\n"
			"uniform sampler2D uSceneDepth;\n"
			"%s"
			"%s"
			"void main() {\n"
			"    vec4 albedo = texture(uMainTexture, vTexCoord);\n"
			"    vec3 normal = normalize(texture(uGBufferNormal, vTexCoord).xyz * 2.0 - 1.0);\n"
			"    vec4 material = texture(uGBufferMaterial, vTexCoord);\n"
			"    vec3 lightGrid = texture(uGBufferEmissive, vTexCoord).rgb;\n"
			"    float rawDepth = texture(uSceneDepth, vTexCoord).r;\n"
			"    vec3 viewPosition = ModernClusterViewPositionFromDepth(vTexCoord, rawDepth);\n"
			"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
			"    int tileX = clamp(int(floor(vTexCoord.x * float(grid.x))), 0, grid.x - 1);\n"
			"    int tileY = clamp(int(floor((1.0 - vTexCoord.y) * float(grid.y))), 0, grid.y - 1);\n"
			"    int sliceZ = ModernClusterSliceForDepth(viewPosition.z);\n"
			"    int clusterIndex = (sliceZ * grid.y + tileY) * grid.x + tileX;\n"
			"    uvec4 clusterRange = ModernClusterFetchRange(clusterIndex);\n"
			"    int maxLights = int(max(uClusterGrid.grid.w, 1.0));\n"
			"    int clusterLightCount = int(min(clusterRange.y, uint(maxLights)));\n"
			"    vec3 lightAccum = vec3(0.0);\n"
			"    int contributingLights = 0;\n"
			"    int scannedLights = 0;\n"
			"    for (int i = 0; i < clusterLightCount; ++i) {\n"
			"        uint lightIndex = ModernClusterFetchLightIndex(clusterRange.x + uint(i));\n"
			"        if (lightIndex == 0xffffffffu || lightIndex >= uint(max(uClusterGrid.counts.x, 0.0))) { continue; }\n"
			"        ModernClusterLightRecord light = ModernClusterFetchLight(lightIndex);\n"
			"        int type = int(floor(light.colorType.w + 0.5));\n"
			"        bool supported = type == 0 || type == 1;\n"
			"        vec2 pixel = gl_FragCoord.xy;\n"
			"        float inX = step(light.scissorDepth.x, pixel.x) * step(pixel.x, light.scissorDepth.z);\n"
			"        float inY = step(light.scissorDepth.y, pixel.y) * step(pixel.y, light.scissorDepth.w);\n"
			"        float shadowVisibility = ModernClusterShadowVisibility(light);\n"
			"        float attenuation = 0.0;\n"
			"        vec3 contribution = ModernClusterEvaluateLight(light, viewPosition, normal, material.g, material.a, attenuation);\n"
			"        attenuation = supported ? attenuation * inX * inY * shadowVisibility : 0.0;\n"
			"        lightAccum += contribution * inX * inY * shadowVisibility;\n"
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
			glslVersion,
			hasShaderStorage,
			clusterHeader,
			shadowPolicyHeader );
		return;
	}

	if ( kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE || kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"#define MODERN_HAS_SHADER_STORAGE %d\n"
			"%s"
			"%s"
			"%s"
			"void main() {\n"
			"    vec4 texel = ModernSampleMainTexture(vTexCoord);\n"
			"    if (%d != 0 && texel.a < max(uLocalParams.x, 0.001)) { discard; }\n"
			"    vec3 materialNormal = ModernMaterialNormal();\n"
			"    float specular = ModernSpecularStrength();\n"
			"    vec3 emissive = ModernEmissiveColor();\n"
			"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
			"    vec2 viewport = max(uClusterGrid.viewport.xy, vec2(1.0));\n"
			"    vec2 normalizedPixel = clamp(gl_FragCoord.xy / viewport, vec2(0.0), vec2(0.999));\n"
			"    int tileX = clamp(int(floor(normalizedPixel.x * float(grid.x))), 0, grid.x - 1);\n"
			"    int tileY = clamp(int(floor((1.0 - normalizedPixel.y) * float(grid.y))), 0, grid.y - 1);\n"
			"    int sliceZ = ModernClusterSliceForDepth(max(vViewPosition.z, ModernClusterLinearDepth(gl_FragCoord.z)));\n"
			"    int clusterIndex = (sliceZ * grid.y + tileY) * grid.x + tileX;\n"
			"    uvec4 clusterRange = ModernClusterFetchRange(clusterIndex);\n"
			"    int maxLights = int(max(uClusterGrid.grid.w, 1.0));\n"
			"    int clusterLightCount = int(min(clusterRange.y, uint(maxLights)));\n"
			"    vec3 lightAccum = vec3(0.0);\n"
			"    int scannedLights = 0;\n"
			"    for (int i = 0; i < clusterLightCount; ++i) {\n"
			"        uint lightIndex = ModernClusterFetchLightIndex(clusterRange.x + uint(i));\n"
			"        if (lightIndex == 0xffffffffu || lightIndex >= uint(max(uClusterGrid.counts.x, 0.0))) { continue; }\n"
			"        ModernClusterLightRecord light = ModernClusterFetchLight(lightIndex);\n"
			"        int type = int(floor(light.colorType.w + 0.5));\n"
			"        bool supported = type == 0 || type == 1;\n"
			"        float inX = step(light.scissorDepth.x, gl_FragCoord.x) * step(gl_FragCoord.x, light.scissorDepth.z);\n"
			"        float inY = step(light.scissorDepth.y, gl_FragCoord.y) * step(gl_FragCoord.y, light.scissorDepth.w);\n"
			"        float shadowVisibility = ModernClusterShadowVisibility(light);\n"
			"        float attenuation = 0.0;\n"
			"        vec3 contribution = ModernClusterEvaluateLight(light, vViewPosition, materialNormal, specular, ModernMaterialFresnel(), attenuation);\n"
			"        lightAccum += supported ? contribution * inX * inY * shadowVisibility : vec3(0.0);\n"
			"        scannedLights++;\n"
			"    }\n"
			"    float lightScale = clamp(0.18 + uLocalParams.y + float(scannedLights) * 0.02, 0.18, 2.5);\n"
			"    vec3 lit = texel.rgb * max(uDebugColor.rgb, vec3(0.0)) * (lightScale + lightAccum * (0.30 + specular * 0.25)) + emissive;\n"
			"    out_Color = vec4(clamp(lit, vec3(0.0), vec3(1.0)), texel.a * uDebugColor.a);\n"
			"}\n",
			glslVersion,
			hasShaderStorage,
			sharedHeader,
			clusterHeader,
			shadowPolicyHeader,
			kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST ? 1 : 0 );
		return;
	}

	if ( kind == MODERN_GL_SHADER_TRANSPARENT_FORWARD || kind == MODERN_GL_SHADER_FOG_BLEND ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"#define MODERN_HAS_SHADER_STORAGE %d\n"
			"%s"
			"%s"
			"%s"
			"void main() {\n"
			"    vec4 texel = ModernSampleMainTexture(vTexCoord);\n"
			"    float blendAmount = clamp(uLocalParams.x, 0.0, 1.0);\n"
			"    vec3 blendColor = vec3(uLocalParams.y, uLocalParams.z, uLocalParams.w);\n"
			"    vec3 baseColor = texel.rgb * max(uDebugColor.rgb, vec3(0.0));\n"
			"    vec3 materialNormal = ModernMaterialNormal();\n"
			"    float specular = ModernSpecularStrength();\n"
			"    vec3 emissive = ModernEmissiveColor();\n"
			"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
			"    int maxLights = int(max(uClusterGrid.grid.w, 1.0));\n"
			"    vec2 viewport = max(uClusterGrid.viewport.xy, vec2(1.0));\n"
			"    vec2 normalizedPixel = clamp(gl_FragCoord.xy / viewport, vec2(0.0), vec2(0.999));\n"
			"    int tileX = clamp(int(floor(normalizedPixel.x * float(grid.x))), 0, grid.x - 1);\n"
			"    int tileY = clamp(int(floor((1.0 - normalizedPixel.y) * float(grid.y))), 0, grid.y - 1);\n"
			"    int sliceZ = ModernClusterSliceForDepth(max(vViewPosition.z, ModernClusterLinearDepth(gl_FragCoord.z)));\n"
			"    int clusterIndex = (sliceZ * grid.y + tileY) * grid.x + tileX;\n"
			"    uvec4 clusterRange = ModernClusterFetchRange(clusterIndex);\n"
			"    int clusterLightCount = int(min(clusterRange.y, uint(maxLights)));\n"
			"    vec3 lightAccum = vec3(0.0);\n"
			"    for (int i = 0; i < clusterLightCount; ++i) {\n"
			"        uint lightIndex = ModernClusterFetchLightIndex(clusterRange.x + uint(i));\n"
			"        if (lightIndex == 0xffffffffu || lightIndex >= uint(max(uClusterGrid.counts.x, 0.0))) { continue; }\n"
			"        ModernClusterLightRecord light = ModernClusterFetchLight(lightIndex);\n"
			"        int type = int(floor(light.colorType.w + 0.5));\n"
			"        float shadowVisibility = ModernClusterShadowVisibility(light);\n"
			"        float attenuation = 0.0;\n"
			"        vec3 contribution = ModernClusterEvaluateLight(light, vViewPosition, materialNormal, specular, ModernMaterialFresnel(), attenuation);\n"
			"        if (type == 0 || type == 1) { lightAccum += contribution * shadowVisibility; }\n"
			"    }\n"
			"    out_Color = vec4(clamp(mix(baseColor, blendColor, blendAmount) + lightAccum + emissive, vec3(0.0), vec3(1.0)), texel.a * uDebugColor.a);\n"
			"}\n",
			glslVersion,
			hasShaderStorage,
			sharedHeader,
			clusterHeader,
			shadowPolicyHeader );
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
			"%s"
			"void main() {\n"
			"    out_Color = ModernSampleMainTexture(vTexCoord) * uDebugColor;\n"
			"}\n",
			glslVersion,
			materialTextureHeader );
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
			"%s"
			"void main() {\n"
			"    vec2 uv = clamp(vTexCoord + uLocalParams.xy, vec2(0.0), vec2(1.0));\n"
			"    vec4 texel = ModernSampleMainTexture(uv);\n"
			"    out_Color = vec4(texel.rgb * max(uDebugColor.rgb, vec3(0.0)), texel.a * uDebugColor.a);\n"
			"}\n",
			glslVersion,
			materialTextureHeader );
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

static bool R_ModernGLShaderLibrary_KindUsesMaterialTextures( modernGLShaderProgramKind_t kind ) {
	return kind == MODERN_GL_SHADER_GBUFFER_OPAQUE
		|| kind == MODERN_GL_SHADER_GBUFFER_ALPHA_TEST
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST
		|| kind == MODERN_GL_SHADER_TRANSPARENT_FORWARD
		|| kind == MODERN_GL_SHADER_FOG_BLEND;
}

static bool R_ModernGLShaderLibrary_KindUsesDrawVertColor( modernGLShaderProgramKind_t kind ) {
	(void)kind;
	return false;
}

static bool R_ModernGLShaderLibrary_KindUsesDrawVertTangentSpace( modernGLShaderProgramKind_t kind ) {
	return kind == MODERN_GL_SHADER_GBUFFER_OPAQUE
		|| kind == MODERN_GL_SHADER_GBUFFER_ALPHA_TEST
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST
		|| kind == MODERN_GL_SHADER_TRANSPARENT_FORWARD
		|| kind == MODERN_GL_SHADER_FOG_BLEND;
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
	info.reflection.colorAttribute = 3;
	info.reflection.texCoordAttribute = 8;
	info.reflection.tangentAttribute = 9;
	info.reflection.bitangentAttribute = 10;
	info.reflection.normalAttribute = 11;
	info.reflection.drawRecordAttribute = 12;
	info.reflection.usesFrameConstants = true;
	info.reflection.usesModelViewProjection = true;
	info.reflection.usesModelViewMatrix = R_ModernGLShaderLibrary_KindUsesDrawVertTangentSpace( info.kind );
	info.reflection.usesDebugColor = R_ModernGLShaderLibrary_KindUsesDebugColor( info.kind );
	info.reflection.usesLocalParams = R_ModernGLShaderLibrary_KindUsesLocalParams( info.kind );
	info.reflection.usesMainTexture = R_ModernGLShaderLibrary_KindUsesMainTexture( info.kind );
	info.reflection.usesMaterialTextures = R_ModernGLShaderLibrary_KindUsesMaterialTextures( info.kind );
	info.reflection.usesMaterialTextureTable = info.glslVersion >= 430 && info.reflection.usesMainTexture && info.kind != MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE;
	info.reflection.usesMaterialFlags = info.reflection.usesMaterialTextures;
	info.reflection.usesMaterialEnhancement = info.reflection.usesMaterialTextures;
	info.reflection.usesDrawRecords = info.glslVersion >= 430 && info.kind != MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE;
	info.reflection.usesTexCoord = info.reflection.usesMainTexture;
	info.reflection.usesDrawVertColor = R_ModernGLShaderLibrary_KindUsesDrawVertColor( info.kind );
	info.reflection.usesDrawVertTangentSpace = R_ModernGLShaderLibrary_KindUsesDrawVertTangentSpace( info.kind );
	info.reflection.usesShaderStorage = R_ModernGLShaderLibrary_KindUsesShaderStorage( info.kind, info.glslVersion );
	info.reflection.usesImage = R_ModernGLShaderLibrary_KindUsesImage( info.kind, info.glslVersion );

	const GLuint frameBlockIndex = glGetUniformBlockIndex( info.program, "ModernFrameConstants" );
	info.reflection.frameBlockIndex = frameBlockIndex == GL_INVALID_INDEX ? -1 : static_cast<int>( frameBlockIndex );
	info.reflection.modelViewProjectionLocation = glGetUniformLocation( info.program, "uModelViewProjection" );
	info.reflection.modelViewMatrixLocation = glGetUniformLocation( info.program, "uModelViewMatrix" );
	info.reflection.debugColorLocation = glGetUniformLocation( info.program, "uDebugColor" );
	info.reflection.localParamsLocation = glGetUniformLocation( info.program, "uLocalParams" );
	info.reflection.mainTextureLocation = glGetUniformLocation( info.program, "uMainTexture" );
	info.reflection.normalTextureLocation = glGetUniformLocation( info.program, "uNormalTexture" );
	info.reflection.specularTextureLocation = glGetUniformLocation( info.program, "uSpecularTexture" );
	info.reflection.emissiveTextureLocation = glGetUniformLocation( info.program, "uEmissiveTexture" );
	info.reflection.textureIndicesLocation = glGetUniformLocation( info.program, "uTextureIndices" );
	info.reflection.textureTableModeLocation = glGetUniformLocation( info.program, "uTextureTableMode" );
	info.reflection.materialTextureTableLocation = glGetUniformLocation( info.program, "uMaterialTextures[0]" );
	info.reflection.materialFlagsLocation = glGetUniformLocation( info.program, "uMaterialFlags" );
	info.reflection.materialEnhancementLocation = glGetUniformLocation( info.program, "uMaterialEnhancement" );
	info.reflection.drawRecordModeLocation = glGetUniformLocation( info.program, "uDrawRecordMode" );

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
	if ( info.reflection.usesModelViewMatrix ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uModelViewMatrix",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.modelViewMatrixLocation,
			-1,
			1,
			GL_FLOAT_MAT4,
			true,
			info.reflection.modelViewMatrixLocation >= 0 );
	}
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
	if ( info.reflection.usesMaterialTextures ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uNormalTexture",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			info.reflection.normalTextureLocation,
			1,
			1,
			GL_SAMPLER_2D,
			true,
			info.reflection.normalTextureLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uSpecularTexture",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			info.reflection.specularTextureLocation,
			2,
			1,
			GL_SAMPLER_2D,
			true,
			info.reflection.specularTextureLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uEmissiveTexture",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			info.reflection.emissiveTextureLocation,
			3,
			1,
			GL_SAMPLER_2D,
			true,
			info.reflection.emissiveTextureLocation >= 0 );
	}
	if ( info.reflection.usesMaterialTextureTable ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uTextureIndices",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.textureIndicesLocation,
			-1,
			1,
			GL_UNSIGNED_INT_VEC4,
			true,
			info.reflection.textureIndicesLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uTextureTableMode",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.textureTableModeLocation,
			-1,
			1,
			GL_UNSIGNED_INT,
			true,
			info.reflection.textureTableModeLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uMaterialTextures",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			info.reflection.materialTextureTableLocation,
			0,
			MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY,
			GL_SAMPLER_2D,
			true,
			info.reflection.materialTextureTableLocation >= 0 );
	}
	if ( info.reflection.usesMaterialFlags ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uMaterialFlags",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.materialFlagsLocation,
			-1,
			1,
			GL_FLOAT_VEC4,
			true,
			info.reflection.materialFlagsLocation >= 0 );
	}
	if ( info.reflection.usesMaterialEnhancement ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uMaterialEnhancement",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.materialEnhancementLocation,
			-1,
			1,
			GL_FLOAT_VEC4,
			true,
			info.reflection.materialEnhancementLocation >= 0 );
	}
	if ( info.reflection.usesDrawRecords ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uDrawRecordMode",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.drawRecordModeLocation,
			-1,
			1,
			GL_UNSIGNED_INT,
			true,
			info.reflection.drawRecordModeLocation >= 0 );
		const int drawRecordSSBOIndex = R_ModernGLShaderLibrary_ProgramResourceIndex( info.program, GL_SHADER_STORAGE_BLOCK, "ModernDrawRecords" );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.shaderStorageBlocks,
			info.reflection.shaderStorageBlockCount,
			"ModernDrawRecords",
			MODERN_GL_SHADER_RESOURCE_SHADER_STORAGE_BLOCK,
			drawRecordSSBOIndex,
			-1,
			4,
			1,
			GL_SHADER_STORAGE_BLOCK,
			true,
			drawRecordSSBOIndex >= 0 );
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
		"attr_Color",
		MODERN_GL_SHADER_RESOURCE_ATTRIBUTE,
		-1,
		3,
		-1,
		1,
		GL_FLOAT_VEC4,
		info.reflection.usesDrawVertColor,
		info.reflection.usesDrawVertColor );
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
	R_ModernGLShaderLibrary_AddReflectionRecord(
		info.reflection.attributes,
		info.reflection.attributeCount,
		"attr_Tangent0",
		MODERN_GL_SHADER_RESOURCE_ATTRIBUTE,
		-1,
		9,
		-1,
		1,
		GL_FLOAT_VEC3,
		info.reflection.usesDrawVertTangentSpace,
		info.reflection.usesDrawVertTangentSpace );
	if ( info.reflection.usesDrawRecords ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.attributes,
			info.reflection.attributeCount,
			"attr_DrawRecordIndex",
			MODERN_GL_SHADER_RESOURCE_ATTRIBUTE,
			-1,
			info.reflection.drawRecordAttribute,
			-1,
			1,
			GL_FLOAT,
			true,
			true );
	}
	R_ModernGLShaderLibrary_AddReflectionRecord(
		info.reflection.attributes,
		info.reflection.attributeCount,
		"attr_Tangent1",
		MODERN_GL_SHADER_RESOURCE_ATTRIBUTE,
		-1,
		10,
		-1,
		1,
		GL_FLOAT_VEC3,
		info.reflection.usesDrawVertTangentSpace,
		info.reflection.usesDrawVertTangentSpace );
	R_ModernGLShaderLibrary_AddReflectionRecord(
		info.reflection.attributes,
		info.reflection.attributeCount,
		"attr_Normal",
		MODERN_GL_SHADER_RESOURCE_ATTRIBUTE,
		-1,
		11,
		-1,
		1,
		GL_FLOAT_VEC3,
		info.reflection.usesDrawVertTangentSpace,
		info.reflection.usesDrawVertTangentSpace );

	info.frameBlockIndex = info.reflection.frameBlockIndex;
	info.modelViewProjectionLocation = info.reflection.modelViewProjectionLocation;
	info.modelViewMatrixLocation = info.reflection.modelViewMatrixLocation;
	info.debugColorLocation = info.reflection.debugColorLocation;
	info.localParamsLocation = info.reflection.localParamsLocation;
	info.mainTextureLocation = info.reflection.mainTextureLocation;
	info.normalTextureLocation = info.reflection.normalTextureLocation;
	info.specularTextureLocation = info.reflection.specularTextureLocation;
	info.emissiveTextureLocation = info.reflection.emissiveTextureLocation;
	info.textureIndicesLocation = info.reflection.textureIndicesLocation;
	info.textureTableModeLocation = info.reflection.textureTableModeLocation;
	info.materialTextureTableLocation = info.reflection.materialTextureTableLocation;
	info.materialFlagsLocation = info.reflection.materialFlagsLocation;
	info.materialEnhancementLocation = info.reflection.materialEnhancementLocation;
	info.drawRecordModeLocation = info.reflection.drawRecordModeLocation;

	if ( info.frameBlockIndex < 0 || info.modelViewProjectionLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing required reflected bindings", info.name );
		return false;
	}
	if ( info.reflection.usesModelViewMatrix && info.modelViewMatrixLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uModelViewMatrix", info.name );
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
	if ( info.reflection.usesMaterialTextures
		&& ( info.normalTextureLocation < 0 || info.specularTextureLocation < 0 || info.emissiveTextureLocation < 0 ) ) {
		common->Warning( "Modern GL program '%s' is missing material texture samplers", info.name );
		return false;
	}
	if ( info.reflection.usesMaterialTextureTable
		&& ( info.textureIndicesLocation < 0 || info.textureTableModeLocation < 0 || info.materialTextureTableLocation < 0 ) ) {
		common->Warning( "Modern GL program '%s' is missing material texture table bindings", info.name );
		return false;
	}
	if ( info.reflection.usesMaterialFlags && info.materialFlagsLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uMaterialFlags", info.name );
		return false;
	}
	if ( info.reflection.usesMaterialEnhancement && info.materialEnhancementLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uMaterialEnhancement", info.name );
		return false;
	}
	if ( info.reflection.usesDrawRecords && info.drawRecordModeLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uDrawRecordMode", info.name );
		return false;
	}

	glUniformBlockBinding( info.program, static_cast<GLuint>( info.frameBlockIndex ), 0 );
	if ( info.reflection.usesMainTexture || info.reflection.usesMaterialTextures ) {
		glUseProgram( info.program );
		if ( info.reflection.usesMainTexture ) {
			glUniform1i( info.mainTextureLocation, 0 );
		}
		if ( info.reflection.usesMaterialTextures ) {
			glUniform1i( info.normalTextureLocation, 1 );
			glUniform1i( info.specularTextureLocation, 2 );
			glUniform1i( info.emissiveTextureLocation, 3 );
		}
		if ( info.reflection.usesMaterialTextureTable && glUniform1iv != NULL ) {
			GLint tableUnits[MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY];
			for ( int i = 0; i < MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY; ++i ) {
				tableUnits[i] = i;
			}
			glUniform1iv( info.materialTextureTableLocation, MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY, tableUnits );
		}
		glUseProgram( 0 );
	}
	if ( info.reflection.usesMaterialTextureTable && glUniform1ui != NULL ) {
		glUseProgram( info.program );
		glUniform1ui( info.textureTableModeLocation, 0 );
		if ( glUniform4ui != NULL ) {
			glUniform4ui( info.textureIndicesLocation, 0, 0, 0, 0 );
		}
		glUseProgram( 0 );
	}
	if ( info.reflection.usesDrawRecords && glUniform1ui != NULL ) {
		glUseProgram( info.program );
		glUniform1ui( info.drawRecordModeLocation, 0 );
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
	info.modelViewMatrixLocation = -1;
	info.debugColorLocation = -1;
	info.localParamsLocation = -1;
	info.mainTextureLocation = -1;
	info.normalTextureLocation = -1;
	info.specularTextureLocation = -1;
	info.emissiveTextureLocation = -1;
	info.textureIndicesLocation = -1;
	info.textureTableModeLocation = -1;
	info.materialTextureTableLocation = -1;
	info.materialFlagsLocation = -1;
	info.materialEnhancementLocation = -1;
	info.drawRecordModeLocation = -1;
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

	char vertexSource[16384];
	char fragmentSource[65536];
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
	glBindAttribLocation( info.program, 12, "attr_DrawRecordIndex" );
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
		if ( program->reflection.usesModelViewMatrix && program->modelViewMatrixLocation < 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: model-view reflection mismatch for %s\n", program->name );
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
		if ( program->reflection.usesMaterialTextures
			&& ( program->normalTextureLocation < 0 || program->specularTextureLocation < 0 || program->emissiveTextureLocation < 0 || program->materialFlagsLocation < 0 || program->materialEnhancementLocation < 0 ) ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: material texture reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesMaterialTextureTable
			&& ( program->textureIndicesLocation < 0 || program->textureTableModeLocation < 0 || program->materialTextureTableLocation < 0 ) ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: texture-table reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.uniformBlockCount <= 0 || program->reflection.uniformCount <= 0 || program->reflection.attributeCount <= 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: reflection record coverage mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesDrawVertTangentSpace && program->reflection.attributeCount < 6 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: draw-vertex tangent-space reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesShaderStorage && program->reflection.shaderStorageBlockCount <= 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: SSBO reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesDrawRecords && ( program->drawRecordModeLocation < 0 || program->reflection.shaderStorageBlockCount <= 0 || program->reflection.attributeCount < 7 ) ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: draw-record reflection mismatch for %s\n", program->name );
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
