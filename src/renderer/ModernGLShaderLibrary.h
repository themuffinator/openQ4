// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_GL_SHADER_LIBRARY_H__
#define __MODERN_GL_SHADER_LIBRARY_H__

#include "RendererCaps.h"
#include "ScenePackets.h"

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
const int MODERN_GL_SHADER_MAX_REFLECTION_RECORDS = 8;

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
	int		mainTextureLocation;
	int		positionAttribute;
	int		colorAttribute;
	int		texCoordAttribute;
	int		normalAttribute;
	int		tangentAttribute;
	int		bitangentAttribute;
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
	bool	usesMainTexture;
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
	int							mainTextureLocation;
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
