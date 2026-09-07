// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "GLDebugScope.h"
#include "MaterialResourceTable.h"
#include "ModernGLShaderLibrary.h"
#include "ModernSpecularProbeAtlas.h"

static_assert( MODERN_SPECULAR_PROBE_ATLAS_SIZE == 2048,
	"authored-probe GLSL atlas-size ABI drift" );
static_assert( MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE == 256,
	"authored-probe GLSL face-size ABI drift" );
static_assert( MODERN_SPECULAR_PROBE_ATLAS_CELLS_PER_ROW == 8,
	"authored-probe GLSL cell-grid ABI drift" );
static_assert( MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT == 6,
	"authored-probe GLSL face-count ABI drift" );
static_assert( MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES == 8,
	"authored-probe GLSL slot-count ABI drift" );
static_assert( MODERN_SPECULAR_PROBE_FACE_POSITIVE_X == 0
	&& MODERN_SPECULAR_PROBE_FACE_NEGATIVE_X == 1
	&& MODERN_SPECULAR_PROBE_FACE_POSITIVE_Y == 2
	&& MODERN_SPECULAR_PROBE_FACE_NEGATIVE_Y == 3
	&& MODERN_SPECULAR_PROBE_FACE_POSITIVE_Z == 4
	&& MODERN_SPECULAR_PROBE_FACE_NEGATIVE_Z == 5,
	"authored-probe GLSL face-order ABI drift" );

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
		"uniform uint uDrawRecordCount;\n"
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
		"        float drawRecordValue = attr_DrawRecordIndex + 0.5;\n"
		"        if (attr_DrawRecordIndex >= 0.0 && drawRecordValue >= 0.0 && drawRecordValue < float(uDrawRecordCount)) {\n"
		"            uint drawRecordIndex = uint(drawRecordValue);\n"
		"            return uDrawRecords.records[int(drawRecordIndex)];\n"
		"        }\n"
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
		"uniform sampler2D uMetallicTexture;\n"
		"uniform sampler2D uRoughnessTexture;\n"
		"uniform sampler2D uAOTexture;\n"
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
		"}\n"
		"vec4 ModernSampleMetallicTexture(vec2 uv) { return texture(uMetallicTexture, uv); }\n"
		"vec4 ModernSampleRoughnessTexture(vec2 uv) { return texture(uRoughnessTexture, uv); }\n"
		"vec4 ModernSampleAOTexture(vec2 uv) { return texture(uAOTexture, uv); }\n",
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
		// interaction.vfp parity: DXT5/RXGB carries x in alpha, y in green and z
		// in blue, and the stock decode does NOT renormalize (see
		// Vulkan/shaders/interaction.frag). The transform in ModernMaterialNormal
		// normalizes once afterwards, which the clustered N.L math requires; an
		// extra normalize here would just be redundant, not more correct.
		"vec3 ModernDecodeClassicNormal(vec4 bumpSample) {\n"
		"    return vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0;\n"
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
		"    if (uMaterialFlags.w >= 7.5) {\n"
		"        int normalFormat = int(floor(uMaterialFlags.w - 9.0 + 0.5));\n"
		"        if (normalFormat == 1) { vec2 xy = (bumpSample.rg * 2.0 - 1.0) * max(uLocalParams.w, 0.0); float z = sqrt(max(1.0 - dot(xy, xy), 0.0)); return ModernSafeNormal(vec3(xy, z)); }\n"
		"        if (normalFormat == 2) { return ModernSafeNormal((bumpSample.rgb * 2.0 - 1.0) * vec3(max(uLocalParams.w, 0.0), max(uLocalParams.w, 0.0), 1.0)); }\n"
		"        return ModernSafeNormal(ModernDecodeClassicNormal(bumpSample));\n"
		"    }\n"
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
		"bool ModernIsPBRMaterial(void) { return uMaterialFlags.w >= 7.5; }\n"
		"vec3 ModernPBRBaseColor(void) { return pow(max(ModernSampleMainTexture(vTexCoord).rgb, vec3(0.0)), vec3(2.2)); }\n"
		"vec3 ModernPBRMaterialData(void) {\n"
		"    vec3 orm = uMaterialFlags.y > 0.5 ? ModernSampleSpecularTexture(vTexCoord).rgb : vec3(1.0);\n"
		"    if (uMaterialFlags.y < -0.5) { orm = vec3(ModernSampleAOTexture(vTexCoord).r, ModernSampleRoughnessTexture(vTexCoord).r, ModernSampleMetallicTexture(vTexCoord).r); }\n"
		"    return vec3(clamp(orm.b * uLocalParams.x, 0.0, 1.0), clamp(orm.g * uLocalParams.y, 0.02, 1.0), clamp(orm.r * uLocalParams.z, 0.0, 1.0));\n"
		"}\n"
		"vec3 ModernPBREmissiveColor(void) {\n"
		"    vec3 texel = uMaterialFlags.z > 0.5 ? ModernSampleEmissiveTexture(vTexCoord).rgb : vec3(0.0);\n"
		"    return pow(max(texel, vec3(0.0)), vec3(2.2)) * max(uDebugColor.rgb, vec3(0.0));\n"
		"}\n"
		"float ModernNormalLightScale(void) {\n"
		"    vec3 lightDir = normalize(vec3(0.25, 0.35, 1.0));\n"
		"    return clamp(dot(ModernMaterialNormal(), lightDir) * 0.5 + 0.5, 0.18, 1.0);\n"
		"}\n"
		"vec3 ModernSceneReferredColor(vec3 color) {\n"
		"    return max(color, vec3(0.0));\n"
		"}\n",
		hasDrawRecords,
		materialTextureHeader );

	const char *clusterHeader =
		"#define MODERN_CLUSTER_UBO_MAX_LIGHTS 256\n"
		"#define MODERN_CLUSTER_UBO_MAX_INDEX_RECORDS 1024\n"
		"#define MODERN_CLUSTER_UBO_MAX_SHADOW_DESCRIPTORS 64\n"
		"#define MODERN_SPECULAR_PROBE_MAX_RECORDS 32\n"
		"#define MODERN_SPECULAR_PROBE_ATLAS_SIZE 2048\n"
		"#define MODERN_SPECULAR_PROBE_FACE_SIZE 256\n"
		"#define MODERN_SPECULAR_PROBE_CELLS_PER_ROW 8\n"
		"#define MODERN_SPECULAR_PROBE_FACE_COUNT 6\n"
		"#define MODERN_SPECULAR_PROBE_ATLAS_SLOTS 8\n"
		"struct ModernClusterLightRecord {\n"
		"    vec4 positionRadius;\n"
		"    vec4 worldOriginRadius;\n"
		"    vec4 colorType;\n"
		"    vec4 scissorDepth;\n"
		"    vec4 flags;\n"
		"    vec4 depthRange;\n"
		"    vec4 falloff;\n"
		"    vec4 projectS;\n"
		"    vec4 projectT;\n"
		"    vec4 projectQ;\n"
		"    vec4 falloffRect;\n"
		"    vec4 projectionRect;\n"
		"};\n"
		"struct ModernClusterShadowDescriptor {\n"
		"    vec4 identity;\n"
		"    vec4 policy;\n"
		"    vec4 layoutInfo;\n"
		"    vec4 counts;\n"
		"    vec4 freshness;\n"
		"    mat4 shadowMatrix[4];\n"
		"    vec4 cascadeSplitDepths;\n"
		"    vec4 cascadeBiasScale;\n"
		"    vec4 texelDepthBias;\n"
		"    vec4 worldTexelSize;\n"
		"    vec4 bias;\n"
		"    vec4 projection;\n"
		"    vec4 projectedAtlasRect[4];\n"
		"};\n"
		"struct ModernSpecularProbeRecord {\n"
		"    vec4 positionRadius;\n"
		"    vec4 tintIntensity;\n"
		"    vec4 axisXPriority;\n"
		"    vec4 axisYBlend;\n"
		"    vec4 axisZSlot;\n"
		"    vec4 identity;\n"
		"};\n"
		"layout(std140) uniform ModernClusterGridParams {\n"
		"    vec4 grid;\n"
		"    vec4 depth;\n"
		"    vec4 viewport;\n"
		"    vec4 counts;\n"
		"    vec4 viewToWorldX;\n"
		"    vec4 viewToWorldY;\n"
		"    vec4 viewToWorldZ;\n"
		"    vec4 projection;\n"
		"    vec4 projectionDepth;\n"
		"} uClusterGrid;\n"
		"layout(std140) uniform ModernSpecularProbeRecords {\n"
		"    ModernSpecularProbeRecord probes[MODERN_SPECULAR_PROBE_MAX_RECORDS];\n"
		"} uModernSpecularProbes;\n"
		"uniform sampler2D uModernLightImageAtlas;\n"
		"uniform sampler2D uModernSpecularProbeAtlas;\n"
		"uniform sampler2D uModernShadowAtlas;\n"
		"uniform samplerCube uModernPointShadowAtlas;\n"
		"uniform sampler2D uModernTranslucentShadowMoments[3];\n"
		"uniform samplerCube uModernPointTranslucentShadowMoments[3];\n"
		"uniform vec4 uModernShadowResourceState;\n"
		"uniform vec4 uModernShadowSamplerState;\n"
		"uniform vec4 uModernShadowMomentState;\n"
		"uniform vec4 uModernShadowContractState;\n"
		"// x=enabled, y=intensity. This is deliberately global rather than\n"
		"// material data: it is a stable lighting environment shared by every\n"
		"// explicitly admitted PBR record in the current view.\n"
		"uniform vec4 uPBRIBL;\n"
		"#if MODERN_HAS_SHADER_STORAGE\n"
		"layout(std430, binding = 6) readonly buffer ModernLightRecords {\n"
		"    ModernClusterLightRecord lights[];\n"
		"} uClusterLightsSSBO;\n"
		"layout(std430, binding = 7) readonly buffer ModernClusterIndexRecordsSSBO {\n"
		"    uvec4 indices[];\n"
		"} uClusterIndicesSSBO;\n"
		"layout(std430, binding = 8) readonly buffer ModernClusterShadowDescriptorsSSBO {\n"
		"    ModernClusterShadowDescriptor descriptors[];\n"
		"} uClusterShadowDescriptorsSSBO;\n"
		"#else\n"
		"layout(std140) uniform ModernClusterLightRecords {\n"
		"    ModernClusterLightRecord lights[MODERN_CLUSTER_UBO_MAX_LIGHTS];\n"
		"} uClusterLights;\n"
		"layout(std140) uniform ModernClusterIndexRecords {\n"
		"    uvec4 indices[MODERN_CLUSTER_UBO_MAX_INDEX_RECORDS];\n"
		"} uClusterIndices;\n"
		"layout(std140) uniform ModernClusterShadowDescriptors {\n"
		"    ModernClusterShadowDescriptor descriptors[MODERN_CLUSTER_UBO_MAX_SHADOW_DESCRIPTORS];\n"
		"} uClusterShadowDescriptors;\n"
		"#endif\n"
		"ModernClusterLightRecord ModernClusterEmptyLight() {\n"
		"    ModernClusterLightRecord light;\n"
		"    light.positionRadius = vec4(0.0);\n"
		"    light.worldOriginRadius = vec4(0.0);\n"
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
		"ModernClusterShadowDescriptor ModernClusterEmptyShadowDescriptor() {\n"
		"    ModernClusterShadowDescriptor descriptor;\n"
		"    descriptor.identity = vec4(-1.0, 0.0, -1.0, 0.0);\n"
		"    descriptor.policy = vec4(0.0);\n"
		"    descriptor.layoutInfo = vec4(0.0);\n"
		"    descriptor.counts = vec4(0.0);\n"
		"    descriptor.freshness = vec4(-1.0, 0.0, -1.0, 0.0);\n"
		"    for (int i = 0; i < 4; ++i) { descriptor.shadowMatrix[i] = mat4(1.0); }\n"
		"    descriptor.cascadeSplitDepths = vec4(0.0);\n"
		"    descriptor.cascadeBiasScale = vec4(0.0);\n"
		"    descriptor.texelDepthBias = vec4(0.0);\n"
		"    descriptor.worldTexelSize = vec4(0.0);\n"
		"    descriptor.bias = vec4(0.0);\n"
		"    descriptor.projection = vec4(0.0);\n"
		"    for (int i = 0; i < 4; ++i) { descriptor.projectedAtlasRect[i] = vec4(0.0); }\n"
		"    return descriptor;\n"
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
		"uint ModernClusterShadowDescriptorCount() {\n"
		"    return uint(max(uClusterGrid.counts.w, 0.0));\n"
		"}\n"
		"ModernClusterShadowDescriptor ModernClusterFetchShadowDescriptor(float descriptorIndexValue) {\n"
		"    int descriptorIndex = int(floor(descriptorIndexValue + 0.5));\n"
		"    uint descriptorCount = ModernClusterShadowDescriptorCount();\n"
		"    if (descriptorIndex < 0 || descriptorCount == 0u || uint(descriptorIndex) >= descriptorCount) { return ModernClusterEmptyShadowDescriptor(); }\n"
		"#if MODERN_HAS_SHADER_STORAGE\n"
		"    if (descriptorIndex >= uClusterShadowDescriptorsSSBO.descriptors.length()) { return ModernClusterEmptyShadowDescriptor(); }\n"
		"    return uClusterShadowDescriptorsSSBO.descriptors[descriptorIndex];\n"
		"#else\n"
		"    if (descriptorIndex >= MODERN_CLUSTER_UBO_MAX_SHADOW_DESCRIPTORS) { return ModernClusterEmptyShadowDescriptor(); }\n"
		"    return uClusterShadowDescriptors.descriptors[descriptorIndex];\n"
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
		"vec3 ModernClusterFromEyeSpace(vec3 eyeVector) {\n"
		"    // GL eye space (x=right, z=negative-forward) to the cluster basis\n"
		"    // (x=left, z=forward-positive): a proper rotation, so it applies\n"
		"    // to positions, normals, and directions alike\n"
		"    return vec3(-eyeVector.x, eyeVector.y, -eyeVector.z);\n"
		"}\n"
		"float ModernClusterProjectionDepth(float rawDepth) {\n"
		"    // inverts the ACTUAL scene depth mapping (id's infinite-far\n"
		"    // crunched projection): z_ndc = -m22 - m32/z_eye with w = -z_eye,\n"
		"    // so forward depth = m32 / (m22 + z_ndc). ModernClusterLinearDepth\n"
		"    // assumes a finite-far frustum and underestimates badly at range.\n"
		"    float zNdc = rawDepth * 2.0 - 1.0;\n"
		"    float denom = uClusterGrid.projectionDepth.x + zNdc;\n"
		"    float n = max(uClusterGrid.depth.x, 0.01);\n"
		"    float f = max(uClusterGrid.depth.y, n + 1.0);\n"
		"    if (abs(denom) < 0.000001) { return f; }\n"
		"    return clamp(uClusterGrid.projectionDepth.y / denom, n, f);\n"
		"}\n"
		"vec3 ModernClusterViewPositionFromDepth(vec2 uv, float rawDepth) {\n"
		"    // real inverse projection: with w_clip = zLinear, GL gives\n"
		"    // x_eye = (ndc.x + proj[2][0]) * z / proj[0][0]; the cluster basis\n"
		"    // then flips x (left-positive) and keeps z forward-positive\n"
		"    float z = ModernClusterProjectionDepth(rawDepth);\n"
		"    vec2 ndc = uv * 2.0 - 1.0;\n"
		"    float invProj00 = abs(uClusterGrid.projection.x) > 0.0001 ? 1.0 / uClusterGrid.projection.x : 1.0;\n"
		"    float invProj11 = abs(uClusterGrid.projection.y) > 0.0001 ? 1.0 / uClusterGrid.projection.y : 1.0;\n"
		"    float xEye = (ndc.x + uClusterGrid.projection.z) * z * invProj00;\n"
		"    float yEye = (ndc.y + uClusterGrid.projection.w) * z * invProj11;\n"
		"    return vec3(-xEye, yEye, z);\n"
		"}\n"
		"float ModernClusterProjectedMask(ModernClusterLightRecord light, vec3 viewPosition) {\n"
		"    vec4 p = vec4(viewPosition, 1.0);\n"
		"    float q = dot(light.projectQ, p);\n"
		"    if (abs(q) < 0.0001) { return 0.0; }\n"
		"    vec2 uv = vec2(dot(light.projectS, p), dot(light.projectT, p)) / q;\n"
		"    vec2 inside = step(vec2(0.0), uv) * step(uv, vec2(1.0));\n"
		"    return inside.x * inside.y;\n"
		"}\n"
		"bool ModernClusterAtlasRectValid(vec4 rect) {\n"
		"    return rect.z > 0.0 && rect.w > 0.0;\n"
		"}\n"
		"vec3 ModernClusterSampleLightAtlas(vec4 rect, vec2 uv) {\n"
		"    vec2 cellUV = clamp(uv, vec2(0.0), vec2(1.0));\n"
		"    return texture(uModernLightImageAtlas, rect.xy + cellUV * rect.zw).rgb;\n"
		"}\n"
		"vec3 ModernClusterProjectionColor(ModernClusterLightRecord light, vec3 viewPosition) {\n"
		"    vec4 p = vec4(viewPosition, 1.0);\n"
		"    float q = dot(light.projectQ, p);\n"
		"    if (abs(q) < 0.0001) { return vec3(0.0); }\n"
		"    vec2 uv = vec2(dot(light.projectS, p), dot(light.projectT, p)) / q;\n"
		"    vec2 inside = step(vec2(0.0), uv) * step(uv, vec2(1.0));\n"
		"    float inFrustum = inside.x * inside.y;\n"
		"    if (!ModernClusterAtlasRectValid(light.projectionRect)) { return vec3(inFrustum); }\n"
		"    return ModernClusterSampleLightAtlas(light.projectionRect, uv) * inFrustum;\n"
		"}\n"
		"vec3 ModernClusterFalloffColor(ModernClusterLightRecord light, vec3 viewPosition, float radialFallback) {\n"
		"    if (!ModernClusterAtlasRectValid(light.falloffRect)) { return vec3(radialFallback); }\n"
		"    float s = dot(light.falloff, vec4(viewPosition, 1.0));\n"
		"    return ModernClusterSampleLightAtlas(light.falloffRect, vec2(clamp(s, 0.0, 1.0), 0.5));\n"
		"}\n"
		"float ModernClusterFogDensity(ModernClusterLightRecord light, vec3 viewPosition) {\n"
		"    // Quake 4 drives fog from distance along the light's own projection\n"
		"    // axis, not from a radial distance to the light origin.\n"
		"    vec4 p = vec4(viewPosition, 1.0);\n"
		"    float q = dot(light.projectQ, p);\n"
		"    if (abs(q) < 0.0001) { return 0.0; }\n"
		"    float axis = clamp(dot(light.projectS, p) / q, 0.0, 1.0);\n"
		"    if (!ModernClusterAtlasRectValid(light.falloffRect)) { return axis; }\n"
		"    return clamp(ModernClusterSampleLightAtlas(light.falloffRect, vec2(axis, 0.5)).r, 0.0, 1.0);\n"
		"}\n"
		"vec3 ModernClusterApplyFogAndBlend(vec3 color, uvec4 clusterRange, int clusterLightCount, vec3 viewPosition) {\n"
		"    for (int i = 0; i < clusterLightCount; ++i) {\n"
		"        uint lightIndex = ModernClusterFetchLightIndex(clusterRange.x + uint(i));\n"
		"        if (lightIndex == 0xffffffffu || lightIndex >= uint(max(uClusterGrid.counts.x, 0.0))) { continue; }\n"
		"        ModernClusterLightRecord light = ModernClusterFetchLight(lightIndex);\n"
		"        int type = int(floor(light.colorType.w + 0.5));\n"
		"        if (type == 2) {\n"
		"            // fog: mix toward the fog colour by density, never additive\n"
		"            float density = ModernClusterFogDensity(light, viewPosition);\n"
		"            color = mix(color, light.colorType.rgb, density);\n"
		"        } else if (type == 4) {\n"
		"            // blend light: modulates what is already there\n"
		"            vec3 modulate = light.colorType.rgb;\n"
		"            if (ModernClusterAtlasRectValid(light.projectionRect)) {\n"
		"                modulate *= ModernClusterProjectionColor(light, viewPosition);\n"
		"            }\n"
		"            color *= clamp(modulate, vec3(0.0), vec3(1.0));\n"
		"        }\n"
		"    }\n"
		"    return color;\n"
		"}\n"
		"vec3 ModernClusterEvaluateLight(ModernClusterLightRecord light, vec3 viewPosition, vec3 normal, float specular, float fresnelStrength, out float attenuation) {\n"
		"    int type = int(floor(light.colorType.w + 0.5));\n"
		"    bool projected = type == 1;\n"
		"    bool point = type == 0;\n"
		"    bool ambient = type == 3;\n"
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
		"    float specBase = clamp(dot(normal, halfDir) * 4.0 - 3.0, 0.0, 1.0);\n"
		"    float spec = specBase * specBase * 2.0 * specular * fresnel;\n"
		"    vec3 projection = projected ? ModernClusterProjectionColor(light, viewPosition) : vec3(1.0);\n"
		"    vec3 shaped = projection * ModernClusterFalloffColor(light, viewPosition, radial);\n"
		"    float supported = (point || projected || ambient) ? 1.0 : 0.0;\n"
		"    float shading = ambient ? 1.0 : (ndotl + spec);\n"
		"    attenuation = supported * max(max(shaped.r, shaped.g), shaped.b) * shading;\n"
		"    return light.colorType.rgb * shaped * shading * supported;\n"
		"}\n"
		"vec3 ModernClusterEvaluatePBRLight(ModernClusterLightRecord light, vec3 viewPosition, vec3 normal, vec3 baseColor, float metallic, float roughness, out float attenuation) {\n"
		"    int type = int(floor(light.colorType.w + 0.5));\n"
		"    bool projected = type == 1; bool point = type == 0; bool ambient = type == 3;\n"
		"    vec3 toLight = light.positionRadius.xyz - viewPosition; float dist = length(toLight);\n"
		"    vec3 lightDir = dist > 0.0001 ? toLight / dist : vec3(0.0, 0.0, 1.0);\n"
		"    float radius = max(light.positionRadius.w, 1.0); float radial = clamp(1.0 - dist / radius, 0.0, 1.0); radial *= radial;\n"
		"    vec3 projection = projected ? ModernClusterProjectionColor(light, viewPosition) : vec3(1.0);\n"
		"    vec3 radiance = light.colorType.rgb * projection * ModernClusterFalloffColor(light, viewPosition, radial);\n"
		"    if (ambient) { attenuation = max(max(radiance.r, radiance.g), radiance.b); return radiance * baseColor; }\n"
		"    if (!(point || projected)) { attenuation = 0.0; return vec3(0.0); }\n"
		"    vec3 viewDir = length(viewPosition) > 0.0001 ? normalize(-viewPosition) : vec3(0.0, 0.0, -1.0); vec3 halfDir = normalize(lightDir + viewDir);\n"
		"    float ndotl = max(dot(normal, lightDir), 0.0); float ndotv = max(dot(normal, viewDir), 0.0);\n"
		"    float ndoth = max(dot(normal, halfDir), 0.0); float vdoth = max(dot(viewDir, halfDir), 0.0);\n"
		"    float alpha = max(roughness * roughness, 0.0004); float alpha2 = alpha * alpha;\n"
		"    float denom = max(3.14159265 * pow(max(ndoth * ndoth * (alpha2 - 1.0) + 1.0, 0.0001), 2.0), 0.0001); float distribution = alpha2 / denom;\n"
		"    float k = (roughness + 1.0); k = k * k * 0.125; float visibility = 1.0 / max((ndotv * (1.0 - k) + k) * (ndotl * (1.0 - k) + k), 0.0001);\n"
		"    vec3 f0 = mix(vec3(0.04), baseColor, metallic); vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - vdoth, 5.0);\n"
		"    vec3 specular = distribution * visibility * fresnel; vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * baseColor * (1.0 / 3.14159265);\n"
		"    attenuation = max(max(radiance.r, radiance.g), radiance.b) * ndotl; return (diffuse + specular) * radiance * ndotl;\n"
		"}\n"
		"vec3 ModernPBRAnalyticEnvironment(vec3 direction) {\n"
		"    // A neutral, analytic studio hemisphere provides stable PBR indirect\n"
		"    // light without requiring replacement cubemaps or a probe format.\n"
		"    float up = smoothstep(-0.55, 0.70, normalize(direction).y);\n"
		"    vec3 ground = vec3(0.025, 0.022, 0.020);\n"
		"    vec3 sky = vec3(0.46, 0.53, 0.68);\n"
		"    vec3 environment = mix(ground, sky, up);\n"
		"    vec3 keyDirection = normalize(vec3(-0.35, 0.62, 0.70));\n"
		"    environment += vec3(1.30, 1.16, 0.96) * pow(max(dot(normalize(direction), keyDirection), 0.0), 48.0);\n"
		"    // The current modern corridor is scene-referred and has no mandatory\n"
		"    // tonemap handoff, so retain a calibrated HDR range here rather than\n"
		"    // making default PBR metals disappear into the legacy black floor.\n"
		"    return environment * 4.0;\n"
		"}\n"
		"bool ModernSpecularProbeExactInteger(float value, float minimumValue, float maximumValue) {\n"
		"    return !isnan(value) && !isinf(value) && value >= minimumValue && value <= maximumValue && value == floor(value);\n"
		"}\n"
		"bool ModernSpecularProbeFiniteVec3(vec3 value) {\n"
		"    return !any(isnan(value)) && !any(isinf(value));\n"
		"}\n"
		"bool ModernSpecularProbeAtlasReady(void) {\n"
		"    return all(equal(textureSize(uModernSpecularProbeAtlas, 0), ivec2(MODERN_SPECULAR_PROBE_ATLAS_SIZE)));\n"
		"}\n"
		"bool ModernSpecularProbeCount(out uint probeCount) {\n"
		"    probeCount = 0u;\n"
		"    float countValue = uClusterGrid.projectionDepth.z;\n"
		"    if (!ModernSpecularProbeExactInteger(countValue, 0.0, float(MODERN_SPECULAR_PROBE_MAX_RECORDS))) { return false; }\n"
		"    probeCount = uint(countValue);\n"
		"    return probeCount > 0u;\n"
		"}\n"
		"bool ModernSpecularProbeRecordReady(ModernSpecularProbeRecord probe) {\n"
		"    float frameGeneration = uClusterGrid.projectionDepth.w;\n"
		"    if (!ModernSpecularProbeExactInteger(frameGeneration, 1.0, 16777215.0)) { return false; }\n"
		"    if (!ModernSpecularProbeExactInteger(probe.identity.x, 0.0, 16777215.0)\n"
		"            || !ModernSpecularProbeExactInteger(probe.identity.y, 1.0, 16777215.0)\n"
		"            || !ModernSpecularProbeExactInteger(probe.identity.z, 1.0, 16777215.0)\n"
		"            || !ModernSpecularProbeExactInteger(probe.identity.w, 1.0, 16777215.0)\n"
		"            || probe.identity.w != frameGeneration) { return false; }\n"
		"    if (!ModernSpecularProbeFiniteVec3(probe.positionRadius.xyz) || isnan(probe.positionRadius.w) || isinf(probe.positionRadius.w) || probe.positionRadius.w <= 0.0) { return false; }\n"
		"    if (!ModernSpecularProbeFiniteVec3(probe.tintIntensity.rgb) || any(lessThan(probe.tintIntensity.rgb, vec3(0.0)))\n"
		"            || any(greaterThan(probe.tintIntensity.rgb, vec3(64.0)))\n"
		"            || isnan(probe.tintIntensity.w) || isinf(probe.tintIntensity.w)\n"
		"            || probe.tintIntensity.w <= 0.0 || probe.tintIntensity.w > 64.0) { return false; }\n"
		"    if (!ModernSpecularProbeFiniteVec3(probe.axisXPriority.xyz)\n"
		"            || !ModernSpecularProbeFiniteVec3(probe.axisYBlend.xyz)\n"
		"            || !ModernSpecularProbeFiniteVec3(probe.axisZSlot.xyz)) { return false; }\n"
		"    if (!ModernSpecularProbeExactInteger(probe.axisXPriority.w, 0.0, 255.0)\n"
		"            || isnan(probe.axisYBlend.w) || isinf(probe.axisYBlend.w)\n"
		"            || probe.axisYBlend.w <= 0.0 || probe.axisYBlend.w > 1.0\n"
		"            || !ModernSpecularProbeExactInteger(probe.axisZSlot.w, 0.0, float(MODERN_SPECULAR_PROBE_ATLAS_SLOTS - 1))) { return false; }\n"
		"    float axisXLength2 = dot(probe.axisXPriority.xyz, probe.axisXPriority.xyz);\n"
		"    float axisYLength2 = dot(probe.axisYBlend.xyz, probe.axisYBlend.xyz);\n"
		"    float axisZLength2 = dot(probe.axisZSlot.xyz, probe.axisZSlot.xyz);\n"
		"    if (axisXLength2 <= 0.000001 || axisYLength2 <= 0.000001 || axisZLength2 <= 0.000001) { return false; }\n"
		"    vec3 axisX = probe.axisXPriority.xyz * inversesqrt(axisXLength2);\n"
		"    vec3 axisY = probe.axisYBlend.xyz * inversesqrt(axisYLength2);\n"
		"    vec3 axisZ = probe.axisZSlot.xyz * inversesqrt(axisZLength2);\n"
		"    float determinant = dot(cross(axisX, axisY), axisZ);\n"
		"    return abs(dot(axisX, axisY)) < 0.01 && abs(dot(axisX, axisZ)) < 0.01\n"
		"        && abs(dot(axisY, axisZ)) < 0.01 && abs(determinant) > 0.99;\n"
		"}\n"
		"vec3 ModernSpecularProbeSampleAtlas(int slot, vec3 direction) {\n"
		"    vec3 d = normalize(direction);\n"
		"    vec3 ad = abs(d);\n"
		"    int face = 0;\n"
		"    vec2 faceCoordinate = vec2(0.0);\n"
		"    float majorAxis = 1.0;\n"
		"    if (ad.x >= ad.y && ad.x >= ad.z) {\n"
		"        majorAxis = ad.x;\n"
		"        if (d.x >= 0.0) { face = 0; faceCoordinate = vec2(-d.z, -d.y); }\n"
		"        else { face = 1; faceCoordinate = vec2(d.z, -d.y); }\n"
		"    } else if (ad.y >= ad.z) {\n"
		"        majorAxis = ad.y;\n"
		"        if (d.y >= 0.0) { face = 2; faceCoordinate = vec2(d.x, d.z); }\n"
		"        else { face = 3; faceCoordinate = vec2(d.x, -d.z); }\n"
		"    } else {\n"
		"        majorAxis = ad.z;\n"
		"        if (d.z >= 0.0) { face = 4; faceCoordinate = vec2(d.x, -d.y); }\n"
		"        else { face = 5; faceCoordinate = vec2(-d.x, -d.y); }\n"
		"    }\n"
		"    vec2 faceUV = clamp(faceCoordinate / max(majorAxis, 0.000001) * 0.5 + 0.5, vec2(0.0), vec2(1.0));\n"
		"    int cell = slot * MODERN_SPECULAR_PROBE_FACE_COUNT + face;\n"
		"    ivec2 cellIndex = ivec2(cell % MODERN_SPECULAR_PROBE_CELLS_PER_ROW, cell / MODERN_SPECULAR_PROBE_CELLS_PER_ROW);\n"
		"    vec2 atlasTexel = vec2(cellIndex * MODERN_SPECULAR_PROBE_FACE_SIZE) + vec2(0.5)\n"
		"        + faceUV * float(MODERN_SPECULAR_PROBE_FACE_SIZE - 1);\n"
		"    vec2 atlasUV = atlasTexel / float(MODERN_SPECULAR_PROBE_ATLAS_SIZE);\n"
		"    return pow(max(textureLod(uModernSpecularProbeAtlas, atlasUV, 0.0).rgb, vec3(0.0)), vec3(2.2));\n"
		"}\n"
		"bool ModernSpecularProbeContribution(uint probeIndex, uint probeCount, vec3 viewPosition, vec3 reflectionDirection, out vec3 radiance, out float weight) {\n"
		"    radiance = vec3(0.0); weight = 0.0;\n"
		"    if (probeIndex == 0xffffffffu || probeIndex >= probeCount || probeIndex >= uint(MODERN_SPECULAR_PROBE_MAX_RECORDS)) { return false; }\n"
		"    ModernSpecularProbeRecord probe = uModernSpecularProbes.probes[int(probeIndex)];\n"
		"    if (!ModernSpecularProbeRecordReady(probe)) { return false; }\n"
		"    float distanceToProbe = length(viewPosition - probe.positionRadius.xyz);\n"
		"    float radius = probe.positionRadius.w;\n"
		"    if (isnan(distanceToProbe) || isinf(distanceToProbe) || distanceToProbe > radius) { return false; }\n"
		"    float blendWidth = max(radius * probe.axisYBlend.w, 0.000001);\n"
		"    weight = clamp((radius - distanceToProbe) / blendWidth, 0.0, 1.0);\n"
		"    if (weight <= 0.0) { return false; }\n"
		"    vec3 localDirection = vec3(\n"
		"        dot(reflectionDirection, normalize(probe.axisXPriority.xyz)),\n"
		"        dot(reflectionDirection, normalize(probe.axisYBlend.xyz)),\n"
		"        dot(reflectionDirection, normalize(probe.axisZSlot.xyz)));\n"
		"    if (dot(localDirection, localDirection) <= 0.000001) { return false; }\n"
		"    int slot = int(probe.axisZSlot.w);\n"
		"    radiance = ModernSpecularProbeSampleAtlas(slot, localDirection) * probe.tintIntensity.rgb * probe.tintIntensity.w;\n"
		"    return true;\n"
		"}\n"
		"bool ModernSpecularProbeEnvironment(uvec4 clusterRange, vec3 viewPosition, vec3 reflectionDirection, out vec3 environment, out float coverage) {\n"
		"    environment = vec3(0.0); coverage = 0.0;\n"
		"    uint probeCount = 0u;\n"
		"    if (!ModernSpecularProbeAtlasReady() || !ModernSpecularProbeCount(probeCount)) { return false; }\n"
		"    float totalWeight = 0.0;\n"
		"    for (int candidate = 0; candidate < 2; ++candidate) {\n"
		"        uint probeIndex = candidate == 0 ? clusterRange.z : clusterRange.w;\n"
		"        if (candidate == 1 && probeIndex == clusterRange.z) { continue; }\n"
		"        vec3 radiance = vec3(0.0); float weight = 0.0;\n"
		"        if (!ModernSpecularProbeContribution(probeIndex, probeCount, viewPosition, reflectionDirection, radiance, weight)) { continue; }\n"
		"        environment += radiance * weight; totalWeight += weight;\n"
		"    }\n"
		"    if (totalWeight <= 0.000001) { environment = vec3(0.0); return false; }\n"
		"    environment /= totalWeight;\n"
		"    coverage = clamp(totalWeight, 0.0, 1.0);\n"
		"    return true;\n"
		"}\n"
		"vec3 ModernPBRIndirect(vec3 viewPosition, vec3 normal, vec3 baseColor, float metallic, float roughness, float ao, uvec4 clusterRange) {\n"
		"    vec3 n = normalize(normal);\n"
		"    vec3 viewDir = length(viewPosition) > 0.0001 ? normalize(-viewPosition) : vec3(0.0, 0.0, -1.0);\n"
		"    float ndotv = clamp(dot(n, viewDir), 0.0, 1.0);\n"
		"    vec3 f0 = mix(vec3(0.04), baseColor, metallic);\n"
		"    vec3 fresnel = f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - ndotv, 5.0);\n"
		"    vec3 legacyIndirect = baseColor * vec3(0.12) * ao * (1.0 - metallic);\n"
		"    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * baseColor * ModernPBRAnalyticEnvironment(n);\n"
		"    vec3 reflection = reflect(-viewDir, n);\n"
		"    vec3 analyticPrefiltered = ModernPBRAnalyticEnvironment(normalize(mix(reflection, n, roughness * roughness)));\n"
		"    vec3 prefiltered = analyticPrefiltered;\n"
		"    vec3 probeEnvironment = vec3(0.0); float probeCoverage = 0.0;\n"
		"    if (ModernSpecularProbeEnvironment(clusterRange, viewPosition, reflection, probeEnvironment, probeCoverage)) {\n"
		"        vec3 analyticSharp = ModernPBRAnalyticEnvironment(normalize(reflection));\n"
		"        vec3 edgeBlendedProbe = mix(analyticSharp, probeEnvironment, probeCoverage);\n"
		"        prefiltered = mix(edgeBlendedProbe, analyticPrefiltered, clamp(roughness, 0.0, 1.0));\n"
		"    }\n"
		"    vec3 specular = prefiltered * fresnel * mix(1.0, 0.28, roughness);\n"
		"    vec3 analyticIndirect = (diffuse + specular) * ao * max(uPBRIBL.y, 0.0);\n"
		"    return mix(legacyIndirect, analyticIndirect, clamp(uPBRIBL.x, 0.0, 1.0));\n"
		"}\n";
	const char *shadowPolicyHeader =
		"#define MODERN_SHADOW_MAP_PROJECTED 1.0\n"
		"#define MODERN_SHADOW_MAP_POINT 2.0\n"
		"#define MODERN_SHADOW_MAP_CASCADE 3.0\n"
		"#define MODERN_SHADOW_COMPARE_NONE 0.0\n"
		"#define MODERN_SHADOW_COMPARE_MANUAL_DEPTH 1.0\n"
		"#define MODERN_SHADOW_COMPARE_MANUAL_PACKED_DEPTH 2.0\n"
		"#define MODERN_SHADOW_COMPARE_HARDWARE 3.0\n"
		"#define MODERN_SHADOW_POLICY_MAPPED 1.0\n"
		"#define MODERN_SHADOW_POLICY_CACHE_REUSE 2.0\n"
		"#define MODERN_SHADOW_POLICY_STENCIL_FALLBACK 3.0\n"
		"#define MODERN_SHADOW_POLICY_SKIPPED 4.0\n"
		"#define MODERN_SHADOW_FLAG_RECEIVER_BLOCKED 16\n"
		"#define MODERN_SHADOW_FLAG_TRANSLUCENT 256\n"
		"#define MODERN_SHADOW_FLAG_ATLAS_READY 512\n"
		"#define MODERN_SHADOW_FLAG_SAMPLING_READY 4096\n"
		"#define MODERN_SHADOW_FLAG_PROJECTED_STATE_READY 16384\n"
		"#define MODERN_SHADOW_FLAG_RECEIVER_PLANE_BIAS 65536\n"
		"#define MODERN_SHADOW_FLAG_ATLAS_SLOT 131072\n"
		"#define MODERN_SHADOW_BIAS_MIN_LIGHT_COS 0.20\n"
		"#define MODERN_SHADOW_BIAS_MAX_SLOPE 4.0\n"
		"vec4 ModernClusterShadowResourceProbe(void) {\n"
		"    vec4 state = uModernShadowResourceState;\n"
		"    state += vec4(uModernShadowSamplerState.xyz, 0.0) * 0.000001;\n"
		"    state += uModernShadowMomentState * 0.000001;\n"
		"    float projectedAtlas = texture(uModernShadowAtlas, vec2(0.5, 0.5)).r;\n"
		"    float pointAtlas = texture(uModernPointShadowAtlas, vec3(1.0, 0.0, 0.0)).r;\n"
		"    float projectedMoments = texture(uModernTranslucentShadowMoments[0], vec2(0.5, 0.5)).r + texture(uModernTranslucentShadowMoments[1], vec2(0.5, 0.5)).r + texture(uModernTranslucentShadowMoments[2], vec2(0.5, 0.5)).r;\n"
		"    float pointMoments = texture(uModernPointTranslucentShadowMoments[0], vec3(1.0, 0.0, 0.0)).r + texture(uModernPointTranslucentShadowMoments[1], vec3(1.0, 0.0, 0.0)).r + texture(uModernPointTranslucentShadowMoments[2], vec3(1.0, 0.0, 0.0)).r;\n"
		"    state += vec4(projectedAtlas, pointAtlas, projectedMoments, pointMoments) * 0.000001;\n"
		"    return state;\n"
		"}\n"
		"bool ModernClusterShadowFlag(ModernClusterShadowDescriptor descriptor, int flag) {\n"
		"    int flags = int(floor(descriptor.policy.z + 0.5));\n"
		"    return (flags & flag) != 0;\n"
		"}\n"
		"float ModernClusterShadowComponent(vec4 value, int index) {\n"
		"    if (index <= 0) { return value.x; }\n"
		"    if (index == 1) { return value.y; }\n"
		"    if (index == 2) { return value.z; }\n"
		"    return value.w;\n"
		"}\n"
		"vec4 ModernClusterShadowAtlasRect(ModernClusterShadowDescriptor descriptor, int index) {\n"
		"    if (index <= 0) { return descriptor.projectedAtlasRect[0]; }\n"
		"    if (index == 1) { return descriptor.projectedAtlasRect[1]; }\n"
		"    if (index == 2) { return descriptor.projectedAtlasRect[2]; }\n"
		"    return descriptor.projectedAtlasRect[3]; }\n"
		"mat4 ModernClusterShadowMatrix(ModernClusterShadowDescriptor descriptor, int index) {\n"
		"    if (index <= 0) { return descriptor.shadowMatrix[0]; }\n"
		"    if (index == 1) { return descriptor.shadowMatrix[1]; }\n"
		"    if (index == 2) { return descriptor.shadowMatrix[2]; }\n"
		"    return descriptor.shadowMatrix[3]; }\n"
		"vec3 gModernShadowPositionDx;\n"
		"vec3 gModernShadowPositionDy;\n"
		"void ModernClusterPrepareShadowDerivatives(vec3 viewPosition) {\n"
		"    // Evaluate before alpha discard and divergent cluster/light/cascade selection.\n"
		"    gModernShadowPositionDx = dFdx(viewPosition);\n"
		"    gModernShadowPositionDy = dFdy(viewPosition);\n"
		"}\n"
		"float ModernClusterShadowReceiverBias(ModernClusterShadowDescriptor descriptor, int cascadeIndex, vec3 normal, vec3 lightDir, float depth) {\n"
		"    float lightCos = clamp(dot(normalize(normal), normalize(lightDir)), MODERN_SHADOW_BIAS_MIN_LIGHT_COS, 1.0);\n"
		"    float sinTheta = sqrt(max(1.0 - lightCos * lightCos, 0.0));\n"
		"    float slopeBias = min(sinTheta / lightCos, MODERN_SHADOW_BIAS_MAX_SLOPE);\n"
		"    float cascadeScale = max(ModernClusterShadowComponent(descriptor.cascadeBiasScale, cascadeIndex), 0.0);\n"
		"    float scalarBias = (max(descriptor.bias.x, 0.0) + max(descriptor.bias.y, 0.0) * sinTheta) * cascadeScale;\n"
		"    float texelBias = max(ModernClusterShadowComponent(descriptor.texelDepthBias, cascadeIndex), 0.0) * (1.0 + slopeBias);\n"
		"    float receiverPlaneScale = ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_RECEIVER_PLANE_BIAS) ? 1.0 : 0.0;\n"
		"    mat4 shadowMatrix = ModernClusterShadowMatrix(descriptor, cascadeIndex);\n"
		"    float depthGradient = abs((shadowMatrix * vec4(gModernShadowPositionDx, 0.0)).z) + abs((shadowMatrix * vec4(gModernShadowPositionDy, 0.0)).z);\n"
		"    float receiverPlaneBias = receiverPlaneScale * depthGradient * max(descriptor.layoutInfo.z, 1.0);\n"
		"    return max(max(scalarBias, texelBias), receiverPlaneBias);\n"
		"}\n"
		"float ModernClusterCompareProjected(ModernClusterShadowDescriptor descriptor, vec2 uv, float depth, int cascadeIndex, vec3 normal, vec3 lightDir) {\n"
		"    float compareMode = floor(descriptor.policy.w + 0.5);\n"
		"    if (compareMode <= MODERN_SHADOW_COMPARE_NONE) { return 1.0; }\n"
		"    float bias = ModernClusterShadowReceiverBias(descriptor, cascadeIndex, normal, lightDir, depth);\n"
		"    float storedDepth = texture(uModernShadowAtlas, uv).r;\n"
		"    return (depth - bias <= storedDepth) ? 1.0 : 0.0;\n"
		"}\n"
		"float ModernClusterApproxErf(float x) {\n"
		"    float s = sign(x);\n"
		"    float ax = abs(x);\n"
		"    float t = 1.0 / (1.0 + 0.3275911 * ax);\n"
		"    float y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-ax * ax);\n"
		"    return s * y;\n"
		"}\n"
		"float ModernClusterNormalCdf(float x) {\n"
		"    return 0.5 * (1.0 + ModernClusterApproxErf(x * 0.70710678));\n"
		"}\n"
		"float ModernClusterResolveMoments(vec4 moments, float depth) {\n"
		"    float totalTau = max(moments.x, 0.0);\n"
		"    if (totalTau <= 0.0001) { return 1.0; }\n"
		"    float mean = moments.y / totalTau;\n"
		"    float variance = max(moments.z / totalTau - mean * mean, max(uModernShadowMomentState.z, 0.000001));\n"
		"    float sigma = sqrt(variance);\n"
		"    float fraction = clamp(ModernClusterNormalCdf((depth - mean) / max(sigma, 0.000001)), 0.0, 1.0);\n"
		"    float bleed = clamp(uModernShadowMomentState.w, 0.0, 0.95);\n"
		"    fraction = clamp((fraction - bleed) / max(1.0 - bleed, 0.0001), 0.0, 1.0);\n"
		"    return exp(-min(totalTau * fraction * max(uModernShadowMomentState.x, 0.0), 16.0));\n"
		"}\n"
		"float ModernClusterProjectedTranslucentVisibility(ModernClusterShadowDescriptor descriptor, vec2 uv, float depth) {\n"
		"    if (!ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_TRANSLUCENT) || uModernShadowResourceState.z < 0.5) { return 1.0; }\n"
		"    vec3 t = vec3(\n"
		"        ModernClusterResolveMoments(texture(uModernTranslucentShadowMoments[0], uv), depth),\n"
		"        ModernClusterResolveMoments(texture(uModernTranslucentShadowMoments[1], uv), depth),\n"
		"        ModernClusterResolveMoments(texture(uModernTranslucentShadowMoments[2], uv), depth));\n"
		"    return clamp(dot(t, vec3(0.333333)), 0.0, 1.0);\n"
		"}\n"
		"float ModernClusterSampleProjectedCascade(ModernClusterShadowDescriptor descriptor, int cascadeIndex, vec3 viewPosition, vec3 normal, vec3 lightDir) {\n"
		"    vec4 shadowCoord = ModernClusterShadowMatrix(descriptor, cascadeIndex) * vec4(viewPosition, 1.0);\n"
		"    if (shadowCoord.w != shadowCoord.w || shadowCoord.w <= 0.00001 || shadowCoord.w > 65536.0) { return 1.0; }\n"
		"    vec2 localUv = shadowCoord.xy / shadowCoord.w * 0.5 + 0.5;\n"
		"    float depth = shadowCoord.z;\n"
		"    if (localUv.x <= 0.0 || localUv.x >= 1.0 || localUv.y <= 0.0 || localUv.y >= 1.0 || depth <= 0.0 || depth >= 1.0) { return 1.0; }\n"
		"    vec4 rect = ModernClusterShadowAtlasRect(descriptor, cascadeIndex);\n"
		"    vec2 atlasMin = rect.xy;\n"
		"    vec2 atlasMax = rect.zw;\n"
		"    if (atlasMax.x <= atlasMin.x || atlasMax.y <= atlasMin.y) { return 1.0; }\n"
		"    ivec2 atlasSize = textureSize(uModernShadowAtlas, 0);\n"
		"    vec2 texel = 1.0 / max(vec2(atlasSize), vec2(1.0));\n"
		"    vec2 guard = texel * max(descriptor.layoutInfo.z + 0.75, 0.5);\n"
		"    vec2 clampMin = min(atlasMin + guard, atlasMax - guard);\n"
		"    vec2 clampMax = max(atlasMin + guard, atlasMax - guard);\n"
		"    vec2 uv = clamp(mix(atlasMin, atlasMax, localUv), clampMin, clampMax);\n"
		"    float shadow = ModernClusterCompareProjected(descriptor, uv, depth, cascadeIndex, normal, lightDir);\n"
		"    float radius = max(descriptor.layoutInfo.z, 0.0);\n"
		"    if (radius > 0.0) {\n"
		"        vec2 tap = texel * radius;\n"
		"        shadow += ModernClusterCompareProjected(descriptor, clamp(uv + vec2(-0.5, -0.5) * tap, clampMin, clampMax), depth, cascadeIndex, normal, lightDir);\n"
		"        shadow += ModernClusterCompareProjected(descriptor, clamp(uv + vec2( 0.5, -0.5) * tap, clampMin, clampMax), depth, cascadeIndex, normal, lightDir);\n"
		"        shadow += ModernClusterCompareProjected(descriptor, clamp(uv + vec2(-0.5,  0.5) * tap, clampMin, clampMax), depth, cascadeIndex, normal, lightDir);\n"
		"        shadow += ModernClusterCompareProjected(descriptor, clamp(uv + vec2( 0.5,  0.5) * tap, clampMin, clampMax), depth, cascadeIndex, normal, lightDir);\n"
		"        shadow *= 0.2;\n"
		"    }\n"
		"    return shadow * ModernClusterProjectedTranslucentVisibility(descriptor, uv, depth);\n"
		"}\n"
		"int ModernClusterSelectShadowCascade(ModernClusterShadowDescriptor descriptor, float viewDepth) {\n"
		"    int cascadeCount = int(clamp(floor(descriptor.counts.x + 0.5), 1.0, 4.0));\n"
		"    if (cascadeCount <= 1 || viewDepth < descriptor.cascadeSplitDepths.x) { return 0; }\n"
		"    if (cascadeCount <= 2 || viewDepth < descriptor.cascadeSplitDepths.y) { return 1; }\n"
		"    if (cascadeCount <= 3 || viewDepth < descriptor.cascadeSplitDepths.z) { return 2; }\n"
		"    return 3;\n"
		"}\n"
		"float ModernClusterSampleProjectedShadow(ModernClusterShadowDescriptor descriptor, ModernClusterLightRecord light, vec3 viewPosition, vec3 normal) {\n"
		"    int cascadeIndex = ModernClusterSelectShadowCascade(descriptor, max(viewPosition.z, 0.0));\n"
		"    vec3 lightDir = light.positionRadius.xyz - viewPosition;\n"
		"    float shadow = ModernClusterSampleProjectedCascade(descriptor, cascadeIndex, viewPosition, normal, lightDir);\n"
		"    int cascadeCount = int(clamp(floor(descriptor.counts.x + 0.5), 1.0, 4.0));\n"
		"    int lastInterior = cascadeCount - 2;\n"
		"    float blendAmount = clamp(uModernShadowSamplerState.w, 0.0, 0.5);\n"
		"    if (cascadeIndex > lastInterior || blendAmount <= 0.0) { return shadow; }\n"
		"    float previousSplit = cascadeIndex == 0 ? 0.0 : ModernClusterShadowComponent(descriptor.cascadeSplitDepths, cascadeIndex - 1);\n"
		"    float currentSplit = ModernClusterShadowComponent(descriptor.cascadeSplitDepths, cascadeIndex);\n"
		"    float blendWidth = max(1.0, (currentSplit - previousSplit) * blendAmount);\n"
		"    float blendStart = currentSplit - blendWidth;\n"
		"    if (viewPosition.z <= blendStart) { return shadow; }\n"
		"    float nextShadow = ModernClusterSampleProjectedCascade(descriptor, cascadeIndex + 1, viewPosition, normal, lightDir);\n"
		"    float blend = clamp((viewPosition.z - blendStart) / blendWidth, 0.0, 1.0);\n"
		"    return mix(shadow, nextShadow, blend);\n"
		"}\n"
		"vec3 ModernClusterViewVectorToWorld(vec3 viewVector) {\n"
		"    return uClusterGrid.viewToWorldX.xyz * viewVector.x + uClusterGrid.viewToWorldY.xyz * viewVector.y + uClusterGrid.viewToWorldZ.xyz * viewVector.z;\n"
		"}\n"
		"float ModernClusterDecodePointDepth(vec4 encodedDepth, ModernClusterShadowDescriptor descriptor) {\n"
		"    float compareMode = floor(descriptor.policy.w + 0.5);\n"
		"    if (uModernShadowSamplerState.z > 0.5 || compareMode == MODERN_SHADOW_COMPARE_HARDWARE) { return encodedDepth.r; }\n"
		"    return encodedDepth.r + encodedDepth.g * (1.0 / 255.0);\n"
		"}\n"
		"float ModernClusterPointReceiverBias(ModernClusterShadowDescriptor descriptor, vec3 normal, vec3 lightDir) {\n"
		"    float lightCos = clamp(dot(normalize(normal), normalize(lightDir)), MODERN_SHADOW_BIAS_MIN_LIGHT_COS, 1.0);\n"
		"    float sinTheta = sqrt(max(1.0 - lightCos * lightCos, 0.0));\n"
		"    float slopeBias = min(sinTheta / lightCos, MODERN_SHADOW_BIAS_MAX_SLOPE);\n"
		"    float texelBias = max(descriptor.texelDepthBias.x, 0.0) * (1.0 + slopeBias);\n"
		"    return max(max(descriptor.bias.x + descriptor.bias.y * sinTheta, 0.0), texelBias);\n"
		"}\n"
		"float ModernClusterComparePoint(ModernClusterShadowDescriptor descriptor, vec3 direction, float depth, vec3 normal, vec3 lightDir) {\n"
		"    float compareMode = floor(descriptor.policy.w + 0.5);\n"
		"    if (compareMode <= MODERN_SHADOW_COMPARE_NONE) { return 1.0; }\n"
		"    float bias = ModernClusterPointReceiverBias(descriptor, normal, lightDir);\n"
		"    float storedDepth = ModernClusterDecodePointDepth(texture(uModernPointShadowAtlas, direction), descriptor);\n"
		"    return (depth - bias <= storedDepth) ? 1.0 : 0.0;\n"
		"}\n"
		"float ModernClusterPointTranslucentVisibility(ModernClusterShadowDescriptor descriptor, vec3 direction, float depth) {\n"
		"    if (!ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_TRANSLUCENT) || uModernShadowResourceState.w < 0.5) { return 1.0; }\n"
		"    vec3 t = vec3(\n"
		"        ModernClusterResolveMoments(texture(uModernPointTranslucentShadowMoments[0], direction), depth),\n"
		"        ModernClusterResolveMoments(texture(uModernPointTranslucentShadowMoments[1], direction), depth),\n"
		"        ModernClusterResolveMoments(texture(uModernPointTranslucentShadowMoments[2], direction), depth));\n"
		"    return clamp(dot(t, vec3(0.333333)), 0.0, 1.0);\n"
		"}\n"
		"float ModernClusterSamplePointShadow(ModernClusterShadowDescriptor descriptor, ModernClusterLightRecord light, vec3 viewPosition, vec3 normal) {\n"
		"    float pointFar = max(descriptor.projection.x, 1.0);\n"
		"    vec3 viewVector = viewPosition - light.positionRadius.xyz;\n"
		"    float depth = length(viewVector) / pointFar;\n"
		"    if (depth <= 0.0 || depth >= 1.0) { return 1.0; }\n"
		"    vec3 direction = normalize(ModernClusterViewVectorToWorld(viewVector));\n"
		"    vec3 lightDir = -viewVector;\n"
		"    float shadow = ModernClusterComparePoint(descriptor, direction, depth, normal, lightDir);\n"
		"    float radius = max(descriptor.layoutInfo.z, 0.0);\n"
		"    if (radius > 0.0) {\n"
		"        vec3 up = abs(direction.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);\n"
		"        vec3 tangent = normalize(cross(up, direction));\n"
		"        vec3 bitangent = cross(direction, tangent);\n"
		"        ivec2 atlasSize = textureSize(uModernPointShadowAtlas, 0);\n"
		"        float texelScale = max(descriptor.projection.y, 2.0 / max(float(atlasSize.x), 1.0));\n"
		"        float tap = texelScale * radius;\n"
		"        shadow += ModernClusterComparePoint(descriptor, normalize(direction + (tangent * -0.5 + bitangent * -0.5) * tap), depth, normal, lightDir);\n"
		"        shadow += ModernClusterComparePoint(descriptor, normalize(direction + (tangent *  0.5 + bitangent * -0.5) * tap), depth, normal, lightDir);\n"
		"        shadow += ModernClusterComparePoint(descriptor, normalize(direction + (tangent * -0.5 + bitangent *  0.5) * tap), depth, normal, lightDir);\n"
		"        shadow += ModernClusterComparePoint(descriptor, normalize(direction + (tangent *  0.5 + bitangent *  0.5) * tap), depth, normal, lightDir);\n"
		"        shadow *= 0.2;\n"
		"    }\n"
		"    return shadow * ModernClusterPointTranslucentVisibility(descriptor, direction, depth);\n"
		"}\n"
		"float ModernClusterShadowAtlasReady(ModernClusterShadowDescriptor descriptor) {\n"
		"    vec4 resources = ModernClusterShadowResourceProbe();\n"
		"    float mapType = floor(descriptor.identity.w + 0.5);\n"
		"    if (mapType == MODERN_SHADOW_MAP_POINT) { return step(0.5, resources.y); }\n"
		"    if (mapType == MODERN_SHADOW_MAP_PROJECTED || mapType == MODERN_SHADOW_MAP_CASCADE) { return step(0.5, resources.x); }\n"
		"    return 0.0;\n"
		"}\n"
		"float ModernClusterShadowBrokenVisibility(void) {\n"
		"    // fail-visible (I7): a light whose CPU policy promises shadows but\n"
		"    // whose descriptor cannot deliver them is a contract break, not a\n"
		"    // benign no-shadow state. Strict mode blacks the light out on\n"
		"    // screen; lenient mode lights it, and the CPU-side per-light gate\n"
		"    // accounts for it either way.\n"
		"    return uModernShadowContractState.x > 0.5 ? 0.0 : 1.0;\n"
		"}\n"
		"float ModernClusterShadowVisibility(ModernClusterLightRecord light, vec3 viewPosition, vec3 normal) {\n"
		"    float policy = floor(light.flags.w + 0.5);\n"
		"    if (policy != MODERN_SHADOW_POLICY_MAPPED && policy != MODERN_SHADOW_POLICY_CACHE_REUSE) { return 1.0; }\n"
		"    ModernClusterShadowDescriptor descriptor = ModernClusterFetchShadowDescriptor(light.flags.z);\n"
		"    float descriptorPolicy = floor(descriptor.policy.x + 0.5);\n"
		"    float descriptorValid = step(0.0, light.flags.z) * step(0.5, descriptor.policy.x);\n"
		"    if (descriptorValid < 0.5) { return ModernClusterShadowBrokenVisibility(); }\n"
		"    if (abs(descriptor.freshness.x - uModernShadowContractState.y) > 0.5) { return ModernClusterShadowBrokenVisibility(); }\n"
		"    if (ModernClusterShadowAtlasReady(descriptor) < 0.5) { return ModernClusterShadowBrokenVisibility(); }\n"
		"    if (ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_RECEIVER_BLOCKED) || !ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_SAMPLING_READY)) { return ModernClusterShadowBrokenVisibility(); }\n"
		"    if (policy == MODERN_SHADOW_POLICY_MAPPED && descriptorPolicy != MODERN_SHADOW_POLICY_MAPPED) { return ModernClusterShadowBrokenVisibility(); }\n"
		"    if (policy == MODERN_SHADOW_POLICY_CACHE_REUSE && descriptorPolicy != MODERN_SHADOW_POLICY_CACHE_REUSE) { return ModernClusterShadowBrokenVisibility(); }\n"
		"    float mapType = floor(descriptor.identity.w + 0.5);\n"
		"    if (mapType == MODERN_SHADOW_MAP_POINT && ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_ATLAS_SLOT) && descriptor.freshness.y > 0.5) { return ModernClusterSamplePointShadow(descriptor, light, viewPosition, normal); }\n"
		"    if ((mapType == MODERN_SHADOW_MAP_PROJECTED || mapType == MODERN_SHADOW_MAP_CASCADE) && ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_PROJECTED_STATE_READY) && ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_ATLAS_SLOT) && descriptor.freshness.y > 0.5) { return ModernClusterSampleProjectedShadow(descriptor, light, viewPosition, normal); }\n"
		"    return ModernClusterShadowBrokenVisibility();\n"
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
			"    if (uMaterialFlags.w >= 7.5) {\n"
			"        int normalFormat = int(floor(uMaterialFlags.w - 9.0 + 0.5));\n"
			"        if (normalFormat == 1) { vec2 xy = (bumpSample.rg * 2.0 - 1.0) * max(uLocalParams.w, 0.0); float z = sqrt(max(1.0 - dot(xy, xy), 0.0)); return ModernSafeNormal(vec3(xy, z)); }\n"
			"        if (normalFormat == 2) { return ModernSafeNormal((bumpSample.rgb * 2.0 - 1.0) * vec3(max(uLocalParams.w, 0.0), max(uLocalParams.w, 0.0), 1.0)); }\n"
			"        return ModernSafeNormal(ModernDecodeClassicNormal(bumpSample));\n"
			"    }\n"
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
			"    bool pbr = uMaterialFlags.w >= 7.5;\n"
			"    vec3 baseColor = pbr ? pow(max(texel.rgb, vec3(0.0)), vec3(2.2)) : texel.rgb * max(uDebugColor.rgb, vec3(0.0));\n"
			"    vec3 normal = ModernMaterialNormal();\n"
			"    float specular = ModernSpecularStrength();\n"
			"    vec3 orm = uMaterialFlags.y > 0.5 ? ModernSampleSpecularTexture(vTexCoord).rgb : vec3(1.0);\n"
			"    if (pbr && uMaterialFlags.y < -0.5) { orm = vec3(ModernSampleAOTexture(vTexCoord).r, ModernSampleRoughnessTexture(vTexCoord).r, ModernSampleMetallicTexture(vTexCoord).r); }\n"
			"    float metallic = pbr ? clamp(orm.b * uLocalParams.x, 0.0, 1.0) : 0.04;\n"
			"    float roughness = pbr ? clamp(orm.g * uLocalParams.y, 0.02, 1.0) : specular;\n"
			"    float ao = pbr ? clamp(orm.r, 0.0, 1.0) * clamp(uLocalParams.z, 0.0, 1.0) : uLocalParams.z;\n"
			"    vec3 emissive = uMaterialFlags.z > 0.5 ? ModernSampleEmissiveTexture(vTexCoord).rgb : vec3(0.0);\n"
			"    if (pbr) { emissive = pow(max(emissive, vec3(0.0)), vec3(2.2)) * max(uDebugColor.rgb, vec3(0.0)); } else { emissive += vec3(uLocalParams.w); }\n"
			"    out_Albedo = vec4(baseColor, 1.0);\n"
			"    out_Normal = vec4(normal * 0.5 + 0.5, vTangentSign * 0.5 + 0.5);\n"
			"    out_Material = pbr ? vec4(metallic, roughness, ao, 0.0) : vec4(0.04, specular, uLocalParams.z, ModernMaterialFresnel());\n"
			"    // Emissive alpha is an explicit layout tag; material alpha remains classic Fresnel data.\n"
			"    out_Emissive = vec4(emissive, pbr ? 1.0 : 0.0);\n"
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
			"    if (uMaterialFlags.w >= 7.5) {\n"
			"        int normalFormat = int(floor(uMaterialFlags.w - 9.0 + 0.5));\n"
			"        if (normalFormat == 1) { vec2 xy = (bumpSample.rg * 2.0 - 1.0) * max(uLocalParams.w, 0.0); float z = sqrt(max(1.0 - dot(xy, xy), 0.0)); return ModernSafeNormal(vec3(xy, z)); }\n"
			"        if (normalFormat == 2) { return ModernSafeNormal((bumpSample.rgb * 2.0 - 1.0) * vec3(max(uLocalParams.w, 0.0), max(uLocalParams.w, 0.0), 1.0)); }\n"
			"        return ModernSafeNormal(ModernDecodeClassicNormal(bumpSample));\n"
			"    }\n"
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
			"    bool pbr = uMaterialFlags.w >= 7.5;\n"
			"    if (texel.a < (pbr ? max(uMaterialEnhancement.w, 0.001) : max(uLocalParams.x, 0.001))) { discard; }\n"
			"    vec3 normal = ModernMaterialNormal();\n"
			"    float specular = ModernSpecularStrength();\n"
			"    vec3 orm = uMaterialFlags.y > 0.5 ? ModernSampleSpecularTexture(vTexCoord).rgb : vec3(1.0);\n"
			"    if (pbr && uMaterialFlags.y < -0.5) { orm = vec3(ModernSampleAOTexture(vTexCoord).r, ModernSampleRoughnessTexture(vTexCoord).r, ModernSampleMetallicTexture(vTexCoord).r); }\n"
			"    float metallic = pbr ? clamp(orm.b * uLocalParams.x, 0.0, 1.0) : 0.04;\n"
			"    float roughness = pbr ? clamp(orm.g * uLocalParams.y, 0.02, 1.0) : specular;\n"
			"    float ao = pbr ? clamp(orm.r, 0.0, 1.0) * clamp(uLocalParams.z, 0.0, 1.0) : uLocalParams.z;\n"
			"    vec3 emissive = uMaterialFlags.z > 0.5 ? ModernSampleEmissiveTexture(vTexCoord).rgb : vec3(0.0);\n"
			"    if (pbr) { emissive = pow(max(emissive, vec3(0.0)), vec3(2.2)) * max(uDebugColor.rgb, vec3(0.0)); }\n"
			"    out_Albedo = vec4(pbr ? pow(max(texel.rgb, vec3(0.0)), vec3(2.2)) : texel.rgb * max(uDebugColor.rgb, vec3(0.0)), 1.0);\n"
			"    out_Normal = vec4(normal * 0.5 + 0.5, vTangentSign * 0.5 + 0.5);\n"
			"    out_Material = pbr ? vec4(metallic, roughness, ao, 0.0) : vec4(0.04, specular, uLocalParams.z, ModernMaterialFresnel());\n"
			"    // Emissive alpha is an explicit layout tag; material alpha remains classic Fresnel data.\n"
			"    out_Emissive = vec4(emissive, pbr ? 1.0 : 0.0);\n"
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
			"    // G-buffer normals are written in GL eye space; the cluster\n"
			"    // light math runs in the cluster basis (M2)\n"
			"    vec3 normal = ModernClusterFromEyeSpace(normalize(texture(uGBufferNormal, vTexCoord).xyz * 2.0 - 1.0));\n"
			"    vec4 material = texture(uGBufferMaterial, vTexCoord);\n"
			"    vec4 emissiveRecord = texture(uGBufferEmissive, vTexCoord);\n"
			"    bool pbr = emissiveRecord.a > 0.5;\n"
			"    vec3 emissive = emissiveRecord.rgb;\n"
			"    vec3 lightGrid = pbr ? vec3(0.0) : emissive;\n"
			"    float rawDepth = texture(uSceneDepth, vTexCoord).r;\n"
			"    vec3 viewPosition = ModernClusterViewPositionFromDepth(vTexCoord, rawDepth);\n"
			"    ModernClusterPrepareShadowDerivatives(viewPosition);\n"
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
			"        bool supported = type == 0 || type == 1 || type == 3;\n"
			"        vec2 pixel = gl_FragCoord.xy;\n"
			"        float inX = step(light.scissorDepth.x, pixel.x) * step(pixel.x, light.scissorDepth.z);\n"
			"        float inY = step(light.scissorDepth.y, pixel.y) * step(pixel.y, light.scissorDepth.w);\n"
		"        float shadowVisibility = ModernClusterShadowVisibility(light, viewPosition, normal);\n"
			"        float attenuation = 0.0;\n"
			"        vec3 contribution = pbr ? ModernClusterEvaluatePBRLight(light, viewPosition, normal, albedo.rgb, material.r, material.g, attenuation) : ModernClusterEvaluateLight(light, viewPosition, normal, material.g, material.a, attenuation);\n"
			"        attenuation = supported ? attenuation * inX * inY * shadowVisibility : 0.0;\n"
			"        lightAccum += contribution * inX * inY * shadowVisibility;\n"
			"        contributingLights += attenuation > 0.001 ? 1 : 0;\n"
			"        scannedLights++;\n"
			"    }\n"
			"    float exposure = max(uLocalParams.x, 0.25);\n"
			"    float debugMode = floor(uLocalParams.y + 0.5);\n"
			"    float overflowPressure = clamp(uLocalParams.w, 0.0, 1.0);\n"
			"    float shadowBindingProbe = dot(ModernClusterShadowResourceProbe(), vec4(0.000001));\n"
			"    vec3 pbrIndirect = ModernPBRIndirect(viewPosition, normal, albedo.rgb, material.r, material.g, material.b, clusterRange);\n"
			"    vec3 lit = pbr ? pbrIndirect + lightAccum + emissive : albedo.rgb * (vec3(0.12) + lightGrid + lightAccum * (0.35 + material.g)) * max(uDebugColor.rgb, vec3(0.0)) * exposure;\n"
			"    lit += vec3(shadowBindingProbe);\n"
			"    // fog and blend lights are compositing operations, not additive\n"
			"    // contributions, so they are applied over the accumulated result\n"
			"    lit = ModernClusterApplyFogAndBlend(lit, clusterRange, clusterLightCount, viewPosition);\n"
			"    float pbrDebug = floor(uDebugColor.a + 0.5);\n"
			"    if (pbr && pbrDebug == 1.0) { out_Color = vec4(albedo.rgb, 1.0); }\n"
			"    else if (pbr && pbrDebug == 2.0) { out_Color = vec4(normal * 0.5 + 0.5, 1.0); }\n"
			"    else if (pbr && pbrDebug == 3.0) { out_Color = vec4(vec3(material.r), 1.0); }\n"
			"    else if (pbr && pbrDebug == 4.0) { out_Color = vec4(vec3(material.g), 1.0); }\n"
			"    else if (pbr && pbrDebug == 5.0) { out_Color = vec4(vec3(material.b), 1.0); }\n"
			"    else if (pbr && pbrDebug == 6.0) { out_Color = vec4(emissive, 1.0); }\n"
			"    else if (pbrDebug == 7.0) { out_Color = vec4(pbr ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 1.0), 1.0); }\n"
			"    else if (debugMode == 1.0) {\n"
			"        out_Color = vec4(clamp(lightAccum, vec3(0.0), vec3(1.0)), 1.0);\n"
			"    } else if (debugMode == 2.0) {\n"
			"        out_Color = vec4(fract(vec3(float(tileX) * 0.173, float(tileY) * 0.271, float(sliceZ) * 0.067)), 1.0);\n"
			"    } else if (debugMode == 3.0) {\n"
			"        float heat = clamp(float(scannedLights) / max(float(maxLights), 1.0), 0.0, 1.0);\n"
			"        out_Color = vec4(heat, float(contributingLights) * 0.25, 1.0 - heat, 1.0);\n"
			"    } else if (debugMode == 4.0) {\n"
			"        out_Color = vec4(overflowPressure, uLocalParams.z, 0.1, 1.0);\n"
			"    } else {\n"
			"        out_Color = vec4(max(lit, vec3(0.0)), albedo.a);\n"
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
			"    bool pbr = ModernIsPBRMaterial();\n"
			"    ModernClusterPrepareShadowDerivatives(ModernClusterFromEyeSpace(vViewPosition));\n"
			"    if (%d != 0 && texel.a < (pbr ? max(uMaterialEnhancement.w, 0.001) : max(uLocalParams.x, 0.001))) { discard; }\n"
			"    // vViewPosition/normals are GL eye space; convert once so every\n"
			"    // cluster consumer (slice, light math, shadows) shares the\n"
			"    // cluster basis instead of mixing conventions (M2)\n"
			"    vec3 clusterPosition = ModernClusterFromEyeSpace(vViewPosition);\n"
			"    vec3 materialNormal = ModernClusterFromEyeSpace(ModernMaterialNormal());\n"
			"    float specular = ModernSpecularStrength();\n"
			"    vec3 pbrData = ModernPBRMaterialData();\n"
			"    vec3 baseColor = pbr ? ModernPBRBaseColor() : texel.rgb * max(uDebugColor.rgb, vec3(0.0));\n"
			"    vec3 emissive = pbr ? ModernPBREmissiveColor() : ModernEmissiveColor();\n"
			"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
			"    vec2 viewport = max(uClusterGrid.viewport.xy, vec2(1.0));\n"
			"    vec2 normalizedPixel = clamp(gl_FragCoord.xy / viewport, vec2(0.0), vec2(0.999));\n"
			"    int tileX = clamp(int(floor(normalizedPixel.x * float(grid.x))), 0, grid.x - 1);\n"
			"    int tileY = clamp(int(floor((1.0 - normalizedPixel.y) * float(grid.y))), 0, grid.y - 1);\n"
			"    int sliceZ = ModernClusterSliceForDepth(max(clusterPosition.z, ModernClusterLinearDepth(gl_FragCoord.z)));\n"
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
			"        bool supported = type == 0 || type == 1 || type == 3;\n"
			"        float inX = step(light.scissorDepth.x, gl_FragCoord.x) * step(gl_FragCoord.x, light.scissorDepth.z);\n"
			"        float inY = step(light.scissorDepth.y, gl_FragCoord.y) * step(gl_FragCoord.y, light.scissorDepth.w);\n"
		"        float shadowVisibility = ModernClusterShadowVisibility(light, clusterPosition, materialNormal);\n"
			"        float attenuation = 0.0;\n"
			"        vec3 contribution = pbr ? ModernClusterEvaluatePBRLight(light, clusterPosition, materialNormal, baseColor, pbrData.x, pbrData.y, attenuation) : ModernClusterEvaluateLight(light, clusterPosition, materialNormal, specular, ModernMaterialFresnel(), attenuation);\n"
			"        lightAccum += supported ? contribution * inX * inY * shadowVisibility : vec3(0.0);\n"
			"        scannedLights++;\n"
			"    }\n"
			"    float lightScale = clamp(0.18 + uLocalParams.y + float(scannedLights) * 0.02, 0.18, 2.5);\n"
			"    float shadowBindingProbe = dot(ModernClusterShadowResourceProbe(), vec4(0.000001));\n"
			"    vec3 lit = pbr ? ModernPBRIndirect(clusterPosition, materialNormal, baseColor, pbrData.x, pbrData.y, pbrData.z, clusterRange) + lightAccum + emissive : baseColor * (lightScale + lightAccum * (0.30 + specular * 0.25)) + emissive;\n"
			"    int pbrDebug = int(floor(uDebugColor.a + 0.5));\n"
			"    if (pbr && pbrDebug == 1) lit = baseColor; else if (pbr && pbrDebug == 2) lit = materialNormal * 0.5 + 0.5; else if (pbr && pbrDebug == 3) lit = vec3(pbrData.x); else if (pbr && pbrDebug == 4) lit = vec3(pbrData.y); else if (pbr && pbrDebug == 5) lit = vec3(pbrData.z); else if (pbr && pbrDebug == 6) lit = emissive; else if (pbrDebug == 7) lit = pbr ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 1.0);\n"
			"    out_Color = vec4(ModernSceneReferredColor(lit + vec3(shadowBindingProbe)), texel.a);\n"
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
			"    bool pbr = ModernIsPBRMaterial();\n"
			"    ModernClusterPrepareShadowDerivatives(ModernClusterFromEyeSpace(vViewPosition));\n"
			"    float decalMode = pbr ? step(0.5, uMaterialEnhancement.x) : step(0.5, uLocalParams.w);\n"
			"    vec4 vertexTint = clamp(vVertexColor, vec4(0.0), vec4(1.0));\n"
			// PBR materials without an authored vertex-color contract must take
			// transparency from their albedo alpha. World vertices commonly carry
			// an unset alpha channel, so multiplying it here makes valid
			// source-alpha PBR surfaces disappear completely.
			"    vec4 materialColor = pbr ? vec4(ModernPBRBaseColor() * vertexTint.rgb, texel.a) : texel * vertexTint * max(uDebugColor, vec4(0.0));\n"
			"    vec3 baseColor = materialColor.rgb;\n"
			"    vec3 clusterPosition = ModernClusterFromEyeSpace(vViewPosition);\n"
			"    vec3 materialNormal = ModernClusterFromEyeSpace(ModernMaterialNormal());\n"
			"    float specular = ModernSpecularStrength();\n"
			"    vec3 pbrData = ModernPBRMaterialData();\n"
			"    vec3 emissive = pbr ? ModernPBREmissiveColor() : ModernEmissiveColor();\n"
			"    ivec3 grid = ivec3(max(uClusterGrid.grid.xyz, vec3(1.0)));\n"
			"    int maxLights = int(max(uClusterGrid.grid.w, 1.0));\n"
			"    vec2 viewport = max(uClusterGrid.viewport.xy, vec2(1.0));\n"
			"    vec2 normalizedPixel = clamp(gl_FragCoord.xy / viewport, vec2(0.0), vec2(0.999));\n"
			"    int tileX = clamp(int(floor(normalizedPixel.x * float(grid.x))), 0, grid.x - 1);\n"
			"    int tileY = clamp(int(floor((1.0 - normalizedPixel.y) * float(grid.y))), 0, grid.y - 1);\n"
			"    int sliceZ = ModernClusterSliceForDepth(max(clusterPosition.z, ModernClusterLinearDepth(gl_FragCoord.z)));\n"
			"    int clusterIndex = (sliceZ * grid.y + tileY) * grid.x + tileX;\n"
			"    uvec4 clusterRange = ModernClusterFetchRange(clusterIndex);\n"
			"    int clusterLightCount = int(min(clusterRange.y, uint(maxLights)));\n"
			"    vec3 lightAccum = vec3(0.0);\n"
			"    for (int i = 0; i < clusterLightCount; ++i) {\n"
			"        uint lightIndex = ModernClusterFetchLightIndex(clusterRange.x + uint(i));\n"
			"        if (lightIndex == 0xffffffffu || lightIndex >= uint(max(uClusterGrid.counts.x, 0.0))) { continue; }\n"
			"        ModernClusterLightRecord light = ModernClusterFetchLight(lightIndex);\n"
			"        int type = int(floor(light.colorType.w + 0.5));\n"
		"        float shadowVisibility = ModernClusterShadowVisibility(light, clusterPosition, materialNormal);\n"
			"        float attenuation = 0.0;\n"
			"        vec3 contribution = pbr ? ModernClusterEvaluatePBRLight(light, clusterPosition, materialNormal, baseColor, pbrData.x, pbrData.y, attenuation) : ModernClusterEvaluateLight(light, clusterPosition, materialNormal, specular, ModernMaterialFresnel(), attenuation);\n"
			"        if (type == 0 || type == 1 || type == 3) { lightAccum += contribution * shadowVisibility; }\n"
			"    }\n"
			"    float shadowBindingProbe = dot(ModernClusterShadowResourceProbe(), vec4(0.000001));\n"
			"    vec3 transparentColor = pbr ? ModernPBRIndirect(clusterPosition, materialNormal, baseColor, pbrData.x, pbrData.y, pbrData.z, clusterRange) + lightAccum + emissive + vec3(shadowBindingProbe) : baseColor + lightAccum + emissive + vec3(shadowBindingProbe);\n"
			"    transparentColor = ModernClusterApplyFogAndBlend(transparentColor, clusterRange, clusterLightCount, clusterPosition);\n"
			"    int pbrDebug = int(floor(uDebugColor.a + 0.5));\n"
			"    if (pbr && pbrDebug == 1) transparentColor = baseColor; else if (pbr && pbrDebug == 2) transparentColor = materialNormal * 0.5 + 0.5; else if (pbr && pbrDebug == 3) transparentColor = vec3(pbrData.x); else if (pbr && pbrDebug == 4) transparentColor = vec3(pbrData.y); else if (pbr && pbrDebug == 5) transparentColor = vec3(pbrData.z); else if (pbr && pbrDebug == 6) transparentColor = emissive; else if (pbrDebug == 7) transparentColor = pbr ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 1.0);\n"
			"    out_Color = vec4(ModernSceneReferredColor(mix(transparentColor, baseColor, decalMode)), materialColor.a);\n"
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

static bool R_ModernGLShaderLibrary_KindUsesPBRMaterialData( modernGLShaderProgramKind_t kind ) {
	return kind == MODERN_GL_SHADER_GBUFFER_OPAQUE
		|| kind == MODERN_GL_SHADER_GBUFFER_ALPHA_TEST
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST
		|| kind == MODERN_GL_SHADER_TRANSPARENT_FORWARD;
}

static bool R_ModernGLShaderLibrary_KindUsesPBRIBL( modernGLShaderProgramKind_t kind ) {
	return kind == MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST
		|| kind == MODERN_GL_SHADER_TRANSPARENT_FORWARD;
}

static bool R_ModernGLShaderLibrary_KindUsesMaterialTextureTable( modernGLShaderProgramKind_t kind ) {
	return kind != MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE;
}

static bool R_ModernGLShaderLibrary_KindUsesSceneDepthTexture( modernGLShaderProgramKind_t kind ) {
	(void)kind;
	return false;
}

static bool R_ModernGLShaderLibrary_KindUsesShadowTextures( modernGLShaderProgramKind_t kind ) {
	return kind == MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE
		|| kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST
		|| kind == MODERN_GL_SHADER_TRANSPARENT_FORWARD;
}

static bool R_ModernGLShaderLibrary_KindUsesSpecularProbes( modernGLShaderProgramKind_t kind ) {
	return R_ModernGLShaderLibrary_KindUsesPBRIBL( kind );
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
	info.reflection.usesPBRIBL = R_ModernGLShaderLibrary_KindUsesPBRIBL( info.kind );
	info.reflection.usesMainTexture = R_ModernGLShaderLibrary_KindUsesMainTexture( info.kind );
	info.reflection.usesMaterialTextures = R_ModernGLShaderLibrary_KindUsesMaterialTextures( info.kind );
	info.reflection.usesMaterialTextureTable = info.glslVersion >= 430 && info.reflection.usesMainTexture && R_ModernGLShaderLibrary_KindUsesMaterialTextureTable( info.kind );
	info.reflection.usesMaterialFlags = info.reflection.usesMaterialTextures;
	info.reflection.usesMaterialEnhancement = info.reflection.usesMaterialTextures;
	info.reflection.usesDrawRecords = info.glslVersion >= 430 && info.kind != MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE;
	info.reflection.usesSceneDepthTexture = R_ModernGLShaderLibrary_KindUsesSceneDepthTexture( info.kind );
	info.reflection.usesShadowTextures = R_ModernGLShaderLibrary_KindUsesShadowTextures( info.kind );
	info.reflection.usesSpecularProbes = R_ModernGLShaderLibrary_KindUsesSpecularProbes( info.kind );
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
	info.reflection.pbrIBLLocation = glGetUniformLocation( info.program, "uPBRIBL" );
	info.reflection.mainTextureLocation = glGetUniformLocation( info.program, "uMainTexture" );
	info.reflection.normalTextureLocation = glGetUniformLocation( info.program, "uNormalTexture" );
	info.reflection.specularTextureLocation = glGetUniformLocation( info.program, "uSpecularTexture" );
	info.reflection.emissiveTextureLocation = glGetUniformLocation( info.program, "uEmissiveTexture" );
	info.reflection.metallicTextureLocation = glGetUniformLocation( info.program, "uMetallicTexture" );
	info.reflection.roughnessTextureLocation = glGetUniformLocation( info.program, "uRoughnessTexture" );
	info.reflection.aoTextureLocation = glGetUniformLocation( info.program, "uAOTexture" );
	info.reflection.textureIndicesLocation = glGetUniformLocation( info.program, "uTextureIndices" );
	info.reflection.textureTableModeLocation = glGetUniformLocation( info.program, "uTextureTableMode" );
	info.reflection.materialTextureTableLocation = glGetUniformLocation( info.program, "uMaterialTextures[0]" );
	info.reflection.materialFlagsLocation = glGetUniformLocation( info.program, "uMaterialFlags" );
	info.reflection.materialEnhancementLocation = glGetUniformLocation( info.program, "uMaterialEnhancement" );
	info.reflection.drawRecordModeLocation = glGetUniformLocation( info.program, "uDrawRecordMode" );
	info.reflection.drawRecordCountLocation = glGetUniformLocation( info.program, "uDrawRecordCount" );
	info.reflection.sceneDepthTextureLocation = glGetUniformLocation( info.program, "uSceneDepth" );
	info.reflection.specularProbeAtlasLocation = glGetUniformLocation( info.program, "uModernSpecularProbeAtlas" );
	const GLuint specularProbeBlockIndex = glGetUniformBlockIndex( info.program, "ModernSpecularProbeRecords" );
	info.reflection.specularProbeBlockIndex = specularProbeBlockIndex == GL_INVALID_INDEX
		? -1 : static_cast<int>( specularProbeBlockIndex );
	const GLint shadowAtlasLocation = glGetUniformLocation( info.program, "uModernShadowAtlas" );
	const GLint pointShadowAtlasLocation = glGetUniformLocation( info.program, "uModernPointShadowAtlas" );
	const GLint translucentShadowMomentsLocation = glGetUniformLocation( info.program, "uModernTranslucentShadowMoments[0]" );
	const GLint pointTranslucentShadowMomentsLocation = glGetUniformLocation( info.program, "uModernPointTranslucentShadowMoments[0]" );
	const GLint shadowResourceStateLocation = glGetUniformLocation( info.program, "uModernShadowResourceState" );
	const GLint shadowSamplerStateLocation = glGetUniformLocation( info.program, "uModernShadowSamplerState" );
	const GLint shadowMomentStateLocation = glGetUniformLocation( info.program, "uModernShadowMomentState" );

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
	if ( info.reflection.usesSpecularProbes ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniformBlocks,
			info.reflection.uniformBlockCount,
			"ModernSpecularProbeRecords",
			MODERN_GL_SHADER_RESOURCE_UNIFORM_BLOCK,
			info.reflection.specularProbeBlockIndex,
			-1,
			7,
			32,
			GL_UNIFORM_BLOCK,
			true,
			info.reflection.specularProbeBlockIndex >= 0 );
	}
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
	if ( info.reflection.usesSceneDepthTexture ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uSceneDepth",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			info.reflection.sceneDepthTextureLocation,
			1,
			1,
			GL_SAMPLER_2D,
			true,
			info.reflection.sceneDepthTextureLocation >= 0 );
	}
	if ( info.reflection.usesShadowTextures ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uModernShadowAtlas",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			shadowAtlasLocation,
			MODERN_GL_SHADOW_TEXTURE_UNIT_PROJECTED_ATLAS,
			1,
			GL_SAMPLER_2D,
			true,
			shadowAtlasLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uModernPointShadowAtlas",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			pointShadowAtlasLocation,
			MODERN_GL_SHADOW_TEXTURE_UNIT_POINT_ATLAS,
			1,
			GL_SAMPLER_CUBE,
			true,
			pointShadowAtlasLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uModernTranslucentShadowMoments",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			translucentShadowMomentsLocation,
			MODERN_GL_SHADOW_TEXTURE_UNIT_PROJECTED_MOMENTS,
			RENDERER_SHADOW_TEXTURE_MOMENT_COUNT,
			GL_SAMPLER_2D,
			true,
			translucentShadowMomentsLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uModernPointTranslucentShadowMoments",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			pointTranslucentShadowMomentsLocation,
			MODERN_GL_SHADOW_TEXTURE_UNIT_POINT_MOMENTS,
			RENDERER_SHADOW_TEXTURE_MOMENT_COUNT,
			GL_SAMPLER_CUBE,
			true,
			pointTranslucentShadowMomentsLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uModernShadowResourceState",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			shadowResourceStateLocation,
			-1,
			1,
			GL_FLOAT_VEC4,
			true,
			shadowResourceStateLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uModernShadowSamplerState",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			shadowSamplerStateLocation,
			-1,
			1,
			GL_FLOAT_VEC4,
			true,
			shadowSamplerStateLocation >= 0 );
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uModernShadowMomentState",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			shadowMomentStateLocation,
			-1,
			1,
			GL_FLOAT_VEC4,
			true,
			shadowMomentStateLocation >= 0 );
	}
	if ( info.reflection.usesSpecularProbes ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.samplers,
			info.reflection.samplerCount,
			"uModernSpecularProbeAtlas",
			MODERN_GL_SHADER_RESOURCE_SAMPLER,
			-1,
			info.reflection.specularProbeAtlasLocation,
			MODERN_SPECULAR_PROBE_ATLAS_TEXTURE_UNIT,
			1,
			GL_SAMPLER_2D,
			true,
			info.reflection.specularProbeAtlasLocation >= 0 );
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
		if ( R_ModernGLShaderLibrary_KindUsesPBRMaterialData( info.kind ) ) {
			R_ModernGLShaderLibrary_AddReflectionRecord( info.reflection.samplers, info.reflection.samplerCount, "uMetallicTexture", MODERN_GL_SHADER_RESOURCE_SAMPLER, -1, info.reflection.metallicTextureLocation, 4, 1, GL_SAMPLER_2D, true, info.reflection.metallicTextureLocation >= 0 );
			R_ModernGLShaderLibrary_AddReflectionRecord( info.reflection.samplers, info.reflection.samplerCount, "uRoughnessTexture", MODERN_GL_SHADER_RESOURCE_SAMPLER, -1, info.reflection.roughnessTextureLocation, 5, 1, GL_SAMPLER_2D, true, info.reflection.roughnessTextureLocation >= 0 );
			R_ModernGLShaderLibrary_AddReflectionRecord( info.reflection.samplers, info.reflection.samplerCount, "uAOTexture", MODERN_GL_SHADER_RESOURCE_SAMPLER, -1, info.reflection.aoTextureLocation, 6, 1, GL_SAMPLER_2D, true, info.reflection.aoTextureLocation >= 0 );
		}
	}
	if ( info.reflection.usesPBRIBL ) {
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uPBRIBL",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.pbrIBLLocation,
			-1,
			1,
			GL_FLOAT_VEC4,
			true,
			info.reflection.pbrIBLLocation >= 0 );
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
		R_ModernGLShaderLibrary_AddReflectionRecord(
			info.reflection.uniforms,
			info.reflection.uniformCount,
			"uDrawRecordCount",
			MODERN_GL_SHADER_RESOURCE_UNIFORM,
			-1,
			info.reflection.drawRecordCountLocation,
			-1,
			1,
			GL_UNSIGNED_INT,
			true,
			info.reflection.drawRecordCountLocation >= 0 );
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
	info.pbrIBLLocation = info.reflection.pbrIBLLocation;
	info.mainTextureLocation = info.reflection.mainTextureLocation;
	info.normalTextureLocation = info.reflection.normalTextureLocation;
	info.specularTextureLocation = info.reflection.specularTextureLocation;
	info.emissiveTextureLocation = info.reflection.emissiveTextureLocation;
	info.metallicTextureLocation = info.reflection.metallicTextureLocation;
	info.roughnessTextureLocation = info.reflection.roughnessTextureLocation;
	info.aoTextureLocation = info.reflection.aoTextureLocation;
	info.textureIndicesLocation = info.reflection.textureIndicesLocation;
	info.textureTableModeLocation = info.reflection.textureTableModeLocation;
	info.materialTextureTableLocation = info.reflection.materialTextureTableLocation;
	info.materialFlagsLocation = info.reflection.materialFlagsLocation;
	info.materialEnhancementLocation = info.reflection.materialEnhancementLocation;
	info.drawRecordModeLocation = info.reflection.drawRecordModeLocation;
	info.drawRecordCountLocation = info.reflection.drawRecordCountLocation;
	info.sceneDepthTextureLocation = info.reflection.sceneDepthTextureLocation;
	info.specularProbeAtlasLocation = info.reflection.specularProbeAtlasLocation;
	info.specularProbeBlockIndex = info.reflection.specularProbeBlockIndex;

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
	if ( info.reflection.usesSceneDepthTexture && info.sceneDepthTextureLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uSceneDepth", info.name );
		return false;
	}
	if ( info.reflection.usesShadowTextures
		&& ( shadowAtlasLocation < 0
			|| pointShadowAtlasLocation < 0
			|| translucentShadowMomentsLocation < 0
			|| pointTranslucentShadowMomentsLocation < 0
			|| shadowResourceStateLocation < 0
			|| shadowSamplerStateLocation < 0
			|| shadowMomentStateLocation < 0 ) ) {
		common->Warning(
			"Modern GL program '%s' is missing shadow texture bindings (projectedAtlas=%d pointAtlas=%d projectedMoments=%d pointMoments=%d resource=%d sampler=%d moment=%d)",
			info.name,
			static_cast<int>( shadowAtlasLocation ),
			static_cast<int>( pointShadowAtlasLocation ),
			static_cast<int>( translucentShadowMomentsLocation ),
			static_cast<int>( pointTranslucentShadowMomentsLocation ),
			static_cast<int>( shadowResourceStateLocation ),
			static_cast<int>( shadowSamplerStateLocation ),
			static_cast<int>( shadowMomentStateLocation ) );
		return false;
	}
	if ( info.reflection.usesMaterialTextures
		&& ( info.normalTextureLocation < 0 || info.specularTextureLocation < 0 || info.emissiveTextureLocation < 0 ) ) {
		common->Warning( "Modern GL program '%s' is missing material texture samplers", info.name );
		return false;
	}
	if ( info.reflection.usesPBRIBL && info.pbrIBLLocation < 0 ) {
		common->Warning( "Modern GL PBR program '%s' is missing uPBRIBL", info.name );
		return false;
	}
	if ( info.reflection.usesSpecularProbes
			&& ( info.specularProbeAtlasLocation < 0 || info.specularProbeBlockIndex < 0 ) ) {
		common->Warning( "Modern GL PBR program '%s' is missing authored specular-probe bindings", info.name );
		return false;
	}
	if ( R_ModernGLShaderLibrary_KindUsesPBRMaterialData( info.kind )
		&& ( info.metallicTextureLocation < 0 || info.roughnessTextureLocation < 0 || info.aoTextureLocation < 0 ) ) {
		common->Warning( "Modern GL PBR program '%s' is missing separate material-map samplers", info.name );
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
	if ( info.reflection.usesDrawRecords && info.drawRecordCountLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uDrawRecordCount", info.name );
		return false;
	}

	glUniformBlockBinding( info.program, static_cast<GLuint>( info.frameBlockIndex ), 0 );
	if ( info.reflection.usesMainTexture || info.reflection.usesMaterialTextures
			|| info.reflection.usesSceneDepthTexture || info.reflection.usesShadowTextures
			|| info.reflection.usesSpecularProbes ) {
		glUseProgram( info.program );
		if ( info.reflection.usesMainTexture ) {
			glUniform1i( info.mainTextureLocation, 0 );
		}
		if ( info.reflection.usesSceneDepthTexture ) {
			glUniform1i( info.sceneDepthTextureLocation, 1 );
		}
		if ( info.reflection.usesMaterialTextures ) {
			glUniform1i( info.normalTextureLocation, 1 );
			glUniform1i( info.specularTextureLocation, 2 );
			glUniform1i( info.emissiveTextureLocation, 3 );
			if ( info.metallicTextureLocation >= 0 ) {
				glUniform1i( info.metallicTextureLocation, 4 );
			}
			if ( info.roughnessTextureLocation >= 0 ) {
				glUniform1i( info.roughnessTextureLocation, 5 );
			}
			if ( info.aoTextureLocation >= 0 ) {
				glUniform1i( info.aoTextureLocation, 6 );
			}
		}
		if ( info.reflection.usesShadowTextures ) {
			glUniform1i( shadowAtlasLocation, MODERN_GL_SHADOW_TEXTURE_UNIT_PROJECTED_ATLAS );
			glUniform1i( pointShadowAtlasLocation, MODERN_GL_SHADOW_TEXTURE_UNIT_POINT_ATLAS );
			if ( glUniform1iv != NULL ) {
				GLint projectedMomentUnits[RENDERER_SHADOW_TEXTURE_MOMENT_COUNT];
				GLint pointMomentUnits[RENDERER_SHADOW_TEXTURE_MOMENT_COUNT];
				for ( int i = 0; i < RENDERER_SHADOW_TEXTURE_MOMENT_COUNT; ++i ) {
					projectedMomentUnits[i] = MODERN_GL_SHADOW_TEXTURE_UNIT_PROJECTED_MOMENTS + i;
					pointMomentUnits[i] = MODERN_GL_SHADOW_TEXTURE_UNIT_POINT_MOMENTS + i;
				}
				glUniform1iv( translucentShadowMomentsLocation, RENDERER_SHADOW_TEXTURE_MOMENT_COUNT, projectedMomentUnits );
				glUniform1iv( pointTranslucentShadowMomentsLocation, RENDERER_SHADOW_TEXTURE_MOMENT_COUNT, pointMomentUnits );
			}
			glUniform4f( shadowResourceStateLocation, 0.0f, 0.0f, 0.0f, 0.0f );
			glUniform4f( shadowSamplerStateLocation, 0.0f, 0.0f, 0.0f, 0.0f );
			glUniform4f( shadowMomentStateLocation, 0.0f, 0.0f, 0.0f, 0.0f );
		}
		if ( info.reflection.usesSpecularProbes ) {
			glUniform1i( info.specularProbeAtlasLocation, MODERN_SPECULAR_PROBE_ATLAS_TEXTURE_UNIT );
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
		glUniform1ui( info.drawRecordCountLocation, 0 );
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
	info.pbrIBLLocation = -1;
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
	info.drawRecordCountLocation = -1;
	info.sceneDepthTextureLocation = -1;
	info.specularProbeAtlasLocation = -1;
	info.specularProbeBlockIndex = -1;
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
		if ( program->reflection.usesPBRIBL && program->pbrIBLLocation < 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: PBR IBL reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesSpecularProbes
				&& ( program->specularProbeAtlasLocation < 0 || program->specularProbeBlockIndex < 0 ) ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: authored specular-probe reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesMainTexture && program->mainTextureLocation < 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: sampler reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesSceneDepthTexture && program->sceneDepthTextureLocation < 0 ) {
			common->Printf( "RendererModernGLShaderLibrary self-test failed: scene-depth sampler reflection mismatch for %s\n", program->name );
			return false;
		}
		if ( program->reflection.usesShadowTextures ) {
			const char *shadowBindingUniforms[] = {
				"uModernShadowAtlas",
				"uModernPointShadowAtlas",
				"uModernTranslucentShadowMoments[0]",
				"uModernPointTranslucentShadowMoments[0]",
				"uModernShadowResourceState",
				"uModernShadowSamplerState",
				"uModernShadowMomentState"
			};
			for ( int uniformIndex = 0; uniformIndex < static_cast<int>( sizeof( shadowBindingUniforms ) / sizeof( shadowBindingUniforms[0] ) ); ++uniformIndex ) {
				const GLint location = glGetUniformLocation( program->program, shadowBindingUniforms[uniformIndex] );
				if ( location < 0 ) {
					common->Printf( "RendererModernGLShaderLibrary self-test failed: shadow texture reflection mismatch for %s missing %s\n", program->name, shadowBindingUniforms[uniformIndex] );
					return false;
				}
			}
			if ( program->reflection.samplerCount < 4 || program->reflection.uniformCount < 4 ) {
				common->Printf( "RendererModernGLShaderLibrary self-test failed: shadow texture reflection records missing for %s\n", program->name );
				return false;
			}
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
		if ( program->reflection.usesDrawRecords && ( program->drawRecordModeLocation < 0 || program->drawRecordCountLocation < 0 || program->reflection.shaderStorageBlockCount <= 0 || program->reflection.attributeCount < 7 ) ) {
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
