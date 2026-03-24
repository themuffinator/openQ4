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

static bool RB_ImageIsCurrentRender( const idImage *image ) {
	if ( image == NULL ) {
		return false;
	}

	if ( image == globalImages->currentRenderImage || image == globalImages->originalCurrentRenderImage ) {
		return true;
	}

	const char *name = image->GetName();
	if ( name == NULL ) {
		return false;
	}

	return idStr::Icmpn( name, "_currentRender", 14 ) == 0;
}

static bool RB_StageUsesCurrentRender( const shaderStage_t *stage ) {
	if ( stage == NULL ) {
		return false;
	}

	if ( RB_ImageIsCurrentRender( stage->texture.image ) ) {
		return true;
	}

	const newShaderStage_t *newStage = stage->newStage;
	if ( newStage == NULL ) {
		return false;
	}

	for ( int i = 0; i < newStage->numFragmentProgramImages; i++ ) {
		if ( RB_ImageIsCurrentRender( newStage->fragmentProgramImages[i] ) ) {
			return true;
		}
	}

	for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
		if ( RB_ImageIsCurrentRender( newStage->shaderTextureImages[i] ) ) {
			return true;
		}
	}

	return false;
}

static inline void RB_SetStageVertexColorPointer( const drawSurf_t *surf, int stage, idDrawVert *ac ) {
	if ( surf->decalColorCache != NULL && stage >= 0 && stage < surf->decalColorStageCount && surf->decalColorStride > 0 ) {
		byte *colorData = (byte *)vertexCache.Position( surf->decalColorCache );
		glColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorData + stage * surf->decalColorStride );
		return;
	}

	glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), (void *)&ac->color );
}

static bool RB_UseAlphaToCoverage( const idMaterial *shader ) {
	if ( !r_msaaAlphaToCoverage.GetBool() ) {
		return false;
	}

	if ( shader == NULL || shader->Coverage() != MC_PERFORATED ) {
		return false;
	}

	if ( !( GLEW_ARB_multisample || GLEW_VERSION_1_3 ) ) {
		return false;
	}

	if ( backEnd.renderTexture == NULL || backEnd.renderTexture->GetNumColorImages() <= 0 ) {
		return false;
	}

	idImage *colorImage = backEnd.renderTexture->GetColorImage( 0 );
	if ( colorImage == NULL ) {
		return false;
	}

	return colorImage->GetOpts().numMSAASamples > 1;
}

static void RB_FreeGLSLProgram( newShaderStage_t *stage ) {
	if ( stage == NULL ) {
		return;
	}

	if ( stage->glslProgramObject != 0 ) {
		if ( stage->glslVertexShaderObject != 0 ) {
			glDetachObjectARB(
				(GLhandleARB)stage->glslProgramObject,
				(GLhandleARB)stage->glslVertexShaderObject );
			glDeleteObjectARB( (GLhandleARB)stage->glslVertexShaderObject );
		}
		if ( stage->glslFragmentShaderObject != 0 ) {
			glDetachObjectARB(
				(GLhandleARB)stage->glslProgramObject,
				(GLhandleARB)stage->glslFragmentShaderObject );
			glDeleteObjectARB( (GLhandleARB)stage->glslFragmentShaderObject );
		}
		glDeleteObjectARB( (GLhandleARB)stage->glslProgramObject );
	}

	stage->glslProgramObject = 0;
	stage->glslVertexShaderObject = 0;
	stage->glslFragmentShaderObject = 0;
	stage->glslProgramLoaded = false;
	stage->glslProgramValid = false;
	stage->glslProgramGeneration = 0;
}

static void RB_PrintGLSLInfoLog( GLhandleARB object, const char *label, const char *name ) {
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

static bool RB_PathHasGlprogsPrefix( const idStr &path ) {
	return idStr::Icmpn( path.c_str(), "glprogs/", 8 ) == 0;
}

static idStr RB_NormalizeGLSLPath( const idStr &path ) {
	idStr result = path;
	result.BackSlashesToSlashes();
	if ( !RB_PathHasGlprogsPrefix( result ) ) {
		idStr prefixed = "glprogs/";
		prefixed += result;
		return prefixed;
	}
	return result;
}

static bool RB_ReadGLSLSourcePair( const idStr &vertexPath, const idStr &fragmentPath, char **vertexBuffer, char **fragmentBuffer ) {
	*vertexBuffer = NULL;
	*fragmentBuffer = NULL;

	fileSystem->ReadFile( vertexPath.c_str(), (void **)vertexBuffer, NULL );
	if ( *vertexBuffer == NULL ) {
		return false;
	}

	fileSystem->ReadFile( fragmentPath.c_str(), (void **)fragmentBuffer, NULL );
	if ( *fragmentBuffer == NULL ) {
		fileSystem->FreeFile( *vertexBuffer );
		*vertexBuffer = NULL;
		return false;
	}

	return true;
}

static bool RB_FindGLSLSourcePair( const char *programName, idStr &vertexPath, idStr &fragmentPath, char **vertexBuffer, char **fragmentBuffer ) {
	idStr name = programName;
	name.BackSlashesToSlashes();

	idStr stripped = name;
	stripped.StripFileExtension();

	idStr ext;
	const char *dot = strrchr( name.c_str(), '.' );
	if ( dot != NULL ) {
		ext = dot + 1;
		ext.ToLower();
	}

	idStr vertexCandidates[10];
	idStr fragmentCandidates[10];
	int numCandidates = 0;

	if ( ext.Length() > 0 ) {
		if ( ext == "glsl" ) {
			vertexCandidates[numCandidates] = stripped + ".glslvp";
			fragmentCandidates[numCandidates++] = stripped + ".glslfp";
			vertexCandidates[numCandidates] = stripped + ".vs";
			fragmentCandidates[numCandidates++] = stripped + ".fs";
		} else if ( ext == "fs" ) {
			vertexCandidates[numCandidates] = stripped + ".vs";
			fragmentCandidates[numCandidates++] = name;
		} else if ( ext == "vs" ) {
			vertexCandidates[numCandidates] = name;
			fragmentCandidates[numCandidates++] = stripped + ".fs";
		} else if ( ext == "fp" ) {
			vertexCandidates[numCandidates] = stripped + ".vp";
			fragmentCandidates[numCandidates++] = name;
		} else if ( ext == "vp" ) {
			vertexCandidates[numCandidates] = name;
			fragmentCandidates[numCandidates++] = stripped + ".fp";
		}
	}

	vertexCandidates[numCandidates] = name + ".vs";
	fragmentCandidates[numCandidates++] = name + ".fs";
	vertexCandidates[numCandidates] = name + ".glslvp";
	fragmentCandidates[numCandidates++] = name + ".glslfp";
	vertexCandidates[numCandidates] = name + ".vp";
	fragmentCandidates[numCandidates++] = name + ".fp";
	vertexCandidates[numCandidates] = stripped + ".vs";
	fragmentCandidates[numCandidates++] = stripped + ".fs";
	vertexCandidates[numCandidates] = stripped + ".glslvp";
	fragmentCandidates[numCandidates++] = stripped + ".glslfp";
	vertexCandidates[numCandidates] = stripped + ".vp";
	fragmentCandidates[numCandidates++] = stripped + ".fp";

	for ( int i = 0; i < numCandidates; i++ ) {
		const idStr candidateVertex = RB_NormalizeGLSLPath( vertexCandidates[i] );
		const idStr candidateFragment = RB_NormalizeGLSLPath( fragmentCandidates[i] );
		if ( RB_ReadGLSLSourcePair( candidateVertex, candidateFragment, vertexBuffer, fragmentBuffer ) ) {
			vertexPath = candidateVertex;
			fragmentPath = candidateFragment;
			return true;
		}
	}

	return false;
}

bool R_ValidateGLSLProgram( newShaderStage_t *stage ) {
	if ( !stage->glslProgram ) {
		return false;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		return false;
	}

	if ( stage->glslProgramLoaded && stage->glslProgramGeneration == tr.videoRestartCount ) {
		return stage->glslProgramValid;
	}

	RB_FreeGLSLProgram( stage );

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	idStr vertexPath;
	idStr fragmentPath;
	if ( !RB_FindGLSLSourcePair( stage->glslProgramName, vertexPath, fragmentPath, &vertexBuffer, &fragmentBuffer ) ) {
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		common->Warning( "Couldn't find GLSL sources for program '%s'", stage->glslProgramName );
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
		RB_PrintGLSLInfoLog( vertexShader, "vertex shader compile", stage->glslProgramName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( fragmentShader, "fragment shader compile", stage->glslProgramName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( programObject, "program link", stage->glslProgramName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		return false;
	}

	stage->glslProgramObject = (int)programObject;
	stage->glslVertexShaderObject = (int)vertexShader;
	stage->glslFragmentShaderObject = (int)fragmentShader;
	stage->glslProgramLoaded = true;
	stage->glslProgramValid = true;
	stage->glslProgramGeneration = tr.videoRestartCount;

	for ( int i = 0; i < stage->numShaderParms; i++ ) {
		stage->shaderParmLocations[i] = glGetUniformLocationARB( programObject, stage->shaderParmNames[i] );
	}
	for ( int i = 0; i < stage->numShaderTextures; i++ ) {
		stage->shaderTextureLocations[i] = glGetUniformLocationARB( programObject, stage->shaderTextureNames[i] );
		if ( stage->shaderTextureLocations[i] < 0 ) {
			common->Warning(
				"GLSL program '%s' is missing sampler uniform '%s' declared by the material stage.",
				stage->glslProgramName,
				stage->shaderTextureNames[i] );
			RB_FreeGLSLProgram( stage );
			return false;
		}
	}

	common->Printf( "Loaded GLSL program '%s' (%s, %s)\n",
		stage->glslProgramName, vertexPath.c_str(), fragmentPath.c_str() );

	return true;
}

static bool RB_IsMainScenePostProcessView( void ) {
	if ( !backEnd.viewDef ) {
		return false;
	}

	// Fullscreen 2D GUI/menu passes are emitted as standalone views without
	// view entities. Skip scene post-process passes on those views so menu
	// assets stay unfiltered while mirrors, cameras, and other 3D subviews
	// follow the same scene post stack as the main world view.
	if ( backEnd.viewDef->viewEntitys == NULL ) {
		return false;
	}

	// X-ray subviews intentionally diverge from the normal scene shading path.
	return !backEnd.viewDef->isXraySubview;
}

static void RB_BeginFullscreenPostProcessPass( int scissorX, int scissorY, int scissorWidth, int scissorHeight ) {
	// Fullscreen post-process passes must never inherit stale light/material scissors.
	glEnable( GL_SCISSOR_TEST );
	glScissor( scissorX, scissorY, scissorWidth, scissorHeight );

	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, 1, 0, 1, -1, 1 );

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_Cull( CT_TWO_SIDED );
	GL_SelectTexture( 0 );
	glDisable( GL_TEXTURE_GEN_S );
	glDisable( GL_TEXTURE_GEN_T );
	glDisable( GL_TEXTURE_GEN_R );
	glDisable( GL_TEXTURE_GEN_Q );
	glMatrixMode( GL_TEXTURE );
	glLoadIdentity();
	glMatrixMode( GL_MODELVIEW );
	GL_SelectTexture( 1 );
	glDisable( GL_TEXTURE_GEN_S );
	glDisable( GL_TEXTURE_GEN_T );
	glDisable( GL_TEXTURE_GEN_R );
	glDisable( GL_TEXTURE_GEN_Q );
	glMatrixMode( GL_TEXTURE );
	glLoadIdentity();
	glMatrixMode( GL_MODELVIEW );
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
}

static void RB_DrawFullscreenPostProcessQuad( int viewportWidth, int viewportHeight, int textureWidth, int textureHeight ) {
	const float maxS = static_cast<float>( viewportWidth ) / static_cast<float>( textureWidth );
	const float maxT = static_cast<float>( viewportHeight ) / static_cast<float>( textureHeight );

	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glBegin( GL_QUADS );
	glTexCoord2f( 0.0f, 0.0f );
	glVertex2f( 0.0f, 0.0f );
	glTexCoord2f( 0.0f, maxT );
	glVertex2f( 0.0f, 1.0f );
	glTexCoord2f( maxS, maxT );
	glVertex2f( 1.0f, 1.0f );
	glTexCoord2f( maxS, 0.0f );
	glVertex2f( 1.0f, 0.0f );
	glEnd();
}

static void RB_DrawFullscreenPostProcessQuadUnitUV( void ) {
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glBegin( GL_QUADS );
	glTexCoord2f( 0.0f, 0.0f );
	glVertex2f( 0.0f, 0.0f );
	glTexCoord2f( 0.0f, 1.0f );
	glVertex2f( 0.0f, 1.0f );
	glTexCoord2f( 1.0f, 1.0f );
	glVertex2f( 1.0f, 1.0f );
	glTexCoord2f( 1.0f, 0.0f );
	glVertex2f( 1.0f, 0.0f );
	glEnd();
}

static void RB_EndFullscreenPostProcessPass( void ) {
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glEnable( GL_DEPTH_TEST );
	glEnable( GL_STENCIL_TEST );
	glMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_FRONT_SIDED );
}

struct rbBuiltinUniformDef_t {
	const char *name;
	int components;
};

enum rbSSAOUniformIndex_t {
	RB_SSAO_UNIFORM_INV_TEX_SIZE = 0,
	RB_SSAO_UNIFORM_PROJECTION_INFO,
	RB_SSAO_UNIFORM_DEPTH_PROJECTION,
	RB_SSAO_UNIFORM_PROJECTION_SCALE,
	RB_SSAO_UNIFORM_RADIUS,
	RB_SSAO_UNIFORM_BIAS,
	RB_SSAO_UNIFORM_INTENSITY,
	RB_SSAO_UNIFORM_POWER,
	RB_SSAO_UNIFORM_MAX_DISTANCE,
	RB_SSAO_UNIFORM_SAMPLE_COUNT,
	RB_SSAO_UNIFORM_DEBUG_VIEW,
	RB_SSAO_UNIFORM_COUNT
};

static newShaderStage_t rbSSAOStage;
static bool rbSSAOStageInitialized = false;

static void RB_InitSSAOStage( void ) {
	if ( rbSSAOStageInitialized ) {
		return;
	}

	memset( &rbSSAOStage, 0, sizeof( rbSSAOStage ) );
	rbSSAOStage.glslProgram = true;
	idStr::Copynz( rbSSAOStage.glslProgramName, "openprey_ssao.fs", sizeof( rbSSAOStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_SSAO_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "projectionInfo", 4 },
		{ "depthProjection", 2 },
		{ "projectionScale", 1 },
		{ "ssaoRadius", 1 },
		{ "ssaoBias", 1 },
		{ "ssaoIntensity", 1 },
		{ "ssaoPower", 1 },
		{ "ssaoMaxDistance", 1 },
		{ "ssaoSampleCount", 1 },
		{ "ssaoDebugView", 1 }
	};

	rbSSAOStage.numShaderParms = RB_SSAO_UNIFORM_COUNT;
	for ( int i = 0; i < RB_SSAO_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbSSAOStage.shaderParmNames[i], uniforms[i].name, sizeof( rbSSAOStage.shaderParmNames[i] ) );
		rbSSAOStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbSSAOStage.numShaderTextures = 2;
	idStr::Copynz( rbSSAOStage.shaderTextureNames[0], "Scene", sizeof( rbSSAOStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbSSAOStage.shaderTextureNames[1], "DepthBuffer", sizeof( rbSSAOStage.shaderTextureNames[1] ) );

	rbSSAOStageInitialized = true;
}

static void RB_STD_SSAO( void ) {
	if ( r_skipPostProcess.GetBool() || !r_ssao.GetBool() ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	if ( !RB_IsMainScenePostProcessView() ) {
		return;
	}

	const GLfloat radius = r_ssaoRadius.GetFloat();
	const GLfloat intensity = r_ssaoIntensity.GetFloat();
	if ( radius <= 0.0f || intensity <= 0.0f ) {
		return;
	}

	RB_InitSSAOStage();
	if ( !R_ValidateGLSLProgram( &rbSSAOStage ) ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	idImage *depthImage = globalImages->currentDepthImage;
	if ( sceneImage == NULL || depthImage == NULL ) {
		return;
	}

	const GLfloat projX = backEnd.viewDef->projectionMatrix[0];
	const GLfloat projY = backEnd.viewDef->projectionMatrix[5];
	if ( idMath::Fabs( projX ) <= 0.00001f || idMath::Fabs( projY ) <= 0.00001f ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_SSAO ----------\n" );

	sceneImage->CopyFramebuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	depthImage->CopyDepthbuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	const int depthTextureWidth = depthImage->GetOpts().width;
	const int depthTextureHeight = depthImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 || depthTextureWidth <= 0 || depthTextureHeight <= 0 ) {
		return;
	}

	backEnd.currentScissor = backEnd.viewDef->scissor;

	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_SelectTexture( 1 );
	depthImage->Bind();
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbSSAOStage.glslProgramObject );

	const int sceneLocation = rbSSAOStage.shaderTextureLocations[0];
	if ( sceneLocation >= 0 ) {
		glUniform1iARB( sceneLocation, 0 );
	}

	const int depthLocation = rbSSAOStage.shaderTextureLocations[1];
	if ( depthLocation >= 0 ) {
		glUniform1iARB( depthLocation, 1 );
	}

	const GLfloat invTexSize[2] = {
		1.0f / static_cast<GLfloat>( depthTextureWidth ),
		1.0f / static_cast<GLfloat>( depthTextureHeight )
	};
	const GLfloat projectionInfo[4] = {
		1.0f / projX,
		1.0f / projY,
		backEnd.viewDef->projectionMatrix[8],
		backEnd.viewDef->projectionMatrix[9]
	};
	const GLfloat depthProjection[2] = {
		backEnd.viewDef->projectionMatrix[10],
		backEnd.viewDef->projectionMatrix[14]
	};
	const GLfloat projectionScale = 0.5f * static_cast<GLfloat>( depthTextureHeight ) * idMath::Fabs( projY );
	const GLfloat bias = r_ssaoBias.GetFloat();
	const GLfloat power = r_ssaoPower.GetFloat();
	const GLfloat maxDistance = r_ssaoMaxDistance.GetFloat();
	const GLfloat sampleCount = static_cast<GLfloat>( idMath::ClampInt( 4, 32, r_ssaoSamples.GetInteger() ) );
	const GLfloat debugView = r_ssaoDebug.GetBool() ? 1.0f : 0.0f;

	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_INV_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_PROJECTION_INFO] >= 0 ) {
		glUniform4fvARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_PROJECTION_INFO], 1, projectionInfo );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_DEPTH_PROJECTION] >= 0 ) {
		glUniform2fvARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_DEPTH_PROJECTION], 1, depthProjection );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_PROJECTION_SCALE] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_PROJECTION_SCALE], projectionScale );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_RADIUS] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_RADIUS], radius );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_BIAS] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_BIAS], bias );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_INTENSITY] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_INTENSITY], intensity );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_POWER] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_POWER], power );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_MAX_DISTANCE] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_MAX_DISTANCE], maxDistance );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_SAMPLE_COUNT] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_SAMPLE_COUNT], sampleCount );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_DEBUG_VIEW] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_DEBUG_VIEW], debugView );
	}

	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	RB_EndFullscreenPostProcessPass();
}

enum rbBloomExtractUniformIndex_t {
	RB_BLOOM_EXTRACT_UNIFORM_INV_TEX_SIZE = 0,
	RB_BLOOM_EXTRACT_UNIFORM_THRESHOLD,
	RB_BLOOM_EXTRACT_UNIFORM_SOFT_KNEE,
	RB_BLOOM_EXTRACT_UNIFORM_COUNT
};

enum rbBloomBlurUniformIndex_t {
	RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE = 0,
	RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS,
	RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS,
	RB_BLOOM_BLUR_UNIFORM_COUNT
};

enum rbBloomCompositeUniformIndex_t {
	RB_BLOOM_COMPOSITE_UNIFORM_INTENSITY = 0,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_ENABLED,
	RB_BLOOM_COMPOSITE_UNIFORM_TONEMAP_ENABLED,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_EXPOSURE,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_WHITE_POINT,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_LIFT,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_POST_GAMMA,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAIN,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_VIBRANCE,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_SATURATION,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_CONTRAST,
	RB_BLOOM_COMPOSITE_UNIFORM_COUNT
};

static newShaderStage_t rbBloomExtractStage;
static newShaderStage_t rbBloomBlurStage;
static newShaderStage_t rbBloomCompositeStage;
static bool rbBloomStagesInitialized = false;
static idImage *rbBloomImages[2] = { NULL, NULL };
static idRenderTexture *rbBloomRenderTextures[2] = { NULL, NULL };

static void RB_InitBloomStages( void ) {
	if ( rbBloomStagesInitialized ) {
		return;
	}

	memset( &rbBloomExtractStage, 0, sizeof( rbBloomExtractStage ) );
	rbBloomExtractStage.glslProgram = true;
	idStr::Copynz( rbBloomExtractStage.glslProgramName, "openprey_bloom_extract.fs", sizeof( rbBloomExtractStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t extractUniforms[RB_BLOOM_EXTRACT_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "bloomThreshold", 1 },
		{ "bloomSoftKnee", 1 }
	};

	rbBloomExtractStage.numShaderParms = RB_BLOOM_EXTRACT_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_EXTRACT_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomExtractStage.shaderParmNames[i], extractUniforms[i].name, sizeof( rbBloomExtractStage.shaderParmNames[i] ) );
		rbBloomExtractStage.shaderParmNumRegisters[i] = extractUniforms[i].components;
	}
	rbBloomExtractStage.numShaderTextures = 1;
	idStr::Copynz( rbBloomExtractStage.shaderTextureNames[0], "Scene", sizeof( rbBloomExtractStage.shaderTextureNames[0] ) );

	memset( &rbBloomBlurStage, 0, sizeof( rbBloomBlurStage ) );
	rbBloomBlurStage.glslProgram = true;
	idStr::Copynz( rbBloomBlurStage.glslProgramName, "openprey_bloom_blur.fs", sizeof( rbBloomBlurStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t blurUniforms[RB_BLOOM_BLUR_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "blurAxis", 2 },
		{ "blurRadius", 1 }
	};

	rbBloomBlurStage.numShaderParms = RB_BLOOM_BLUR_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_BLUR_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomBlurStage.shaderParmNames[i], blurUniforms[i].name, sizeof( rbBloomBlurStage.shaderParmNames[i] ) );
		rbBloomBlurStage.shaderParmNumRegisters[i] = blurUniforms[i].components;
	}
	rbBloomBlurStage.numShaderTextures = 1;
	idStr::Copynz( rbBloomBlurStage.shaderTextureNames[0], "Scene", sizeof( rbBloomBlurStage.shaderTextureNames[0] ) );

	memset( &rbBloomCompositeStage, 0, sizeof( rbBloomCompositeStage ) );
	rbBloomCompositeStage.glslProgram = true;
	idStr::Copynz( rbBloomCompositeStage.glslProgramName, "openprey_bloom.fs", sizeof( rbBloomCompositeStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t compositeUniforms[RB_BLOOM_COMPOSITE_UNIFORM_COUNT] = {
		{ "bloomIntensity", 1 },
		{ "bloomEnabled", 1 },
		{ "toneMapEnabled", 1 },
		{ "hdrExposure", 1 },
		{ "hdrWhitePoint", 1 },
		{ "hdrLift", 1 },
		{ "hdrPostGamma", 1 },
		{ "hdrGain", 1 },
		{ "hdrVibrance", 1 },
		{ "hdrSaturation", 1 },
		{ "hdrContrast", 1 }
	};

	rbBloomCompositeStage.numShaderParms = RB_BLOOM_COMPOSITE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_COMPOSITE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomCompositeStage.shaderParmNames[i], compositeUniforms[i].name, sizeof( rbBloomCompositeStage.shaderParmNames[i] ) );
		rbBloomCompositeStage.shaderParmNumRegisters[i] = compositeUniforms[i].components;
	}
	rbBloomCompositeStage.numShaderTextures = 2;
	idStr::Copynz( rbBloomCompositeStage.shaderTextureNames[0], "Scene", sizeof( rbBloomCompositeStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbBloomCompositeStage.shaderTextureNames[1], "BloomTex", sizeof( rbBloomCompositeStage.shaderTextureNames[1] ) );

	rbBloomStagesInitialized = true;
}

static bool RB_EnsureBloomRenderTextures( int viewportWidth, int viewportHeight ) {
	const int bloomWidth = idMath::ClampInt( 1, viewportWidth, ( viewportWidth + 1 ) / 2 );
	const int bloomHeight = idMath::ClampInt( 1, viewportHeight, ( viewportHeight + 1 ) / 2 );
	static const char *imageNames[2] = { "_bloomPing0", "_bloomPing1" };

	for ( int i = 0; i < 2; i++ ) {
		if ( rbBloomImages[i] == NULL ) {
			idImageOpts opts;
			opts.textureType = TT_2D;
			opts.format = FMT_RGBA16F;
			opts.width = bloomWidth;
			opts.height = bloomHeight;
			opts.numLevels = 1;
			opts.isPersistant = true;
			rbBloomImages[i] = globalImages->ScratchImage( imageNames[i], &opts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
		}

		if ( rbBloomRenderTextures[i] == NULL ) {
			if ( rbBloomImages[i] == NULL ) {
				return false;
			}
			if ( rbBloomImages[i]->GetOpts().width != bloomWidth || rbBloomImages[i]->GetOpts().height != bloomHeight ) {
				tr.ResizeImage( rbBloomImages[i], bloomWidth, bloomHeight );
			}
			rbBloomRenderTextures[i] = tr.CreateRenderTexture( rbBloomImages[i], NULL );
		} else if ( rbBloomRenderTextures[i]->GetWidth() != bloomWidth || rbBloomRenderTextures[i]->GetHeight() != bloomHeight ) {
			tr.ResizeRenderTexture( rbBloomRenderTextures[i], bloomWidth, bloomHeight );
		}

		if ( rbBloomImages[i] == NULL || rbBloomRenderTextures[i] == NULL ) {
			return false;
		}
	}

	return true;
}

static void RB_BindBloomRenderTexture( idRenderTexture *renderTexture, int width, int height ) {
	backEnd.renderTexture = renderTexture;
	renderTexture->MakeCurrent();
	glViewport( 0, 0, width, height );
	glScissor( 0, 0, width, height );
}

static void RB_RestoreBloomTarget( idRenderTexture *renderTexture, int viewportWidth, int viewportHeight ) {
	backEnd.renderTexture = renderTexture;
	if ( renderTexture != NULL ) {
		renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}

	glViewport(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	glScissor(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
	backEnd.currentScissor = backEnd.viewDef->scissor;
}

static void RB_STD_Bloom( void ) {
	if ( r_skipPostProcess.GetBool() ) {
		return;
	}

	const bool bloomRequested = r_bloom.GetBool() && !r_skipGlowOverlay.GetBool();
	const bool toneMapEnabled = r_hdrToneMap.GetBool();

	if ( !bloomRequested && !toneMapEnabled ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	if ( !RB_IsMainScenePostProcessView() ) {
		return;
	}

	RB_InitBloomStages();
	if ( !R_ValidateGLSLProgram( &rbBloomCompositeStage ) ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_Bloom ----------\n" );

	sceneImage->CopyFramebuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	const GLfloat hdrExposure = r_hdrExposure.GetFloat();
	const GLfloat hdrWhitePoint = r_hdrWhitePoint.GetFloat();
	const GLfloat hdrLift = r_hdrLift.GetFloat();
	const GLfloat hdrPostGamma = r_hdrPostGamma.GetFloat();
	const GLfloat hdrGain = r_hdrGain.GetFloat();
	const GLfloat hdrVibrance = r_hdrVibrance.GetFloat();
	const GLfloat hdrSaturation = r_hdrSaturation.GetFloat();
	const GLfloat hdrContrast = r_hdrContrast.GetFloat();
	const GLfloat bloomIntensity = bloomRequested ? r_bloomIntensity.GetFloat() : 0.0f;
	const GLfloat bloomRadius = Max( r_bloomRadius.GetFloat(), 0.1f );
	const GLfloat bloomThreshold = r_bloomThreshold.GetFloat();
	const GLfloat bloomSoftKnee = r_bloomSoftKnee.GetFloat();
	const GLfloat toneMapToggle = toneMapEnabled ? 1.0f : 0.0f;

	idImage *bloomImage = globalImages->blackImage;
	const idRenderTexture *originalRenderTexture = backEnd.renderTexture;
	bool bloomEnabled = false;

	if ( bloomRequested ) {
		RB_InitBloomStages();
		if ( R_ValidateGLSLProgram( &rbBloomExtractStage ) &&
			R_ValidateGLSLProgram( &rbBloomBlurStage ) &&
			RB_EnsureBloomRenderTextures( viewportWidth, viewportHeight ) ) {
			const int bloomWidth = rbBloomRenderTextures[0]->GetWidth();
			const int bloomHeight = rbBloomRenderTextures[0]->GetHeight();
			const GLfloat sceneInvTexSize[2] = {
				1.0f / static_cast<GLfloat>( Max( 1, textureWidth ) ),
				1.0f / static_cast<GLfloat>( Max( 1, textureHeight ) )
			};
			const GLfloat bloomInvTexSize[2] = {
				1.0f / static_cast<GLfloat>( Max( 1, bloomWidth ) ),
				1.0f / static_cast<GLfloat>( Max( 1, bloomHeight ) )
			};

			RB_BindBloomRenderTexture( rbBloomRenderTextures[0], bloomWidth, bloomHeight );
			RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
			GL_SelectTexture( 0 );
			sceneImage->Bind();
			glUseProgramObjectARB( (GLhandleARB)rbBloomExtractStage.glslProgramObject );
			if ( rbBloomExtractStage.shaderTextureLocations[0] >= 0 ) {
				glUniform1iARB( rbBloomExtractStage.shaderTextureLocations[0], 0 );
			}
			if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_INV_TEX_SIZE] >= 0 ) {
				glUniform2fvARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_INV_TEX_SIZE], 1, sceneInvTexSize );
			}
			if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_THRESHOLD] >= 0 ) {
				glUniform1fARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_THRESHOLD], bloomThreshold );
			}
			if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_SOFT_KNEE] >= 0 ) {
				glUniform1fARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_SOFT_KNEE], bloomSoftKnee );
			}
			RB_DrawFullscreenPostProcessQuadUnitUV();
			glUseProgramObjectARB( 0 );
			RB_EndFullscreenPostProcessPass();

			RB_BindBloomRenderTexture( rbBloomRenderTextures[1], bloomWidth, bloomHeight );
			RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
			GL_SelectTexture( 0 );
			rbBloomImages[0]->Bind();
			glUseProgramObjectARB( (GLhandleARB)rbBloomBlurStage.glslProgramObject );
			if ( rbBloomBlurStage.shaderTextureLocations[0] >= 0 ) {
				glUniform1iARB( rbBloomBlurStage.shaderTextureLocations[0], 0 );
			}
			if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE] >= 0 ) {
				glUniform2fvARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE], 1, bloomInvTexSize );
			}
			if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS] >= 0 ) {
				const GLfloat blurAxisX[2] = { 1.0f, 0.0f };
				glUniform2fvARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS], 1, blurAxisX );
			}
			if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS] >= 0 ) {
				glUniform1fARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS], bloomRadius );
			}
			RB_DrawFullscreenPostProcessQuadUnitUV();
			glUseProgramObjectARB( 0 );
			RB_EndFullscreenPostProcessPass();

			RB_BindBloomRenderTexture( rbBloomRenderTextures[0], bloomWidth, bloomHeight );
			RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
			GL_SelectTexture( 0 );
			rbBloomImages[1]->Bind();
			glUseProgramObjectARB( (GLhandleARB)rbBloomBlurStage.glslProgramObject );
			if ( rbBloomBlurStage.shaderTextureLocations[0] >= 0 ) {
				glUniform1iARB( rbBloomBlurStage.shaderTextureLocations[0], 0 );
			}
			if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE] >= 0 ) {
				glUniform2fvARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE], 1, bloomInvTexSize );
			}
			if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS] >= 0 ) {
				const GLfloat blurAxisY[2] = { 0.0f, 1.0f };
				glUniform2fvARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS], 1, blurAxisY );
			}
			if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS] >= 0 ) {
				glUniform1fARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS], bloomRadius );
			}
			RB_DrawFullscreenPostProcessQuadUnitUV();
			glUseProgramObjectARB( 0 );
			RB_EndFullscreenPostProcessPass();

			bloomImage = rbBloomImages[0];
			bloomEnabled = true;
		}
	}

	RB_RestoreBloomTarget( const_cast<idRenderTexture *>( originalRenderTexture ), viewportWidth, viewportHeight );
	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_SelectTexture( 1 );
	bloomImage->Bind();
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbBloomCompositeStage.glslProgramObject );
	if ( rbBloomCompositeStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbBloomCompositeStage.shaderTextureLocations[0], 0 );
	}
	if ( rbBloomCompositeStage.shaderTextureLocations[1] >= 0 ) {
		glUniform1iARB( rbBloomCompositeStage.shaderTextureLocations[1], 1 );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_INTENSITY] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_INTENSITY], bloomIntensity );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_ENABLED] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_ENABLED], bloomEnabled ? 1.0f : 0.0f );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_TONEMAP_ENABLED] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_TONEMAP_ENABLED], toneMapToggle );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_EXPOSURE] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_EXPOSURE], hdrExposure );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_WHITE_POINT] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_WHITE_POINT], hdrWhitePoint );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_LIFT] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_LIFT], hdrLift );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_POST_GAMMA] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_POST_GAMMA], hdrPostGamma );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAIN] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAIN], hdrGain );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_VIBRANCE] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_VIBRANCE], hdrVibrance );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_SATURATION] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_SATURATION], hdrSaturation );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_CONTRAST] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_CONTRAST], hdrContrast );
	}

	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );
	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	RB_EndFullscreenPostProcessPass();

	if ( originalRenderTexture != NULL ) {
		sceneImage->CopyFramebuffer(
			backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y1,
			viewportWidth,
			viewportHeight );
		backEnd.currentRenderCopied = true;
	}
}

enum rbResolutionScaleUniformIndex_t {
	RB_RES_SCALE_UNIFORM_INV_TEX_SIZE = 0,
	RB_RES_SCALE_UNIFORM_INV_LOW_RES_SIZE,
	RB_RES_SCALE_UNIFORM_SHARPEN_AMOUNT,
	RB_RES_SCALE_UNIFORM_COUNT
};

static newShaderStage_t rbResolutionScaleStage;
static bool rbResolutionScaleStageInitialized = false;

static void RB_InitResolutionScaleStage( void ) {
	if ( rbResolutionScaleStageInitialized ) {
		return;
	}

	memset( &rbResolutionScaleStage, 0, sizeof( rbResolutionScaleStage ) );
	rbResolutionScaleStage.glslProgram = true;
	idStr::Copynz( rbResolutionScaleStage.glslProgramName, "openq4_resolutionscale.fs", sizeof( rbResolutionScaleStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_RES_SCALE_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "invLowResSize", 2 },
		{ "sharpenAmount", 1 }
	};

	rbResolutionScaleStage.numShaderParms = RB_RES_SCALE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_RES_SCALE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbResolutionScaleStage.shaderParmNames[i], uniforms[i].name, sizeof( rbResolutionScaleStage.shaderParmNames[i] ) );
		rbResolutionScaleStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbResolutionScaleStage.numShaderTextures = 1;
	idStr::Copynz( rbResolutionScaleStage.shaderTextureNames[0], "Scene", sizeof( rbResolutionScaleStage.shaderTextureNames[0] ) );

	rbResolutionScaleStageInitialized = true;
}

void RB_ApplyResolutionScaleToBackBuffer( void ) {
	if ( r_skipPostProcess.GetBool() ) {
		return;
	}

	const int scalePercent = idMath::ClampInt( 10, 100, r_screenFraction.GetInteger() );
	if ( scalePercent >= 100 ) {
		return;
	}

	int mode = idMath::ClampInt( 0, 2, r_resolutionScaleMode.GetInteger() );
	if ( mode == 0 ) {
		// Legacy path: BeginFrame crop mode without fullscreen upscale.
		return;
	}

	const int viewportWidth = glConfig.vidWidth;
	const int viewportHeight = glConfig.vidHeight;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	const int sourceWidth = idMath::ClampInt( 1, viewportWidth,
		idMath::Ftoi( static_cast<float>( viewportWidth ) * ( static_cast<float>( scalePercent ) * 0.01f ) + 0.5f ) );
	const int sourceHeight = idMath::ClampInt( 1, viewportHeight,
		idMath::Ftoi( static_cast<float>( viewportHeight ) * ( static_cast<float>( scalePercent ) * 0.01f ) + 0.5f ) );
	if ( sourceWidth <= 0 || sourceHeight <= 0 ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	RB_InitResolutionScaleStage();
	if ( !R_ValidateGLSLProgram( &rbResolutionScaleStage ) ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_ApplyResolutionScaleToBackBuffer ----------\n" );

	idRenderTexture::BindNull();
	backEnd.renderTexture = NULL;
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
	glViewport( 0, 0, viewportWidth, viewportHeight );
	glScissor( 0, 0, viewportWidth, viewportHeight );

	// Copy the full back buffer; the resolution-scale shader samples this image
	// on a reduced grid so output always fills the screen.
	sceneImage->CopyFramebuffer( 0, 0, viewportWidth, viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	RB_BeginFullscreenPostProcessPass( 0, 0, viewportWidth, viewportHeight );
	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_TexEnv( GL_MODULATE );

	glUseProgramObjectARB( (GLhandleARB)rbResolutionScaleStage.glslProgramObject );

	const int sceneLocation = rbResolutionScaleStage.shaderTextureLocations[0];
	if ( sceneLocation >= 0 ) {
		glUniform1iARB( sceneLocation, 0 );
	}

	const GLfloat invTexSize[2] = {
		1.0f / static_cast<GLfloat>( textureWidth ),
		1.0f / static_cast<GLfloat>( textureHeight )
	};
	const GLfloat invLowResSize[2] = {
		1.0f / static_cast<GLfloat>( sourceWidth ),
		1.0f / static_cast<GLfloat>( sourceHeight )
	};
	const GLfloat sharpenAmount = ( mode == 2 )
		? idMath::ClampFloat( 0.0f, 1.5f, r_resolutionScaleSharpness.GetFloat() )
		: 0.0f;

	if ( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_INV_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
	}
	if ( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_INV_LOW_RES_SIZE] >= 0 ) {
		glUniform2fvARB( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_INV_LOW_RES_SIZE], 1, invLowResSize );
	}
	if ( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_SHARPEN_AMOUNT] >= 0 ) {
		glUniform1fARB( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_SHARPEN_AMOUNT], sharpenAmount );
	}

	RB_DrawFullscreenPostProcessQuadUnitUV();
	glUseProgramObjectARB( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
}

enum rbCRTUniformIndex_t {
	RB_CRT_UNIFORM_INV_TEX_SIZE = 0,
	RB_CRT_UNIFORM_AMOUNT,
	RB_CRT_UNIFORM_SCANLINE_STRENGTH,
	RB_CRT_UNIFORM_MASK_STRENGTH,
	RB_CRT_UNIFORM_CURVATURE,
	RB_CRT_UNIFORM_CHROMATIC_ABERRATION,
	RB_CRT_UNIFORM_TIME_SECONDS,
	RB_CRT_UNIFORM_COUNT
};

static newShaderStage_t rbCRTStage;
static bool rbCRTStageInitialized = false;

static void RB_InitCRTStage( void ) {
	if ( rbCRTStageInitialized ) {
		return;
	}

	memset( &rbCRTStage, 0, sizeof( rbCRTStage ) );
	rbCRTStage.glslProgram = true;
	idStr::Copynz( rbCRTStage.glslProgramName, "openprey_crt.fs", sizeof( rbCRTStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_CRT_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "crtAmount", 1 },
		{ "scanlineStrength", 1 },
		{ "maskStrength", 1 },
		{ "curvature", 1 },
		{ "chromaticAberration", 1 },
		{ "timeSeconds", 1 }
	};

	rbCRTStage.numShaderParms = RB_CRT_UNIFORM_COUNT;
	for ( int i = 0; i < RB_CRT_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbCRTStage.shaderParmNames[i], uniforms[i].name, sizeof( rbCRTStage.shaderParmNames[i] ) );
		rbCRTStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbCRTStage.numShaderTextures = 1;
	idStr::Copynz( rbCRTStage.shaderTextureNames[0], "Scene", sizeof( rbCRTStage.shaderTextureNames[0] ) );

	rbCRTStageInitialized = true;
}

void RB_ApplyCRTToBackBuffer( void ) {
	if ( r_skipPostProcess.GetBool() || !r_crt.GetBool() ) {
		return;
	}

	const GLfloat amount = idMath::ClampFloat( 0.0f, 1.0f, r_crtAmount.GetFloat() );
	if ( amount <= 0.001f ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	RB_InitCRTStage();
	if ( !R_ValidateGLSLProgram( &rbCRTStage ) ) {
		return;
	}

	const int viewportWidth = glConfig.vidWidth;
	const int viewportHeight = glConfig.vidHeight;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_ApplyCRTToBackBuffer ----------\n" );

	idRenderTexture::BindNull();
	backEnd.renderTexture = NULL;
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
	glViewport( 0, 0, viewportWidth, viewportHeight );
	glScissor( 0, 0, viewportWidth, viewportHeight );

	sceneImage->CopyFramebuffer( 0, 0, viewportWidth, viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	RB_BeginFullscreenPostProcessPass( 0, 0, viewportWidth, viewportHeight );
	GL_SelectTexture( 0 );
	sceneImage->Bind();

	glUseProgramObjectARB( (GLhandleARB)rbCRTStage.glslProgramObject );

	const int sceneLocation = rbCRTStage.shaderTextureLocations[0];
	if ( sceneLocation >= 0 ) {
		glUniform1iARB( sceneLocation, 0 );
	}

	const GLfloat invTexSize[2] = {
		1.0f / static_cast<GLfloat>( textureWidth ),
		1.0f / static_cast<GLfloat>( textureHeight )
	};
	const GLfloat scanlineStrength = idMath::ClampFloat( 0.0f, 1.0f, r_crtScanlineStrength.GetFloat() );
	const GLfloat maskStrength = idMath::ClampFloat( 0.0f, 1.0f, r_crtMaskStrength.GetFloat() );
	const GLfloat curvature = idMath::ClampFloat( 0.0f, 0.25f, r_crtCurvature.GetFloat() );
	const GLfloat chromaticAberration = idMath::ClampFloat( 0.0f, 8.0f, r_crtChromatic.GetFloat() );
	const GLfloat timeSeconds = static_cast<GLfloat>( backEnd.frameCount ) * ( 1.0f / 60.0f );

	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_INV_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_AMOUNT] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_AMOUNT], amount );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_SCANLINE_STRENGTH] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_SCANLINE_STRENGTH], scanlineStrength );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_MASK_STRENGTH] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_MASK_STRENGTH], maskStrength );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_CURVATURE] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_CURVATURE], curvature );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_CHROMATIC_ABERRATION] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_CHROMATIC_ABERRATION], chromaticAberration );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_TIME_SECONDS] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_TIME_SECONDS], timeSeconds );
	}

	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );
	glUseProgramObjectARB( 0 );
	RB_EndFullscreenPostProcessPass();
}

/*
=====================
RB_BakeTextureMatrixIntoTexgen
=====================
*/
void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float *textureMatrix ) {
	float	genMatrix[16];
	float	final[16];

	genMatrix[0] = lightProject[0][0];
	genMatrix[4] = lightProject[0][1];
	genMatrix[8] = lightProject[0][2];
	genMatrix[12] = lightProject[0][3];

	genMatrix[1] = lightProject[1][0];
	genMatrix[5] = lightProject[1][1];
	genMatrix[9] = lightProject[1][2];
	genMatrix[13] = lightProject[1][3];

	genMatrix[2] = 0;
	genMatrix[6] = 0;
	genMatrix[10] = 0;
	genMatrix[14] = 0;

	genMatrix[3] = lightProject[2][0];
	genMatrix[7] = lightProject[2][1];
	genMatrix[11] = lightProject[2][2];
	genMatrix[15] = lightProject[2][3];

	myGlMultMatrix( genMatrix, backEnd.lightTextureMatrix, final );

	lightProject[0][0] = final[0];
	lightProject[0][1] = final[4];
	lightProject[0][2] = final[8];
	lightProject[0][3] = final[12];

	lightProject[1][0] = final[1];
	lightProject[1][1] = final[5];
	lightProject[1][2] = final[9];
	lightProject[1][3] = final[13];
}

/*
================
RB_PrepareStageTexturing
================
*/
bool RB_PrepareStageTexturing( const shaderStage_t *pStage,  const drawSurf_t *surf, idDrawVert *ac ) {
	// set privatePolygonOffset if necessary
	if ( pStage->privatePolygonOffset ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
	}

	// set the texture matrix if needed
	if ( pStage->texture.hasMatrix ) {
		RB_LoadShaderTextureMatrix( surf->shaderRegisters, &pStage->texture );
	}

	// texgens
	if ( pStage->texture.texgen == TG_DIFFUSE_CUBE ) {
		glTexCoordPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
	}
	if ( pStage->texture.texgen == TG_SKYBOX_CUBE || pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
		glTexCoordPointer( 3, GL_FLOAT, 0, vertexCache.Position( surf->dynamicTexCoords ) );
	}
	if ( pStage->texture.texgen == TG_SCREEN ) {
		glEnable( GL_TEXTURE_GEN_S );
		glEnable( GL_TEXTURE_GEN_T );
		glEnable( GL_TEXTURE_GEN_Q );

		float	mat[16], plane[4];
		myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

		plane[0] = mat[0];
		plane[1] = mat[4];
		plane[2] = mat[8];
		plane[3] = mat[12];
		glTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

		plane[0] = mat[1];
		plane[1] = mat[5];
		plane[2] = mat[9];
		plane[3] = mat[13];
		glTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

		plane[0] = mat[3];
		plane[1] = mat[7];
		plane[2] = mat[11];
		plane[3] = mat[15];
		glTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );
	}

	if ( pStage->texture.texgen == TG_SCREEN2 ) {
		glEnable( GL_TEXTURE_GEN_S );
		glEnable( GL_TEXTURE_GEN_T );
		glEnable( GL_TEXTURE_GEN_Q );

		float	mat[16], plane[4];
		myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

		plane[0] = mat[0];
		plane[1] = mat[4];
		plane[2] = mat[8];
		plane[3] = mat[12];
		glTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

		plane[0] = mat[1];
		plane[1] = mat[5];
		plane[2] = mat[9];
		plane[3] = mat[13];
		glTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

		plane[0] = mat[3];
		plane[1] = mat[7];
		plane[2] = mat[11];
		plane[3] = mat[15];
		glTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );
	}

	if ( pStage->texture.texgen == TG_GLASSWARP ) {
		if ( tr.backEndRenderer == BE_ARB2 /*|| tr.backEndRenderer == BE_NV30*/ ) {
			if ( !R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP, "glasswarp fragment program", false ) ) {
				return false;
			}
			glEnable( GL_FRAGMENT_PROGRAM_ARB );

			GL_SelectTexture( 2 );
			globalImages->scratchImage->Bind();

			GL_SelectTexture( 1 );
			globalImages->scratchImage2->Bind();

			glEnable( GL_TEXTURE_GEN_S );
			glEnable( GL_TEXTURE_GEN_T );
			glEnable( GL_TEXTURE_GEN_Q );

			float	mat[16], plane[4];
			myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

			plane[0] = mat[0];
			plane[1] = mat[4];
			plane[2] = mat[8];
			plane[3] = mat[12];
			glTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

			plane[0] = mat[1];
			plane[1] = mat[5];
			plane[2] = mat[9];
			plane[3] = mat[13];
			glTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

			plane[0] = mat[3];
			plane[1] = mat[7];
			plane[2] = mat[11];
			plane[3] = mat[15];
			glTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );

			GL_SelectTexture( 0 );
		}
	}

	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
		if ( tr.backEndRenderer == BE_ARB2 ) {
			// see if there is also a bump map specified
			const shaderStage_t *bumpStage = surf->material->GetBumpStage();
			if ( bumpStage ) {
				if ( !R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT, "bumpy environment fragment program", false ) ||
					!R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT, "bumpy environment vertex program", false ) ) {
					return false;
				}

				// per-pixel reflection mapping with bump mapping
				GL_SelectTexture( 1 );
				bumpStage->texture.image->Bind();
				GL_SelectTexture( 0 );

				glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
				glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
				glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );

				glEnableVertexAttribArrayARB( 9 );
				glEnableVertexAttribArrayARB( 10 );
				glEnableClientState( GL_NORMAL_ARRAY );

				// Program env 5, 6, 7, 8 have been set in RB_SetProgramEnvironmentSpace

				glEnable( GL_FRAGMENT_PROGRAM_ARB );
				glEnable( GL_VERTEX_PROGRAM_ARB );
			} else {
				if ( !R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT, "environment fragment program", false ) ||
					!R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT, "environment vertex program", false ) ) {
					return false;
				}

				// per-pixel reflection mapping without a normal map
				glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
				glEnableClientState( GL_NORMAL_ARRAY );

				glEnable( GL_FRAGMENT_PROGRAM_ARB );
				glEnable( GL_VERTEX_PROGRAM_ARB );
			}
		} else {
			glEnable( GL_TEXTURE_GEN_S );
			glEnable( GL_TEXTURE_GEN_T );
			glEnable( GL_TEXTURE_GEN_R );
			glTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			glTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			glTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );

			glMatrixMode( GL_TEXTURE );
			float	mat[16];

			R_TransposeGLMatrix( backEnd.viewDef->worldSpace.modelViewMatrix, mat );

			glLoadMatrixf( mat );
			glMatrixMode( GL_MODELVIEW );
		}
	}

	return true;
}

/*
================
RB_FinishStageTexturing
================
*/
void RB_FinishStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, idDrawVert *ac ) {
	// unset privatePolygonOffset if necessary
	if ( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( pStage->texture.texgen == TG_DIFFUSE_CUBE || pStage->texture.texgen == TG_SKYBOX_CUBE
		|| pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), (void *)&ac->st );
	}

	if ( pStage->texture.texgen == TG_SCREEN ) {
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_Q );
	}
	if ( pStage->texture.texgen == TG_SCREEN2 ) {
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_Q );
	}

	if ( pStage->texture.texgen == TG_GLASSWARP ) {
		if ( tr.backEndRenderer == BE_ARB2 /*|| tr.backEndRenderer == BE_NV30*/ ) {
			GL_SelectTexture( 2 );
			globalImages->BindNull();

			GL_SelectTexture( 1 );
			if ( pStage->texture.hasMatrix ) {
				RB_LoadShaderTextureMatrix( surf->shaderRegisters, &pStage->texture );
			}
			glDisable( GL_TEXTURE_GEN_S );
			glDisable( GL_TEXTURE_GEN_T );
			glDisable( GL_TEXTURE_GEN_Q );
			glDisable( GL_FRAGMENT_PROGRAM_ARB );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
		}
	}

	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
		if ( tr.backEndRenderer == BE_ARB2 ) {
			// see if there is also a bump map specified
			const shaderStage_t *bumpStage = surf->material->GetBumpStage();
			if ( bumpStage ) {
				// per-pixel reflection mapping with bump mapping
				GL_SelectTexture( 1 );
				globalImages->BindNull();
				GL_SelectTexture( 0 );

				glDisableVertexAttribArrayARB( 9 );
				glDisableVertexAttribArrayARB( 10 );
			} else {
				// per-pixel reflection mapping without bump mapping
			}

			glDisableClientState( GL_NORMAL_ARRAY );
			glDisable( GL_FRAGMENT_PROGRAM_ARB );
			glDisable( GL_VERTEX_PROGRAM_ARB );
			// Fixme: Hack to get around an apparent bug in ATI drivers.  Should remove as soon as it gets fixed.
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
		} else {
			glDisable( GL_TEXTURE_GEN_S );
			glDisable( GL_TEXTURE_GEN_T );
			glDisable( GL_TEXTURE_GEN_R );
			glTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			glTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			glTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			glDisableClientState( GL_NORMAL_ARRAY );

			glMatrixMode( GL_TEXTURE );
			glLoadIdentity();
			glMatrixMode( GL_MODELVIEW );
		}
	}

	if ( pStage->texture.hasMatrix ) {
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
	}
}

/*
=============================================================================================

FILL DEPTH BUFFER

=============================================================================================
*/


/*
==================
RB_T_FillDepthBuffer
==================
*/
void RB_T_FillDepthBuffer( const drawSurf_t *surf ) {
	int			stage;
	const idMaterial	*shader;
	const shaderStage_t *pStage;
	const float	*regs;
	float		color[4];
	const srfTriangles_t	*tri;

	tri = surf->geo;
	shader = surf->material;

	// update the clip plane if needed
	if ( backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace ) {
		GL_SelectTexture( 1 );
		
		idPlane	plane;

		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane );
		plane[3] += 0.5;	// the notch is in the middle
		glTexGenfv( GL_S, GL_OBJECT_PLANE, plane.ToFloatPtr() );
		GL_SelectTexture( 0 );
	}

	if ( !shader->IsDrawn() ) {
		return;
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !tri->numIndexes ) {
		return;
	}

	// translucent surfaces don't put anything in the depth buffer and don't
	// test against it, which makes them fail the mirror clip plane operation
	if ( shader->Coverage() == MC_TRANSLUCENT ) {
		return;
	}

	if ( !tri->ambientCache ) {
		common->Printf( "RB_T_FillDepthBuffer: !tri->ambientCache\n" );
		return;
	}

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	const bool useAlphaToCoverage = RB_UseAlphaToCoverage( shader );

	// if all stages of a material have been conditioned off, don't do anything
	for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {		
		pStage = shader->GetStage(stage);
		// check the stage enable condition
		if ( regs[ pStage->conditionRegister ] != 0 ) {
			break;
		}
	}
	if ( stage == shader->GetNumStages() ) {
		return;
	}

	// set polygon offset if necessary
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	if ( useAlphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

	// subviews will just down-modulate the color buffer by overbright
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS );
		color[0] =
		color[1] = 
		color[2] = ( 1.0 / backEnd.overBright );
		color[3] = 1;
	} else {
		// others just draw black
		color[0] = 0;
		color[1] = 0;
		color[2] = 0;
		color[3] = 1;
	}

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>(&ac->st) );

	bool drawSolid = false;

	if ( shader->Coverage() == MC_OPAQUE ) {
		drawSolid = true;
	}

	// we may have multiple alpha tested stages
	if ( shader->Coverage() == MC_PERFORATED ) {
		// if the only alpha tested stages are condition register omitted,
		// draw a normal opaque surface
		bool	didDraw = false;

		glEnable( GL_ALPHA_TEST );
		// perforated surfaces may have multiple alpha tested stages
		for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {		
			pStage = shader->GetStage(stage);

			if ( !pStage->hasAlphaTest ) {
				continue;
			}

			// check the stage enable condition
			if ( regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}

			// if we at least tried to draw an alpha tested stage,
			// we won't draw the opaque surface
			didDraw = true;

			// set the alpha modulate
			color[3] = regs[ pStage->color.registers[3] ];

			// skip the entire stage if alpha would be black
			if ( color[3] <= 0 ) {
				continue;
			}
			glColor4fv( color );

			glAlphaFunc( GL_GREATER, regs[ pStage->alphaTestRegister ] );

			// bind the texture
			pStage->texture.image->Bind();

			// set texture matrix and texGens
			if ( !RB_PrepareStageTexturing( pStage, surf, ac ) ) {
				RB_FinishStageTexturing( pStage, surf, ac );
				continue;
			}

			// draw it
			RB_DrawElementsWithCounters( tri );

			RB_FinishStageTexturing( pStage, surf, ac );
		}
		glDisable( GL_ALPHA_TEST );
		if ( !didDraw ) {
			drawSolid = true;
		}
	}

	// draw the entire surface solid
	if ( drawSolid ) {
		glColor4fv( color );
		globalImages->whiteImage->Bind();

		// draw it
		RB_DrawElementsWithCounters( tri );
	}


	// reset polygon offset
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	// reset blending
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State( GLS_DEPTHFUNC_LESS );
	}

	if ( useAlphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

}

/*
=====================
RB_STD_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void RB_STD_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	// if we are just doing 2D rendering, no need to fill the depth buffer
	if ( !backEnd.viewDef->viewEntitys ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_FillDepthBuffer ----------\n" );

	// enable the second texture for mirror plane clipping if needed
	if ( backEnd.viewDef->numClipPlanes ) {
		GL_SelectTexture( 1 );
		globalImages->alphaNotchImage->Bind();
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glEnable( GL_TEXTURE_GEN_S );
		glTexCoord2f( 1, 0.5 );
	}

	// the first texture will be used for alpha tested surfaces
	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	// decal surfaces may enable polygon offset
	glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	GL_State( GLS_DEPTHFUNC_LESS );

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 1, 255 );

	RB_RenderDrawSurfListWithFunction( drawSurfs, numDrawSurfs, RB_T_FillDepthBuffer );

	if ( backEnd.viewDef->numClipPlanes ) {
		GL_SelectTexture( 1 );
		globalImages->BindNull();
		glDisable( GL_TEXTURE_GEN_S );
		GL_SelectTexture( 0 );
	}

}

/*
=============================================================================================

SHADER PASSES

=============================================================================================
*/

/*
==================
RB_SetProgramEnvironment

Sets variables that can be used by all vertex programs
==================
*/
void RB_SetProgramEnvironment( void ) {
	float	parm[4];
	int		pot;

	if ( !glConfig.ARBVertexProgramAvailable ) {
		return;
	}

#if 0
	// screen power of two correction factor, one pixel in so we don't get a bilerp
	// of an uncopied pixel
	int	 w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	pot = globalImages->currentRenderImage->GetOpts().width;
	if ( w == pot ) {
		parm[0] = 1.0;
	} else {
		parm[0] = (float)(w-1) / pot;
	}

	int	 h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	pot = globalImages->currentRenderImage->GetOpts().height;
	if ( h == pot ) {
		parm[1] = 1.0;
	} else {
		parm[1] = (float)(h-1) / pot;
	}

	parm[2] = 0;
	parm[3] = 1;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 0, parm );
#else
	// screen power of two correction factor, assuming the copy to _currentRender
	// also copied an extra row and column for the bilerp
	int	 w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	pot = globalImages->currentRenderImage->GetOpts().width;
	parm[0] = (float)w / pot;

	int	 h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	pot = globalImages->currentRenderImage->GetOpts().height;
	parm[1] = (float)h / pot;

	parm[2] = 0.0f;
	parm[3] = 1.0f;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 0, parm );
#endif

	if ( glConfig.ARBFragmentProgramAvailable ) {
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, parm );

		// window coord to 0.0 to 1.0 conversion
		parm[0] = 1.0f / w;
		parm[1] = 1.0f / h;
		parm[2] = 0.0f;
		parm[3] = 1.0f;
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, parm );
	}

	//
	// set eye position in global space
	//
	parm[0] = backEnd.viewDef->renderView.vieworg[0];
	parm[1] = backEnd.viewDef->renderView.vieworg[1];
	parm[2] = backEnd.viewDef->renderView.vieworg[2];
	parm[3] = 1.0;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 1, parm );


}

/*
==================
RB_SetProgramEnvironmentSpace

Sets variables related to the current space that can be used by all vertex programs
==================
*/
void RB_SetProgramEnvironmentSpace( void ) {
	if ( !glConfig.ARBVertexProgramAvailable ) {
		return;
	}

	const struct viewEntity_s *space = backEnd.currentSpace;
	float	parm[4];

	// set eye position in local space
	R_GlobalPointToLocal( space->modelMatrix, backEnd.viewDef->renderView.vieworg, *(idVec3 *)parm );
	parm[3] = 1.0;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 5, parm );

	// we need the model matrix without it being combined with the view matrix
	// so we can transform local vectors to global coordinates
	parm[0] = space->modelMatrix[0];
	parm[1] = space->modelMatrix[4];
	parm[2] = space->modelMatrix[8];
	parm[3] = space->modelMatrix[12];
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 6, parm );
	parm[0] = space->modelMatrix[1];
	parm[1] = space->modelMatrix[5];
	parm[2] = space->modelMatrix[9];
	parm[3] = space->modelMatrix[13];
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 7, parm );
	parm[0] = space->modelMatrix[2];
	parm[1] = space->modelMatrix[6];
	parm[2] = space->modelMatrix[10];
	parm[3] = space->modelMatrix[14];
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 8, parm );
}

/*
==================
RB_STD_T_RenderShaderPasses

This is also called for the generated 2D rendering
==================
*/
void RB_STD_T_RenderShaderPasses( const drawSurf_t *surf ) {
	int			stage;
	const idMaterial	*shader;
	const shaderStage_t *pStage;
	const float	*regs;
	float		color[4];
	const srfTriangles_t	*tri;

	tri = surf->geo;
	shader = surf->material;

	if ( !shader->HasAmbient() ) {
		return;
	}

	if ( shader->IsPortalSky() ) {
		return;
	}

	// change the matrix if needed
	if ( surf->space != backEnd.currentSpace ) {
		glLoadMatrixf( surf->space->modelViewMatrix );
		backEnd.currentSpace = surf->space;
		RB_SetProgramEnvironmentSpace();
	}

	// change the scissor if needed
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !tri->numIndexes ) {
		return;
	}

	if ( !tri->ambientCache ) {
		common->Printf( "RB_T_RenderShaderPasses: !tri->ambientCache\n" );
		return;
	}

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	// set face culling appropriately
	GL_Cull( shader->GetCullType() );

	// set polygon offset if necessary
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	const bool useAlphaToCoverage = RB_UseAlphaToCoverage( shader );
	if ( useAlphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
	
	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
	}

	if ( surf->space->modelDepthHack != 0.0f ) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>(&ac->st) );

	for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {		
		pStage = shader->GetStage(stage);

		// check the enable condition
		if ( regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		// skip the stages involved in lighting
		if ( pStage->lighting != SL_AMBIENT ) {
			continue;
		}

		// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
			continue;
		}

		// Fallback for materials that reference _currentRender but were not sorted as post-process.
		// Offscreen render-texture passes manage their own _currentRender capture and must not
		// overwrite it here after clearing the destination render target.
		if ( !backEnd.currentRenderCopied && backEnd.renderTexture == NULL && RB_StageUsesCurrentRender( pStage ) ) {
			globalImages->currentRenderImage->CopyFramebuffer( backEnd.viewDef->viewport.x1,
				backEnd.viewDef->viewport.y1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
			backEnd.currentRenderCopied = true;
		}

		// see if we are a new-style stage
		newShaderStage_t *newStage = pStage->newStage;
		if ( newStage ) {
			if ( r_skipNewAmbient.GetBool() ) {
				continue;
			}

			//--------------------------
			//
			// new style stages
			//
			//--------------------------

			if ( newStage->glslProgram ) {
				if ( !R_ValidateGLSLProgram( newStage ) ) {
					continue;
				}

				// GLSL stages in Quake 4 decal materials often rely on gl_Color
				// from per-vertex stage colors (for DecalLife/depth fade).
				float stageColor[4];
				stageColor[0] = regs[ pStage->color.registers[0] ];
				stageColor[1] = regs[ pStage->color.registers[1] ];
				stageColor[2] = regs[ pStage->color.registers[2] ];
				stageColor[3] = regs[ pStage->color.registers[3] ];
				bool useColorArray = false;
				if ( pStage->vertexColor == SVC_IGNORE ) {
					glColor4fv( stageColor );
				} else {
					RB_SetStageVertexColorPointer( surf, stage, ac );
					glEnableClientState( GL_COLOR_ARRAY );
					useColorArray = true;
				}

				GL_State( pStage->drawStateBits );
				glUseProgramObjectARB( (GLhandleARB)newStage->glslProgramObject );

				for ( int i = 0; i < newStage->numShaderParms; i++ ) {
					const int location = newStage->shaderParmLocations[i];
					const int numRegisters = newStage->shaderParmNumRegisters[i];
					if ( location < 0 || numRegisters <= 0 ) {
						continue;
					}

					float parm[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					for ( int j = 0; j < numRegisters && j < 4; j++ ) {
						parm[j] = regs[ newStage->shaderParmRegisters[i][j] ];
					}

					switch ( numRegisters ) {
					case 1:
						glUniform1fvARB( location, 1, parm );
						break;
					case 2:
						glUniform2fvARB( location, 1, parm );
						break;
					case 3:
						glUniform3fvARB( location, 1, parm );
						break;
					default:
						glUniform4fvARB( location, 1, parm );
						break;
					}
				}

				for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
					idImage *image = newStage->shaderTextureImages[i];
					if ( image == NULL ) {
						continue;
					}
					GL_SelectTexture( i );
					image->SetSamplerState( newStage->shaderTextureFilters[i], newStage->shaderTextureRepeats[i] );
					image->Bind();
					if ( newStage->shaderTextureLocations[i] >= 0 ) {
						glUniform1iARB( newStage->shaderTextureLocations[i], i );
					}
				}

				if ( !RB_PrepareStageTexturing( pStage, surf, ac ) ) {
					RB_FinishStageTexturing( pStage, surf, ac );
					if ( useColorArray ) {
						glDisableClientState( GL_COLOR_ARRAY );
					}
					continue;
				}
				RB_DrawElementsWithCounters( tri );
				RB_FinishStageTexturing( pStage, surf, ac );

				for ( int i = 1; i < newStage->numShaderTextures; i++ ) {
					if ( newStage->shaderTextureImages[i] ) {
						GL_SelectTexture( i );
						globalImages->BindNull();
					}
				}

				GL_SelectTexture( 0 );
				glUseProgramObjectARB( 0 );
				if ( useColorArray ) {
					glDisableClientState( GL_COLOR_ARRAY );
				}
				continue;
			}

			// completely skip ARB program stages if we don't have the capability
			if ( tr.backEndRenderer != BE_ARB2 ) {
				continue;
			}
			RB_SetStageVertexColorPointer( surf, stage, ac );
			glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
			glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
			glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );

			glEnableClientState( GL_COLOR_ARRAY );
			glEnableVertexAttribArrayARB( 9 );
			glEnableVertexAttribArrayARB( 10 );
			glEnableClientState( GL_NORMAL_ARRAY );

			GL_State( pStage->drawStateBits );

			bool vertexProgramEnabled = false;
			bool fragmentProgramEnabled = false;
			if ( newStage->vertexProgram != 0 ) {
				if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, newStage->vertexProgram, "material stage vertex program", false ) ) {
					glDisableClientState( GL_COLOR_ARRAY );
					glDisableVertexAttribArrayARB( 9 );
					glDisableVertexAttribArrayARB( 10 );
					glDisableClientState( GL_NORMAL_ARRAY );
					continue;
				}
				glEnable( GL_VERTEX_PROGRAM_ARB );
				vertexProgramEnabled = true;
			}

			// megaTextures bind a lot of images and set a lot of parameters
			//if ( newStage->megaTexture ) {
			//	newStage->megaTexture->SetMappingForSurface( tri );
			//	idVec3	localViewer;
			//	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewer );
			//	newStage->megaTexture->BindForViewOrigin( localViewer );
			//}

			if ( newStage->fragmentProgram != 0 ) {
				if ( !R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, newStage->fragmentProgram, "material stage fragment program", false ) ) {
					if ( vertexProgramEnabled ) {
						glDisable( GL_VERTEX_PROGRAM_ARB );
						glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
					}
					glDisableClientState( GL_COLOR_ARRAY );
					glDisableVertexAttribArrayARB( 9 );
					glDisableVertexAttribArrayARB( 10 );
					glDisableClientState( GL_NORMAL_ARRAY );
					continue;
				}
				glEnable( GL_FRAGMENT_PROGRAM_ARB );
				fragmentProgramEnabled = true;
			}

			if ( !vertexProgramEnabled && !fragmentProgramEnabled ) {
				glDisableClientState( GL_COLOR_ARRAY );
				glDisableVertexAttribArrayARB( 9 );
				glDisableVertexAttribArrayARB( 10 );
				glDisableClientState( GL_NORMAL_ARRAY );
				continue;
			}

			for ( int i = 0 ; i < newStage->numVertexParms ; i++ ) {
				float	parm[4];
				parm[0] = regs[ newStage->vertexParms[i][0] ];
				parm[1] = regs[ newStage->vertexParms[i][1] ];
				parm[2] = regs[ newStage->vertexParms[i][2] ];
				parm[3] = regs[ newStage->vertexParms[i][3] ];
				if ( vertexProgramEnabled ) {
					glProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, i, parm );
				}
				if ( fragmentProgramEnabled ) {
					glProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, i, parm );
				}
			}

			if ( fragmentProgramEnabled ) {
				for ( int i = 0 ; i < newStage->numFragmentProgramImages ; i++ ) {
					if ( newStage->fragmentProgramImages[i] ) {
						GL_SelectTexture( i );
						newStage->fragmentProgramImages[i]->Bind();
					}
				}
			}

			// draw it
			RB_DrawElementsWithCounters( tri );

			if ( fragmentProgramEnabled ) {
				for ( int i = 1 ; i < newStage->numFragmentProgramImages ; i++ ) {
					if ( newStage->fragmentProgramImages[i] ) {
						GL_SelectTexture( i );
						globalImages->BindNull();
					}
				}
			}
			//if ( newStage->megaTexture ) {
			//	newStage->megaTexture->Unbind();
			//}

			GL_SelectTexture( 0 );

			if ( vertexProgramEnabled ) {
				glDisable( GL_VERTEX_PROGRAM_ARB );
			}
			if ( fragmentProgramEnabled ) {
				glDisable( GL_FRAGMENT_PROGRAM_ARB );
			}
			// Fixme: Hack to get around an apparent bug in ATI drivers.  Should remove as soon as it gets fixed.
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );

			glDisableClientState( GL_COLOR_ARRAY );
			glDisableVertexAttribArrayARB( 9 );
			glDisableVertexAttribArrayARB( 10 );
			glDisableClientState( GL_NORMAL_ARRAY );
			continue;
		}

		//--------------------------
		//
		// old style stages
		//
		//--------------------------

		// set the color
		color[0] = regs[ pStage->color.registers[0] ];
		color[1] = regs[ pStage->color.registers[1] ];
		color[2] = regs[ pStage->color.registers[2] ];
		color[3] = regs[ pStage->color.registers[3] ];

		// skip the entire stage if an add would be black
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) 
			&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
			continue;
		}

		// skip the entire stage if a blend would be completely transparent
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
			&& color[3] <= 0 ) {
			continue;
		}

		const bool hasBakedDecalStageColor =
			( surf->decalColorCache != NULL && stage >= 0 && stage < surf->decalColorStageCount && surf->decalColorStride > 0 );

		// select the vertex color source
		if ( pStage->vertexColor == SVC_IGNORE ) {
			glColor4fv( color );
		} else {
			RB_SetStageVertexColorPointer( surf, stage, ac );
			glEnableClientState( GL_COLOR_ARRAY );

			if ( pStage->vertexColor == SVC_INVERSE_MODULATE ) {
				GL_TexEnv( GL_COMBINE_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_ONE_MINUS_SRC_COLOR );
				glTexEnvi( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1 );
			}

			// for vertex color and modulated color, we need to enable a second
			// texture stage. Skip this when decal stages already baked stage
			// color into per-vertex data; applying both paths darkens decals.
			if ( !hasBakedDecalStageColor && ( color[0] != 1 || color[1] != 1 || color[2] != 1 || color[3] != 1 ) ) {
				GL_SelectTexture( 1 );

				globalImages->whiteImage->Bind();
				GL_TexEnv( GL_COMBINE_ARB );

				glTexEnvfv( GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color );

				glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_CONSTANT_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
				glTexEnvi( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1 );

				glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_CONSTANT_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA );
				glTexEnvi( GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1 );

				GL_SelectTexture( 0 );
			}
		}

		// bind the texture
		RB_BindVariableStageImage( &pStage->texture, regs );

		// set the state
		GL_State( pStage->drawStateBits );
		
		if ( !RB_PrepareStageTexturing( pStage, surf, ac ) ) {
			RB_FinishStageTexturing( pStage, surf, ac );
			continue;
		}

		// draw it
		RB_DrawElementsWithCounters( tri );

		RB_FinishStageTexturing( pStage, surf, ac );
		
		if ( pStage->vertexColor != SVC_IGNORE ) {
			glDisableClientState( GL_COLOR_ARRAY );

			GL_SelectTexture( 1 );
			GL_TexEnv( GL_MODULATE );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
			GL_TexEnv( GL_MODULATE );
		}
	}

	// reset polygon offset
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		RB_LeaveDepthHack();
	}

	if ( useAlphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
}

/*
=====================
RB_STD_DrawShaderPasses

Draw non-light dependent passes
=====================
*/
int RB_STD_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int				i;

	// only obey skipAmbient if we are rendering a view
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}

	RB_LogComment( "---------- RB_STD_DrawShaderPasses ----------\n" );

	// if we are about to draw the first surface that needs
	// the rendering in a texture, copy it over
	if ( drawSurfs[0]->material->GetSort() >= SS_POST_PROCESS ) {
		if ( r_skipPostProcess.GetBool() ) {
			return 0;
		}

		// Copy the current view for any post-process material sampling _currentRender.
		// Do not gate this on viewEntitys: world-only views may still contain post-process surfaces.
		// Offscreen render-texture passes capture _currentRender explicitly and must keep that copy.
		if ( backEnd.renderTexture == NULL ) {
			globalImages->currentRenderImage->CopyFramebuffer( backEnd.viewDef->viewport.x1,
				backEnd.viewDef->viewport.y1,  backEnd.viewDef->viewport.x2 -  backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 -  backEnd.viewDef->viewport.y1 + 1 );
			backEnd.currentRenderCopied = true;
		} else {
			// Offscreen fullscreen passes are explicitly managed by the caller. Mark the copy as
			// satisfied so SS_POST_PROCESS surfaces are allowed to draw in this view.
			backEnd.currentRenderCopied = true;
		}
	}

	GL_SelectTexture( 1 );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	RB_SetProgramEnvironment();

	// we don't use RB_RenderDrawSurfListWithFunction()
	// because we want to defer the matrix load because many
	// surfaces won't draw any ambient passes
	backEnd.currentSpace = NULL;
	for (i = 0  ; i < numDrawSurfs ; i++ ) {
		if ( drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}

		if ( backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef ) {
			//if ( drawSurfs[i]->space->entityDef->parms.xrayIndex != 2 ) {
			//	continue;
			//}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if ( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS
			&& !backEnd.currentRenderCopied ) {
			break;
		}

		RB_STD_T_RenderShaderPasses( drawSurfs[i] );
	}

	GL_Cull( CT_FRONT_SIDED );
	glColor3f( 1, 1, 1 );

	return i;
}



/*
==============================================================================

BACK END RENDERING OF STENCIL SHADOWS

==============================================================================
*/

/*
=====================
RB_T_Shadow

the shadow volumes face INSIDE
=====================
*/
static void RB_T_Shadow( const drawSurf_t *surf ) {
	const srfTriangles_t	*tri;

	// set the light position if we are using a vertex program to project the rear surfaces
	if ( tr.backEndRendererHasVertexPrograms && r_useShadowVertexProgram.GetBool()
		&& surf->space != backEnd.currentSpace ) {
		idVec4 localLight;

		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3() );
		localLight.w = 0.0f;
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_ORIGIN, localLight.ToFloatPtr() );
	}

	tri = surf->geo;

	if ( !tri->shadowCache ) {
		return;
	}

	glVertexPointer( 4, GL_FLOAT, sizeof( shadowCache_t ), vertexCache.Position(tri->shadowCache) );

	// we always draw the sil planes, but we may not need to draw the front or rear caps
	int	numIndexes;
	bool external = false;

	if ( !r_useExternalShadows.GetInteger() ) {
		numIndexes = tri->numIndexes;
	} else if ( r_useExternalShadows.GetInteger() == 2 ) { // force to no caps for testing
		numIndexes = tri->numShadowIndexesNoCaps;
	} else if ( !(surf->dsFlags & DSF_VIEW_INSIDE_SHADOW) ) { 
		// if we aren't inside the shadow projection, no caps are ever needed needed
		numIndexes = tri->numShadowIndexesNoCaps;
		external = true;
	} else if ( !backEnd.vLight->viewInsideLight && !(surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE) ) {
		// if we are inside the shadow projection, but outside the light, and drawing
		// a non-infinite shadow, we can skip some caps
		if ( backEnd.vLight->viewSeesShadowPlaneBits & surf->geo->shadowCapPlaneBits ) {
			// we can see through a rear cap, so we need to draw it, but we can skip the
			// caps on the actual surface
			numIndexes = tri->numShadowIndexesNoFrontCaps;
		} else {
			// we don't need to draw any caps
			numIndexes = tri->numShadowIndexesNoCaps;
		}
		external = true;
	} else {
		// must draw everything
		numIndexes = tri->numIndexes;
	}

	// If this surface could not use external shadow optimizations, the caller will
	// have already forced the "no caps" index counts back to the full index count.
	// In that case treat it as an internal volume so we keep the robust stencil path.
	if ( numIndexes == tri->numIndexes ) {
		external = false;
	}

	// set depth bounds
	if( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		glDepthBoundsEXT( surf->scissorRect.zmin, surf->scissorRect.zmax );
	}

	// debug visualization
	if ( r_showShadows.GetInteger() ) {
		if ( r_showShadows.GetInteger() == 3 ) {
			if ( external ) {
				glColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
			} else {
				// these are the surfaces that require the reverse
				glColor3f( 1/backEnd.overBright, 0.1/backEnd.overBright, 0.1/backEnd.overBright );
			}
		} else {
			// draw different color for turboshadows
			if ( surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) {
				if ( numIndexes == tri->numIndexes ) {
					glColor3f( 1/backEnd.overBright, 0.1/backEnd.overBright, 0.1/backEnd.overBright );
				} else {
					glColor3f( 1/backEnd.overBright, 0.4/backEnd.overBright, 0.1/backEnd.overBright );
				}
			} else {
				if ( numIndexes == tri->numIndexes ) {
					glColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
				} else if ( numIndexes == tri->numShadowIndexesNoFrontCaps ) {
					glColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.6/backEnd.overBright );
				} else {
					glColor3f( 0.6/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
				}
			}
		}

		glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
		glDisable( GL_STENCIL_TEST );
		GL_Cull( CT_TWO_SIDED );
		RB_DrawShadowElementsWithCounters( tri, numIndexes );
		GL_Cull( CT_FRONT_SIDED );
		glEnable( GL_STENCIL_TEST );

		return;
	}

	// patent-free work around
	if ( !external ) {
		// "preload" the stencil buffer with the number of volumes
		// that get clipped by the near or far clip plane
		glStencilOp( GL_KEEP, tr.stencilDecr, tr.stencilDecr );
		GL_Cull( CT_FRONT_SIDED );
		RB_DrawShadowElementsWithCounters( tri, numIndexes );
		glStencilOp( GL_KEEP, tr.stencilIncr, tr.stencilIncr );
		GL_Cull( CT_BACK_SIDED );
		RB_DrawShadowElementsWithCounters( tri, numIndexes );
	}

	// traditional depth-pass stencil shadows
	glStencilOp( GL_KEEP, GL_KEEP, tr.stencilIncr );
	GL_Cull( CT_FRONT_SIDED );
	RB_DrawShadowElementsWithCounters( tri, numIndexes );

	glStencilOp( GL_KEEP, GL_KEEP, tr.stencilDecr );
	GL_Cull( CT_BACK_SIDED );
	RB_DrawShadowElementsWithCounters( tri, numIndexes );
}

/*
=====================
RB_StencilShadowPass

Stencil test should already be enabled, and the stencil buffer should have
been set to 128 on any surfaces that might receive shadows
=====================
*/
void RB_StencilShadowPass( const drawSurf_t *drawSurfs ) {
	if ( !r_shadows.GetBool() ) {
		return;
	}

	if ( !drawSurfs ) {
		return;
	}

	RB_LogComment( "---------- RB_StencilShadowPass ----------\n" );

	globalImages->BindNull();
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	// for visualizing the shadows
	if ( r_showShadows.GetInteger() ) {
		if ( r_showShadows.GetInteger() == 2 ) {
			// draw filled in
			GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS  );
		} else {
			// draw as lines, filling the depth buffer
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS  );
		}
	} else {
		// don't write to the color buffer, just the stencil buffer
		GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS );
	}

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		glPolygonOffset( r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat() );
		glEnable( GL_POLYGON_OFFSET_FILL );
	}

	glStencilFunc( GL_ALWAYS, 1, 255 );

	if ( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		glEnable( GL_DEPTH_BOUNDS_TEST_EXT );
	}

	RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_Shadow );

	GL_Cull( CT_FRONT_SIDED );

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		glDisable( GL_DEPTH_BOUNDS_TEST_EXT );
	}

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	glStencilFunc( GL_GEQUAL, 128, 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
}



/*
=============================================================================================

BLEND LIGHT PROJECTION

=============================================================================================
*/

/*
=====================
RB_T_BlendLight

=====================
*/
static void RB_T_BlendLight( const drawSurf_t *surf ) {
	const srfTriangles_t *tri;

	tri = surf->geo;

	if ( backEnd.currentSpace != surf->space ) {
		idPlane	lightProject[4];
		int		i;

		for ( i = 0 ; i < 4 ; i++ ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
		}

		GL_SelectTexture( 0 );
		glTexGenfv( GL_S, GL_OBJECT_PLANE, lightProject[0].ToFloatPtr() );
		glTexGenfv( GL_T, GL_OBJECT_PLANE, lightProject[1].ToFloatPtr() );
		glTexGenfv( GL_Q, GL_OBJECT_PLANE, lightProject[2].ToFloatPtr() );

		GL_SelectTexture( 1 );
		glTexGenfv( GL_S, GL_OBJECT_PLANE, lightProject[3].ToFloatPtr() );
	}

	// this gets used for both blend lights and shadow draws
	if ( tri->ambientCache ) {
		idDrawVert	*ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
	} else if ( tri->shadowCache ) {
		shadowCache_t	*sc = (shadowCache_t *)vertexCache.Position( tri->shadowCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( shadowCache_t ), sc->xyz.ToFloatPtr() );
	}

	RB_DrawElementsWithCounters( tri );
}


/*
=====================
RB_BlendLight

Dual texture together the falloff and projection texture with a blend
mode to the framebuffer, instead of interacting with the surface texture
=====================
*/
static void RB_BlendLight( const drawSurf_t *drawSurfs,  const drawSurf_t *drawSurfs2 ) {
	const idMaterial	*lightShader;
	const shaderStage_t	*stage;
	int					i;
	const float	*regs;

	if ( !drawSurfs ) {
		return;
	}
	if ( r_skipBlendLights.GetBool() ) {
		return;
	}
	RB_LogComment( "---------- RB_BlendLight ----------\n" );

	lightShader = backEnd.vLight->lightShader;
	regs = backEnd.vLight->shaderRegisters;

	// texture 1 will get the falloff texture
	GL_SelectTexture( 1 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnable( GL_TEXTURE_GEN_S );
	glTexCoord2f( 0, 0.5 );
	backEnd.vLight->falloffImage->Bind();

	// texture 0 will get the projected texture
	GL_SelectTexture( 0 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnable( GL_TEXTURE_GEN_S );
	glEnable( GL_TEXTURE_GEN_T );
	glEnable( GL_TEXTURE_GEN_Q );

	for ( i = 0 ; i < lightShader->GetNumStages() ; i++ ) {
		stage = lightShader->GetStage(i);

		if ( !regs[ stage->conditionRegister ] ) {
			continue;
		}

		GL_State( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL );

		GL_SelectTexture( 0 );
		stage->texture.image->Bind();

		if ( stage->texture.hasMatrix ) {
			RB_LoadShaderTextureMatrix( regs, &stage->texture );
		}

		// get the modulate values from the light, including alpha, unlike normal lights
		backEnd.lightColor[0] = regs[ stage->color.registers[0] ];
		backEnd.lightColor[1] = regs[ stage->color.registers[1] ];
		backEnd.lightColor[2] = regs[ stage->color.registers[2] ];
		backEnd.lightColor[3] = regs[ stage->color.registers[3] ];
		glColor4fv( backEnd.lightColor );

		RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_BlendLight );
		RB_RenderDrawSurfChainWithFunction( drawSurfs2, RB_T_BlendLight );

		if ( stage->texture.hasMatrix ) {
			GL_SelectTexture( 0 );
			glMatrixMode( GL_TEXTURE );
			glLoadIdentity();
			glMatrixMode( GL_MODELVIEW );
		}
	}

	GL_SelectTexture( 1 );
	glDisable( GL_TEXTURE_GEN_S );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	glDisable( GL_TEXTURE_GEN_S );
	glDisable( GL_TEXTURE_GEN_T );
	glDisable( GL_TEXTURE_GEN_Q );
}


//========================================================================

static idPlane	fogPlanes[4];

/*
=====================
RB_T_BasicFog

=====================
*/
static void RB_T_BasicFog( const drawSurf_t *surf ) {
	if ( backEnd.currentSpace != surf->space ) {
		idPlane	local;

		GL_SelectTexture( 0 );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[0], local );
		local[3] += 0.5;
		glTexGenfv( GL_S, GL_OBJECT_PLANE, local.ToFloatPtr() );

//		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[1], local );
//		local[3] += 0.5;
local[0] = local[1] = local[2] = 0; local[3] = 0.5;
		glTexGenfv( GL_T, GL_OBJECT_PLANE, local.ToFloatPtr() );

		GL_SelectTexture( 1 );

		// GL_S is constant per viewer
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[2], local );
		local[3] += FOG_ENTER;
		glTexGenfv( GL_T, GL_OBJECT_PLANE, local.ToFloatPtr() );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[3], local );
		glTexGenfv( GL_S, GL_OBJECT_PLANE, local.ToFloatPtr() );
	}

	RB_T_RenderTriangleSurface( surf );
}



/*
==================
RB_FogPass
==================
*/
static void RB_FogPass( const drawSurf_t *drawSurfs,  const drawSurf_t *drawSurfs2 ) {
	const srfTriangles_t*frustumTris;
	drawSurf_t			ds;
	const idMaterial	*lightShader;
	const shaderStage_t	*stage;
	const float			*regs;

	RB_LogComment( "---------- RB_FogPass ----------\n" );

	// create a surface for the light frustom triangles, which are oriented drawn side out
	frustumTris = backEnd.vLight->frustumTris;

	// if we ran out of vertex cache memory, skip it
	if ( !frustumTris->ambientCache ) {
		return;
	}
	memset( &ds, 0, sizeof( ds ) );
	ds.space = &backEnd.viewDef->worldSpace;
	ds.geo = frustumTris;
	ds.scissorRect = backEnd.viewDef->scissor;

	// find the current color and density of the fog
	lightShader = backEnd.vLight->lightShader;
	regs = backEnd.vLight->shaderRegisters;
	// assume fog shaders have only a single stage
	stage = lightShader->GetStage(0);

	backEnd.lightColor[0] = regs[ stage->color.registers[0] ];
	backEnd.lightColor[1] = regs[ stage->color.registers[1] ];
	backEnd.lightColor[2] = regs[ stage->color.registers[2] ];
	backEnd.lightColor[3] = regs[ stage->color.registers[3] ];

	glColor3fv( backEnd.lightColor );

	// calculate the falloff planes
	float	a;

	// if they left the default value on, set a fog distance of 500
	if ( backEnd.lightColor[3] <= 1.0 ) {
		a = -0.5f / DEFAULT_FOG_DISTANCE;
	} else {
		// otherwise, distance = alpha color
		a = -0.5f / backEnd.lightColor[3];
	}

	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );

	// texture 0 is the falloff image
	GL_SelectTexture( 0 );
	globalImages->fogImage->Bind();
	//GL_Bind( tr.whiteImage );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnable( GL_TEXTURE_GEN_S );
	glEnable( GL_TEXTURE_GEN_T );
	glTexCoord2f( 0.5f, 0.5f );		// make sure Q is set

	fogPlanes[0][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
	fogPlanes[0][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
	fogPlanes[0][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
	fogPlanes[0][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

	fogPlanes[1][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[0];
	fogPlanes[1][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[4];
	fogPlanes[1][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[8];
	fogPlanes[1][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[12];


	// texture 1 is the entering plane fade correction
	GL_SelectTexture( 1 );
	globalImages->fogEnterImage->Bind();
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnable( GL_TEXTURE_GEN_S );
	glEnable( GL_TEXTURE_GEN_T );

	// T will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
	fogPlanes[2][0] = 0.001f * backEnd.vLight->fogPlane[0];
	fogPlanes[2][1] = 0.001f * backEnd.vLight->fogPlane[1];
	fogPlanes[2][2] = 0.001f * backEnd.vLight->fogPlane[2];
	fogPlanes[2][3] = 0.001f * backEnd.vLight->fogPlane[3];

	// S is based on the view origin
	float s = backEnd.viewDef->renderView.vieworg * fogPlanes[2].Normal() + fogPlanes[2][3];

	fogPlanes[3][0] = 0;
	fogPlanes[3][1] = 0;
	fogPlanes[3][2] = 0;
	fogPlanes[3][3] = FOG_ENTER + s;

	glTexCoord2f( FOG_ENTER + s, FOG_ENTER );


	// draw it
	RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_BasicFog );
	RB_RenderDrawSurfChainWithFunction( drawSurfs2, RB_T_BasicFog );

	// the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
	// of depthfunc_equal
	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS );
	GL_Cull( CT_BACK_SIDED );
	RB_RenderDrawSurfChainWithFunction( &ds, RB_T_BasicFog );
	GL_Cull( CT_FRONT_SIDED );

	GL_SelectTexture( 1 );
	glDisable( GL_TEXTURE_GEN_S );
	glDisable( GL_TEXTURE_GEN_T );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	glDisable( GL_TEXTURE_GEN_S );
	glDisable( GL_TEXTURE_GEN_T );
}


/*
==================
RB_STD_FogAllLights
==================
*/
void RB_STD_FogAllLights( void ) {
	viewLight_t	*vLight;

	if ( r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0 
		 || backEnd.viewDef->isXraySubview /* dont fog in xray mode*/
		 ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_FogAllLights ----------\n" );

	glDisable( GL_STENCIL_TEST );

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		if ( !vLight->lightShader->IsFogLight() && !vLight->lightShader->IsBlendLight() ) {
			continue;
		}

#if 0 // _D3XP disabled that
		if ( r_ignore.GetInteger() ) {
			// we use the stencil buffer to guarantee that no pixels will be
			// double fogged, which happens in some areas that are thousands of
			// units from the origin
			backEnd.currentScissor = vLight->scissorRect;
			if ( r_useScissor.GetBool() ) {
				glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
					backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
					backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
					backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
			}
			glClear( GL_STENCIL_BUFFER_BIT );

			glEnable( GL_STENCIL_TEST );

			// only pass on the cleared stencil values
			glStencilFunc( GL_EQUAL, 128, 255 );

			// when we pass the stencil test and depth test and are going to draw,
			// increment the stencil buffer so we don't ever draw on that pixel again
			glStencilOp( GL_KEEP, GL_KEEP, GL_INCR );
		}
#endif

		if ( vLight->lightShader->IsFogLight() ) {
			RB_FogPass( vLight->globalInteractions, vLight->localInteractions );
		} else if ( vLight->lightShader->IsBlendLight() ) {
			RB_BlendLight( vLight->globalInteractions, vLight->localInteractions );
		}
		glDisable( GL_STENCIL_TEST );
	}

	glEnable( GL_STENCIL_TEST );
}

//=========================================================================================

/*
==================
RB_STD_LightScale

Perform extra blending passes to multiply the entire buffer by
a floating point value
==================
*/
void RB_STD_LightScale( void ) {
	float	v, f;

	if ( backEnd.overBright == 1.0f ) {
		return;
	}

	if ( r_skipLightScale.GetBool() ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_LightScale ----------\n" );

	// the scissor may be smaller than the viewport for subviews
	if ( r_useScissor.GetBool() ) {
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1, 
			backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1, 
			backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
			backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
		backEnd.currentScissor = backEnd.viewDef->scissor;
	}

	// full screen blends
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity(); 
    glOrtho( 0, 1, 0, 1, -1, 1 );

	GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_SRC_COLOR );
	GL_Cull( CT_TWO_SIDED );	// so mirror views also get it
	globalImages->BindNull();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );

	v = 1;
	while ( idMath::Fabs( v - backEnd.overBright ) > 0.01 ) {	// a little extra slop
		f = backEnd.overBright / v;
		f /= 2;
		if ( f > 1 ) {
			f = 1;
		}
		glColor3f( f, f, f );
		v = v * f * 2;

		glBegin( GL_QUADS );
		glVertex2f( 0,0 );	
		glVertex2f( 0,1 );
		glVertex2f( 1,1 );	
		glVertex2f( 1,0 );	
		glEnd();
	}


	glPopMatrix();
	glEnable( GL_DEPTH_TEST );
	glMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_FRONT_SIDED );
}

/*
==================
RB_STD_ForceAmbient

Lift the final scene toward a minimum brightness floor.
==================
*/
static void RB_STD_ForceAmbient( void ) {
	const GLuint interactionVertexProgram = r_testARBProgram.GetBool() ? VPROG_TEST : VPROG_INTERACTION;
	const GLuint interactionFragmentProgram = r_testARBProgram.GetBool() ? FPROG_TEST : FPROG_INTERACTION;
	const bool interactionRescueActive =
		tr.backEndRenderer == BE_ARB2 &&
		( !R_IsARBProgramValid( GL_VERTEX_PROGRAM_ARB, interactionVertexProgram ) ||
			!R_IsARBProgramValid( GL_FRAGMENT_PROGRAM_ARB, interactionFragmentProgram ) );
	const float ambientFloor = interactionRescueActive ? 0.20f : 0.0f;
	const float ambient = idMath::ClampFloat( 0.0f, 1.0f, Max( r_forceAmbient.GetFloat(), ambientFloor ) );
	if ( ambient <= 0.0f || !backEnd.viewDef->viewEntitys ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_ForceAmbient ----------\n" );

	// the scissor may be smaller than the viewport for subviews
	if ( r_useScissor.GetBool() ) {
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
			backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
			backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
		backEnd.currentScissor = backEnd.viewDef->scissor;
	}

	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, 1, 0, 1, -1, 1 );

	// This blend computes: dst = dst + ambient * ( 1 - dst ).
	GL_State( GLS_SRCBLEND_ONE_MINUS_DST_COLOR | GLS_DSTBLEND_ONE );
	GL_Cull( CT_TWO_SIDED );
	globalImages->BindNull();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glColor3f( ambient, ambient, ambient );

	glBegin( GL_QUADS );
	glVertex2f( 0, 0 );
	glVertex2f( 0, 1 );
	glVertex2f( 1, 1 );
	glVertex2f( 1, 0 );
	glEnd();

	glColor3f( 1.0f, 1.0f, 1.0f );
	glPopMatrix();
	glEnable( GL_DEPTH_TEST );
	glMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_FRONT_SIDED );
}

//=========================================================================================

/*
=============
RB_STD_DrawView

=============
*/
void	RB_STD_DrawView( void ) {
	drawSurf_t	 **drawSurfs;
	int			numDrawSurfs;

	RB_LogComment( "---------- RB_STD_DrawView ----------\n" );

	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

	drawSurfs = (drawSurf_t **)&backEnd.viewDef->drawSurfs[0];
	numDrawSurfs = backEnd.viewDef->numDrawSurfs;

	// If we have a backend rendertexture, assign it here.
	if (backEnd.renderTexture)
	{
		backEnd.renderTexture->MakeCurrent();
	}

	// clear the z buffer, set the projection matrix, etc
	RB_BeginDrawingView();

	// decide how much overbrighting we are going to do
	RB_DetermineLightScale();

	// fill the depth buffer and clear color buffer to black except on
	// subviews
	RB_STD_FillDepthBuffer( drawSurfs, numDrawSurfs );

	// main light renderer
	RB_ARB2_DrawInteractions();

	// disable stencil shadow test
	glStencilFunc( GL_ALWAYS, 128, 255 );

	// uplight the entire screen to crutch up not having better blending range
	RB_STD_LightScale();

	// now draw any non-light dependent shading passes
	int	processed = RB_STD_DrawShaderPasses( drawSurfs, numDrawSurfs );

	// Apply a configurable brightness floor after ambient/material passes.
	RB_STD_ForceAmbient();

	// fob and blend lights
	RB_STD_FogAllLights();

	// Apply SSAO before bloom and tonemapping so indirect shadowing modulates the lit scene.
	RB_STD_SSAO();

	// Apply scene bloom before authored post-process overlays that sample _currentRender.
	RB_STD_Bloom();

	// now draw any post-processing effects using _currentRender
	if ( processed < numDrawSurfs ) {
		RB_STD_DrawShaderPasses( drawSurfs+processed, numDrawSurfs-processed );
	}

	RB_RenderDebugTools( drawSurfs, numDrawSurfs );

// jmarshall - stupid OpenGL
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);
// jmarshall end
}
