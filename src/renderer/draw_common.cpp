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

static bool RB_ResolveGLSLProgram( newShaderStage_t *stage ) {
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
	}

	common->Printf( "Loaded GLSL program '%s' (%s, %s)\n",
		stage->glslProgramName, vertexPath.c_str(), fragmentPath.c_str() );

	return true;
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
void RB_PrepareStageTexturing( const shaderStage_t *pStage,  const drawSurf_t *surf, idDrawVert *ac ) {
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
			glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP );
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

				glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT );
				glEnable( GL_FRAGMENT_PROGRAM_ARB );
				glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT );
				glEnable( GL_VERTEX_PROGRAM_ARB );
			} else {
				// per-pixel reflection mapping without a normal map
				glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
				glEnableClientState( GL_NORMAL_ARRAY );

				glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT );
				glEnable( GL_FRAGMENT_PROGRAM_ARB );
				glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT );
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
			RB_PrepareStageTexturing( pStage, surf, ac );

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
		if ( !backEnd.currentRenderCopied && RB_StageUsesCurrentRender( pStage ) ) {
			globalImages->currentRenderImage->CopyFramebuffer( backEnd.viewDef->viewport.x1,
				backEnd.viewDef->viewport.y1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
			backEnd.currentRenderCopied = true;
		}

		// see if we are a new-style stage
		newShaderStage_t *newStage = pStage->newStage;
		if ( newStage ) {
			//--------------------------
			//
			// new style stages
			//
			//--------------------------

			if ( newStage->glslProgram ) {
				if ( !RB_ResolveGLSLProgram( newStage ) ) {
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
					image->Bind();
					if ( newStage->shaderTextureLocations[i] >= 0 ) {
						glUniform1iARB( newStage->shaderTextureLocations[i], i );
					}
				}

				RB_PrepareStageTexturing( pStage, surf, ac );
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
			//if ( r_skipNewAmbient.GetBool() ) {
			//	continue;
			//}
			RB_SetStageVertexColorPointer( surf, stage, ac );
			glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
			glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
			glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );

			glEnableClientState( GL_COLOR_ARRAY );
			glEnableVertexAttribArrayARB( 9 );
			glEnableVertexAttribArrayARB( 10 );
			glEnableClientState( GL_NORMAL_ARRAY );

			GL_State( pStage->drawStateBits );
			
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, newStage->vertexProgram );
			glEnable( GL_VERTEX_PROGRAM_ARB );

			// megaTextures bind a lot of images and set a lot of parameters
			//if ( newStage->megaTexture ) {
			//	newStage->megaTexture->SetMappingForSurface( tri );
			//	idVec3	localViewer;
			//	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewer );
			//	newStage->megaTexture->BindForViewOrigin( localViewer );
			//}

			for ( int i = 0 ; i < newStage->numVertexParms ; i++ ) {
				float	parm[4];
				parm[0] = regs[ newStage->vertexParms[i][0] ];
				parm[1] = regs[ newStage->vertexParms[i][1] ];
				parm[2] = regs[ newStage->vertexParms[i][2] ];
				parm[3] = regs[ newStage->vertexParms[i][3] ];
				glProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, i, parm );
				glProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, i, parm );
			}

			for ( int i = 0 ; i < newStage->numFragmentProgramImages ; i++ ) {
				if ( newStage->fragmentProgramImages[i] ) {
					GL_SelectTexture( i );
					newStage->fragmentProgramImages[i]->Bind();
				}
			}
			glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, newStage->fragmentProgram );
			glEnable( GL_FRAGMENT_PROGRAM_ARB );

			// draw it
			RB_DrawElementsWithCounters( tri );

			for ( int i = 1 ; i < newStage->numFragmentProgramImages ; i++ ) {
				if ( newStage->fragmentProgramImages[i] ) {
					GL_SelectTexture( i );
					globalImages->BindNull();
				}
			}
			//if ( newStage->megaTexture ) {
			//	newStage->megaTexture->Unbind();
			//}

			GL_SelectTexture( 0 );

			glDisable( GL_VERTEX_PROGRAM_ARB );
			glDisable( GL_FRAGMENT_PROGRAM_ARB );
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
		
		RB_PrepareStageTexturing( pStage, surf, ac );

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
		globalImages->currentRenderImage->CopyFramebuffer( backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y1,  backEnd.viewDef->viewport.x2 -  backEnd.viewDef->viewport.x1 + 1,
			backEnd.viewDef->viewport.y2 -  backEnd.viewDef->viewport.y1 + 1 );
		backEnd.currentRenderCopied = true;
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

	if ( backEnd.vLight && backEnd.vLight->lightDef && R_ShouldUseShadowMapForLight( backEnd.vLight->lightDef ) ) {
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
RB_STD_ApplyIndirectAmbient
==================
*/
static void RB_STD_ApplyIndirectAmbient( void ) {
	if ( !r_useIndirectLighting.GetBool() || !r_indirectFullscreenPass.GetBool() ) {
		return;
	}

	const idVec4 ambient = tr.indirectAmbientColor;
	if ( ambient.x <= 0.0f && ambient.y <= 0.0f && ambient.z <= 0.0f ) {
		return;
	}

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

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	GL_Cull( CT_TWO_SIDED );
	globalImages->BindNull();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glColor3f( ambient.x, ambient.y, ambient.z );

	glBegin( GL_QUADS );
	glVertex2f( 0, 0 );
	glVertex2f( 0, 1 );
	glVertex2f( 1, 1 );
	glVertex2f( 1, 0 );
	glEnd();

	glPopMatrix();
	glEnable( GL_DEPTH_TEST );
	glEnable( GL_STENCIL_TEST );
	glMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_FRONT_SIDED );
}

static bool		rbPhase5TAAHistoryValid = false;
static int		rbPhase5TAAHistoryWidth = 0;
static int		rbPhase5TAAHistoryHeight = 0;
static int		rbPhase5TAAHistoryFrame = -1;
static int		rbPhase5TimingPrintMsec = 0;

static void RB_STD_GetViewportSize( int &viewWidth, int &viewHeight ) {
	viewWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	viewHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
}

static void RB_STD_GetImageScale( const idImage *image, float &maxS, float &maxT ) {
	int viewWidth = 0;
	int viewHeight = 0;
	RB_STD_GetViewportSize( viewWidth, viewHeight );

	const int imageWidth = Max( 1, ( image != NULL ) ? image->GetOpts().width : viewWidth );
	const int imageHeight = Max( 1, ( image != NULL ) ? image->GetOpts().height : viewHeight );

	maxS = static_cast<float>( viewWidth ) / static_cast<float>( imageWidth );
	maxT = static_cast<float>( viewHeight ) / static_cast<float>( imageHeight );
}

static ID_INLINE void RB_STD_DrawFullscreenQuad( float maxS, float maxT ) {
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

static ID_INLINE void RB_STD_DrawFullscreenQuadMultiTex( float maxS, float maxT ) {
	glBegin( GL_QUADS );
	glMultiTexCoord2fARB( GL_TEXTURE0_ARB, 0.0f, 0.0f );
	glMultiTexCoord2fARB( GL_TEXTURE1_ARB, 0.0f, 0.0f );
	glVertex2f( 0.0f, 0.0f );

	glMultiTexCoord2fARB( GL_TEXTURE0_ARB, 0.0f, maxT );
	glMultiTexCoord2fARB( GL_TEXTURE1_ARB, 0.0f, maxT );
	glVertex2f( 0.0f, 1.0f );

	glMultiTexCoord2fARB( GL_TEXTURE0_ARB, maxS, maxT );
	glMultiTexCoord2fARB( GL_TEXTURE1_ARB, maxS, maxT );
	glVertex2f( 1.0f, 1.0f );

	glMultiTexCoord2fARB( GL_TEXTURE0_ARB, maxS, 0.0f );
	glMultiTexCoord2fARB( GL_TEXTURE1_ARB, maxS, 0.0f );
	glVertex2f( 1.0f, 0.0f );
	glEnd();
}

static void RB_STD_BeginFullscreenPass( void ) {
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
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	GL_Cull( CT_TWO_SIDED );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
}

static void RB_STD_EndFullscreenPass( void ) {
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glEnable( GL_DEPTH_TEST );
	glEnable( GL_STENCIL_TEST );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_Cull( CT_FRONT_SIDED );
}

static bool RB_STD_EnableFragmentProgram( const char *programName ) {
	if ( !glConfig.ARBFragmentProgramAvailable ) {
		return false;
	}

	const int fragmentProgram = R_FindARBProgram( GL_FRAGMENT_PROGRAM_ARB, programName );
	if ( fragmentProgram <= 0 ) {
		return false;
	}

	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, fragmentProgram );
	glEnable( GL_FRAGMENT_PROGRAM_ARB );
	return true;
}

static int RB_STD_ApplySSAO( void ) {
	if ( !r_useSSAO.GetBool() || !r_usePostLightingStack.GetBool() ) {
		return 0;
	}

	if ( !glConfig.ARBFragmentProgramAvailable ) {
		return 0;
	}

	int viewWidth = 0;
	int viewHeight = 0;
	RB_STD_GetViewportSize( viewWidth, viewHeight );
	if ( viewWidth <= 0 || viewHeight <= 0 ) {
		return 0;
	}

	const int startMsec = Sys_Milliseconds();
	const int x = backEnd.viewDef->viewport.x1;
	const int y = backEnd.viewDef->viewport.y1;

	globalImages->currentRenderImage->CopyFramebuffer( x, y, viewWidth, viewHeight );
	globalImages->currentDepthImage->CopyDepthbuffer( x, y, viewWidth, viewHeight );

	if ( !RB_STD_EnableFragmentProgram( "openq4_phase5_ssao.fp" ) ) {
		return 0;
	}

	float sourceS = 1.0f;
	float sourceT = 1.0f;
	RB_STD_GetImageScale( globalImages->currentRenderImage, sourceS, sourceT );

	const float strength = idMath::ClampFloat( 0.0f, 1.0f, r_ssaoStrength.GetFloat() );
	const float radius = Max( 0.25f, r_ssaoRadius.GetFloat() );
	const float depthBias = Max( 0.0f, r_ssaoDepthBias.GetFloat() );
	const float depthScale = Max( 0.1f, r_ssaoDepthScale.GetFloat() );
	const float minVisibility = Max( 0.05f, 1.0f - strength );
	const float invWidth = 1.0f / static_cast<float>( Max( 1, viewWidth ) );
	const float invHeight = 1.0f / static_cast<float>( Max( 1, viewHeight ) );

	RB_STD_BeginFullscreenPass();
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

	GL_SelectTexture( 1 );
	globalImages->currentDepthImage->Bind();
	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();

	glProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, strength, depthBias, minVisibility, depthScale );
	glProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, invWidth * radius, 0.0f, 0.0f, 0.0f );
	glProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 2, 0.0f, invHeight * radius, 0.0f, 0.0f );

	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	RB_STD_DrawFullscreenQuad( sourceS, sourceT );

	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_STD_EndFullscreenPass();

	return Sys_Milliseconds() - startMsec;
}

static int RB_STD_ApplyTemporalAA( void ) {
	if ( !r_useTAA.GetBool() || !r_usePostLightingStack.GetBool() ) {
		rbPhase5TAAHistoryValid = false;
		return 0;
	}

	if ( backEnd.viewDef->isSubview ) {
		rbPhase5TAAHistoryValid = false;
		return 0;
	}

	int viewWidth = 0;
	int viewHeight = 0;
	RB_STD_GetViewportSize( viewWidth, viewHeight );
	if ( viewWidth <= 0 || viewHeight <= 0 ) {
		rbPhase5TAAHistoryValid = false;
		return 0;
	}

	const int startMsec = Sys_Milliseconds();
	const int x = backEnd.viewDef->viewport.x1;
	const int y = backEnd.viewDef->viewport.y1;
	const bool resetHistory = r_taaReset.GetBool();

	const bool historyValid = rbPhase5TAAHistoryValid
		&& rbPhase5TAAHistoryWidth == viewWidth
		&& rbPhase5TAAHistoryHeight == viewHeight
		&& !resetHistory
		&& ( tr.frameCount - rbPhase5TAAHistoryFrame ) <= 2;

	if ( historyValid ) {
		float historyS = 1.0f;
		float historyT = 1.0f;
		RB_STD_GetImageScale( globalImages->scratchImage, historyS, historyT );

		const float historyBlend = idMath::ClampFloat( 0.0f, 0.95f, r_taaBlend.GetFloat() );
		if ( historyBlend > 0.0f ) {
			RB_STD_BeginFullscreenPass();
			GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
			GL_SelectTexture( 0 );
			globalImages->scratchImage->Bind();
			glColor4f( 1.0f, 1.0f, 1.0f, historyBlend );
			RB_STD_DrawFullscreenQuad( historyS, historyT );
			GL_SelectTexture( 0 );
			globalImages->BindNull();
			RB_STD_EndFullscreenPass();
		}
	}

	globalImages->scratchImage->CopyFramebuffer( x, y, viewWidth, viewHeight );
	rbPhase5TAAHistoryValid = true;
	rbPhase5TAAHistoryWidth = viewWidth;
	rbPhase5TAAHistoryHeight = viewHeight;
	rbPhase5TAAHistoryFrame = tr.frameCount;

	if ( resetHistory ) {
		r_taaReset.SetBool( false );
	}

	return Sys_Milliseconds() - startMsec;
}

static int RB_STD_ApplyTonemap( void ) {
	if ( !r_useTonemap.GetBool() || !r_usePostLightingStack.GetBool() ) {
		return 0;
	}

	// Current OpenQ4 game-side composition path is LDR; applying an additional
	// tonemap stage here causes over-compression/washed output. Keep the pass
	// disabled until a linear/HDR composition path is available.
	static bool warnedTonemapDisabled = false;
	if ( !warnedTonemapDisabled ) {
		warnedTonemapDisabled = true;
		common->Printf( "phase5_post: tonemap disabled on LDR composition path (stack remains SSAO/TAA)\n" );
	}
	return 0;

	if ( !glConfig.ARBFragmentProgramAvailable ) {
		return 0;
	}

	int viewWidth = 0;
	int viewHeight = 0;
	RB_STD_GetViewportSize( viewWidth, viewHeight );
	if ( viewWidth <= 0 || viewHeight <= 0 ) {
		return 0;
	}

	const int startMsec = Sys_Milliseconds();
	const int x = backEnd.viewDef->viewport.x1;
	const int y = backEnd.viewDef->viewport.y1;

	globalImages->currentRenderImage->CopyFramebuffer( x, y, viewWidth, viewHeight );

	if ( !RB_STD_EnableFragmentProgram( "openq4_phase5_tonemap.fp" ) ) {
		return 0;
	}

	float sourceS = 1.0f;
	float sourceT = 1.0f;
	RB_STD_GetImageScale( globalImages->currentRenderImage, sourceS, sourceT );

	float exposure = Max( 0.1f, r_tonemapExposure.GetFloat() );
	float gamma = Max( 0.1f, r_tonemapGamma.GetFloat() );
	float shoulder = Max( 0.0f, r_tonemapShoulder.GetFloat() );
	static bool warnedLegacyTonemapPreset = false;
	// Compatibility migration for archived pre-neutral defaults.
	// Older runs persisted gamma=2.2 and shoulder=1.0, which over-brighten
	// already-LDR scene color in the current OpenQ4 post pipeline.
	if ( idMath::Fabs( exposure - 1.0f ) < 0.001f &&
		idMath::Fabs( gamma - 2.2f ) < 0.05f &&
		idMath::Fabs( shoulder - 1.0f ) < 0.05f ) {
		exposure = 1.0f;
		gamma = 1.0f;
		shoulder = 0.0f;
		if ( !warnedLegacyTonemapPreset ) {
			warnedLegacyTonemapPreset = true;
			common->Printf( "phase5_post: neutralizing legacy tonemap preset (gamma=2.2, shoulder=1.0) to avoid washed output\n" );
		}
	}
	const float invGamma = 1.0f / gamma;

	RB_STD_BeginFullscreenPass();
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();
	glProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, exposure, invGamma, shoulder, 0.0f );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	RB_STD_DrawFullscreenQuad( sourceS, sourceT );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_STD_EndFullscreenPass();

	return Sys_Milliseconds() - startMsec;
}

static int RB_STD_ApplySMAA1x( void ) {
	if ( !r_usePostLightingStack.GetBool() || r_postAA.GetInteger() != 1 ) {
		return 0;
	}

	// Phase-5 stack currently uses TAA as its AA path. Running legacy SMAA in
	// this mode causes conflicting post ownership in OpenQ4's game-side pipeline.
	static bool warnedSmaaInPhase5 = false;
	if ( !warnedSmaaInPhase5 ) {
		warnedSmaaInPhase5 = true;
		common->Printf( "phase5_post: r_postAA=1 requested, but SMAA is disabled while r_usePostLightingStack=1 (using TAA path)\n" );
	}
	return 0;

	if ( !glConfig.ARBFragmentProgramAvailable ) {
		return 0;
	}

	int viewWidth = 0;
	int viewHeight = 0;
	RB_STD_GetViewportSize( viewWidth, viewHeight );
	if ( viewWidth <= 0 || viewHeight <= 0 ) {
		return 0;
	}

	const int startMsec = Sys_Milliseconds();
	const int x = backEnd.viewDef->viewport.x1;
	const int y = backEnd.viewDef->viewport.y1;
	const float invWidth = 1.0f / static_cast<float>( Max( 1, viewWidth ) );
	const float invHeight = 1.0f / static_cast<float>( Max( 1, viewHeight ) );

	const int edgeProgram = R_FindARBProgram( GL_FRAGMENT_PROGRAM_ARB, "openq4_smaa_edge.vfp" );
	const int blendProgram = R_FindARBProgram( GL_FRAGMENT_PROGRAM_ARB, "openq4_smaa_blend.vfp" );
	if ( edgeProgram <= 0 || blendProgram <= 0 ) {
		return 0;
	}

	globalImages->currentRenderImage->CopyFramebuffer( x, y, viewWidth, viewHeight );
	float sourceS = 1.0f;
	float sourceT = 1.0f;
	RB_STD_GetImageScale( globalImages->currentRenderImage, sourceS, sourceT );

	// edge pass -> framebuffer
	RB_STD_BeginFullscreenPass();
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, edgeProgram );
	glEnable( GL_FRAGMENT_PROGRAM_ARB );
	glProgramEnvParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, invWidth, invHeight, 0.0f, 1.0f );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	RB_STD_DrawFullscreenQuad( sourceS, sourceT );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_STD_EndFullscreenPass();

	// capture edge buffer for blend stage
	globalImages->scratchImage2->CopyFramebuffer( x, y, viewWidth, viewHeight );

	// blend pass -> framebuffer
	RB_STD_BeginFullscreenPass();
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_SelectTexture( 1 );
	globalImages->scratchImage2->Bind();
	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, blendProgram );
	glEnable( GL_FRAGMENT_PROGRAM_ARB );
	glProgramEnvParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, invWidth, invHeight, 0.0f, 1.0f );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	RB_STD_DrawFullscreenQuadMultiTex( sourceS, sourceT );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_STD_EndFullscreenPass();

	return Sys_Milliseconds() - startMsec;
}

static void RB_STD_ApplyModernPostLightingStack( void ) {
	if ( !r_usePostLightingStack.GetBool() || r_skipPostProcess.GetBool() ) {
		rbPhase5TAAHistoryValid = false;
		return;
	}

	if ( backEnd.viewDef->isXraySubview ) {
		rbPhase5TAAHistoryValid = false;
		return;
	}

	// Game-side post composition uses intermediate render textures; avoid
	// running the renderer-owned phase5 chain on those offscreen passes.
	if ( backEnd.renderTexture != NULL ) {
		rbPhase5TAAHistoryValid = false;
		return;
	}

	const int startMsec = Sys_Milliseconds();
	const int ssaoMsec = RB_STD_ApplySSAO();
	const int taaMsec = RB_STD_ApplyTemporalAA();
	const int tonemapMsec = RB_STD_ApplyTonemap();
	const int smaaMsec = RB_STD_ApplySMAA1x();
	const int totalMsec = Sys_Milliseconds() - startMsec;

	if ( r_showPostPassTiming.GetBool() ) {
		const int now = Sys_Milliseconds();
		if ( now - rbPhase5TimingPrintMsec >= 1000 ) {
			rbPhase5TimingPrintMsec = now;
			common->Printf( "phase5_post: total=%dms ssao=%dms taa=%dms tonemap=%dms smaa=%dms\n",
				totalMsec, ssaoMsec, taaMsec, tonemapMsec, smaaMsec );
		}
	}
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
	RB_STD_ApplyIndirectAmbient();

	// disable stencil shadow test
	glStencilFunc( GL_ALWAYS, 128, 255 );

	// uplight the entire screen to crutch up not having better blending range
	RB_STD_LightScale();

	// now draw any non-light dependent shading passes
	int	processed = RB_STD_DrawShaderPasses( drawSurfs, numDrawSurfs );

	// fob and blend lights
	RB_STD_FogAllLights();
	RB_STD_ApplyModernPostLightingStack();

	// When phase-5 stack is active, it owns SSAO/TAA/tonemap/SMAA ordering and
	// legacy material post-process surfaces must not run afterwards.
	const bool runLegacyPostMaterials = !r_usePostLightingStack.GetBool();

	// now draw any post-processing effects using _currentRender
	if ( processed < numDrawSurfs && runLegacyPostMaterials ) {
		RB_STD_DrawShaderPasses( drawSurfs+processed, numDrawSurfs-processed );
	}

	RB_RenderDebugTools( drawSurfs, numDrawSurfs );

// jmarshall - stupid OpenGL
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);
// jmarshall end
}
