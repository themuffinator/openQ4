// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_GL_SHADER_LIBRARY_H__
#define __MODERN_GL_SHADER_LIBRARY_H__

#include "RendererCaps.h"
#include "ScenePackets.h"
// MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY anchors the shadow texture
// units below, which must not overlap the material texture table's samplers.
#include "MaterialResourceTable.h"

enum modernGLShaderProgramKind_t {
	MODERN_GL_SHADER_DEPTH = 0,
	MODERN_GL_SHADER_SHADOW_DEPTH,
	MODERN_GL_SHADER_FLAT_MATERIAL,
	MODERN_GL_SHADER_LIGHT_GRID,
	MODERN_GL_SHADER_FOG_BLEND,
	MODERN_GL_SHADER_GBUFFER_OPAQUE,
	MODERN_GL_SHADER_GBUFFER_ALPHA_TEST,
	MODERN_GL_SHADER_DEFERRED_LIGHT_RESOLVE,
	MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE,
	MODERN_GL_SHADER_CLUSTERED_FORWARD_ALPHA_TEST,
	MODERN_GL_SHADER_TRANSPARENT_FORWARD,
	MODERN_GL_SHADER_GUI,
	MODERN_GL_SHADER_POST_COPY,
	MODERN_GL_SHADER_DEBUG_VISUALIZATION,
	MODERN_GL_SHADER_PROGRAM_KIND_COUNT
};

const int MODERN_GL_SHADER_MAX_PROGRAMS = 64;
const int MODERN_GL_SHADER_MAX_REFLECTION_RECORDS = 20;
// The shadow set must start above the material texture table, which claims
// units 0..MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY-1 as sampler2D (see
// the tableUnits[i] = i assignment in ModernGLShaderLibrary.cpp). Overlapping
// the two put samplerCube uniforms (uModernPointShadowAtlas, the point moment
// set) on the same units as sampler2D uMaterialTextures entries, and GL fails
// any draw whose active samplers disagree on a unit's type with
// GL_INVALID_OPERATION "State(s) are invalid: program texture usage" -- whether
// or not the shader ever samples them, and whether or not the executor skipped
// binding the table for that draw. Declaring both is enough to fail.
const int MODERN_GL_SHADOW_TEXTURE_UNIT_FIRST = MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY;
const int MODERN_GL_SHADOW_TEXTURE_UNIT_PROJECTED_ATLAS = MODERN_GL_SHADOW_TEXTURE_UNIT_FIRST;
const int MODERN_GL_SHADOW_TEXTURE_UNIT_POINT_ATLAS = MODERN_GL_SHADOW_TEXTURE_UNIT_FIRST + 1;
const int MODERN_GL_SHADOW_TEXTURE_UNIT_PROJECTED_MOMENTS = MODERN_GL_SHADOW_TEXTURE_UNIT_FIRST + 2;
const int MODERN_GL_SHADOW_TEXTURE_UNIT_POINT_MOMENTS = MODERN_GL_SHADOW_TEXTURE_UNIT_FIRST + 5;
const int MODERN_GL_SHADOW_TEXTURE_UNIT_COUNT = 8;
// shadow units occupy FIRST..FIRST+7; the shared per-light falloff/projection
// atlas takes the next free unit
const int MODERN_GL_LIGHT_IMAGE_ATLAS_TEXTURE_UNIT = MODERN_GL_SHADOW_TEXTURE_UNIT_FIRST + MODERN_GL_SHADOW_TEXTURE_UNIT_COUNT;

enum modernGLShaderResourceType_t {
	MODERN_GL_SHADER_RESOURCE_UNIFORM = 0,
	MODERN_GL_SHADER_RESOURCE_UNIFORM_BLOCK,
	MODERN_GL_SHADER_RESOURCE_SHADER_STORAGE_BLOCK,
	MODERN_GL_SHADER_RESOURCE_SAMPLER,
	MODERN_GL_SHADER_RESOURCE_IMAGE,
	MODERN_GL_SHADER_RESOURCE_ATTRIBUTE
};

typedef struct modernGLShaderResourceReflection_s {
	char	name[64];
	int		index;
	int		location;
	int		binding;
	int		size;
	unsigned int type;
	bool	required;
	bool	present;
} modernGLShaderResourceReflection_t;

typedef struct modernGLShaderReflection_s {
	int		frameBlockIndex;
	int		modelViewProjectionLocation;
	int		modelViewMatrixLocation;
	int		debugColorLocation;
	int		localParamsLocation;
	int		pbrIBLLocation;
	int							bakedGridLocation;
	int		mainTextureLocation;
	int		normalTextureLocation;
	int		specularTextureLocation;
	int		emissiveTextureLocation;
	int		metallicTextureLocation;
	int		roughnessTextureLocation;
	int		aoTextureLocation;
	int		textureIndicesLocation;
	int		textureTableModeLocation;
	int		materialTextureTableLocation;
	int		materialFlagsLocation;
	int		materialEnhancementLocation;
	int		drawRecordModeLocation;
	int		drawRecordCountLocation;
	int		sceneDepthTextureLocation;
	int		specularProbeAtlasLocation;
	int		specularProbeBlockIndex;
	int		positionAttribute;
	int		colorAttribute;
	int		texCoordAttribute;
	int		normalAttribute;
	int		tangentAttribute;
	int		bitangentAttribute;
	int		drawRecordAttribute;
	int		uniformCount;
	int		uniformBlockCount;
	int		shaderStorageBlockCount;
	int		samplerCount;
	int		imageCount;
	int		attributeCount;
	modernGLShaderResourceReflection_t uniforms[MODERN_GL_SHADER_MAX_REFLECTION_RECORDS];
	modernGLShaderResourceReflection_t uniformBlocks[MODERN_GL_SHADER_MAX_REFLECTION_RECORDS];
	modernGLShaderResourceReflection_t shaderStorageBlocks[MODERN_GL_SHADER_MAX_REFLECTION_RECORDS];
	modernGLShaderResourceReflection_t samplers[MODERN_GL_SHADER_MAX_REFLECTION_RECORDS];
	modernGLShaderResourceReflection_t images[MODERN_GL_SHADER_MAX_REFLECTION_RECORDS];
	modernGLShaderResourceReflection_t attributes[MODERN_GL_SHADER_MAX_REFLECTION_RECORDS];
	bool	usesFrameConstants;
	bool	usesModelViewProjection;
	bool	usesModelViewMatrix;
	bool	usesDebugColor;
	bool	usesLocalParams;
	bool	usesPBRIBL;
	bool	usesMainTexture;
	bool	usesMaterialTextures;
	bool	usesMaterialTextureTable;
	bool	usesMaterialFlags;
	bool	usesMaterialEnhancement;
	bool	usesDrawRecords;
	bool	usesSceneDepthTexture;
	bool	usesShadowTextures;
	bool	usesSpecularProbes;
	bool	usesTexCoord;
	bool	usesDrawVertColor;
	bool	usesDrawVertTangentSpace;
	bool	usesShaderStorage;
	bool	usesImage;
} modernGLShaderReflection_t;

typedef struct modernGLShaderProgramInfo_s {
	modernGLShaderProgramKind_t	kind;
	renderPassCategory_t		passCategory;
	rendererMaterialClass_t		materialClass;
	rendererPermutationKey_t		permutation;
	modernGLShaderReflection_t	reflection;
	int							glslVersion;
	unsigned int				program;
	int							frameBlockIndex;
	int							modelViewProjectionLocation;
	int							modelViewMatrixLocation;
	int							debugColorLocation;
	int							localParamsLocation;
	int							pbrIBLLocation;
	int							bakedGridLocation;
	int							mainTextureLocation;
	int							sceneDepthTextureLocation;
	int							normalTextureLocation;
	int							specularTextureLocation;
	int							emissiveTextureLocation;
	int							metallicTextureLocation;
	int							roughnessTextureLocation;
	int							aoTextureLocation;
	int							textureIndicesLocation;
	int							textureTableModeLocation;
	int							materialTextureTableLocation;
	int							materialFlagsLocation;
	int							materialEnhancementLocation;
	int							drawRecordModeLocation;
	int							drawRecordCountLocation;
	int							specularProbeAtlasLocation;
	int							specularProbeBlockIndex;
	bool						linked;
	char						name[64];
} modernGLShaderProgramInfo_t;

typedef struct modernGLShaderLibraryStats_s {
	bool	available;
	bool	initialized;
	bool	frameConstantsReady;
	bool	depthProgramReady;
	bool	shadowDepthProgramReady;
	bool	flatMaterialProgramReady;
	bool	lightGridProgramReady;
	bool	fogBlendProgramReady;
	bool	gbufferOpaqueProgramReady;
	bool	gbufferAlphaTestProgramReady;
	bool	deferredLightResolveProgramReady;
	bool	clusteredForwardOpaqueProgramReady;
	bool	clusteredForwardAlphaTestProgramReady;
	bool	transparentForwardProgramReady;
	bool	guiProgramReady;
	bool	postCopyProgramReady;
	bool	debugVisualizationProgramReady;
	int		programKindCount;
	int		readyProgramKindCount;
	int		programCount;
	int		permutationCount;
	int		failedProgramCount;
	int		textureProgramCount;
	int		reflectedUniformCount;
	int		reflectedUniformBlockCount;
	int		reflectedShaderStorageBlockCount;
	int		reflectedSamplerCount;
	int		reflectedImageCount;
	int		reflectedAttributeCount;
	int		validatedGLSLVersionCount;
	int		glsl330ProgramCount;
	int		glsl410ProgramCount;
	int		glsl430ProgramCount;
	int		glsl450ProgramCount;
	int		reloadCount;
	int		highestGLSLVersion;
	// Wall time of the last library build and its slowest program; a cold
	// driver shader cache makes this the bulk of GL renderer startup.
	int		compileMilliseconds;
	int		slowestProgramMilliseconds;
	char	slowestProgramName[64];
	// Kinds that failed at the drawn (highest) tier and linked one tier lower.
	int		fallbackProgramCount;
	char	status[96];
} modernGLShaderLibraryStats_t;

const char *ModernGLShaderProgramKind_Name( modernGLShaderProgramKind_t kind );
void R_ModernGLShaderLibrary_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernGLShaderLibrary_Shutdown( void );
bool R_ModernGLShaderLibrary_Reload( void );
const modernGLShaderLibraryStats_t &R_ModernGLShaderLibrary_Stats( void );
const modernGLShaderProgramInfo_t *R_ModernGLShaderLibrary_FindProgram( modernGLShaderProgramKind_t kind, int preferredGLSLVersion );
void R_ModernGLShaderLibrary_PrintGfxInfo( void );
bool RendererModernGLShaderLibrary_RunSelfTest( void );

#endif /* !__MODERN_GL_SHADER_LIBRARY_H__ */
