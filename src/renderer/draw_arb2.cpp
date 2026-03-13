/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "tr_local.h"

#include "cg_explicit.h"
#include <ctype.h>

static ID_INLINE GLint RB_ShadowMapSafeStencilClearValue() {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

CGcontext cg_context;

typedef enum {
	ICM_PACKED,
	ICM_VECTOR
} interactionColorMode_t;

static interactionColorMode_t g_interactionVertexProgramAutoColorMode = ICM_PACKED;
static interactionColorMode_t g_interactionVertexProgramColorMode = ICM_PACKED;
static int g_interactionVertexProgramOverride = 0;

static const char *RB_InteractionColorModeName( interactionColorMode_t mode ) {
	switch ( mode ) {
	case ICM_PACKED:
		return "packed env16.xy";
	case ICM_VECTOR:
		return "vector env16/env17";
	default:
		return "unknown";
	}
}

static const char *RB_InteractionColorOverrideName( int modeOverride ) {
	switch ( modeOverride ) {
	case 0:
		return "auto";
	case 1:
		return "packed";
	case 2:
		return "vector";
	default:
		return "invalid";
	}
}

static void RB_StripARBProgramCommentsAndWhitespace( const char *source, idStr &normalizedSource ) {
	normalizedSource.Empty();
	if ( source == NULL ) {
		return;
	}

	bool inComment = false;
	for ( const char *p = source; *p != '\0'; ++p ) {
		const char c = *p;
		if ( c == '\r' || c == '\n' ) {
			inComment = false;
			continue;
		}

		if ( inComment ) {
			continue;
		}

		if ( c == '#' ) {
			inComment = true;
			continue;
		}

		if ( c == ' ' || c == '\t' ) {
			continue;
		}

		normalizedSource.Append( (char)tolower( (unsigned char)c ) );
	}
}

static bool RB_DetectInteractionColorMode( const char *programSource, interactionColorMode_t &modeOut ) {
	idStr normalizedProgram;
	RB_StripARBProgramCommentsAndWhitespace( programSource, normalizedProgram );
	const char *normalized = normalizedProgram.c_str();

	if ( strstr( normalized, "madresult.color,vertex.color,program.env[16].x,program.env[16].y;" ) != NULL ) {
		modeOut = ICM_PACKED;
		return true;
	}

	if ( strstr( normalized, "madresult.color,vertex.color,program.env[16],program.env[17];" ) != NULL ) {
		modeOut = ICM_VECTOR;
		return true;
	}

	const bool packedHints = strstr( normalized, "program.env[16].x" ) != NULL &&
		strstr( normalized, "program.env[16].y" ) != NULL;
	const bool usesEnv17 = strstr( normalized, "program.env[17]" ) != NULL;

	if ( packedHints && !usesEnv17 ) {
		modeOut = ICM_PACKED;
		return true;
	}

	if ( usesEnv17 ) {
		modeOut = ICM_VECTOR;
		return true;
	}

	return false;
}

static void RB_UpdateInteractionColorMode( bool forcePrint ) {
	const int modeOverride = idMath::ClampInt( 0, 2, r_interactionColorMode.GetInteger() );
	interactionColorMode_t requestedMode = g_interactionVertexProgramAutoColorMode;
	interactionColorMode_t effectiveMode = g_interactionVertexProgramAutoColorMode;

	if ( modeOverride == 1 ) {
		requestedMode = ICM_PACKED;
	} else if ( modeOverride == 2 ) {
		requestedMode = ICM_VECTOR;
	}

	if ( modeOverride != 0 && requestedMode != g_interactionVertexProgramAutoColorMode ) {
		effectiveMode = g_interactionVertexProgramAutoColorMode;
		if ( forcePrint ||
			modeOverride != g_interactionVertexProgramOverride ||
			effectiveMode != g_interactionVertexProgramColorMode ) {
			common->Warning( "r_interactionColorMode=%d (%s) is incompatible with interaction.vfp (%s); forcing compatible mode",
				modeOverride,
				RB_InteractionColorModeName( requestedMode ),
				RB_InteractionColorModeName( g_interactionVertexProgramAutoColorMode ) );
		}
	} else {
		effectiveMode = requestedMode;
	}

	const bool changed = effectiveMode != g_interactionVertexProgramColorMode ||
		modeOverride != g_interactionVertexProgramOverride;

	g_interactionVertexProgramColorMode = effectiveMode;
	g_interactionVertexProgramOverride = modeOverride;

	if ( forcePrint || changed ) {
		common->Printf( ": interaction color mode = %s (auto=%s, override=%s)\n",
			RB_InteractionColorModeName( g_interactionVertexProgramColorMode ),
			RB_InteractionColorModeName( g_interactionVertexProgramAutoColorMode ),
			RB_InteractionColorOverrideName( g_interactionVertexProgramOverride ) );
	}
}

static void cg_error_callback( void ) {
	CGerror i = cgGetError();
	common->Printf( "Cg error (%d): %s\n", i, cgGetErrorString(i) );
}

/*
=========================================================================================

GENERAL INTERACTION RENDERING

=========================================================================================
*/

/*
====================
GL_SelectTextureNoClient
====================
*/
void GL_SelectTextureNoClient( int unit ) {
	backEnd.glState.currenttmu = unit;
	glActiveTextureARB( GL_TEXTURE0_ARB + unit );
	RB_LogComment( "glActiveTextureARB( %i )\n", unit );
}

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			localLightOrigin;
	GLint			localViewOrigin;
	GLint			lightProjectionS;
	GLint			lightProjectionT;
	GLint			lightProjectionQ;
	GLint			lightFalloffS;
	GLint			bumpMatrixS;
	GLint			bumpMatrixT;
	GLint			diffuseMatrixS;
	GLint			diffuseMatrixT;
	GLint			specularMatrixS;
	GLint			specularMatrixT;
	GLint			shadowRow[4];
	GLint			vertexColorParams;
	GLint			diffuseColor;
	GLint			specularColor;
	GLint			shadowTexelSize;
	GLint			shadowBias;
	GLint			shadowFilterRadius;

	GLint			bumpMap;
	GLint			lightFalloffMap;
	GLint			lightProjectionMap;
	GLint			diffuseMap;
	GLint			specularMap;
	GLint			shadowMap;
} shadowMapProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			localLightOrigin;
	GLint			localViewOrigin;
	GLint			lightProjectionS;
	GLint			lightProjectionT;
	GLint			lightProjectionQ;
	GLint			lightFalloffS;
	GLint			bumpMatrixS;
	GLint			bumpMatrixT;
	GLint			diffuseMatrixS;
	GLint			diffuseMatrixT;
	GLint			specularMatrixS;
	GLint			specularMatrixT;
	GLint			modelMatrixRow0;
	GLint			modelMatrixRow1;
	GLint			modelMatrixRow2;
	GLint			globalLightOrigin;
	GLint			pointShadowFar;
	GLint			vertexColorParams;
	GLint			diffuseColor;
	GLint			specularColor;
	GLint			shadowBias;
	GLint			shadowFilterRadius;
	GLint			pointShadowTexelScale;

	GLint			bumpMap;
	GLint			lightFalloffMap;
	GLint			lightProjectionMap;
	GLint			diffuseMap;
	GLint			specularMap;
	GLint			pointShadowMap;
} pointShadowMapProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			modelMatrixRow0;
	GLint			modelMatrixRow1;
	GLint			modelMatrixRow2;
	GLint			globalLightOrigin;
	GLint			pointShadowFar;
} pointShadowCasterProgram_t;

static shadowMapProgram_t	g_shadowMapProgram = { 0 };
static pointShadowMapProgram_t	g_pointShadowMapProgram = { 0 };
static pointShadowCasterProgram_t	g_pointShadowCasterProgram = { 0 };
static idImage *			g_shadowMapDepthImage = NULL;
static idRenderTexture *	g_shadowMapRenderTexture = NULL;
static idImage *			g_pointShadowMapColorImage = NULL;
static idImage *			g_pointShadowMapDepthImage = NULL;
static idRenderTexture *	g_pointShadowMapRenderTexture = NULL;

typedef enum {
	SHADOWMAP_SUPPORT_OK = 0,
	SHADOWMAP_SUPPORT_DISABLED,
	SHADOWMAP_SUPPORT_SHADOWS_DISABLED,
	SHADOWMAP_SUPPORT_NULL_LIGHT,
	SHADOWMAP_SUPPORT_NO_SHADOWS_FLAG,
	SHADOWMAP_SUPPORT_AMBIENT_LIGHT,
	SHADOWMAP_SUPPORT_TEXTURE_LIMIT,
	SHADOWMAP_SUPPORT_NO_INTERACTIONS,
	SHADOWMAP_SUPPORT_CUBEMAP_UNAVAILABLE,
	SHADOWMAP_SUPPORT_RESOURCE_FAILURE,
	SHADOWMAP_SUPPORT_COUNT
} shadowMapLightSupportReason_t;

typedef enum {
	SHADOWMAP_PASS_LOCAL = 0,
	SHADOWMAP_PASS_GLOBAL
} shadowMapPassKind_t;

typedef struct {
	int					totalLights;
	int					supportedLights;
	int					pointLights;
	int					projectedLights;
	int					parallelLights;
	int					mappedLocalPasses;
	int					mappedGlobalPasses;
	int					unshadowedLocalPasses;
	int					unshadowedGlobalPasses;
	int					fallbackLocalPasses;
	int					fallbackGlobalPasses;
	int					renderFailLocalPasses;
	int					renderFailGlobalPasses;
	int					maskFailLocalPasses;
	int					maskFailGlobalPasses;
	int					unsupportedLights[SHADOWMAP_SUPPORT_COUNT];
} shadowMapStats_t;

static shadowMapStats_t	g_shadowMapStats;
static int				g_shadowMapLastReportFrame = -0x3fffffff;
static bool				g_shadowMapReportThisFrame = false;

typedef enum {
	SHADOWMAP_PASS_RESULT_MAPPED = 0,
	SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS,
	SHADOWMAP_PASS_RESULT_RENDER_FAIL,
	SHADOWMAP_PASS_RESULT_MASK_FAIL
} shadowMapPassResult_t;

void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf );

static const char *RB_ShadowMapSupportReasonName( shadowMapLightSupportReason_t reason ) {
	switch ( reason ) {
	case SHADOWMAP_SUPPORT_OK:
		return "ok";
	case SHADOWMAP_SUPPORT_DISABLED:
		return "disabled";
	case SHADOWMAP_SUPPORT_SHADOWS_DISABLED:
		return "shadows-disabled";
	case SHADOWMAP_SUPPORT_NULL_LIGHT:
		return "null-light";
	case SHADOWMAP_SUPPORT_NO_SHADOWS_FLAG:
		return "noShadows-flag";
	case SHADOWMAP_SUPPORT_AMBIENT_LIGHT:
		return "ambient-light";
	case SHADOWMAP_SUPPORT_TEXTURE_LIMIT:
		return "texture-limit";
	case SHADOWMAP_SUPPORT_NO_INTERACTIONS:
		return "no-interactions";
	case SHADOWMAP_SUPPORT_CUBEMAP_UNAVAILABLE:
		return "cubemap-unavailable";
	case SHADOWMAP_SUPPORT_RESOURCE_FAILURE:
		return "resource-failure";
	default:
		return "unknown";
	}
}

static const char *RB_ShadowMapPassName( shadowMapPassKind_t passKind ) {
	return ( passKind == SHADOWMAP_PASS_LOCAL ) ? "local" : "global";
}

static const char *RB_ShadowMapPassResultName( shadowMapPassResult_t result ) {
	switch ( result ) {
	case SHADOWMAP_PASS_RESULT_MAPPED:
		return "mapped";
	case SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS:
		return "no-shadow-surfs";
	case SHADOWMAP_PASS_RESULT_RENDER_FAIL:
		return "render-fail";
	case SHADOWMAP_PASS_RESULT_MASK_FAIL:
		return "mask-fail";
	default:
		return "unknown";
	}
}

static int RB_CountDrawSurfChain( const drawSurf_t *surf ) {
	int count = 0;
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		count++;
	}
	return count;
}

static bool RB_ShadowMapShouldReport( void ) {
	if ( !r_useShadowMap.GetBool() ) {
		return false;
	}
	if ( backEnd.viewDef == NULL ) {
		return false;
	}
	if ( backEnd.viewDef->renderWorld == NULL || backEnd.viewDef->viewEntitys == NULL || backEnd.viewDef->viewLights == NULL ) {
		return false;
	}
	if ( backEnd.viewDef->isSubview || backEnd.viewDef->superView != NULL ) {
		return false;
	}
	return true;
}

static void RB_ShadowMapFreeProgram( void ) {
	if ( g_shadowMapProgram.programObject != 0 ) {
		if ( g_shadowMapProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_shadowMapProgram.programObject, g_shadowMapProgram.vertexShaderObject );
			glDeleteObjectARB( g_shadowMapProgram.vertexShaderObject );
		}
		if ( g_shadowMapProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_shadowMapProgram.programObject, g_shadowMapProgram.fragmentShaderObject );
			glDeleteObjectARB( g_shadowMapProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_shadowMapProgram.programObject );
	}

	memset( &g_shadowMapProgram, 0, sizeof( g_shadowMapProgram ) );
}

static void RB_PointShadowMapFreeProgram( void ) {
	if ( g_pointShadowMapProgram.programObject != 0 ) {
		if ( g_pointShadowMapProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_pointShadowMapProgram.programObject, g_pointShadowMapProgram.vertexShaderObject );
			glDeleteObjectARB( g_pointShadowMapProgram.vertexShaderObject );
		}
		if ( g_pointShadowMapProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_pointShadowMapProgram.programObject, g_pointShadowMapProgram.fragmentShaderObject );
			glDeleteObjectARB( g_pointShadowMapProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_pointShadowMapProgram.programObject );
	}

	memset( &g_pointShadowMapProgram, 0, sizeof( g_pointShadowMapProgram ) );
}

static void RB_PointShadowMapFreeCasterProgram( void ) {
	if ( g_pointShadowCasterProgram.programObject != 0 ) {
		if ( g_pointShadowCasterProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_pointShadowCasterProgram.programObject, g_pointShadowCasterProgram.vertexShaderObject );
			glDeleteObjectARB( g_pointShadowCasterProgram.vertexShaderObject );
		}
		if ( g_pointShadowCasterProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_pointShadowCasterProgram.programObject, g_pointShadowCasterProgram.fragmentShaderObject );
			glDeleteObjectARB( g_pointShadowCasterProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_pointShadowCasterProgram.programObject );
	}

	memset( &g_pointShadowCasterProgram, 0, sizeof( g_pointShadowCasterProgram ) );
}

static void RB_ShadowMapPrintInfoLog( GLhandleARB object, const char *label, const char *name ) {
	GLint logLength = 0;
	glGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &logLength );
	if ( logLength <= 1 ) {
		common->Warning( "GLSL %s error in '%s' (no info log)", label, name );
		return;
	}

	char *logBuffer = (char *)_alloca( logLength );
	GLsizei written = 0;
	glGetInfoLogARB( object, logLength, &written, logBuffer );
	common->Warning( "GLSL %s error in '%s':\n%s", label, name, logBuffer );
}

static bool RB_ShadowMapLoadProgram( void ) {
	static const char *programBaseName = "glprogs/openq4_shadow_interaction";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	if ( g_shadowMapProgram.programObject != 0 && g_shadowMapProgram.programGeneration == tr.videoRestartCount ) {
		return g_shadowMapProgram.programValid;
	}

	RB_ShadowMapFreeProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load shadow-map GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_shadowMapProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_shadowMapProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_shadowMapProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glBindAttribLocationARB( programObject, 8, "attr_TexCoord0" );
	glBindAttribLocationARB( programObject, 9, "attr_Tangent" );
	glBindAttribLocationARB( programObject, 10, "attr_Bitangent" );
	glBindAttribLocationARB( programObject, 11, "attr_Normal" );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_shadowMapProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapProgram.programValid = false;
		return false;
	}

	g_shadowMapProgram.programObject = programObject;
	g_shadowMapProgram.vertexShaderObject = vertexShader;
	g_shadowMapProgram.fragmentShaderObject = fragmentShader;
	g_shadowMapProgram.programGeneration = tr.videoRestartCount;
	g_shadowMapProgram.programValid = true;

	g_shadowMapProgram.localLightOrigin = glGetUniformLocationARB( programObject, "uLocalLightOrigin" );
	g_shadowMapProgram.localViewOrigin = glGetUniformLocationARB( programObject, "uLocalViewOrigin" );
	g_shadowMapProgram.lightProjectionS = glGetUniformLocationARB( programObject, "uLightProjectionS" );
	g_shadowMapProgram.lightProjectionT = glGetUniformLocationARB( programObject, "uLightProjectionT" );
	g_shadowMapProgram.lightProjectionQ = glGetUniformLocationARB( programObject, "uLightProjectionQ" );
	g_shadowMapProgram.lightFalloffS = glGetUniformLocationARB( programObject, "uLightFalloffS" );
	g_shadowMapProgram.bumpMatrixS = glGetUniformLocationARB( programObject, "uBumpMatrixS" );
	g_shadowMapProgram.bumpMatrixT = glGetUniformLocationARB( programObject, "uBumpMatrixT" );
	g_shadowMapProgram.diffuseMatrixS = glGetUniformLocationARB( programObject, "uDiffuseMatrixS" );
	g_shadowMapProgram.diffuseMatrixT = glGetUniformLocationARB( programObject, "uDiffuseMatrixT" );
	g_shadowMapProgram.specularMatrixS = glGetUniformLocationARB( programObject, "uSpecularMatrixS" );
	g_shadowMapProgram.specularMatrixT = glGetUniformLocationARB( programObject, "uSpecularMatrixT" );
	g_shadowMapProgram.shadowRow[0] = glGetUniformLocationARB( programObject, "uShadowRow0" );
	g_shadowMapProgram.shadowRow[1] = glGetUniformLocationARB( programObject, "uShadowRow1" );
	g_shadowMapProgram.shadowRow[2] = glGetUniformLocationARB( programObject, "uShadowRow2" );
	g_shadowMapProgram.shadowRow[3] = glGetUniformLocationARB( programObject, "uShadowRow3" );
	g_shadowMapProgram.vertexColorParams = glGetUniformLocationARB( programObject, "uVertexColorParams" );
	g_shadowMapProgram.diffuseColor = glGetUniformLocationARB( programObject, "uDiffuseColor" );
	g_shadowMapProgram.specularColor = glGetUniformLocationARB( programObject, "uSpecularColor" );
	g_shadowMapProgram.shadowTexelSize = glGetUniformLocationARB( programObject, "uShadowTexelSize" );
	g_shadowMapProgram.shadowBias = glGetUniformLocationARB( programObject, "uShadowBias" );
	g_shadowMapProgram.shadowFilterRadius = glGetUniformLocationARB( programObject, "uShadowFilterRadius" );
	g_shadowMapProgram.bumpMap = glGetUniformLocationARB( programObject, "uBumpMap" );
	g_shadowMapProgram.lightFalloffMap = glGetUniformLocationARB( programObject, "uLightFalloffMap" );
	g_shadowMapProgram.lightProjectionMap = glGetUniformLocationARB( programObject, "uLightProjectionMap" );
	g_shadowMapProgram.diffuseMap = glGetUniformLocationARB( programObject, "uDiffuseMap" );
	g_shadowMapProgram.specularMap = glGetUniformLocationARB( programObject, "uSpecularMap" );
	g_shadowMapProgram.shadowMap = glGetUniformLocationARB( programObject, "uShadowMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_PointShadowMapLoadProgram( void ) {
	static const char *programBaseName = "glprogs/openq4_shadow_point_interaction";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	if ( g_pointShadowMapProgram.programObject != 0 && g_pointShadowMapProgram.programGeneration == tr.videoRestartCount ) {
		return g_pointShadowMapProgram.programValid;
	}

	RB_PointShadowMapFreeProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load point shadow-map GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowMapProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowMapProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowMapProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glBindAttribLocationARB( programObject, 8, "attr_TexCoord0" );
	glBindAttribLocationARB( programObject, 9, "attr_Tangent" );
	glBindAttribLocationARB( programObject, 10, "attr_Bitangent" );
	glBindAttribLocationARB( programObject, 11, "attr_Normal" );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowMapProgram.programValid = false;
		return false;
	}

	g_pointShadowMapProgram.programObject = programObject;
	g_pointShadowMapProgram.vertexShaderObject = vertexShader;
	g_pointShadowMapProgram.fragmentShaderObject = fragmentShader;
	g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
	g_pointShadowMapProgram.programValid = true;

	g_pointShadowMapProgram.localLightOrigin = glGetUniformLocationARB( programObject, "uLocalLightOrigin" );
	g_pointShadowMapProgram.localViewOrigin = glGetUniformLocationARB( programObject, "uLocalViewOrigin" );
	g_pointShadowMapProgram.lightProjectionS = glGetUniformLocationARB( programObject, "uLightProjectionS" );
	g_pointShadowMapProgram.lightProjectionT = glGetUniformLocationARB( programObject, "uLightProjectionT" );
	g_pointShadowMapProgram.lightProjectionQ = glGetUniformLocationARB( programObject, "uLightProjectionQ" );
	g_pointShadowMapProgram.lightFalloffS = glGetUniformLocationARB( programObject, "uLightFalloffS" );
	g_pointShadowMapProgram.bumpMatrixS = glGetUniformLocationARB( programObject, "uBumpMatrixS" );
	g_pointShadowMapProgram.bumpMatrixT = glGetUniformLocationARB( programObject, "uBumpMatrixT" );
	g_pointShadowMapProgram.diffuseMatrixS = glGetUniformLocationARB( programObject, "uDiffuseMatrixS" );
	g_pointShadowMapProgram.diffuseMatrixT = glGetUniformLocationARB( programObject, "uDiffuseMatrixT" );
	g_pointShadowMapProgram.specularMatrixS = glGetUniformLocationARB( programObject, "uSpecularMatrixS" );
	g_pointShadowMapProgram.specularMatrixT = glGetUniformLocationARB( programObject, "uSpecularMatrixT" );
	g_pointShadowMapProgram.modelMatrixRow0 = glGetUniformLocationARB( programObject, "uModelMatrixRow0" );
	g_pointShadowMapProgram.modelMatrixRow1 = glGetUniformLocationARB( programObject, "uModelMatrixRow1" );
	g_pointShadowMapProgram.modelMatrixRow2 = glGetUniformLocationARB( programObject, "uModelMatrixRow2" );
	g_pointShadowMapProgram.globalLightOrigin = glGetUniformLocationARB( programObject, "uGlobalLightOrigin" );
	g_pointShadowMapProgram.pointShadowFar = glGetUniformLocationARB( programObject, "uPointShadowFar" );
	g_pointShadowMapProgram.vertexColorParams = glGetUniformLocationARB( programObject, "uVertexColorParams" );
	g_pointShadowMapProgram.diffuseColor = glGetUniformLocationARB( programObject, "uDiffuseColor" );
	g_pointShadowMapProgram.specularColor = glGetUniformLocationARB( programObject, "uSpecularColor" );
	g_pointShadowMapProgram.shadowBias = glGetUniformLocationARB( programObject, "uShadowBias" );
	g_pointShadowMapProgram.shadowFilterRadius = glGetUniformLocationARB( programObject, "uShadowFilterRadius" );
	g_pointShadowMapProgram.pointShadowTexelScale = glGetUniformLocationARB( programObject, "uPointShadowTexelScale" );
	g_pointShadowMapProgram.bumpMap = glGetUniformLocationARB( programObject, "uBumpMap" );
	g_pointShadowMapProgram.lightFalloffMap = glGetUniformLocationARB( programObject, "uLightFalloffMap" );
	g_pointShadowMapProgram.lightProjectionMap = glGetUniformLocationARB( programObject, "uLightProjectionMap" );
	g_pointShadowMapProgram.diffuseMap = glGetUniformLocationARB( programObject, "uDiffuseMap" );
	g_pointShadowMapProgram.specularMap = glGetUniformLocationARB( programObject, "uSpecularMap" );
	g_pointShadowMapProgram.pointShadowMap = glGetUniformLocationARB( programObject, "uPointShadowMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_PointShadowMapLoadCasterProgram( void ) {
	static const char *programBaseName = "glprogs/openq4_shadow_point_caster";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	if ( g_pointShadowCasterProgram.programObject != 0 && g_pointShadowCasterProgram.programGeneration == tr.videoRestartCount ) {
		return g_pointShadowCasterProgram.programValid;
	}

	RB_PointShadowMapFreeCasterProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load point shadow caster GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowCasterProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowCasterProgram.programValid = false;
		return false;
	}

	g_pointShadowCasterProgram.programObject = programObject;
	g_pointShadowCasterProgram.vertexShaderObject = vertexShader;
	g_pointShadowCasterProgram.fragmentShaderObject = fragmentShader;
	g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
	g_pointShadowCasterProgram.programValid = true;

	g_pointShadowCasterProgram.modelMatrixRow0 = glGetUniformLocationARB( programObject, "uModelMatrixRow0" );
	g_pointShadowCasterProgram.modelMatrixRow1 = glGetUniformLocationARB( programObject, "uModelMatrixRow1" );
	g_pointShadowCasterProgram.modelMatrixRow2 = glGetUniformLocationARB( programObject, "uModelMatrixRow2" );
	g_pointShadowCasterProgram.globalLightOrigin = glGetUniformLocationARB( programObject, "uGlobalLightOrigin" );
	g_pointShadowCasterProgram.pointShadowFar = glGetUniformLocationARB( programObject, "uPointShadowFar" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static void RB_ShadowMapBuildClipPlanes( const idPlane lightProject[4], idPlane clipPlanes[4] ) {
	for ( int i = 0; i < 4; i++ ) {
		clipPlanes[0][i] = lightProject[0][i] * 2.0f - lightProject[2][i];
		clipPlanes[1][i] = lightProject[1][i] * 2.0f - lightProject[2][i];
		clipPlanes[2][i] = lightProject[3][i] * 2.0f - lightProject[2][i];
		clipPlanes[3][i] = lightProject[2][i];
	}
}

static void RB_ShadowMapClipPlanesToGLMatrix( const idPlane clipPlanes[4], float matrix[16] ) {
	matrix[0] = clipPlanes[0][0];
	matrix[4] = clipPlanes[0][1];
	matrix[8] = clipPlanes[0][2];
	matrix[12] = clipPlanes[0][3];

	matrix[1] = clipPlanes[1][0];
	matrix[5] = clipPlanes[1][1];
	matrix[9] = clipPlanes[1][2];
	matrix[13] = clipPlanes[1][3];

	matrix[2] = clipPlanes[2][0];
	matrix[6] = clipPlanes[2][1];
	matrix[10] = clipPlanes[2][2];
	matrix[14] = clipPlanes[2][3];

	matrix[3] = clipPlanes[3][0];
	matrix[7] = clipPlanes[3][1];
	matrix[11] = clipPlanes[3][2];
	matrix[15] = clipPlanes[3][3];
}

static bool RB_ShadowMapEnsureResources( void ) {
	if ( !RB_ShadowMapLoadProgram() ) {
		return false;
	}

	const int shadowMapSize = idMath::ClampInt( 128, 4096, r_shadowMapSize.GetInteger() );

	if ( g_shadowMapDepthImage == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_DEPTH;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		g_shadowMapDepthImage = globalImages->ScratchImage( "_shadowMapDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	}

	if ( g_shadowMapRenderTexture == NULL ) {
		g_shadowMapRenderTexture = tr.CreateRenderTexture( NULL, g_shadowMapDepthImage );
	} else if ( g_shadowMapRenderTexture->GetWidth() != shadowMapSize || g_shadowMapRenderTexture->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( g_shadowMapRenderTexture, shadowMapSize, shadowMapSize );
	}

	if ( g_shadowMapDepthImage == NULL || g_shadowMapRenderTexture == NULL ) {
		return false;
	}

	GL_SelectTextureNoClient( 5 );
	g_shadowMapDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );
	backEnd.glState.currenttmu = -1;

	return true;
}

static bool RB_PointShadowMapEnsureResources( void ) {
	if ( !glConfig.cubeMapAvailable ) {
		return false;
	}
	if ( !RB_PointShadowMapLoadProgram() || !RB_PointShadowMapLoadCasterProgram() ) {
		return false;
	}

	const int shadowMapSize = idMath::ClampInt( 128, 2048, r_shadowMapSize.GetInteger() );

	if ( g_pointShadowMapColorImage == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_CUBIC;
		opts.format = FMT_RGBA8;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		g_pointShadowMapColorImage = globalImages->ScratchImage( "_pointShadowMapColor", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	}

	if ( g_pointShadowMapDepthImage == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_CUBIC;
		opts.format = FMT_DEPTH;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		g_pointShadowMapDepthImage = globalImages->ScratchImage( "_pointShadowMapDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	}

	if ( g_pointShadowMapRenderTexture == NULL ) {
		g_pointShadowMapRenderTexture = tr.CreateRenderTexture( g_pointShadowMapColorImage, g_pointShadowMapDepthImage );
	} else if ( g_pointShadowMapRenderTexture->GetWidth() != shadowMapSize || g_pointShadowMapRenderTexture->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( g_pointShadowMapRenderTexture, shadowMapSize, shadowMapSize );
	}

	return g_pointShadowMapColorImage != NULL && g_pointShadowMapDepthImage != NULL && g_pointShadowMapRenderTexture != NULL;
}

static shadowMapLightSupportReason_t RB_ShadowMapLightSupportReason( const viewLight_t *vLight ) {
	if ( !r_useShadowMap.GetBool() || !r_shadows.GetBool() ) {
		return !r_useShadowMap.GetBool() ? SHADOWMAP_SUPPORT_DISABLED : SHADOWMAP_SUPPORT_SHADOWS_DISABLED;
	}
	if ( vLight == NULL ) {
		return SHADOWMAP_SUPPORT_NULL_LIGHT;
	}
	if ( vLight->lightDef != NULL && vLight->lightDef->parms.noShadows ) {
		return SHADOWMAP_SUPPORT_NO_SHADOWS_FLAG;
	}
	if ( vLight->lightShader == NULL || vLight->lightShader->IsAmbientLight() ) {
		return SHADOWMAP_SUPPORT_AMBIENT_LIGHT;
	}
	if ( glConfig.maxTextureUnits < 6 || glConfig.maxTextureImageUnits < 6 ) {
		return SHADOWMAP_SUPPORT_TEXTURE_LIMIT;
	}
	if ( vLight->globalInteractions == NULL && vLight->localInteractions == NULL ) {
		return SHADOWMAP_SUPPORT_NO_INTERACTIONS;
	}
	if ( vLight->pointLight ) {
		if ( !glConfig.cubeMapAvailable ) {
			return SHADOWMAP_SUPPORT_CUBEMAP_UNAVAILABLE;
		}
		return RB_PointShadowMapEnsureResources() ? SHADOWMAP_SUPPORT_OK : SHADOWMAP_SUPPORT_RESOURCE_FAILURE;
	}
	return RB_ShadowMapEnsureResources() ? SHADOWMAP_SUPPORT_OK : SHADOWMAP_SUPPORT_RESOURCE_FAILURE;
}

static void RB_ShadowMapStatsReset( void ) {
	memset( &g_shadowMapStats, 0, sizeof( g_shadowMapStats ) );
	g_shadowMapReportThisFrame = false;

	if ( !RB_ShadowMapShouldReport() ) {
		return;
	}

	const int reportLevel = idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() );
	if ( reportLevel <= 0 ) {
		return;
	}

	const int reportInterval = Max( 1, r_shadowMapReportInterval.GetInteger() );
	if ( tr.frameCount - g_shadowMapLastReportFrame < reportInterval ) {
		return;
	}

	g_shadowMapLastReportFrame = tr.frameCount;
	g_shadowMapReportThisFrame = true;
}

static void RB_ShadowMapStatsReport( void ) {
	const int reportLevel = idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() );
	if ( reportLevel <= 0 || !g_shadowMapReportThisFrame || g_shadowMapStats.totalLights <= 0 ) {
		return;
	}

	idStr unsupportedSummary;
	for ( int i = 0; i < SHADOWMAP_SUPPORT_COUNT; i++ ) {
		if ( g_shadowMapStats.unsupportedLights[i] <= 0 ) {
			continue;
		}
		if ( unsupportedSummary.Length() > 0 ) {
			unsupportedSummary += ", ";
		}
		unsupportedSummary += va( "%s=%d", RB_ShadowMapSupportReasonName( static_cast<shadowMapLightSupportReason_t>( i ) ), g_shadowMapStats.unsupportedLights[i] );
	}

	if ( unsupportedSummary.Length() > 0 ) {
		common->Printf(
			"SM summary: lights=%d supported=%d point=%d projected=%d parallel=%d mapped(local=%d global=%d) unshadowed(local=%d global=%d) fallback(local=%d global=%d) unsupported[%s]\n",
			g_shadowMapStats.totalLights,
			g_shadowMapStats.supportedLights,
			g_shadowMapStats.pointLights,
			g_shadowMapStats.projectedLights,
			g_shadowMapStats.parallelLights,
			g_shadowMapStats.mappedLocalPasses,
			g_shadowMapStats.mappedGlobalPasses,
			g_shadowMapStats.unshadowedLocalPasses,
			g_shadowMapStats.unshadowedGlobalPasses,
			g_shadowMapStats.fallbackLocalPasses,
			g_shadowMapStats.fallbackGlobalPasses,
			unsupportedSummary.c_str() );
		common->Printf(
			"SM detail: failures(render local=%d global=%d, mask local=%d global=%d)\n",
			g_shadowMapStats.renderFailLocalPasses,
			g_shadowMapStats.renderFailGlobalPasses,
			g_shadowMapStats.maskFailLocalPasses,
			g_shadowMapStats.maskFailGlobalPasses );
	} else {
		common->Printf(
			"SM summary: lights=%d supported=%d point=%d projected=%d parallel=%d mapped(local=%d global=%d) unshadowed(local=%d global=%d) fallback(local=%d global=%d)\n",
			g_shadowMapStats.totalLights,
			g_shadowMapStats.supportedLights,
			g_shadowMapStats.pointLights,
			g_shadowMapStats.projectedLights,
			g_shadowMapStats.parallelLights,
			g_shadowMapStats.mappedLocalPasses,
			g_shadowMapStats.mappedGlobalPasses,
			g_shadowMapStats.unshadowedLocalPasses,
			g_shadowMapStats.unshadowedGlobalPasses,
			g_shadowMapStats.fallbackLocalPasses,
			g_shadowMapStats.fallbackGlobalPasses );
		common->Printf(
			"SM detail: failures(render local=%d global=%d, mask local=%d global=%d)\n",
			g_shadowMapStats.renderFailLocalPasses,
			g_shadowMapStats.renderFailGlobalPasses,
			g_shadowMapStats.maskFailLocalPasses,
			g_shadowMapStats.maskFailGlobalPasses );
	}

}

static void RB_ShadowMapLightReport( const viewLight_t *vLight, shadowMapLightSupportReason_t reason ) {
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame ) {
		return;
	}

	const char *shaderName = ( vLight != NULL && vLight->lightShader != NULL ) ? vLight->lightShader->GetName() : "<null>";
	idVec3 origin( 0.0f, 0.0f, 0.0f );
	if ( vLight != NULL ) {
		origin = vLight->globalLightOrigin;
	}
	common->Printf(
		"SM light '%s' origin=(%.1f %.1f %.1f) type=%s%s interactions(local=%d global=%d) shadows(local=%d global=%d) support=%s\n",
		shaderName,
		origin[0], origin[1], origin[2],
		( vLight != NULL && vLight->pointLight ) ? "point" : "projected",
		( vLight != NULL && vLight->parallel ) ? "/parallel" : "",
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localInteractions : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalInteractions : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localShadows : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalShadows : NULL ),
		RB_ShadowMapSupportReasonName( reason ) );
}

static void RB_ShadowMapPassReport( const viewLight_t *vLight, shadowMapPassKind_t passKind, bool pointLight, shadowMapPassResult_t result, const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *shadowSurfs, const drawSurf_t *interactions ) {
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame ) {
		return;
	}

	const char *shaderName = ( vLight != NULL && vLight->lightShader != NULL ) ? vLight->lightShader->GetName() : "<null>";
	common->Printf(
		"SM pass %s '%s' type=%s result=%s casters(primary=%d secondary=%d) shadowSurfs=%d receivers=%d\n",
		RB_ShadowMapPassName( passKind ),
		shaderName,
		pointLight ? "point" : "projected",
		RB_ShadowMapPassResultName( result ),
		RB_CountDrawSurfChain( primaryCasters ),
		RB_CountDrawSurfChain( secondaryCasters ),
		RB_CountDrawSurfChain( shadowSurfs ),
		RB_CountDrawSurfChain( interactions ) );
}

static void RB_ShadowMapDrawCasterChain( const drawSurf_t *surf ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		const srfTriangles_t *casterGeo = surf->geo;
		if ( casterGeo == NULL ) {
			continue;
		}

		if ( casterGeo->ambientSurface != NULL ) {
			casterGeo = casterGeo->ambientSurface;
		}

		if ( casterGeo == NULL ) {
			continue;
		}

		vertCache_s *ambientCache = casterGeo->ambientCache;
		if ( ambientCache == NULL ) {
			ambientCache = surf->geo->ambientCache;
		}
		if ( ambientCache == NULL ) {
			continue;
		}

		if ( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;
			glLoadMatrixf( surf->space->modelMatrix );
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		RB_DrawElementsWithCounters( casterGeo );
	}
}

static void RB_ShadowMapModelMatrixRows( const float modelMatrix[16], float row0[4], float row1[4], float row2[4] ) {
	row0[0] = modelMatrix[0];
	row0[1] = modelMatrix[4];
	row0[2] = modelMatrix[8];
	row0[3] = modelMatrix[12];

	row1[0] = modelMatrix[1];
	row1[1] = modelMatrix[5];
	row1[2] = modelMatrix[9];
	row1[3] = modelMatrix[13];

	row2[0] = modelMatrix[2];
	row2[1] = modelMatrix[6];
	row2[2] = modelMatrix[10];
	row2[3] = modelMatrix[14];
}

static void RB_PointShadowMapBuildViewAxis( const int cubeFace, idMat3 &axis ) {
	memset( &axis, 0, sizeof( axis ) );

	switch ( cubeFace ) {
	case 0:
		axis[0][0] = 1.0f;
		axis[1][2] = 1.0f;
		axis[2][1] = -1.0f;
		break;
	case 1:
		axis[0][0] = -1.0f;
		axis[1][2] = -1.0f;
		axis[2][1] = -1.0f;
		break;
	case 2:
		axis[0][1] = 1.0f;
		axis[1][0] = -1.0f;
		axis[2][2] = 1.0f;
		break;
	case 3:
		axis[0][1] = -1.0f;
		axis[1][0] = -1.0f;
		axis[2][2] = -1.0f;
		break;
	case 4:
		axis[0][2] = 1.0f;
		axis[1][0] = -1.0f;
		axis[2][1] = -1.0f;
		break;
	default:
		axis[0][2] = -1.0f;
		axis[1][0] = 1.0f;
		axis[2][1] = -1.0f;
		break;
	}
}

static void RB_PointShadowMapBuildModelViewMatrix( const idVec3 &origin, const int cubeFace, float matrix[16] ) {
	viewDef_t shadowView;
	memset( &shadowView, 0, sizeof( shadowView ) );
	shadowView.renderView.vieworg = origin;
	RB_PointShadowMapBuildViewAxis( cubeFace, shadowView.renderView.viewaxis );
	R_SetViewMatrix( &shadowView );
	memcpy( matrix, shadowView.worldSpace.modelViewMatrix, sizeof( shadowView.worldSpace.modelViewMatrix ) );
}

static void RB_PointShadowMapBuildProjectionMatrix( const float zNear, const float zFar, float matrix[16] ) {
	memset( matrix, 0, 16 * sizeof( float ) );
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = -( zFar + zNear ) / ( zFar - zNear );
	matrix[11] = -1.0f;
	matrix[14] = -( 2.0f * zFar * zNear ) / ( zFar - zNear );
}

static float RB_PointShadowMapLightFar( const viewLight_t *vLight ) {
	idVec3 adjustedRadius = vLight->lightRadius;
	if ( vLight->lightDef != NULL ) {
		const renderLight_t &parms = vLight->lightDef->parms;
		adjustedRadius[0] = parms.lightRadius[0] + idMath::Fabs( parms.lightCenter[0] );
		adjustedRadius[1] = parms.lightRadius[1] + idMath::Fabs( parms.lightCenter[1] );
		adjustedRadius[2] = parms.lightRadius[2] + idMath::Fabs( parms.lightCenter[2] );
	}

	return Max( adjustedRadius.Length() * r_shadowMapPointFarScale.GetFloat(), 1.0f );
}

static void RB_PointShadowMapDrawCasterChain( const drawSurf_t *surf, const float lightModelViewMatrix[16] ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		const srfTriangles_t *casterGeo = surf->geo;
		if ( casterGeo == NULL ) {
			continue;
		}

		if ( casterGeo->ambientSurface != NULL ) {
			casterGeo = casterGeo->ambientSurface;
		}

		if ( casterGeo == NULL ) {
			continue;
		}

		vertCache_s *ambientCache = casterGeo->ambientCache;
		if ( ambientCache == NULL ) {
			ambientCache = surf->geo->ambientCache;
		}
		if ( ambientCache == NULL ) {
			continue;
		}

		if ( surf->space != backEnd.currentSpace ) {
			float modelViewMatrix[16];
			float row0[4];
			float row1[4];
			float row2[4];

			backEnd.currentSpace = surf->space;
			myGlMultMatrix( surf->space->modelMatrix, lightModelViewMatrix, modelViewMatrix );
			glLoadMatrixf( modelViewMatrix );

			RB_ShadowMapModelMatrixRows( surf->space->modelMatrix, row0, row1, row2 );
			glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow0, 1, row0 );
			glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow1, 1, row1 );
			glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow2, 1, row2 );
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		RB_DrawElementsWithCounters( casterGeo );
	}
}

static bool RB_RenderShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters ) {
	if ( !RB_ShadowMapEnsureResources() ) {
		return false;
	}

	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const int savedFaceCulling = backEnd.glState.faceCulling;

	idPlane clipPlanes[4];
	float clipMatrix[16];
	RB_ShadowMapBuildClipPlanes( backEnd.vLight->lightProject, clipPlanes );
	RB_ShadowMapClipPlanesToGLMatrix( clipPlanes, clipMatrix );

	g_shadowMapRenderTexture->MakeCurrent();

	const int shadowMapWidth = g_shadowMapRenderTexture->GetWidth();
	const int shadowMapHeight = g_shadowMapRenderTexture->GetHeight();
	glViewport( 0, 0, shadowMapWidth, shadowMapHeight );
	glScissor( 0, 0, shadowMapWidth, shadowMapHeight );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( clipMatrix );
	glMatrixMode( GL_MODELVIEW );

	glUseProgramObjectARB( 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );

	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( r_shadowMapPolygonFactor.GetFloat(), r_shadowMapPolygonOffset.GetFloat() );

	glClear( GL_DEPTH_BUFFER_BIT );

	backEnd.currentSpace = NULL;
	RB_ShadowMapDrawCasterChain( primaryCasters );
	RB_ShadowMapDrawCasterChain( secondaryCasters );

	glDisable( GL_POLYGON_OFFSET_FILL );

	if ( backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}

	glViewport( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );
	glScissor( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	if ( blendWasEnabled ) {
		glEnable( GL_BLEND );
	} else {
		glDisable( GL_BLEND );
	}
	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );

	return true;
}

static bool RB_RenderPointShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters ) {
	if ( !RB_PointShadowMapEnsureResources() ) {
		return false;
	}

	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const int savedFaceCulling = backEnd.glState.faceCulling;

	const float farClip = RB_PointShadowMapLightFar( backEnd.vLight );
	const float nearClip = idMath::ClampFloat( 0.5f, 16.0f, farClip * 0.01f );
	float projectionMatrix[16];
	RB_PointShadowMapBuildProjectionMatrix( nearClip, farClip, projectionMatrix );

	const float globalLightOrigin[4] = {
		backEnd.vLight->globalLightOrigin[0],
		backEnd.vLight->globalLightOrigin[1],
		backEnd.vLight->globalLightOrigin[2],
		1.0f
	};
	GLfloat clearColor[4];
	glGetFloatv( GL_COLOR_CLEAR_VALUE, clearColor );

	const int shadowMapWidth = g_pointShadowMapRenderTexture->GetWidth();
	const int shadowMapHeight = g_pointShadowMapRenderTexture->GetHeight();

	glUseProgramObjectARB( g_pointShadowCasterProgram.programObject );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUniform4fvARB( g_pointShadowCasterProgram.globalLightOrigin, 1, globalLightOrigin );
	glUniform1fARB( g_pointShadowCasterProgram.pointShadowFar, farClip );

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( r_shadowMapPolygonFactor.GetFloat(), r_shadowMapPolygonOffset.GetFloat() );
	glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	for ( int cubeFace = 0; cubeFace < 6; cubeFace++ ) {
		float lightModelViewMatrix[16];
		RB_PointShadowMapBuildModelViewMatrix( backEnd.vLight->globalLightOrigin, cubeFace, lightModelViewMatrix );

		g_pointShadowMapRenderTexture->MakeCurrent( cubeFace );
		glViewport( 0, 0, shadowMapWidth, shadowMapHeight );
		glScissor( 0, 0, shadowMapWidth, shadowMapHeight );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

		backEnd.currentSpace = NULL;
		RB_PointShadowMapDrawCasterChain( primaryCasters, lightModelViewMatrix );
		RB_PointShadowMapDrawCasterChain( secondaryCasters, lightModelViewMatrix );
	}

	glDisable( GL_POLYGON_OFFSET_FILL );
	glUseProgramObjectARB( 0 );
	glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );

	if ( backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}

	glViewport( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );
	glScissor( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	if ( blendWasEnabled ) {
		glEnable( GL_BLEND );
	} else {
		glDisable( GL_BLEND );
	}
	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );

	return true;
}

static void RB_GLSLShadowMap_DrawInteraction( const drawInteraction_t *din ) {
	idPlane shadowClipGlobal[4];
	idPlane shadowClipLocal[4];
	RB_ShadowMapBuildClipPlanes( backEnd.vLight->lightProject, shadowClipGlobal );
	for ( int i = 0; i < 4; i++ ) {
		R_GlobalPlaneToLocal( din->surf->space->modelMatrix, shadowClipGlobal[i], shadowClipLocal[i] );
	}

	glUniform4fvARB( g_shadowMapProgram.localLightOrigin, 1, din->localLightOrigin.ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.localViewOrigin, 1, din->localViewOrigin.ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.lightProjectionS, 1, din->lightProjection[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.lightProjectionT, 1, din->lightProjection[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.lightProjectionQ, 1, din->lightProjection[2].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.lightFalloffS, 1, din->lightProjection[3].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.bumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.bumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.diffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.diffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.specularMatrixS, 1, din->specularMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.specularMatrixT, 1, din->specularMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.shadowRow[0], 1, shadowClipLocal[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.shadowRow[1], 1, shadowClipLocal[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.shadowRow[2], 1, shadowClipLocal[2].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.shadowRow[3], 1, shadowClipLocal[3].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.diffuseColor, 1, din->diffuseColor.ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.specularColor, 1, din->specularColor.ToFloatPtr() );

	float modulate = 0.0f;
	float add = 1.0f;
	switch ( din->vertexColor ) {
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	case SVC_IGNORE:
	default:
		break;
	}
	const float vertexColorParams[2] = { modulate, add };
	glUniform2fvARB( g_shadowMapProgram.vertexColorParams, 1, vertexColorParams );

	GL_SelectTextureNoClient( 0 );
	din->bumpImage->Bind();
	GL_SelectTextureNoClient( 1 );
	din->lightFalloffImage->Bind();
	GL_SelectTextureNoClient( 2 );
	din->lightImage->Bind();
	GL_SelectTextureNoClient( 3 );
	din->diffuseImage->Bind();
	GL_SelectTextureNoClient( 4 );
	din->specularImage->Bind();
	GL_SelectTextureNoClient( 5 );
	g_shadowMapDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );

	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

static void RB_GLSLPointShadowMap_DrawInteraction( const drawInteraction_t *din ) {
	const float globalLightOrigin[4] = {
		backEnd.vLight->globalLightOrigin[0],
		backEnd.vLight->globalLightOrigin[1],
		backEnd.vLight->globalLightOrigin[2],
		1.0f
	};
	float row0[4];
	float row1[4];
	float row2[4];

	RB_ShadowMapModelMatrixRows( din->surf->space->modelMatrix, row0, row1, row2 );

	glUniform4fvARB( g_pointShadowMapProgram.localLightOrigin, 1, din->localLightOrigin.ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.localViewOrigin, 1, din->localViewOrigin.ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.lightProjectionS, 1, din->lightProjection[0].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.lightProjectionT, 1, din->lightProjection[1].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.lightProjectionQ, 1, din->lightProjection[2].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.lightFalloffS, 1, din->lightProjection[3].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.bumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.bumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.diffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.diffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.specularMatrixS, 1, din->specularMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.specularMatrixT, 1, din->specularMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow0, 1, row0 );
	glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow1, 1, row1 );
	glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow2, 1, row2 );
	glUniform4fvARB( g_pointShadowMapProgram.globalLightOrigin, 1, globalLightOrigin );
	glUniform1fARB( g_pointShadowMapProgram.pointShadowFar, RB_PointShadowMapLightFar( backEnd.vLight ) );
	glUniform4fvARB( g_pointShadowMapProgram.diffuseColor, 1, din->diffuseColor.ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.specularColor, 1, din->specularColor.ToFloatPtr() );

	float modulate = 0.0f;
	float add = 1.0f;
	switch ( din->vertexColor ) {
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	case SVC_IGNORE:
	default:
		break;
	}
	const float vertexColorParams[2] = { modulate, add };
	glUniform2fvARB( g_pointShadowMapProgram.vertexColorParams, 1, vertexColorParams );

	GL_SelectTextureNoClient( 0 );
	din->bumpImage->Bind();
	GL_SelectTextureNoClient( 1 );
	din->lightFalloffImage->Bind();
	GL_SelectTextureNoClient( 2 );
	din->lightImage->Bind();
	GL_SelectTextureNoClient( 3 );
	din->diffuseImage->Bind();
	GL_SelectTextureNoClient( 4 );
	din->specularImage->Bind();
	GL_SelectTextureNoClient( 5 );
	g_pointShadowMapColorImage->Bind();

	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

static bool RB_GLSLShadowMap_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( surf == NULL || !RB_ShadowMapLoadProgram() ) {
		return false;
	}

	GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | backEnd.depthFunc );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( g_shadowMapProgram.programObject );

	if ( g_shadowMapProgram.shadowMap >= 0 ) {
		glUniform1iARB( g_shadowMapProgram.shadowMap, 0 );
	}
	if ( g_shadowMapProgram.shadowTexelSize >= 0 ) {
		const float texelSize[2] = {
			1.0f / Max( 1, g_shadowMapRenderTexture->GetWidth() ),
			1.0f / Max( 1, g_shadowMapRenderTexture->GetHeight() )
		};
		glUniform2fvARB( g_shadowMapProgram.shadowTexelSize, 1, texelSize );
	}
	if ( g_shadowMapProgram.shadowBias >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowBias, r_shadowMapBias.GetFloat() );
	}
	if ( g_shadowMapProgram.shadowFilterRadius >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowFilterRadius, r_shadowMapFilterRadius.GetFloat() );
	}

	glEnable( GL_STENCIL_TEST );
	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
	glStencilFunc( GL_ALWAYS, 128, 255 );
	backEnd.currentScissor = backEnd.vLight->scissorRect;
	if ( r_useScissor.GetBool() ) {
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}
	glClearStencil( 0 );
	glClear( GL_STENCIL_BUFFER_BIT );
	glClearStencil( RB_ShadowMapSafeStencilClearValue() );

	GL_SelectTextureNoClient( 0 );
	g_shadowMapDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );

	idPlane shadowClipGlobal[4];
	RB_ShadowMapBuildClipPlanes( backEnd.vLight->lightProject, shadowClipGlobal );

	backEnd.currentSpace = NULL;

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( surf->geo == NULL || surf->geo->ambientCache == NULL ) {
			continue;
		}

		if ( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;
			glLoadMatrixf( surf->space->modelViewMatrix );
		}

		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			backEnd.currentScissor = surf->scissorRect;
			glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}

		if ( surf->space->weaponDepthHack ) {
			RB_EnterWeaponDepthHack();
		}
		if ( surf->space->modelDepthHack != 0.0f ) {
			RB_EnterModelDepthHack( surf->space->modelDepthHack );
		}

		idPlane shadowClipLocal[4];
		for ( int i = 0; i < 4; i++ ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, shadowClipGlobal[i], shadowClipLocal[i] );
		}
		glUniform4fvARB( g_shadowMapProgram.shadowRow[0], 1, shadowClipLocal[0].ToFloatPtr() );
		glUniform4fvARB( g_shadowMapProgram.shadowRow[1], 1, shadowClipLocal[1].ToFloatPtr() );
		glUniform4fvARB( g_shadowMapProgram.shadowRow[2], 1, shadowClipLocal[2].ToFloatPtr() );
		glUniform4fvARB( g_shadowMapProgram.shadowRow[3], 1, shadowClipLocal[3].ToFloatPtr() );

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		const idMaterial *surfaceMaterial = surf->material;
		if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
		}
		RB_DrawElementsWithCounters( surf->geo );
		if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			glDisable( GL_POLYGON_OFFSET_FILL );
		}

		if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
			RB_LeaveDepthHack();
		}
	}

	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glStencilFunc( GL_EQUAL, 128, 255 );
	globalImages->BindNull();
	glUseProgramObjectARB( 0 );
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	return true;
}

static bool RB_GLSLPointShadowMap_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( surf == NULL || !RB_PointShadowMapLoadProgram() ) {
		return false;
	}

	GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | backEnd.depthFunc );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( g_pointShadowMapProgram.programObject );

	if ( g_pointShadowMapProgram.pointShadowMap >= 0 ) {
		glUniform1iARB( g_pointShadowMapProgram.pointShadowMap, 0 );
	}
	if ( g_pointShadowMapProgram.shadowBias >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowBias, r_shadowMapPointBias.GetFloat() );
	}
	if ( g_pointShadowMapProgram.shadowFilterRadius >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowFilterRadius, r_shadowMapPointFilterRadius.GetFloat() );
	}
	if ( g_pointShadowMapProgram.globalLightOrigin >= 0 ) {
		const float globalLightOrigin[4] = {
			backEnd.vLight->globalLightOrigin[0],
			backEnd.vLight->globalLightOrigin[1],
			backEnd.vLight->globalLightOrigin[2],
			1.0f
		};
		glUniform4fvARB( g_pointShadowMapProgram.globalLightOrigin, 1, globalLightOrigin );
	}
	if ( g_pointShadowMapProgram.pointShadowFar >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.pointShadowFar, RB_PointShadowMapLightFar( backEnd.vLight ) );
	}
	if ( g_pointShadowMapProgram.pointShadowTexelScale >= 0 ) {
		const float texelScale = ( 2.0f * r_shadowMapPointFilterRadius.GetFloat() ) / Max( 1, g_pointShadowMapRenderTexture->GetWidth() );
		glUniform1fARB( g_pointShadowMapProgram.pointShadowTexelScale, texelScale );
	}

	glEnable( GL_STENCIL_TEST );
	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
	glStencilFunc( GL_ALWAYS, 128, 255 );
	backEnd.currentScissor = backEnd.vLight->scissorRect;
	if ( r_useScissor.GetBool() ) {
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}
	glClearStencil( 0 );
	glClear( GL_STENCIL_BUFFER_BIT );
	glClearStencil( RB_ShadowMapSafeStencilClearValue() );

	GL_SelectTextureNoClient( 0 );
	g_pointShadowMapColorImage->Bind();

	backEnd.currentSpace = NULL;

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( surf->geo == NULL || surf->geo->ambientCache == NULL ) {
			continue;
		}

		if ( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;
			glLoadMatrixf( surf->space->modelViewMatrix );
		}

		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			backEnd.currentScissor = surf->scissorRect;
			glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}

		if ( surf->space->weaponDepthHack ) {
			RB_EnterWeaponDepthHack();
		}
		if ( surf->space->modelDepthHack != 0.0f ) {
			RB_EnterModelDepthHack( surf->space->modelDepthHack );
		}

		float row0[4];
		float row1[4];
		float row2[4];
		RB_ShadowMapModelMatrixRows( surf->space->modelMatrix, row0, row1, row2 );
		glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow0, 1, row0 );
		glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow1, 1, row1 );
		glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow2, 1, row2 );

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		const idMaterial *surfaceMaterial = surf->material;
		if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
		}
		RB_DrawElementsWithCounters( surf->geo );
		if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			glDisable( GL_POLYGON_OFFSET_FILL );
		}

		if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
			RB_LeaveDepthHack();
		}
	}

	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glStencilFunc( GL_EQUAL, 128, 255 );
	globalImages->BindNull();
	glUseProgramObjectARB( 0 );
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	return true;
}

static void RB_ShadowMapStencilFallback( const drawSurf_t *shadowSurfs, const drawSurf_t *interactions ) {
	if ( interactions == NULL ) {
		return;
	}

	if ( shadowSurfs != NULL ) {
		backEnd.currentScissor = backEnd.vLight->scissorRect;
		if ( r_useScissor.GetBool() ) {
			glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}
		glClear( GL_STENCIL_BUFFER_BIT );

		if ( r_useShadowVertexProgram.GetBool() ) {
			glEnable( GL_VERTEX_PROGRAM_ARB );
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
			RB_StencilShadowPass( shadowSurfs );
			RB_ARB2_CreateDrawInteractions( interactions );
			glDisable( GL_VERTEX_PROGRAM_ARB );
		} else {
			RB_StencilShadowPass( shadowSurfs );
			RB_ARB2_CreateDrawInteractions( interactions );
		}
	} else {
		glStencilFunc( GL_ALWAYS, 128, 255 );
		RB_ARB2_CreateDrawInteractions( interactions );
	}
}

static void RB_ShadowMapRunPass( const viewLight_t *vLight, shadowMapPassKind_t passKind, bool pointLight, const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *shadowSurfs, const drawSurf_t *interactions ) {
	if ( interactions == NULL ) {
		return;
	}

	if ( shadowSurfs == NULL ) {
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.unshadowedLocalPasses++;
		} else {
			g_shadowMapStats.unshadowedGlobalPasses++;
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight, SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS, primaryCasters, secondaryCasters, shadowSurfs, interactions );
		glStencilFunc( GL_ALWAYS, 128, 255 );
		RB_ARB2_CreateDrawInteractions( interactions );
		return;
	}

	const bool renderOk = pointLight ? RB_RenderPointShadowMap( primaryCasters, secondaryCasters ) : RB_RenderShadowMap( primaryCasters, secondaryCasters );
	const bool maskOk = renderOk && ( pointLight ? RB_GLSLPointShadowMap_CreateDrawInteractions( interactions ) : RB_GLSLShadowMap_CreateDrawInteractions( interactions ) );
	shadowMapPassResult_t passResult = SHADOWMAP_PASS_RESULT_MAPPED;
	if ( !renderOk ) {
		passResult = SHADOWMAP_PASS_RESULT_RENDER_FAIL;
	} else if ( !maskOk ) {
		passResult = SHADOWMAP_PASS_RESULT_MASK_FAIL;
	}
	const bool mapped = ( passResult == SHADOWMAP_PASS_RESULT_MAPPED );

	if ( mapped ) {
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.mappedLocalPasses++;
		} else {
			g_shadowMapStats.mappedGlobalPasses++;
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight, passResult, primaryCasters, secondaryCasters, shadowSurfs, interactions );
		RB_ARB2_CreateDrawInteractions( interactions );
		return;
	}

	if ( passKind == SHADOWMAP_PASS_LOCAL ) {
		g_shadowMapStats.fallbackLocalPasses++;
		if ( passResult == SHADOWMAP_PASS_RESULT_RENDER_FAIL ) {
			g_shadowMapStats.renderFailLocalPasses++;
		} else if ( passResult == SHADOWMAP_PASS_RESULT_MASK_FAIL ) {
			g_shadowMapStats.maskFailLocalPasses++;
		}
	} else {
		g_shadowMapStats.fallbackGlobalPasses++;
		if ( passResult == SHADOWMAP_PASS_RESULT_RENDER_FAIL ) {
			g_shadowMapStats.renderFailGlobalPasses++;
		} else if ( passResult == SHADOWMAP_PASS_RESULT_MASK_FAIL ) {
			g_shadowMapStats.maskFailGlobalPasses++;
		}
	}
	RB_ShadowMapPassReport( vLight, passKind, pointLight, passResult, primaryCasters, secondaryCasters, shadowSurfs, interactions );
	RB_ShadowMapStencilFallback( shadowSurfs, interactions );
}

/*
==================
RB_ARB2_DrawInteraction
==================
*/
void	RB_ARB2_DrawInteraction( const drawInteraction_t *din ) {
	// load all the vertex program parameters
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_S, din->lightProjection[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_T, din->lightProjection[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_Q, din->lightProjection[2].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	// testing fragment based normal mapping
	if ( r_testARBProgram.GetBool() ) {
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 2, din->localLightOrigin.ToFloatPtr() );
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 3, din->localViewOrigin.ToFloatPtr() );
	}

	static const float zero[4] = { 0, 0, 0, 0 };
	float modulate = 0.0f;
	float add = 1.0f;

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		modulate = 0.0f;
		add = 1.0f;
		break;
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	}

	if ( g_interactionVertexProgramColorMode == ICM_PACKED ) {
		// Stock Quake 4 interaction.vfp packs vertex-color mode as env[16].xy.
		const float packed[4] = { modulate, add, 0.0f, 0.0f };
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_MODULATE, packed );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_ADD, zero );
	} else {
		float modulateVec[4] = { modulate, modulate, modulate, modulate };
		float addVec[4] = { add, add, add, add };
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_MODULATE, modulateVec );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_ADD, addVec );
	}

	// set the constant colors
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, din->diffuseColor.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, din->specularColor.ToFloatPtr() );

	// set the textures

	// texture 1 will be the per-surface bump map
	GL_SelectTextureNoClient( 1 );
	din->bumpImage->Bind();

	// texture 2 will be the light falloff texture
	GL_SelectTextureNoClient( 2 );
	din->lightFalloffImage->Bind();

	// texture 3 will be the light projection texture
	GL_SelectTextureNoClient( 3 );
	din->lightImage->Bind();

	// texture 4 is the per-surface diffuse map
	GL_SelectTextureNoClient( 4 );
	din->diffuseImage->Bind();

	// texture 5 is the per-surface specular map
	GL_SelectTextureNoClient( 5 );
	din->specularImage->Bind();

	// Quake 4 applies decal polygon offset in interaction passes as well.
	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	// draw it
	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

/*
=============
RB_ARB2_CreateDrawInteractions

=============
*/
void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( !surf ) {
		return;
	}

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	// bind the vertex program
	if ( r_testARBProgram.GetBool() ) {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_TEST );
		glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_TEST );
	} else {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION );
		glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION );
	}

	glEnable(GL_VERTEX_PROGRAM_ARB);
	glEnable(GL_FRAGMENT_PROGRAM_ARB);

	// enable the vertex arrays
	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	// texture 0 is the normalization cube map for the vector towards the light
	GL_SelectTextureNoClient( 0 );
	if ( backEnd.vLight->lightShader->IsAmbientLight() ) {
		globalImages->ambientNormalMap->Bind();
	} else {
		globalImages->normalCubeMapImage->Bind();
	}

	// texture 6 is the specular lookup table
	GL_SelectTextureNoClient( 6 );
	globalImages->specularTableImage->Bind();


	for ( ; surf ; surf=surf->nextOnLight ) {
		// perform setup here that will not change over multiple interaction passes

		// set the vertex pointers
		idDrawVert	*ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		// this may cause RB_ARB2_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_CreateSingleDrawInteractions( surf, RB_ARB2_DrawInteraction );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	// disable features
	GL_SelectTextureNoClient( 6 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );

	glDisable(GL_VERTEX_PROGRAM_ARB);
	glDisable(GL_FRAGMENT_PROGRAM_ARB);
}

/*
==================
RB_ARB2_DrawInteractions
==================
*/
void RB_ARB2_DrawInteractions( void ) {
	viewLight_t		*vLight;

	if ( r_interactionColorMode.IsModified() ) {
		r_interactionColorMode.ClearModified();
		RB_UpdateInteractionColorMode( true );
	}

	GL_SelectTexture( 0 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	RB_ShadowMapStatsReset();

	//
	// for each light, perform adding and shadowing
	//
	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		// do fogging later
		if ( vLight->lightShader->IsFogLight() ) {
			continue;
		}
		if ( vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( !vLight->localInteractions && !vLight->globalInteractions
			&& !vLight->translucentInteractions ) {
			continue;
		}

		g_shadowMapStats.totalLights++;

		const shadowMapLightSupportReason_t supportReason = RB_ShadowMapLightSupportReason( vLight );
		if ( supportReason == SHADOWMAP_SUPPORT_OK ) {
			g_shadowMapStats.supportedLights++;
			if ( vLight->pointLight ) {
				g_shadowMapStats.pointLights++;
			} else {
				g_shadowMapStats.projectedLights++;
			}
			if ( vLight->parallel ) {
				g_shadowMapStats.parallelLights++;
			}
			RB_ShadowMapLightReport( vLight, supportReason );

			glStencilFunc( GL_ALWAYS, 128, 255 );

			if ( vLight->pointLight ) {
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_LOCAL, true, vLight->globalInteractions, NULL, vLight->globalShadows, vLight->localInteractions );
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_GLOBAL, true, vLight->globalInteractions, vLight->localInteractions, vLight->localShadows, vLight->globalInteractions );
			} else {
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_LOCAL, false, vLight->globalInteractions, NULL, vLight->globalShadows, vLight->localInteractions );
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_GLOBAL, false, vLight->globalInteractions, vLight->localInteractions, vLight->localShadows, vLight->globalInteractions );
			}

			if ( !r_skipTranslucent.GetBool() ) {
				glStencilFunc( GL_ALWAYS, 128, 255 );
				backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
				RB_ARB2_CreateDrawInteractions( vLight->translucentInteractions );
				backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
			}

			continue;
		}

		if ( supportReason >= 0 && supportReason < SHADOWMAP_SUPPORT_COUNT ) {
			g_shadowMapStats.unsupportedLights[supportReason]++;
		}
		RB_ShadowMapLightReport( vLight, supportReason );

		// clear the stencil buffer if needed
		if ( vLight->globalShadows || vLight->localShadows ) {
			backEnd.currentScissor = vLight->scissorRect;
			if ( r_useScissor.GetBool() ) {
				glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
					backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
					backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
					backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
			}
			glClear( GL_STENCIL_BUFFER_BIT );
		} else {
			// no shadows, so no need to read or write the stencil buffer
			// we might in theory want to use GL_ALWAYS instead of disabling
			// completely, to satisfy the invarience rules
			glStencilFunc( GL_ALWAYS, 128, 255 );
		}

		if ( r_useShadowVertexProgram.GetBool() ) {
			glEnable( GL_VERTEX_PROGRAM_ARB );
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
			RB_StencilShadowPass( vLight->globalShadows );
			RB_ARB2_CreateDrawInteractions( vLight->localInteractions );
			glEnable( GL_VERTEX_PROGRAM_ARB );
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
			RB_StencilShadowPass( vLight->localShadows );
			RB_ARB2_CreateDrawInteractions( vLight->globalInteractions );
			glDisable( GL_VERTEX_PROGRAM_ARB );	// if there weren't any globalInteractions, it would have stayed on
		} else {
			RB_StencilShadowPass( vLight->globalShadows );
			RB_ARB2_CreateDrawInteractions( vLight->localInteractions );
			RB_StencilShadowPass( vLight->localShadows );
			RB_ARB2_CreateDrawInteractions( vLight->globalInteractions );
		}

		// translucent surfaces never get stencil shadowed
		if ( r_skipTranslucent.GetBool() ) {
			continue;
		}

		glStencilFunc( GL_ALWAYS, 128, 255 );

		backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
		RB_ARB2_CreateDrawInteractions( vLight->translucentInteractions );

		backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
	}

	// disable stencil shadow test
	glStencilFunc( GL_ALWAYS, 128, 255 );
	RB_ShadowMapStatsReport();

	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
}

//===================================================================================


typedef struct {
	GLenum			target;
	GLuint			ident;
	char			name[64];
} progDef_t;

static	const int	MAX_GLPROGS = 200;

// a single file can have both a vertex program and a fragment program
static progDef_t	progs[MAX_GLPROGS] = {
	{ GL_VERTEX_PROGRAM_ARB, VPROG_TEST, "test.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_TEST, "test.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION, "interaction.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION, "interaction.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_AMBIENT, "ambientLight.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_AMBIENT, "ambientLight.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "shadow.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_R200_INTERACTION, "R200_interaction.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_BUMP_AND_LIGHT, "nv20_bumpAndLight.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_DIFFUSE_COLOR, "nv20_diffuseColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_SPECULAR_COLOR, "nv20_specularColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_DIFFUSE_AND_SPECULAR_COLOR, "nv20_diffuseAndSpecularColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT, "environment.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT, "environment.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_GLASSWARP, "arbVP_glasswarp.txt" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP, "arbFP_glasswarp.txt" },

	// additional programs can be dynamically specified in materials
};

/*
=================
R_LoadARBProgram
=================
*/
void R_LoadARBProgram( int progIndex ) {
	int		ofs;
	int		err;
	idStr	fullPath = "glprogs/";
	fullPath += progs[progIndex].name;
	char	*fileBuffer;
	char	*buffer;
	char	*start, *end;

	common->Printf( "%s", fullPath.c_str() );

	// load the program even if we don't support it, so
	// fs_copyfiles can generate cross-platform data dumps
	fileSystem->ReadFile( fullPath.c_str(), (void **)&fileBuffer, NULL );
	if ( !fileBuffer ) {
		common->Printf( ": File not found\n" );
		return;
	}

	// copy to stack memory and free
	buffer = (char *)_alloca( strlen( fileBuffer ) + 1 );
	strcpy( buffer, fileBuffer );
	fileSystem->FreeFile( fileBuffer );

	if ( !glConfig.isInitialized ) {
		return;
	}

	//
	// submit the program string at start to GL
	//
	if ( progs[progIndex].ident == 0 ) {
		// allocate a new identifier for this program
		progs[progIndex].ident = PROG_USER + progIndex;
	}

	// vertex and fragment programs can both be present in a single file, so
	// scan for the proper header to be the start point, and stamp a 0 in after the end

	if ( progs[progIndex].target == GL_VERTEX_PROGRAM_ARB ) {
		if ( !glConfig.ARBVertexProgramAvailable ) {
			common->Printf( ": GL_VERTEX_PROGRAM_ARB not available\n" );
			return;
		}
		start = strstr( (char *)buffer, "!!ARBvp" );
	}
	if ( progs[progIndex].target == GL_FRAGMENT_PROGRAM_ARB ) {
		if ( !glConfig.ARBFragmentProgramAvailable ) {
			common->Printf( ": GL_FRAGMENT_PROGRAM_ARB not available\n" );
			return;
		}
		start = strstr( (char *)buffer, "!!ARBfp" );
	}
	if ( !start ) {
		common->Printf( ": !!ARB not found\n" );
		return;
	}
	end = strstr( start, "END" );

	if ( !end ) {
		common->Printf( ": END not found\n" );
		return;
	}
	end[3] = 0;

	if ( progs[progIndex].ident == VPROG_INTERACTION ) {
		interactionColorMode_t detectedMode = ICM_PACKED;
		if ( !RB_DetectInteractionColorMode( start, detectedMode ) ) {
			common->Warning( "R_LoadARBProgram: failed to infer interaction color mode from %s, defaulting auto mode to %s",
				fullPath.c_str(), RB_InteractionColorModeName( ICM_PACKED ) );
			detectedMode = ICM_PACKED;
		}
		g_interactionVertexProgramAutoColorMode = detectedMode;
		RB_UpdateInteractionColorMode( true );
	}

	glBindProgramARB( progs[progIndex].target, progs[progIndex].ident );
	glGetError();

	glProgramStringARB( progs[progIndex].target, GL_PROGRAM_FORMAT_ASCII_ARB,
		strlen( start ), (unsigned char *)start );

	err = glGetError();
	glGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, (GLint *)&ofs );
	if ( err == GL_INVALID_OPERATION ) {
		const GLubyte *str = glGetString( GL_PROGRAM_ERROR_STRING_ARB );
		common->Printf( "\nGL_PROGRAM_ERROR_STRING_ARB: %s\n", str );
		if ( ofs < 0 ) {
			common->Printf( "GL_PROGRAM_ERROR_POSITION_ARB < 0 with error\n" );
		} else if ( ofs >= (int)strlen( (char *)start ) ) {
			common->Printf( "error at end of program\n" );
		} else {
			common->Printf( "error at %i:\n%s", ofs, start + ofs );
		}
		return;
	}
	if ( ofs != -1 ) {
		common->Printf( "\nGL_PROGRAM_ERROR_POSITION_ARB != -1 without error\n" );
		return;
	}

	common->Printf( "\n" );
}

/*
==================
R_FindARBProgram

Returns a GL identifier that can be bound to the given target, parsing
a text file if it hasn't already been loaded.
==================
*/
int R_FindARBProgram( GLenum target, const char *program ) {
	int		i;
	idStr	stripped = program;

	stripped.StripFileExtension();

	// see if it is already loaded
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		if ( progs[i].target != target ) {
			continue;
		}

		idStr	compare = progs[i].name;
		compare.StripFileExtension();

		if ( !idStr::Icmp( stripped.c_str(), compare.c_str() ) ) {
			return progs[i].ident;
		}
	}

	if ( i == MAX_GLPROGS ) {
		common->Error( "R_FindARBProgram: MAX_GLPROGS" );
	}

	// add it to the list and load it
	progs[i].ident = (program_t)0;	// will be gen'd by R_LoadARBProgram
	progs[i].target = target;
	strncpy( progs[i].name, program, sizeof( progs[i].name ) - 1 );

	R_LoadARBProgram( i );

	return progs[i].ident;
}

/*
==================
R_ReloadARBPrograms_f
==================
*/
void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	int		i;

	common->Printf( "----- R_ReloadARBPrograms -----\n" );
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		R_LoadARBProgram( i );
	}
	common->Printf( "-------------------------------\n" );
}

/*
==================
R_ARB2_Init

==================
*/
void R_ARB2_Init( void ) {
	glConfig.allowARB2Path = false;

	common->Printf( "---------- R_ARB2_Init ----------\n" );

	if ( !glConfig.ARBVertexProgramAvailable || !glConfig.ARBFragmentProgramAvailable ) {
		common->Printf( "Not available.\n" );
		return;
	}

	common->Printf( "Available.\n" );

	common->Printf( "---------------------------------\n" );

	glConfig.allowARB2Path = true;
}

