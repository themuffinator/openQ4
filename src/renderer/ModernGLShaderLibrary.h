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
	MODERN_GL_SHADER_GUI,
	MODERN_GL_SHADER_POST_COPY,
	MODERN_GL_SHADER_PROGRAM_KIND_COUNT
};

const int MODERN_GL_SHADER_MAX_PROGRAMS = 32;

typedef struct modernGLShaderReflection_s {
	int		frameBlockIndex;
	int		modelViewProjectionLocation;
	int		debugColorLocation;
	int		localParamsLocation;
	int		mainTextureLocation;
	int		positionAttribute;
	int		texCoordAttribute;
	bool	usesFrameConstants;
	bool	usesModelViewProjection;
	bool	usesDebugColor;
	bool	usesLocalParams;
	bool	usesMainTexture;
	bool	usesTexCoord;
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
	bool	guiProgramReady;
	bool	postCopyProgramReady;
	int		programKindCount;
	int		readyProgramKindCount;
	int		programCount;
	int		permutationCount;
	int		failedProgramCount;
	int		textureProgramCount;
	int		reflectedUniformCount;
	int		reflectedSamplerCount;
	int		highestGLSLVersion;
	char	status[96];
} modernGLShaderLibraryStats_t;

const char *ModernGLShaderProgramKind_Name( modernGLShaderProgramKind_t kind );
void R_ModernGLShaderLibrary_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernGLShaderLibrary_Shutdown( void );
const modernGLShaderLibraryStats_t &R_ModernGLShaderLibrary_Stats( void );
const modernGLShaderProgramInfo_t *R_ModernGLShaderLibrary_FindProgram( modernGLShaderProgramKind_t kind, int preferredGLSLVersion );
void R_ModernGLShaderLibrary_PrintGfxInfo( void );
bool RendererModernGLShaderLibrary_RunSelfTest( void );

#endif /* !__MODERN_GL_SHADER_LIBRARY_H__ */
