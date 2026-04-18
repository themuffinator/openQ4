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

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

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

static bool RB_ImageIsCurrentDepth( const idImage *image ) {
	if ( image == NULL ) {
		return false;
	}

	if ( image == globalImages->currentDepthImage ) {
		return true;
	}

	const char *name = image->GetName();
	if ( name == NULL ) {
		return false;
	}

	return idStr::Icmpn( name, "_currentDepth", 13 ) == 0;
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

static bool RB_StageUsesCurrentDepth( const shaderStage_t *stage ) {
	if ( stage == NULL ) {
		return false;
	}

	if ( RB_ImageIsCurrentDepth( stage->texture.image ) ) {
		return true;
	}

	const newShaderStage_t *newStage = stage->newStage;
	if ( newStage == NULL ) {
		return false;
	}

	for ( int i = 0; i < newStage->numFragmentProgramImages; i++ ) {
		if ( RB_ImageIsCurrentDepth( newStage->fragmentProgramImages[i] ) ) {
			return true;
		}
	}

	for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
		if ( RB_ImageIsCurrentDepth( newStage->shaderTextureImages[i] ) ) {
			return true;
		}
	}

	return false;
}

static bool RB_MaterialUsesCurrentDepth( const idMaterial *material ) {
	if ( material == NULL ) {
		return false;
	}

	for ( int i = 0; i < material->GetNumStages(); i++ ) {
		if ( RB_StageUsesCurrentDepth( material->GetStage( i ) ) ) {
			return true;
		}
	}

	return false;
}

static const int RB_STOCK_GAUSSIAN_SAMPLE_COUNT = 15;
static const idVec4 RB_STOCK_COLOR_MATRIX_ROWS[3] = {
	idVec4( 1.0f, 0.0f, 0.0f, 0.0f ),
	idVec4( 0.0f, 1.0f, 0.0f, 0.0f ),
	idVec4( 0.0f, 0.0f, 1.0f, 0.0f )
};

static idVec4 rbStockGaussianSampleOffsets[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static idVec4 rbStockGaussianSampleWeights[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static idVec4 rbStockGaussianSampleOffsetsHorizontal[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static idVec4 rbStockGaussianSampleOffsetsVertical[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static idVec4 rbStockGaussianSampleWeights2[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static int rbStockGaussianViewportWidth = -1;
static int rbStockGaussianViewportHeight = -1;

static float RB_StockGaussian1D( float offset, float deviation ) {
	const float variance = deviation * deviation;
	const float normalization = 1.0f / idMath::Sqrt( 2.0f * idMath::PI * variance );
	return normalization * idMath::Exp( -( offset * offset ) / ( 2.0f * variance ) );
}

static void RB_CalculateStockGaussianCoefficients( int width, int height, float multiplier ) {
	memset( rbStockGaussianSampleOffsets, 0, sizeof( rbStockGaussianSampleOffsets ) );
	memset( rbStockGaussianSampleWeights, 0, sizeof( rbStockGaussianSampleWeights ) );

	float totalWeight = 0.0f;
	int count = 0;
	for ( int y = -2; y <= 2 && count < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; y++ ) {
		for ( int x = -2; x <= 2 && count < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; x++ ) {
			if ( abs( x ) + abs( y ) > 2 ) {
				continue;
			}

			const float weight = RB_StockGaussian1D( idMath::Sqrt( static_cast<float>( x * x + y * y ) ), 1.0f );
			rbStockGaussianSampleOffsets[count].Set(
				static_cast<float>( x ) / static_cast<float>( width ),
				static_cast<float>( y ) / static_cast<float>( height ),
				0.0f,
				0.0f );
			rbStockGaussianSampleWeights[count].Set( weight, weight, weight, weight );
			totalWeight += weight;
			count++;
		}
	}

	if ( totalWeight <= 0.0f ) {
		return;
	}

	const float scale = multiplier / totalWeight;
	for ( int i = 0; i < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; i++ ) {
		rbStockGaussianSampleWeights[i] *= scale;
	}
}

static void RB_CalculateStockGaussianCoefficients1D( int size, float multiplier, float deviation,
	idVec4 *sampleOffsets, idVec4 *sampleWeights ) {
	const int halfSampleCount = ( RB_STOCK_GAUSSIAN_SAMPLE_COUNT + 1 ) / 2;

	memset( sampleOffsets, 0, sizeof( idVec4 ) * RB_STOCK_GAUSSIAN_SAMPLE_COUNT );
	if ( sampleWeights != NULL ) {
		memset( sampleWeights, 0, sizeof( idVec4 ) * RB_STOCK_GAUSSIAN_SAMPLE_COUNT );
	}

	for ( int i = 0; i < halfSampleCount; i++ ) {
		const float offset = static_cast<float>( i ) / static_cast<float>( size );
		sampleOffsets[i].Set( offset, 0.0f, 0.0f, 0.0f );

		if ( sampleWeights != NULL ) {
			const float weight = RB_StockGaussian1D( static_cast<float>( i ), deviation ) * multiplier;
			sampleWeights[i].Set( weight, weight, weight, 1.0f );
		}
	}

	for ( int i = halfSampleCount; i < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; i++ ) {
		const int mirrorIndex = RB_STOCK_GAUSSIAN_SAMPLE_COUNT - i;
		sampleOffsets[i].Set( -sampleOffsets[mirrorIndex].x, 0.0f, 0.0f, 0.0f );
		if ( sampleWeights != NULL ) {
			sampleWeights[i] = sampleWeights[mirrorIndex];
		}
	}
}

static void RB_UpdateStockGLSLShaderConstantCache() {
	if ( backEnd.viewDef == NULL ) {
		return;
	}

	const int viewportWidth = Max( 1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1 );
	const int viewportHeight = Max( 1, backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
	if ( viewportWidth == rbStockGaussianViewportWidth && viewportHeight == rbStockGaussianViewportHeight ) {
		return;
	}

	rbStockGaussianViewportWidth = viewportWidth;
	rbStockGaussianViewportHeight = viewportHeight;

	RB_CalculateStockGaussianCoefficients( viewportWidth, viewportHeight, 1.0f );
	RB_CalculateStockGaussianCoefficients1D( viewportWidth, 1.0f, 3.0f,
		rbStockGaussianSampleOffsetsHorizontal, rbStockGaussianSampleWeights2 );
	RB_CalculateStockGaussianCoefficients1D( viewportWidth, 1.0f, 3.0f,
		rbStockGaussianSampleOffsetsVertical, NULL );

	for ( int i = 0; i < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; i++ ) {
		rbStockGaussianSampleOffsetsVertical[i].y = rbStockGaussianSampleOffsetsVertical[i].x;
		rbStockGaussianSampleOffsetsVertical[i].x = 0.0f;
	}
}

static bool RB_BindStockGLSLShaderParm( glslShaderParmBinding_t binding, int location ) {
	if ( location < 0 || backEnd.viewDef == NULL ) {
		return false;
	}

	switch ( binding ) {
	case GLSL_SHADERPARM_VIEW_ORIGIN: {
		idVec4 viewOrigin;
		viewOrigin.ToVec3() = backEnd.viewDef->renderView.vieworg;
		viewOrigin.w = 1.0f;
		glUniform4fvARB( location, 1, viewOrigin.ToFloatPtr() );
		return true;
	}
	case GLSL_SHADERPARM_COLOR_MATRIX0:
		glUniform4fvARB( location, 1, RB_STOCK_COLOR_MATRIX_ROWS[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_COLOR_MATRIX1:
		glUniform4fvARB( location, 1, RB_STOCK_COLOR_MATRIX_ROWS[1].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_COLOR_MATRIX2:
		glUniform4fvARB( location, 1, RB_STOCK_COLOR_MATRIX_ROWS[2].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleOffsets[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS_HORIZONTAL:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleOffsetsHorizontal[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS_VERTICAL:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleOffsetsVertical[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_WEIGHTS:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleWeights[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_WEIGHTS2:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleWeights2[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_REGISTERS:
	default:
		return false;
	}
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

	// GUI renderDef previews allocate their own transient renderWorld with no
	// loaded map. Those views must composite directly over the already drawn
	// menu instead of being routed through the fullscreen scene-target present
	// path, which would overwrite the menu with an opaque buffer.
	if ( backEnd.viewDef->renderWorld != NULL && backEnd.viewDef->renderWorld->mapName.Length() == 0 ) {
		return false;
	}

	// X-ray subviews intentionally diverge from the normal scene shading path.
	return !backEnd.viewDef->isXraySubview;
}

static const int RB_BLOOM_MAX_LEVELS = 5;
static const int RB_HDR_EXPOSURE_MAX_LEVELS = 12;
static const float RB_BLOOM_BASE_WEIGHTS[RB_BLOOM_MAX_LEVELS] = {
	0.34f, 0.24f, 0.17f, 0.14f, 0.11f
};

static idImage *rbSceneColorImage = NULL;
static idImage *rbSceneDepthStencilImage = NULL;
static idRenderTexture *rbSceneRenderTexture = NULL;
static int rbSceneRenderTextureSamples = -1;
static float rbHDRAdaptedExposure = 1.0f;
static float rbHDRLastAverageLuminance = 1.0f;
static float rbHDRLastTargetExposure = 1.0f;
static float rbHDRLastAdaptationTime = -1.0f;
static bool rbHDRExposureInitialized = false;

static bool RB_PostProcessBloomRequested( void ) {
	return r_bloom.GetBool();
}

static int RB_HDRDebugViewValue( void ) {
	return idMath::ClampInt( 0, 2, r_hdrDebugView.GetInteger() );
}

static bool RB_HDRAutoExposureEnabled( void ) {
	// Auto exposure only makes sense once the full renderer feeds a reliable
	// scene-linear buffer into post. In the legacy SDR path it over-corrects
	// stock Quake 4 content and washes out LDR presentation.
	return false;
}

static bool RB_IsSceneRenderTexture( const idRenderTexture *renderTexture ) {
	return renderTexture != NULL && renderTexture == rbSceneRenderTexture;
}

static bool RB_AutomaticCurrentRenderCaptureAllowed( void ) {
	return backEnd.renderTexture == NULL || RB_IsSceneRenderTexture( backEnd.renderTexture );
}

static void RB_SetFramebufferSRGBEnabled( bool enabled ) {
	if ( !glConfig.framebufferSRGBAvailable ) {
		return;
	}

	const bool strictLinearOutputEnabled = false;

	// Keep stock SDR presentation unless/until the full renderer adopts a
	// verified scene-linear workflow. Archived cvar values should not force the
	// experimental path on.
	if ( enabled && strictLinearOutputEnabled && r_hdrSRGB.GetBool() ) {
		glEnable( GL_FRAMEBUFFER_SRGB );
	} else {
		glDisable( GL_FRAMEBUFFER_SRGB );
	}
}

static void RB_CaptureCurrentRenderImage( int viewportWidth, int viewportHeight ) {
	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL || viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	if ( backEnd.renderTexture != NULL && backEnd.renderTexture->GetNumColorImages() > 0 ) {
		idImage *colorImage = backEnd.renderTexture->GetColorImage( 0 );
		if ( colorImage == sceneImage ) {
			backEnd.currentRenderCopied = true;
			return;
		}
	}

	sceneImage->CopyFramebuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	backEnd.currentRenderCopied = true;
}

static void RB_CaptureCurrentDepthImage( int viewportWidth, int viewportHeight ) {
	idImage *depthImage = globalImages->currentDepthImage;
	if ( depthImage == NULL || viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	if ( backEnd.renderTexture != NULL ) {
		idImage *renderDepthImage = backEnd.renderTexture->GetDepthImage();
		if ( renderDepthImage == depthImage ) {
			backEnd.currentDepthCopied = true;
			return;
		}
	}

	depthImage->CopyDepthbuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	backEnd.currentDepthCopied = true;
}

static bool RB_EnsureSceneRenderTexture( void ) {
	if ( !backEnd.viewDef ) {
		return false;
	}

	const int targetWidth = Max( glConfig.vidWidth, backEnd.viewDef->viewport.x2 + 1 );
	const int targetHeight = Max( glConfig.vidHeight, backEnd.viewDef->viewport.y2 + 1 );
	const int sceneSamples = Max( 0, r_multiSamples.GetInteger() );

	if ( targetWidth <= 0 || targetHeight <= 0 ) {
		return false;
	}

	idImageOpts colorOpts;
	colorOpts.textureType = TT_2D;
	colorOpts.format = FMT_RGBA16F;
	colorOpts.width = targetWidth;
	colorOpts.height = targetHeight;
	colorOpts.numLevels = 1;
	colorOpts.numMSAASamples = sceneSamples;
	colorOpts.isPersistant = true;
	rbSceneColorImage = globalImages->ScratchImage( "_hdrSceneColor", &colorOpts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );

	idImageOpts depthOpts;
	depthOpts.textureType = TT_2D;
	depthOpts.format = FMT_DEPTH_STENCIL;
	depthOpts.width = targetWidth;
	depthOpts.height = targetHeight;
	depthOpts.numLevels = 1;
	depthOpts.numMSAASamples = sceneSamples;
	depthOpts.isPersistant = true;
	rbSceneDepthStencilImage = globalImages->ScratchImage( "_hdrSceneDepthStencil", &depthOpts, TF_NEAREST, TR_CLAMP, TD_DEPTH );

	if ( rbSceneColorImage == NULL || rbSceneDepthStencilImage == NULL ) {
		return false;
	}

	const bool recreateRenderTexture =
		( rbSceneRenderTexture == NULL ) ||
		( rbSceneRenderTexture->GetWidth() != targetWidth ) ||
		( rbSceneRenderTexture->GetHeight() != targetHeight ) ||
		( rbSceneRenderTextureSamples != sceneSamples );

	if ( recreateRenderTexture ) {
		if ( rbSceneRenderTexture != NULL ) {
			tr.DestroyRenderTexture( rbSceneRenderTexture );
			rbSceneRenderTexture = NULL;
		}
		rbSceneRenderTexture = tr.CreateRenderTexture( rbSceneColorImage, rbSceneDepthStencilImage );
		rbSceneRenderTextureSamples = sceneSamples;
	}

	return rbSceneRenderTexture != NULL;
}

static bool RB_SceneRenderTargetRequested( void ) {
	if ( r_skipPostProcess.GetBool() ) {
		return false;
	}
	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}
	if ( !RB_IsMainScenePostProcessView() ) {
		return false;
	}
	const bool bloomRequested = RB_PostProcessBloomRequested();
	if ( !bloomRequested && !r_hdrSceneTarget.GetBool() ) {
		return false;
	}
	if ( backEnd.renderTexture != NULL ) {
		return false;
	}

	// Bloom now always routes through the resolved scene target. The direct
	// back-buffer capture path was fragile when toggling bloom live and during
	// map handoffs, and it also clipped highlight energy before the bright-pass.
	return bloomRequested
		|| r_ssao.GetBool()
		|| r_hdrToneMap.GetBool()
		|| RB_HDRAutoExposureEnabled()
		|| ( RB_HDRDebugViewValue() > 0 );
}

static void RB_BeginFullscreenPostProcessPass( int scissorX, int scissorY, int scissorWidth, int scissorHeight ) {
	// Fullscreen post-process passes must never inherit stale light/material scissors.
	glEnable( GL_SCISSOR_TEST );
	glScissor( scissorX, scissorY, scissorWidth, scissorHeight );

	// Fullscreen composites must start from a known programmable-pipeline state.
	// Level changes and SP/MP transitions can leave legacy ARB programs bound or
	// higher texture units configured by material stages, which causes the
	// tonemap/bloom fullscreen quad to sample garbage or render solid black.
	glUseProgramObjectARB( 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, 0 );

	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, 1, 0, 1, -1, 1 );

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_Cull( CT_TWO_SIDED );

	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );
	for ( int unit = 0; unit < maxStateUnits; unit++ ) {
		GL_SelectTexture( unit );
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glDisable( GL_TEXTURE_GEN_Q );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		globalImages->BindNull();
	}

	GL_SelectTexture( 0 );
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

enum rbLightGridUniformIndex_t {
	RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_S = 0,
	RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_T,
	RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_S,
	RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_T,
	RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW0,
	RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW1,
	RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW2,
	RB_LIGHTGRID_UNIFORM_LIGHTGRID_ORIGIN,
	RB_LIGHTGRID_UNIFORM_LIGHTGRID_SIZE,
	RB_LIGHTGRID_UNIFORM_LIGHTGRID_BOUNDS,
	RB_LIGHTGRID_UNIFORM_ATLAS_INFO,
	RB_LIGHTGRID_UNIFORM_DIFFUSE_COLOR,
	RB_LIGHTGRID_UNIFORM_VERTEX_COLOR_PARAMS,
	RB_LIGHTGRID_UNIFORM_COUNT
};

static newShaderStage_t rbLightGridIndirectStage;
static bool rbLightGridIndirectStageInitialized = false;

static void RB_InitLightGridIndirectStage( void ) {
	if ( rbLightGridIndirectStageInitialized ) {
		return;
	}

	memset( &rbLightGridIndirectStage, 0, sizeof( rbLightGridIndirectStage ) );
	rbLightGridIndirectStage.glslProgram = true;
	idStr::Copynz( rbLightGridIndirectStage.glslProgramName, "lightgrid_indirect.fs", sizeof( rbLightGridIndirectStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_LIGHTGRID_UNIFORM_COUNT] = {
		{ "uBumpMatrixS", 4 },
		{ "uBumpMatrixT", 4 },
		{ "uDiffuseMatrixS", 4 },
		{ "uDiffuseMatrixT", 4 },
		{ "uModelMatrixRow0", 4 },
		{ "uModelMatrixRow1", 4 },
		{ "uModelMatrixRow2", 4 },
		{ "uLightGridOrigin", 4 },
		{ "uLightGridSize", 4 },
		{ "uLightGridBounds", 4 },
		{ "uAtlasInfo", 4 },
		{ "uDiffuseColor", 4 },
		{ "uVertexColorParams", 2 }
	};

	rbLightGridIndirectStage.numShaderParms = RB_LIGHTGRID_UNIFORM_COUNT;
	for ( int i = 0; i < RB_LIGHTGRID_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbLightGridIndirectStage.shaderParmNames[i], uniforms[i].name, sizeof( rbLightGridIndirectStage.shaderParmNames[i] ) );
		rbLightGridIndirectStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbLightGridIndirectStage.numShaderTextures = 3;
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[0], "uBumpMap", sizeof( rbLightGridIndirectStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[1], "uDiffuseMap", sizeof( rbLightGridIndirectStage.shaderTextureNames[1] ) );
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[2], "uLightGridAtlas", sizeof( rbLightGridIndirectStage.shaderTextureNames[2] ) );

	rbLightGridIndirectStageInitialized = true;
}

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
	idStr::Copynz( rbSSAOStage.glslProgramName, "ssao.fs", sizeof( rbSSAOStage.glslProgramName ) );

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

enum rbBloomDownsampleUniformIndex_t {
	RB_BLOOM_DOWNSAMPLE_UNIFORM_INV_TEX_SIZE = 0,
	RB_BLOOM_DOWNSAMPLE_UNIFORM_COUNT
};

enum rbBloomBlurUniformIndex_t {
	RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE = 0,
	RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS,
	RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS,
	RB_BLOOM_BLUR_UNIFORM_COUNT
};

enum rbHDRLuminanceUniformIndex_t {
	RB_HDR_LUMINANCE_UNIFORM_INV_TEX_SIZE = 0,
	RB_HDR_LUMINANCE_UNIFORM_SOURCE_IS_COLOR,
	RB_HDR_LUMINANCE_UNIFORM_COUNT
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
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_HIGHLIGHT_DESATURATION,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAMUT_COMPRESSION,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_DEBUG_VIEW,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT0,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT1,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT2,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT3,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT4,
	RB_BLOOM_COMPOSITE_UNIFORM_COUNT
};

static newShaderStage_t rbBloomExtractStage;
static newShaderStage_t rbBloomDownsampleStage;
static newShaderStage_t rbBloomBlurStage;
static newShaderStage_t rbHDRLuminanceStage;
static newShaderStage_t rbBloomCompositeStage;
static bool rbBloomStagesInitialized = false;
static idImage *rbBloomImages[RB_BLOOM_MAX_LEVELS][2];
static idRenderTexture *rbBloomRenderTextures[RB_BLOOM_MAX_LEVELS][2];
static idImage *rbHDRExposureImages[RB_HDR_EXPOSURE_MAX_LEVELS];
static idRenderTexture *rbHDRExposureRenderTextures[RB_HDR_EXPOSURE_MAX_LEVELS];
static int rbHDRExposureLevelCount = 0;

static void RB_InitBloomStages( void ) {
	if ( rbBloomStagesInitialized ) {
		return;
	}

	memset( &rbBloomExtractStage, 0, sizeof( rbBloomExtractStage ) );
	rbBloomExtractStage.glslProgram = true;
	idStr::Copynz( rbBloomExtractStage.glslProgramName, "bloom_extract.fs", sizeof( rbBloomExtractStage.glslProgramName ) );

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

	memset( &rbBloomDownsampleStage, 0, sizeof( rbBloomDownsampleStage ) );
	rbBloomDownsampleStage.glslProgram = true;
	idStr::Copynz( rbBloomDownsampleStage.glslProgramName, "bloom_downsample.fs", sizeof( rbBloomDownsampleStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t downsampleUniforms[RB_BLOOM_DOWNSAMPLE_UNIFORM_COUNT] = {
		{ "invTexSize", 2 }
	};

	rbBloomDownsampleStage.numShaderParms = RB_BLOOM_DOWNSAMPLE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_DOWNSAMPLE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomDownsampleStage.shaderParmNames[i], downsampleUniforms[i].name, sizeof( rbBloomDownsampleStage.shaderParmNames[i] ) );
		rbBloomDownsampleStage.shaderParmNumRegisters[i] = downsampleUniforms[i].components;
	}
	rbBloomDownsampleStage.numShaderTextures = 1;
	idStr::Copynz( rbBloomDownsampleStage.shaderTextureNames[0], "Scene", sizeof( rbBloomDownsampleStage.shaderTextureNames[0] ) );

	memset( &rbBloomBlurStage, 0, sizeof( rbBloomBlurStage ) );
	rbBloomBlurStage.glslProgram = true;
	idStr::Copynz( rbBloomBlurStage.glslProgramName, "bloom_blur.fs", sizeof( rbBloomBlurStage.glslProgramName ) );

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

	memset( &rbHDRLuminanceStage, 0, sizeof( rbHDRLuminanceStage ) );
	rbHDRLuminanceStage.glslProgram = true;
	idStr::Copynz( rbHDRLuminanceStage.glslProgramName, "hdr_luminance.fs", sizeof( rbHDRLuminanceStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t luminanceUniforms[RB_HDR_LUMINANCE_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "sourceIsColor", 1 }
	};

	rbHDRLuminanceStage.numShaderParms = RB_HDR_LUMINANCE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_HDR_LUMINANCE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbHDRLuminanceStage.shaderParmNames[i], luminanceUniforms[i].name, sizeof( rbHDRLuminanceStage.shaderParmNames[i] ) );
		rbHDRLuminanceStage.shaderParmNumRegisters[i] = luminanceUniforms[i].components;
	}
	rbHDRLuminanceStage.numShaderTextures = 1;
	idStr::Copynz( rbHDRLuminanceStage.shaderTextureNames[0], "Scene", sizeof( rbHDRLuminanceStage.shaderTextureNames[0] ) );

	memset( &rbBloomCompositeStage, 0, sizeof( rbBloomCompositeStage ) );
	rbBloomCompositeStage.glslProgram = true;
	idStr::Copynz( rbBloomCompositeStage.glslProgramName, "bloom.fs", sizeof( rbBloomCompositeStage.glslProgramName ) );

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
		{ "hdrContrast", 1 },
		{ "hdrHighlightDesaturation", 1 },
		{ "hdrGamutCompression", 1 },
		{ "hdrDebugView", 1 },
		{ "bloomWeight0", 1 },
		{ "bloomWeight1", 1 },
		{ "bloomWeight2", 1 },
		{ "bloomWeight3", 1 },
		{ "bloomWeight4", 1 }
	};

	rbBloomCompositeStage.numShaderParms = RB_BLOOM_COMPOSITE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_COMPOSITE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomCompositeStage.shaderParmNames[i], compositeUniforms[i].name, sizeof( rbBloomCompositeStage.shaderParmNames[i] ) );
		rbBloomCompositeStage.shaderParmNumRegisters[i] = compositeUniforms[i].components;
	}
	rbBloomCompositeStage.numShaderTextures = 1 + RB_BLOOM_MAX_LEVELS;
	idStr::Copynz( rbBloomCompositeStage.shaderTextureNames[0], "Scene", sizeof( rbBloomCompositeStage.shaderTextureNames[0] ) );
	for ( int i = 0; i < RB_BLOOM_MAX_LEVELS; i++ ) {
		idStr::Copynz( rbBloomCompositeStage.shaderTextureNames[i + 1], va( "BloomTex%d", i ), sizeof( rbBloomCompositeStage.shaderTextureNames[i + 1] ) );
	}

	rbBloomStagesInitialized = true;
}

static void RB_GetBloomLevelSize( int viewportWidth, int viewportHeight, int level, int &levelWidth, int &levelHeight ) {
	levelWidth = Max( 1, viewportWidth );
	levelHeight = Max( 1, viewportHeight );

	for ( int i = 0; i < level; i++ ) {
		levelWidth = Max( 1, ( levelWidth + 1 ) / 2 );
		levelHeight = Max( 1, ( levelHeight + 1 ) / 2 );
	}
}

static bool RB_EnsureBloomRenderTextures( int viewportWidth, int viewportHeight, int levelCount ) {
	for ( int level = 0; level < levelCount; level++ ) {
		int bloomWidth = 0;
		int bloomHeight = 0;
		RB_GetBloomLevelSize( viewportWidth, viewportHeight, level, bloomWidth, bloomHeight );

		for ( int ping = 0; ping < 2; ping++ ) {
			idImageOpts opts;
			opts.textureType = TT_2D;
			opts.format = FMT_RGBA16F;
			opts.width = bloomWidth;
			opts.height = bloomHeight;
			opts.numLevels = 1;
			opts.isPersistant = true;

			rbBloomImages[level][ping] = globalImages->ScratchImage( va( "_bloomL%dP%d", level, ping ), &opts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
			if ( rbBloomImages[level][ping] == NULL ) {
				return false;
			}

			if ( rbBloomRenderTextures[level][ping] == NULL ) {
				rbBloomRenderTextures[level][ping] = tr.CreateRenderTexture( rbBloomImages[level][ping], NULL );
			} else if ( rbBloomRenderTextures[level][ping]->GetWidth() != bloomWidth || rbBloomRenderTextures[level][ping]->GetHeight() != bloomHeight ) {
				tr.ResizeRenderTexture( rbBloomRenderTextures[level][ping], bloomWidth, bloomHeight );
			}

			if ( rbBloomRenderTextures[level][ping] == NULL ) {
				return false;
			}
		}
	}

	return true;
}

static void RB_BindPostProcessRenderTexture( idRenderTexture *renderTexture, int width, int height ) {
	backEnd.renderTexture = renderTexture;
	renderTexture->MakeCurrent();
	glViewport( 0, 0, width, height );
	glScissor( 0, 0, width, height );
}

static void RB_RestorePostProcessTarget( idRenderTexture *renderTexture, int viewportWidth, int viewportHeight ) {
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

static bool RB_EnsureHDRExposureRenderTextures( int viewportWidth, int viewportHeight ) {
	rbHDRExposureLevelCount = 0;

	int levelWidth = Max( 1, ( viewportWidth + 1 ) / 2 );
	int levelHeight = Max( 1, ( viewportHeight + 1 ) / 2 );

	while ( rbHDRExposureLevelCount < RB_HDR_EXPOSURE_MAX_LEVELS ) {
		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_RGBA16F;
		opts.width = levelWidth;
		opts.height = levelHeight;
		opts.numLevels = 1;
		opts.isPersistant = true;

		const int level = rbHDRExposureLevelCount;
		rbHDRExposureImages[level] = globalImages->ScratchImage( va( "_hdrLum%d", level ), &opts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
		if ( rbHDRExposureImages[level] == NULL ) {
			return false;
		}

		if ( rbHDRExposureRenderTextures[level] == NULL ) {
			rbHDRExposureRenderTextures[level] = tr.CreateRenderTexture( rbHDRExposureImages[level], NULL );
		} else if ( rbHDRExposureRenderTextures[level]->GetWidth() != levelWidth || rbHDRExposureRenderTextures[level]->GetHeight() != levelHeight ) {
			tr.ResizeRenderTexture( rbHDRExposureRenderTextures[level], levelWidth, levelHeight );
		}

		if ( rbHDRExposureRenderTextures[level] == NULL ) {
			return false;
		}

		rbHDRExposureLevelCount++;
		if ( levelWidth == 1 && levelHeight == 1 ) {
			break;
		}

		levelWidth = Max( 1, ( levelWidth + 1 ) / 2 );
		levelHeight = Max( 1, ( levelHeight + 1 ) / 2 );
	}

	return rbHDRExposureLevelCount > 0;
}

static float RB_UpdateHDRAutoExposure( idImage *sceneImage, int viewportWidth, int viewportHeight ) {
	if ( !RB_HDRAutoExposureEnabled() ) {
		rbHDRLastAverageLuminance = 1.0f;
		rbHDRLastTargetExposure = 1.0f;
		return 1.0f;
	}

	if ( sceneImage == NULL ) {
		return rbHDRExposureInitialized ? rbHDRAdaptedExposure : 1.0f;
	}

	RB_InitBloomStages();
	if ( !R_ValidateGLSLProgram( &rbHDRLuminanceStage ) || !RB_EnsureHDRExposureRenderTextures( viewportWidth, viewportHeight ) ) {
		return rbHDRExposureInitialized ? rbHDRAdaptedExposure : 1.0f;
	}

	idRenderTexture *originalRenderTexture = backEnd.renderTexture;
	idImage *sourceImage = sceneImage;
	int sourceWidth = Max( 1, sceneImage->GetOpts().width );
	int sourceHeight = Max( 1, sceneImage->GetOpts().height );
	bool sourceIsColor = true;

	for ( int level = 0; level < rbHDRExposureLevelCount; level++ ) {
		const int levelWidth = rbHDRExposureRenderTextures[level]->GetWidth();
		const int levelHeight = rbHDRExposureRenderTextures[level]->GetHeight();
		const GLfloat invTexSize[2] = {
			1.0f / static_cast<GLfloat>( Max( 1, sourceWidth ) ),
			1.0f / static_cast<GLfloat>( Max( 1, sourceHeight ) )
		};

		RB_BindPostProcessRenderTexture( rbHDRExposureRenderTextures[level], levelWidth, levelHeight );
		RB_BeginFullscreenPostProcessPass( 0, 0, levelWidth, levelHeight );
		GL_SelectTexture( 0 );
		sourceImage->Bind();

		glUseProgramObjectARB( (GLhandleARB)rbHDRLuminanceStage.glslProgramObject );
		if ( rbHDRLuminanceStage.shaderTextureLocations[0] >= 0 ) {
			glUniform1iARB( rbHDRLuminanceStage.shaderTextureLocations[0], 0 );
		}
		if ( rbHDRLuminanceStage.shaderParmLocations[RB_HDR_LUMINANCE_UNIFORM_INV_TEX_SIZE] >= 0 ) {
			glUniform2fvARB( rbHDRLuminanceStage.shaderParmLocations[RB_HDR_LUMINANCE_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
		}
		if ( rbHDRLuminanceStage.shaderParmLocations[RB_HDR_LUMINANCE_UNIFORM_SOURCE_IS_COLOR] >= 0 ) {
			glUniform1fARB( rbHDRLuminanceStage.shaderParmLocations[RB_HDR_LUMINANCE_UNIFORM_SOURCE_IS_COLOR], sourceIsColor ? 1.0f : 0.0f );
		}

		RB_DrawFullscreenPostProcessQuadUnitUV();
		glUseProgramObjectARB( 0 );
		RB_EndFullscreenPostProcessPass();

		sourceImage = rbHDRExposureImages[level];
		sourceWidth = levelWidth;
		sourceHeight = levelHeight;
		sourceIsColor = false;
	}

	GLfloat pixel[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glReadPixels( 0, 0, 1, 1, GL_RGBA, GL_FLOAT, pixel );
	RB_RestorePostProcessTarget( originalRenderTexture, viewportWidth, viewportHeight );

	float averageLogLuminance = pixel[0];
	if ( averageLogLuminance != averageLogLuminance ) {
		averageLogLuminance = 0.0f;
	}
	averageLogLuminance = idMath::ClampFloat( -16.0f, 16.0f, averageLogLuminance );

	const float averageLuminance = Max( idMath::Exp( averageLogLuminance ), 0.0001f );
	const float keyValue = r_hdrKeyValue.GetFloat();
	const float minExposure = Min( r_hdrMinExposure.GetFloat(), r_hdrMaxExposure.GetFloat() );
	const float maxExposure = Max( r_hdrMinExposure.GetFloat(), r_hdrMaxExposure.GetFloat() );
	const float targetExposure = idMath::ClampFloat( minExposure, maxExposure, keyValue / averageLuminance );
	const float now = backEnd.viewDef->floatTime;

	if ( !rbHDRExposureInitialized || now < rbHDRLastAdaptationTime || ( now - rbHDRLastAdaptationTime ) > 1.0f ) {
		rbHDRAdaptedExposure = targetExposure;
		rbHDRExposureInitialized = true;
	} else {
		const float deltaSeconds = Max( 0.0f, now - rbHDRLastAdaptationTime );
		const float adaptationSpeed = ( targetExposure > rbHDRAdaptedExposure ) ? r_hdrAdaptUpSpeed.GetFloat() : r_hdrAdaptDownSpeed.GetFloat();
		const float blend = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - idMath::Exp( -adaptationSpeed * deltaSeconds ) );
		rbHDRAdaptedExposure += ( targetExposure - rbHDRAdaptedExposure ) * blend;
	}

	rbHDRLastAverageLuminance = averageLuminance;
	rbHDRLastTargetExposure = targetExposure;
	rbHDRLastAdaptationTime = now;
	return rbHDRAdaptedExposure;
}

static void RB_STD_Bloom( void ) {
	if ( r_skipPostProcess.GetBool() ) {
		return;
	}

	const bool bloomRequested = RB_PostProcessBloomRequested();
	const bool toneMapEnabled = r_hdrToneMap.GetBool();
	const int hdrDebugView = RB_HDRDebugViewValue();
	if ( !bloomRequested && !toneMapEnabled && hdrDebugView == 0 ) {
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
	RB_CaptureCurrentRenderImage( viewportWidth, viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	const GLfloat adaptedExposure = RB_HDRAutoExposureEnabled()
		? static_cast<GLfloat>( RB_UpdateHDRAutoExposure( sceneImage, viewportWidth, viewportHeight ) )
		: 1.0f;
	const GLfloat hdrExposure = r_hdrExposure.GetFloat() * adaptedExposure;
	const GLfloat hdrWhitePoint = r_hdrWhitePoint.GetFloat();
	const GLfloat hdrLift = r_hdrLift.GetFloat();
	const GLfloat hdrPostGamma = r_hdrPostGamma.GetFloat();
	const GLfloat hdrGain = r_hdrGain.GetFloat();
	const GLfloat hdrVibrance = r_hdrVibrance.GetFloat();
	const GLfloat hdrSaturation = r_hdrSaturation.GetFloat();
	const GLfloat hdrContrast = r_hdrContrast.GetFloat();
	const GLfloat hdrHighlightDesaturation = r_hdrHighlightDesaturation.GetFloat();
	const GLfloat hdrGamutCompression = r_hdrGamutCompression.GetFloat();
	const GLfloat bloomIntensity = bloomRequested ? r_bloomIntensity.GetFloat() : 0.0f;
	const GLfloat bloomRadius = Max( r_bloomRadius.GetFloat(), 0.1f );
	const GLfloat bloomThreshold = r_bloomThreshold.GetFloat();
	const GLfloat bloomSoftKnee = r_bloomSoftKnee.GetFloat();
	const GLfloat toneMapToggle = toneMapEnabled ? 1.0f : 0.0f;
	const int bloomLevelCount = idMath::ClampInt( 1, RB_BLOOM_MAX_LEVELS, r_bloomMipCount.GetInteger() );

	idImage *bloomImages[RB_BLOOM_MAX_LEVELS];
	GLfloat bloomWeights[RB_BLOOM_MAX_LEVELS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	for ( int i = 0; i < RB_BLOOM_MAX_LEVELS; i++ ) {
		bloomImages[i] = globalImages->blackImage;
	}

	idRenderTexture *originalRenderTexture = backEnd.renderTexture;
	bool bloomEnabled = false;

	if ( bloomRequested ) {
		RB_InitBloomStages();
		if ( R_ValidateGLSLProgram( &rbBloomExtractStage ) &&
			R_ValidateGLSLProgram( &rbBloomDownsampleStage ) &&
			R_ValidateGLSLProgram( &rbBloomBlurStage ) &&
			RB_EnsureBloomRenderTextures( viewportWidth, viewportHeight, bloomLevelCount ) ) {
			float weightSum = 0.0f;
			for ( int level = 0; level < bloomLevelCount; level++ ) {
				weightSum += RB_BLOOM_BASE_WEIGHTS[level];
			}
			if ( weightSum <= 0.0f ) {
				weightSum = 1.0f;
			}

			for ( int level = 0; level < bloomLevelCount; level++ ) {
				int bloomWidth = 0;
				int bloomHeight = 0;
				RB_GetBloomLevelSize( viewportWidth, viewportHeight, level, bloomWidth, bloomHeight );

				idImage *sourceImage = ( level == 0 ) ? sceneImage : rbBloomImages[level - 1][0];
				const int sourceWidth = ( level == 0 ) ? textureWidth : rbBloomRenderTextures[level - 1][0]->GetWidth();
				const int sourceHeight = ( level == 0 ) ? textureHeight : rbBloomRenderTextures[level - 1][0]->GetHeight();
				const GLfloat sourceInvTexSize[2] = {
					1.0f / static_cast<GLfloat>( Max( 1, sourceWidth ) ),
					1.0f / static_cast<GLfloat>( Max( 1, sourceHeight ) )
				};
				const GLfloat bloomInvTexSize[2] = {
					1.0f / static_cast<GLfloat>( Max( 1, bloomWidth ) ),
					1.0f / static_cast<GLfloat>( Max( 1, bloomHeight ) )
				};
				const GLfloat blurRadiusForLevel = bloomRadius * ( 1.0f + static_cast<GLfloat>( level ) * 0.65f );

				RB_BindPostProcessRenderTexture( rbBloomRenderTextures[level][0], bloomWidth, bloomHeight );
				RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
				GL_SelectTexture( 0 );
				sourceImage->Bind();
				glUseProgramObjectARB( (GLhandleARB)( ( level == 0 ) ? rbBloomExtractStage.glslProgramObject : rbBloomDownsampleStage.glslProgramObject ) );
				if ( level == 0 ) {
					if ( rbBloomExtractStage.shaderTextureLocations[0] >= 0 ) {
						glUniform1iARB( rbBloomExtractStage.shaderTextureLocations[0], 0 );
					}
					if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_INV_TEX_SIZE] >= 0 ) {
						glUniform2fvARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_INV_TEX_SIZE], 1, sourceInvTexSize );
					}
					if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_THRESHOLD] >= 0 ) {
						glUniform1fARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_THRESHOLD], bloomThreshold );
					}
					if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_SOFT_KNEE] >= 0 ) {
						glUniform1fARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_SOFT_KNEE], bloomSoftKnee );
					}
				} else {
					if ( rbBloomDownsampleStage.shaderTextureLocations[0] >= 0 ) {
						glUniform1iARB( rbBloomDownsampleStage.shaderTextureLocations[0], 0 );
					}
					if ( rbBloomDownsampleStage.shaderParmLocations[RB_BLOOM_DOWNSAMPLE_UNIFORM_INV_TEX_SIZE] >= 0 ) {
						glUniform2fvARB( rbBloomDownsampleStage.shaderParmLocations[RB_BLOOM_DOWNSAMPLE_UNIFORM_INV_TEX_SIZE], 1, sourceInvTexSize );
					}
				}
				RB_DrawFullscreenPostProcessQuadUnitUV();
				glUseProgramObjectARB( 0 );
				RB_EndFullscreenPostProcessPass();

				RB_BindPostProcessRenderTexture( rbBloomRenderTextures[level][1], bloomWidth, bloomHeight );
				RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
				GL_SelectTexture( 0 );
				rbBloomImages[level][0]->Bind();
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
					glUniform1fARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS], blurRadiusForLevel );
				}
				RB_DrawFullscreenPostProcessQuadUnitUV();
				glUseProgramObjectARB( 0 );
				RB_EndFullscreenPostProcessPass();

				RB_BindPostProcessRenderTexture( rbBloomRenderTextures[level][0], bloomWidth, bloomHeight );
				RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
				GL_SelectTexture( 0 );
				rbBloomImages[level][1]->Bind();
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
					glUniform1fARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS], blurRadiusForLevel );
				}
				RB_DrawFullscreenPostProcessQuadUnitUV();
				glUseProgramObjectARB( 0 );
				RB_EndFullscreenPostProcessPass();

				bloomImages[level] = rbBloomImages[level][0];
				bloomWeights[level] = RB_BLOOM_BASE_WEIGHTS[level] / weightSum;
			}

			bloomEnabled = true;
		}
	}

	RB_RestorePostProcessTarget( originalRenderTexture, viewportWidth, viewportHeight );
	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_SelectTexture( 0 );
	sceneImage->Bind();
	for ( int level = 0; level < RB_BLOOM_MAX_LEVELS; level++ ) {
		GL_SelectTexture( level + 1 );
		bloomImages[level]->Bind();
	}
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbBloomCompositeStage.glslProgramObject );
	for ( int i = 0; i < rbBloomCompositeStage.numShaderTextures; i++ ) {
		if ( rbBloomCompositeStage.shaderTextureLocations[i] >= 0 ) {
			glUniform1iARB( rbBloomCompositeStage.shaderTextureLocations[i], i );
		}
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
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_HIGHLIGHT_DESATURATION] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_HIGHLIGHT_DESATURATION], hdrHighlightDesaturation );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAMUT_COMPRESSION] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAMUT_COMPRESSION], hdrGamutCompression );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_DEBUG_VIEW] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_DEBUG_VIEW], static_cast<GLfloat>( hdrDebugView ) );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT0] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT0], bloomWeights[0] );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT1] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT1], bloomWeights[1] );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT2] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT2], bloomWeights[2] );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT3] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT3], bloomWeights[3] );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT4] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT4], bloomWeights[4] );
	}

	if ( originalRenderTexture == NULL ) {
		RB_SetFramebufferSRGBEnabled( true );
	}
	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );
	if ( originalRenderTexture == NULL ) {
		RB_SetFramebufferSRGBEnabled( false );
	}
	glUseProgramObjectARB( 0 );
	for ( int level = RB_BLOOM_MAX_LEVELS; level >= 1; level-- ) {
		GL_SelectTexture( level );
		globalImages->BindNull();
	}
	GL_SelectTexture( 0 );
	RB_EndFullscreenPostProcessPass();

	if ( originalRenderTexture != NULL ) {
		RB_CaptureCurrentRenderImage( viewportWidth, viewportHeight );
	}
}

static void RB_PresentSceneRenderTargetToBackBuffer( void ) {
	if ( !RB_IsSceneRenderTexture( backEnd.renderTexture ) || backEnd.viewDef == NULL ) {
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

	RB_CaptureCurrentRenderImage( viewportWidth, viewportHeight );

	idRenderTexture::BindNull();
	backEnd.renderTexture = NULL;
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
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

	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_TexEnv( GL_MODULATE );

	RB_SetFramebufferSRGBEnabled( true );
	RB_DrawFullscreenPostProcessQuadUnitUV();
	RB_SetFramebufferSRGBEnabled( false );

	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
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
	idStr::Copynz( rbResolutionScaleStage.glslProgramName, "resolutionscale.fs", sizeof( rbResolutionScaleStage.glslProgramName ) );

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
	idStr::Copynz( rbCRTStage.glslProgramName, "crt.fs", sizeof( rbCRTStage.glslProgramName ) );

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

enum rbRVSpecialDepthUniformIndex_t {
	RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE = 0,
	RB_RVSPECIAL_DEPTH_UNIFORM_COUNT
};

enum rbRVSpecialBlurUniformIndex_t {
	RB_RVSPECIAL_BLUR_UNIFORM_TEXTURE_SCALE = 0,
	RB_RVSPECIAL_BLUR_UNIFORM_SAMPLE_DIST,
	RB_RVSPECIAL_BLUR_UNIFORM_COUNT
};

enum rbRVSpecialMedLabsUniformIndex_t {
	RB_RVSPECIAL_MEDLABS_UNIFORM_RANGE = 0,
	RB_RVSPECIAL_MEDLABS_UNIFORM_FOCUS,
	RB_RVSPECIAL_MEDLABS_UNIFORM_SCROLL,
	RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_COLOR,
	RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_PERCENT,
	RB_RVSPECIAL_MEDLABS_UNIFORM_COUNT
};

enum rbRVSpecialALUniformIndex_t {
	RB_RVSPECIAL_AL_UNIFORM_DISTANCE_SCALE = 0,
	RB_RVSPECIAL_AL_UNIFORM_LIGHT_LOC,
	RB_RVSPECIAL_AL_UNIFORM_LIGHT_COLOR,
	RB_RVSPECIAL_AL_UNIFORM_LIGHT_SIZE,
	RB_RVSPECIAL_AL_UNIFORM_LIGHT_MIN_DISTANCE,
	RB_RVSPECIAL_AL_UNIFORM_COUNT
};

static newShaderStage_t rbRVSpecialDepthStage;
static newShaderStage_t rbRVSpecialBlurStage;
static newShaderStage_t rbRVSpecialMedLabsStage;
static newShaderStage_t rbRVSpecialALStage;
static bool rbRVSpecialStagesInitialized = false;
static bool rbRVSpecialBlurPrepared = false;
static bool rbRVSpecialALPrepared = false;
static bool rbRVSpecialCaptureUsesDiffuseImage = false;
static int rbRVSpecialActiveMask = 0;

static void RB_InitRVSpecialStages( void ) {
	if ( rbRVSpecialStagesInitialized ) {
		return;
	}

	memset( &rbRVSpecialDepthStage, 0, sizeof( rbRVSpecialDepthStage ) );
	rbRVSpecialDepthStage.glslProgram = true;
	idStr::Copynz( rbRVSpecialDepthStage.glslProgramName, "rvspecial_depth.fs", sizeof( rbRVSpecialDepthStage.glslProgramName ) );
	rbRVSpecialDepthStage.numShaderParms = RB_RVSPECIAL_DEPTH_UNIFORM_COUNT;
	idStr::Copynz( rbRVSpecialDepthStage.shaderParmNames[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE], "distanceScale",
		sizeof( rbRVSpecialDepthStage.shaderParmNames[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE] ) );
	rbRVSpecialDepthStage.shaderParmNumRegisters[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE] = 1;
	rbRVSpecialDepthStage.numShaderTextures = 1;
	idStr::Copynz( rbRVSpecialDepthStage.shaderTextureNames[0], "Image", sizeof( rbRVSpecialDepthStage.shaderTextureNames[0] ) );

	memset( &rbRVSpecialBlurStage, 0, sizeof( rbRVSpecialBlurStage ) );
	rbRVSpecialBlurStage.glslProgram = true;
	idStr::Copynz( rbRVSpecialBlurStage.glslProgramName, "rvspecial_blur.fs", sizeof( rbRVSpecialBlurStage.glslProgramName ) );
	static const rbBuiltinUniformDef_t blurUniforms[RB_RVSPECIAL_BLUR_UNIFORM_COUNT] = {
		{ "textureScale", 2 },
		{ "sampleDist", 1 }
	};
	rbRVSpecialBlurStage.numShaderParms = RB_RVSPECIAL_BLUR_UNIFORM_COUNT;
	for ( int i = 0; i < RB_RVSPECIAL_BLUR_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbRVSpecialBlurStage.shaderParmNames[i], blurUniforms[i].name,
			sizeof( rbRVSpecialBlurStage.shaderParmNames[i] ) );
		rbRVSpecialBlurStage.shaderParmNumRegisters[i] = blurUniforms[i].components;
	}
	rbRVSpecialBlurStage.numShaderTextures = 1;
	idStr::Copynz( rbRVSpecialBlurStage.shaderTextureNames[0], "Image", sizeof( rbRVSpecialBlurStage.shaderTextureNames[0] ) );

	memset( &rbRVSpecialMedLabsStage, 0, sizeof( rbRVSpecialMedLabsStage ) );
	rbRVSpecialMedLabsStage.glslProgram = true;
	idStr::Copynz( rbRVSpecialMedLabsStage.glslProgramName, "rvspecial_medlabs.fs", sizeof( rbRVSpecialMedLabsStage.glslProgramName ) );
	static const rbBuiltinUniformDef_t medlabsUniforms[RB_RVSPECIAL_MEDLABS_UNIFORM_COUNT] = {
		{ "effectRange", 1 },
		{ "focus", 1 },
		{ "scroll", 1 },
		{ "approachColor", 4 },
		{ "approachPercent", 1 }
	};
	rbRVSpecialMedLabsStage.numShaderParms = RB_RVSPECIAL_MEDLABS_UNIFORM_COUNT;
	for ( int i = 0; i < RB_RVSPECIAL_MEDLABS_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbRVSpecialMedLabsStage.shaderParmNames[i], medlabsUniforms[i].name,
			sizeof( rbRVSpecialMedLabsStage.shaderParmNames[i] ) );
		rbRVSpecialMedLabsStage.shaderParmNumRegisters[i] = medlabsUniforms[i].components;
	}
	rbRVSpecialMedLabsStage.numShaderTextures = 2;
	idStr::Copynz( rbRVSpecialMedLabsStage.shaderTextureNames[0], "Depth", sizeof( rbRVSpecialMedLabsStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbRVSpecialMedLabsStage.shaderTextureNames[1], "Blur1", sizeof( rbRVSpecialMedLabsStage.shaderTextureNames[1] ) );

	memset( &rbRVSpecialALStage, 0, sizeof( rbRVSpecialALStage ) );
	rbRVSpecialALStage.glslProgram = true;
	idStr::Copynz( rbRVSpecialALStage.glslProgramName, "rvspecial_al.fs", sizeof( rbRVSpecialALStage.glslProgramName ) );
	static const rbBuiltinUniformDef_t alUniforms[RB_RVSPECIAL_AL_UNIFORM_COUNT] = {
		{ "distanceScale", 1 },
		{ "LightLoc", 3 },
		{ "LightColor", 4 },
		{ "LightSize", 1 },
		{ "LightMinDistance", 1 }
	};
	rbRVSpecialALStage.numShaderParms = RB_RVSPECIAL_AL_UNIFORM_COUNT;
	for ( int i = 0; i < RB_RVSPECIAL_AL_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbRVSpecialALStage.shaderParmNames[i], alUniforms[i].name,
			sizeof( rbRVSpecialALStage.shaderParmNames[i] ) );
		rbRVSpecialALStage.shaderParmNumRegisters[i] = alUniforms[i].components;
	}
	rbRVSpecialALStage.numShaderTextures = 2;
	idStr::Copynz( rbRVSpecialALStage.shaderTextureNames[0], "RT", sizeof( rbRVSpecialALStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbRVSpecialALStage.shaderTextureNames[1], "LightImage", sizeof( rbRVSpecialALStage.shaderTextureNames[1] ) );

	rbRVSpecialStagesInitialized = true;
}

static idImage *RB_CreateOrUpdateSpecialImage( const char *name, int width, int height, textureFormat_t format, textureFilter_t filter ) {
	idImageOpts opts;
	memset( &opts, 0, sizeof( opts ) );
	opts.textureType = TT_2D;
	opts.format = format;
	opts.width = width;
	opts.height = height;
	opts.numLevels = 1;
	opts.numMSAASamples = 0;
	opts.isPersistant = true;
	return tr.CreateImage( name, &opts, filter );
}

static bool RB_EnsureRVSpecialBlurResources( void ) {
	const int width = 256;
	const int height = 256;

	tr.specialBlurDepthImage = RB_CreateOrUpdateSpecialImage( "DepthTexture", width, height, FMT_RGBA16F, TF_LINEAR );
	tr.specialBlurDepthStencilImage = RB_CreateOrUpdateSpecialImage( "_rvspecialBlurDepthDS", width, height, FMT_DEPTH_STENCIL, TF_NEAREST );
	tr.specialBlurImage = RB_CreateOrUpdateSpecialImage( "BlurTexture1", width, height, FMT_RGBA16F, TF_LINEAR );
	if ( tr.specialBlurDepthImage == NULL || tr.specialBlurDepthStencilImage == NULL || tr.specialBlurImage == NULL ) {
		return false;
	}

	if ( tr.specialBlurDepthRenderTexture == NULL ) {
		tr.specialBlurDepthRenderTexture = tr.CreateRenderTexture( tr.specialBlurDepthImage, tr.specialBlurDepthStencilImage );
	} else if ( tr.specialBlurDepthRenderTexture->GetWidth() != width || tr.specialBlurDepthRenderTexture->GetHeight() != height ) {
		tr.ResizeRenderTexture( tr.specialBlurDepthRenderTexture, width, height );
	}

	if ( tr.specialBlurRenderTexture == NULL ) {
		tr.specialBlurRenderTexture = tr.CreateRenderTexture( tr.specialBlurImage, NULL );
	} else if ( tr.specialBlurRenderTexture->GetWidth() != width || tr.specialBlurRenderTexture->GetHeight() != height ) {
		tr.ResizeRenderTexture( tr.specialBlurRenderTexture, width, height );
	}

	return tr.specialBlurDepthRenderTexture != NULL && tr.specialBlurRenderTexture != NULL;
}

static bool RB_EnsureRVSpecialALResources( void ) {
	const int width = 512;
	const int height = 512;

	tr.specialALDepthImage = RB_CreateOrUpdateSpecialImage( "_rvspecialALDepth", width, height, FMT_RGBA16F, TF_NEAREST );
	tr.specialALDepthStencilImage = RB_CreateOrUpdateSpecialImage( "_rvspecialALDepthDS", width, height, FMT_DEPTH_STENCIL, TF_NEAREST );
	if ( tr.specialALDepthImage == NULL || tr.specialALDepthStencilImage == NULL ) {
		return false;
	}

	if ( tr.specialALDepthRenderTexture == NULL ) {
		tr.specialALDepthRenderTexture = tr.CreateRenderTexture( tr.specialALDepthImage, tr.specialALDepthStencilImage );
	} else if ( tr.specialALDepthRenderTexture->GetWidth() != width || tr.specialALDepthRenderTexture->GetHeight() != height ) {
		tr.ResizeRenderTexture( tr.specialALDepthRenderTexture, width, height );
	}

	if ( tr.specialALLightImage == NULL ) {
		tr.specialALLightImage = globalImages->ImageFromFile( "gfx/lights/round.tga", TF_LINEAR, TR_CLAMP, TD_DEFAULT );
	}

	return tr.specialALDepthRenderTexture != NULL && tr.specialALLightImage != NULL;
}

static void RB_RVSpecialRestoreDrawingView( void ) {
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	backEnd.currentSpace = NULL;

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

	GL_State( GLS_DEPTHFUNC_EQUAL );
	if ( backEnd.viewDef->viewEntitys ) {
		glEnable( GL_DEPTH_TEST );
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_DEPTH_TEST );
		glDisable( GL_STENCIL_TEST );
	}

	backEnd.glState.faceCulling = -1;
	GL_Cull( CT_FRONT_SIDED );
	backEnd.glState.forceGlState = true;
}

static bool RB_SetRVSpecialOrthoForView( void );

enum rbLensFlareUniformIndex_t {
	RB_LENSFLARE_UNIFORM_INV_DEPTH_TEX_SIZE = 0,
	RB_LENSFLARE_UNIFORM_VIEWPORT_TEX_SCALE,
	RB_LENSFLARE_UNIFORM_LIGHT_CENTER_UV,
	RB_LENSFLARE_UNIFORM_LIGHT_COLOR,
	RB_LENSFLARE_UNIFORM_LIGHT_DEPTH,
	RB_LENSFLARE_UNIFORM_OCCLUSION_RADIUS,
	RB_LENSFLARE_UNIFORM_FLARE_AXIS,
	RB_LENSFLARE_UNIFORM_ELEMENT_KIND,
	RB_LENSFLARE_UNIFORM_ELEMENT_PARAMS,
	RB_LENSFLARE_UNIFORM_COUNT
};

static newShaderStage_t rbLensFlareStage;
static bool rbLensFlareStageInitialized = false;

static const int RB_LENSFLARE_MAX_LIGHTS = 8;

typedef struct rbLensFlareCandidate_s {
	float	score;
	float	screenX;
	float	screenY;
	float	screenU;
	float	screenV;
	float	lightDepth;
	float	sourceRadiusPixels;
	float	coronaRadiusPixels;
	idVec2	axis;
	idVec4	color;
} rbLensFlareCandidate_t;

static void RB_InitLensFlareStage( void ) {
	if ( rbLensFlareStageInitialized ) {
		return;
	}

	memset( &rbLensFlareStage, 0, sizeof( rbLensFlareStage ) );
	rbLensFlareStage.glslProgram = true;
	idStr::Copynz( rbLensFlareStage.glslProgramName, "lensflare.fs", sizeof( rbLensFlareStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t lensFlareUniforms[RB_LENSFLARE_UNIFORM_COUNT] = {
		{ "invDepthTexSize", 2 },
		{ "viewportTexScale", 2 },
		{ "lightCenterUV", 2 },
		{ "lightColor", 4 },
		{ "lightDepth", 1 },
		{ "occlusionRadiusPixels", 1 },
		{ "flareAxis", 2 },
		{ "elementKind", 1 },
		{ "elementParams", 4 }
	};

	rbLensFlareStage.numShaderParms = RB_LENSFLARE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_LENSFLARE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbLensFlareStage.shaderParmNames[i], lensFlareUniforms[i].name, sizeof( rbLensFlareStage.shaderParmNames[i] ) );
		rbLensFlareStage.shaderParmNumRegisters[i] = lensFlareUniforms[i].components;
	}

	rbLensFlareStage.numShaderTextures = 1;
	idStr::Copynz( rbLensFlareStage.shaderTextureNames[0], "DepthBuffer", sizeof( rbLensFlareStage.shaderTextureNames[0] ) );
	rbLensFlareStageInitialized = true;
}

static bool RB_ProjectLensFlarePoint( const idVec3 &origin, int viewportWidth, int viewportHeight, float &screenX, float &screenY, float &depth01 ) {
	idPlane eye;
	idPlane clip;
	idVec3 ndc;

	R_TransformModelToClip( origin, backEnd.viewDef->worldSpace.modelViewMatrix, backEnd.viewDef->projectionMatrix, eye, clip );
	if ( clip[3] <= 0.001f ) {
		return false;
	}

	R_TransformClipToDevice( clip, backEnd.viewDef, ndc );
	screenX = ( ndc.x * 0.5f + 0.5f ) * viewportWidth;
	screenY = ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * viewportHeight;
	depth01 = idMath::ClampFloat( 0.0f, 1.0f, ( clip[2] + clip[3] ) / ( 2.0f * clip[3] ) );
	return true;
}

static bool RB_EvaluateLensFlareLightColor( const viewLight_t *vLight, idVec4 &lightColor ) {
	if ( vLight == NULL || vLight->lightShader == NULL || vLight->shaderRegisters == NULL ) {
		return false;
	}
	if ( vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
		return false;
	}

	lightColor.Set( 0.0f, 0.0f, 0.0f, 1.0f );
	const float *regs = vLight->shaderRegisters;
	const idMaterial *lightShader = vLight->lightShader;

	for ( int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++ ) {
		const shaderStage_t *lightStage = lightShader->GetStage( lightStageNum );
		if ( lightStage == NULL || !regs[ lightStage->conditionRegister ] ) {
			continue;
		}

		lightColor[0] += Max( 0.0f, r_lightScale.GetFloat() * regs[ lightStage->color.registers[0] ] );
		lightColor[1] += Max( 0.0f, r_lightScale.GetFloat() * regs[ lightStage->color.registers[1] ] );
		lightColor[2] += Max( 0.0f, r_lightScale.GetFloat() * regs[ lightStage->color.registers[2] ] );
		lightColor[3] = Max( lightColor[3], Max( 0.0f, regs[ lightStage->color.registers[3] ] ) );
	}

	const float brightness = Max( lightColor[0], Max( lightColor[1], lightColor[2] ) );
	return brightness > 0.02f;
}

static float RB_EstimateLensFlareWorldRadius( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightDef == NULL ) {
		return 0.0f;
	}

	if ( vLight->pointLight ) {
		return Max( vLight->lightRadius.x, Max( vLight->lightRadius.y, vLight->lightRadius.z ) );
	}

	const renderLight_t &parms = vLight->lightDef->parms;
	return Max( parms.right.Length(), Max( parms.up.Length(), parms.target.Length() ) ) * 0.35f;
}

static float RB_EstimateLensFlareRadiusPixels( const viewLight_t *vLight, float centerX, float centerY, int viewportWidth, int viewportHeight ) {
	float radiusPixels = 0.0f;
	const float worldRadius = RB_EstimateLensFlareWorldRadius( vLight );

	if ( worldRadius > 0.0f ) {
		const idVec3 offsetPoint = vLight->globalLightOrigin + backEnd.viewDef->renderView.viewaxis[1] * worldRadius;
		float offsetX = 0.0f;
		float offsetY = 0.0f;
		float depth01 = 0.0f;
		if ( RB_ProjectLensFlarePoint( offsetPoint, viewportWidth, viewportHeight, offsetX, offsetY, depth01 ) ) {
			const float dx = offsetX - centerX;
			const float dy = offsetY - centerY;
			radiusPixels = idMath::Sqrt( dx * dx + dy * dy );
		}
	}

	if ( radiusPixels <= 1.0f ) {
		const float scissorWidth = Max( 1.0f, static_cast<float>( vLight->scissorRect.x2 - vLight->scissorRect.x1 + 1 ) );
		const float scissorHeight = Max( 1.0f, static_cast<float>( vLight->scissorRect.y2 - vLight->scissorRect.y1 + 1 ) );
		radiusPixels = idMath::Sqrt( scissorWidth * scissorHeight ) * 0.12f;
	}

	return radiusPixels;
}

static void RB_InsertLensFlareCandidate( rbLensFlareCandidate_t candidates[RB_LENSFLARE_MAX_LIGHTS], int &candidateCount,
		const rbLensFlareCandidate_t &candidate ) {
	int insertIndex = candidateCount;

	for ( int i = 0; i < candidateCount; i++ ) {
		if ( candidate.score > candidates[i].score ) {
			insertIndex = i;
			break;
		}
	}

	if ( insertIndex >= RB_LENSFLARE_MAX_LIGHTS ) {
		return;
	}

	if ( candidateCount < RB_LENSFLARE_MAX_LIGHTS ) {
		candidateCount++;
	}

	for ( int i = candidateCount - 1; i > insertIndex; i-- ) {
		candidates[i] = candidates[i - 1];
	}

	candidates[insertIndex] = candidate;
}

static int RB_CollectLensFlareCandidates( rbLensFlareCandidate_t candidates[RB_LENSFLARE_MAX_LIGHTS], int viewportWidth, int viewportHeight,
		int depthTextureWidth, int depthTextureHeight ) {
	int candidateCount = 0;
	const float screenCenterX = viewportWidth * 0.5f;
	const float screenCenterY = viewportHeight * 0.5f;

	for ( const viewLight_t *vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		if ( vLight->lightDef == NULL || !vLight->viewSeesGlobalLightOrigin || vLight->scissorRect.IsEmpty() ) {
			continue;
		}
		if ( vLight->lightDef->parms.parallel || vLight->lightDef->parms.globalLight ) {
			continue;
		}
		if ( !vLight->localInteractions && !vLight->globalInteractions && !vLight->translucentInteractions ) {
			continue;
		}

		idVec4 lightColor;
		if ( !RB_EvaluateLensFlareLightColor( vLight, lightColor ) ) {
			continue;
		}

		float screenX = 0.0f;
		float screenY = 0.0f;
		float lightDepth = 0.0f;
		if ( !RB_ProjectLensFlarePoint( vLight->globalLightOrigin, viewportWidth, viewportHeight, screenX, screenY, lightDepth ) ) {
			continue;
		}

		float projectedRadius = RB_EstimateLensFlareRadiusPixels( vLight, screenX, screenY, viewportWidth, viewportHeight );
		if ( projectedRadius <= 2.0f ) {
			continue;
		}

		const float brightness = Max( lightColor[0], Max( lightColor[1], lightColor[2] ) );
		const float borderDistanceX = Min( screenX, viewportWidth - screenX );
		const float borderDistanceY = Min( screenY, viewportHeight - screenY );
		const float borderFade = idMath::ClampFloat( 0.25f, 1.0f, Min( borderDistanceX, borderDistanceY ) / 96.0f );

		rbLensFlareCandidate_t candidate;
		memset( &candidate, 0, sizeof( candidate ) );
		candidate.score = brightness * projectedRadius * borderFade;
		candidate.screenX = screenX;
		candidate.screenY = screenY;
		candidate.screenU = idMath::ClampFloat( 0.0f, static_cast<float>( viewportWidth ) / depthTextureWidth, screenX / depthTextureWidth );
		candidate.screenV = idMath::ClampFloat( 0.0f, static_cast<float>( viewportHeight ) / depthTextureHeight, 1.0f - ( screenY / depthTextureHeight ) );
		candidate.lightDepth = lightDepth;
		candidate.sourceRadiusPixels = idMath::ClampFloat( 2.0f, 12.0f, projectedRadius * 0.18f );
		candidate.coronaRadiusPixels = idMath::ClampFloat( 18.0f, 160.0f, projectedRadius * 0.85f + 14.0f );
		candidate.axis.Set( screenCenterX - screenX, screenCenterY - screenY );
		if ( candidate.axis.LengthSqr() <= 0.0001f ) {
			candidate.axis.Set( 1.0f, 0.0f );
		} else {
			candidate.axis.Normalize();
		}
		candidate.color = lightColor;
		candidate.color[0] = Min( candidate.color[0], 4.0f );
		candidate.color[1] = Min( candidate.color[1], 4.0f );
		candidate.color[2] = Min( candidate.color[2], 4.0f );
		candidate.color *= borderFade;
		candidate.color[3] = 1.0f;

		RB_InsertLensFlareCandidate( candidates, candidateCount, candidate );
	}

	return candidateCount;
}

static bool RB_DrawLensFlareQuad( const rbLensFlareCandidate_t &candidate, int viewportWidth, int viewportHeight, int depthTextureWidth,
		int depthTextureHeight, float centerX, float centerY, float halfWidth, float halfHeight, const idVec3 &colorScale,
		float elementKind, const idVec4 &elementParams ) {
	if ( halfWidth <= 0.0f || halfHeight <= 0.0f ) {
		return false;
	}

	const float x1 = centerX - halfWidth;
	const float y1 = centerY - halfHeight;
	const float x2 = centerX + halfWidth;
	const float y2 = centerY + halfHeight;

	if ( x2 < 0.0f || y2 < 0.0f || x1 > viewportWidth || y1 > viewportHeight ) {
		return false;
	}

	const GLfloat lightCenterUv[2] = { candidate.screenU, candidate.screenV };
	const GLfloat lightColor[4] = {
		candidate.color[0] * colorScale.x,
		candidate.color[1] * colorScale.y,
		candidate.color[2] * colorScale.z,
		1.0f
	};
	const GLfloat flareAxis[2] = { candidate.axis.x, candidate.axis.y };

	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_LIGHT_CENTER_UV] >= 0 ) {
		glUniform2fvARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_LIGHT_CENTER_UV], 1, lightCenterUv );
	}
	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_LIGHT_COLOR] >= 0 ) {
		glUniform4fvARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_LIGHT_COLOR], 1, lightColor );
	}
	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_LIGHT_DEPTH] >= 0 ) {
		glUniform1fARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_LIGHT_DEPTH], candidate.lightDepth );
	}
	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_OCCLUSION_RADIUS] >= 0 ) {
		glUniform1fARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_OCCLUSION_RADIUS], candidate.sourceRadiusPixels );
	}
	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_FLARE_AXIS] >= 0 ) {
		glUniform2fvARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_FLARE_AXIS], 1, flareAxis );
	}
	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_ELEMENT_KIND] >= 0 ) {
		glUniform1fARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_ELEMENT_KIND], elementKind );
	}
	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_ELEMENT_PARAMS] >= 0 ) {
		glUniform4fvARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_ELEMENT_PARAMS], 1, elementParams.ToFloatPtr() );
	}

	const float s1 = x1 / depthTextureWidth;
	const float s2 = x2 / depthTextureWidth;
	const float t1 = 1.0f - ( y1 / depthTextureHeight );
	const float t2 = 1.0f - ( y2 / depthTextureHeight );

	glBegin( GL_QUADS );
	glTexCoord2f( s1, t1 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 0.0f, 0.0f );
	glVertex2f( x1, y1 );
	glTexCoord2f( s2, t1 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 1.0f, 0.0f );
	glVertex2f( x2, y1 );
	glTexCoord2f( s2, t2 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 1.0f, 1.0f );
	glVertex2f( x2, y2 );
	glTexCoord2f( s1, t2 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 0.0f, 1.0f );
	glVertex2f( x1, y2 );
	glEnd();

	return true;
}

static void RB_STD_LensFlare( void ) {
	if ( r_skipPostProcess.GetBool() ) {
		return;
	}

	const int lensFlareQuality = r_lensFlare.GetInteger();
	if ( lensFlareQuality <= 0 ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable || !RB_IsMainScenePostProcessView() ) {
		return;
	}

	RB_InitLensFlareStage();
	if ( !R_ValidateGLSLProgram( &rbLensFlareStage ) ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	idImage *depthImage = globalImages->currentDepthImage;
	if ( depthImage == NULL ) {
		return;
	}

	RB_CaptureCurrentDepthImage( viewportWidth, viewportHeight );

	const int depthTextureWidth = depthImage->GetOpts().width;
	const int depthTextureHeight = depthImage->GetOpts().height;
	if ( depthTextureWidth <= 0 || depthTextureHeight <= 0 ) {
		return;
	}

	rbLensFlareCandidate_t candidates[RB_LENSFLARE_MAX_LIGHTS];
	const int candidateCount = RB_CollectLensFlareCandidates( candidates, viewportWidth, viewportHeight, depthTextureWidth, depthTextureHeight );
	if ( candidateCount <= 0 ) {
		return;
	}

	if ( !RB_SetRVSpecialOrthoForView() ) {
		return;
	}

	GL_SelectTexture( 0 );
	depthImage->Bind();
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbLensFlareStage.glslProgramObject );
	if ( rbLensFlareStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbLensFlareStage.shaderTextureLocations[0], 0 );
	}

	const GLfloat invDepthTexSize[2] = {
		1.0f / static_cast<GLfloat>( Max( 1, depthTextureWidth ) ),
		1.0f / static_cast<GLfloat>( Max( 1, depthTextureHeight ) )
	};
	const GLfloat viewportTexScale[2] = {
		static_cast<GLfloat>( viewportWidth ) / static_cast<GLfloat>( Max( 1, depthTextureWidth ) ),
		static_cast<GLfloat>( viewportHeight ) / static_cast<GLfloat>( Max( 1, depthTextureHeight ) )
	};

	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_INV_DEPTH_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_INV_DEPTH_TEX_SIZE], 1, invDepthTexSize );
	}
	if ( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_VIEWPORT_TEX_SCALE] >= 0 ) {
		glUniform2fvARB( rbLensFlareStage.shaderParmLocations[RB_LENSFLARE_UNIFORM_VIEWPORT_TEX_SCALE], 1, viewportTexScale );
	}

	for ( int i = 0; i < candidateCount; i++ ) {
		const rbLensFlareCandidate_t &candidate = candidates[i];
		const float coronaRadius = candidate.coronaRadiusPixels;
		const idVec4 coronaParams( 4.5f, 0.58f, 0.16f, 0.85f );
		const idVec4 haloParams( 2.2f, 0.72f, 0.14f, 0.38f );
		const idVec3 haloScale( 0.85f, 0.85f, 0.85f );

		RB_DrawLensFlareQuad( candidate, viewportWidth, viewportHeight, depthTextureWidth, depthTextureHeight,
			candidate.screenX, candidate.screenY, coronaRadius, coronaRadius, idVec3( 1.0f, 1.0f, 1.0f ), 0.0f, coronaParams );
		RB_DrawLensFlareQuad( candidate, viewportWidth, viewportHeight, depthTextureWidth, depthTextureHeight,
			candidate.screenX, candidate.screenY, coronaRadius * 1.55f, coronaRadius * 1.55f, haloScale, 1.0f, haloParams );

		if ( lensFlareQuality >= 2 ) {
			const idVec2 centerDelta( viewportWidth * 0.5f - candidate.screenX, viewportHeight * 0.5f - candidate.screenY );

			if ( centerDelta.LengthSqr() > 256.0f ) {
				static const float ghostFactors[3] = { 0.35f, 1.15f, 1.8f };
				static const float ghostSizeScales[3] = { 0.60f, 0.42f, 0.78f };
				// Keep flare hue driven by the light itself. Hard-coded chromatic
				// tints here created artificial blue lighting from warm/neutral lights.
				static const float ghostIntensityScales[3] = { 0.95f, 0.90f, 0.82f };
				static const idVec4 ghostParams[3] = {
					idVec4( 3.8f, 0.52f, 0.16f, 0.34f ),
					idVec4( 4.4f, 0.48f, 0.12f, 0.27f ),
					idVec4( 2.6f, 0.60f, 0.18f, 0.32f )
				};

				for ( int ghostIndex = 0; ghostIndex < 3; ghostIndex++ ) {
					const float ghostX = candidate.screenX + centerDelta.x * ghostFactors[ghostIndex];
					const float ghostY = candidate.screenY + centerDelta.y * ghostFactors[ghostIndex];
					const float ghostRadius = coronaRadius * ghostSizeScales[ghostIndex];
					const idVec3 ghostScale(
						ghostIntensityScales[ghostIndex],
						ghostIntensityScales[ghostIndex],
						ghostIntensityScales[ghostIndex] );

					RB_DrawLensFlareQuad( candidate, viewportWidth, viewportHeight, depthTextureWidth, depthTextureHeight,
						ghostX, ghostY, ghostRadius, ghostRadius, ghostScale, 1.0f, ghostParams[ghostIndex] );
				}
			}

			const float streakHalfWidth = coronaRadius * 4.2f;
			const float streakHalfHeight = Max( 4.0f, coronaRadius * 0.14f );
			const idVec4 streakParams( 1.15f, 5.5f, 4.0f, 0.24f );
			const idVec3 streakScale( 0.95f, 0.95f, 0.95f );
			RB_DrawLensFlareQuad( candidate, viewportWidth, viewportHeight, depthTextureWidth, depthTextureHeight,
				candidate.screenX, candidate.screenY, streakHalfWidth, streakHalfHeight, streakScale, 2.0f, streakParams );
		}
	}

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_RVSpecialRestoreDrawingView();
}

static void RB_RVSpecialBeginCapture( idRenderTexture *renderTexture, int width, int height ) {
	RB_BindPostProcessRenderTexture( renderTexture, width, height );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClearDepth( 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

	GL_State( GLS_DEFAULT );
	glDisable( GL_BLEND );
	glDisable( GL_CULL_FACE );
	glEnable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glDepthFunc( GL_LEQUAL );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	backEnd.currentSpace = NULL;
}

static void RB_RVSpecialEndCapture( idRenderTexture *previousRenderTexture ) {
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

	glUseProgramObjectARB( 0 );
	RB_RestorePostProcessTarget( previousRenderTexture, viewportWidth, viewportHeight );

	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );
	for ( int i = 0; i < maxStateUnits; i++ ) {
		GL_SelectTexture( i );
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glDisable( GL_TEXTURE_GEN_Q );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		glDisable( GL_TEXTURE_CUBE_MAP_EXT );
		glDisable( GL_TEXTURE_3D );
		glDisable( GL_TEXTURE_2D );
		backEnd.glState.tmu[i].textureType = TT_DISABLED;
		backEnd.glState.tmu[i].current2DMap = -1;
		backEnd.glState.tmu[i].current3DMap = -1;
		backEnd.glState.tmu[i].currentCubeMap = -1;
		globalImages->whiteImage->Bind();
	}

	GL_SelectTexture( 0 );
	backEnd.glState.forceGlState = true;
}

static bool RB_RVSpecialPrepareSolidStageTexturing( const drawSurf_t *surf, idDrawVert *ac, const shaderStage_t **diffuseStageOut ) {
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;

	if ( diffuseStageOut != NULL ) {
		*diffuseStageOut = NULL;
	}

	if ( !rbRVSpecialCaptureUsesDiffuseImage ) {
		globalImages->whiteImage->Bind();
		return true;
	}

	for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( pStage->lighting != SL_DIFFUSE || regs[ pStage->conditionRegister ] == 0.0f ) {
			continue;
		}

		pStage->texture.image->Bind();
		if ( diffuseStageOut != NULL ) {
			*diffuseStageOut = pStage;
		}
		return RB_PrepareStageTexturing( pStage, surf, ac );
	}

	globalImages->whiteImage->Bind();
	return true;
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
static void RB_T_CaptureRVSpecialDepth( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;
	const idMaterial *shader = surf->material;
	const float *regs;
	const shaderStage_t *pStage = NULL;
	float color[4];

	if ( !shader->IsDrawn() || !tri->numIndexes || shader->Coverage() == MC_TRANSLUCENT ) {
		return;
	}
	if ( !tri->ambientCache ) {
		common->Printf( "RB_T_CaptureRVSpecialDepth: !tri->ambientCache\n" );
		return;
	}

	regs = surf->shaderRegisters;
	for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
		pStage = shader->GetStage( stage );
		if ( regs[ pStage->conditionRegister ] != 0.0f ) {
			break;
		}
	}
	if ( pStage == NULL || regs[ pStage->conditionRegister ] == 0.0f ) {
		return;
	}

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	const bool useAlphaToCoverage = RB_UseAlphaToCoverage( shader );
	if ( useAlphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

	color[0] = 1.0f;
	color[1] = 1.0f;
	color[2] = 1.0f;
	color[3] = 1.0f;

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>( &ac->st ) );

	bool drawSolid = ( shader->Coverage() == MC_OPAQUE );

	if ( shader->Coverage() == MC_PERFORATED ) {
		bool didDraw = false;

		glEnable( GL_ALPHA_TEST );
		for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
			pStage = shader->GetStage( stage );
			if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0.0f ) {
				continue;
			}
			if ( pStage->texture.dynamic == DI_REFLECTION_RENDER || pStage->texture.dynamic == DI_REFRACTION_RENDER ) {
				continue;
			}

			didDraw = true;
			color[3] = regs[ pStage->color.registers[3] ];
			if ( color[3] <= 0.0f ) {
				continue;
			}

			glColor4fv( color );
			glAlphaFunc( GL_GREATER, regs[ pStage->alphaTestRegister ] );
			pStage->texture.image->Bind();
			if ( !RB_PrepareStageTexturing( pStage, surf, ac ) ) {
				RB_FinishStageTexturing( pStage, surf, ac );
				continue;
			}

			RB_DrawElementsWithCounters( tri );
			RB_FinishStageTexturing( pStage, surf, ac );
		}
		glDisable( GL_ALPHA_TEST );

		if ( !didDraw ) {
			drawSolid = true;
		}
	}

	if ( drawSolid ) {
		const shaderStage_t *diffuseStage = NULL;
		glColor4fv( color );
		if ( RB_RVSpecialPrepareSolidStageTexturing( surf, ac, &diffuseStage ) ) {
			RB_DrawElementsWithCounters( tri );
		}
		if ( diffuseStage != NULL ) {
			RB_FinishStageTexturing( diffuseStage, surf, ac );
		}
	}

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( useAlphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
}

static bool RB_CaptureRVSpecialDepth( idRenderTexture *target, int width, int height, bool useDiffuseImage, float distanceScale ) {
	RB_InitRVSpecialStages();
	if ( !R_ValidateGLSLProgram( &rbRVSpecialDepthStage ) ) {
		return false;
	}

	const GLfloat safeDistanceScale = Max( distanceScale, 1.0f );
	idRenderTexture *previousRenderTexture = backEnd.renderTexture;
	rbRVSpecialCaptureUsesDiffuseImage = useDiffuseImage;

	RB_RVSpecialBeginCapture( target, width, height );

	glUseProgramObjectARB( (GLhandleARB)rbRVSpecialDepthStage.glslProgramObject );
	if ( rbRVSpecialDepthStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbRVSpecialDepthStage.shaderTextureLocations[0], 0 );
	}
	if ( rbRVSpecialDepthStage.shaderParmLocations[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE] >= 0 ) {
		glUniform1fARB( rbRVSpecialDepthStage.shaderParmLocations[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE], safeDistanceScale );
	}

	RB_RenderDrawSurfListWithFunctionIgnoreScissor(
		(drawSurf_t **)&backEnd.viewDef->drawSurfs[0],
		backEnd.viewDef->numDrawSurfs,
		RB_T_CaptureRVSpecialDepth );

	RB_RVSpecialEndCapture( previousRenderTexture );
	rbRVSpecialCaptureUsesDiffuseImage = false;
	return true;
}

static bool RB_PrepareRVSpecialBlurImage( void ) {
	if ( !rbRVSpecialBlurPrepared || tr.specialBlurDepthImage == NULL || tr.specialBlurRenderTexture == NULL ) {
		return false;
	}

	RB_InitRVSpecialStages();
	if ( !R_ValidateGLSLProgram( &rbRVSpecialBlurStage ) ) {
		return false;
	}

	idRenderTexture *previousRenderTexture = backEnd.renderTexture;
	const int blurWidth = tr.specialBlurRenderTexture->GetWidth();
	const int blurHeight = tr.specialBlurRenderTexture->GetHeight();
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

	RB_CaptureCurrentRenderImage( viewportWidth, viewportHeight );
	if ( !backEnd.currentRenderCopied || globalImages->currentRenderImage == NULL ) {
		return false;
	}

	RB_BindPostProcessRenderTexture( tr.specialBlurRenderTexture, blurWidth, blurHeight );
	RB_BeginFullscreenPostProcessPass( 0, 0, blurWidth, blurHeight );

	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();
	glUseProgramObjectARB( (GLhandleARB)rbRVSpecialBlurStage.glslProgramObject );
	if ( rbRVSpecialBlurStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbRVSpecialBlurStage.shaderTextureLocations[0], 0 );
	}

	const GLfloat textureScale[2] = { 1.0f, 1.0f };
	const GLfloat sampleDist = 0.00620f;
	if ( rbRVSpecialBlurStage.shaderParmLocations[RB_RVSPECIAL_BLUR_UNIFORM_TEXTURE_SCALE] >= 0 ) {
		glUniform2fvARB( rbRVSpecialBlurStage.shaderParmLocations[RB_RVSPECIAL_BLUR_UNIFORM_TEXTURE_SCALE], 1, textureScale );
	}
	if ( rbRVSpecialBlurStage.shaderParmLocations[RB_RVSPECIAL_BLUR_UNIFORM_SAMPLE_DIST] >= 0 ) {
		glUniform1fARB( rbRVSpecialBlurStage.shaderParmLocations[RB_RVSPECIAL_BLUR_UNIFORM_SAMPLE_DIST], sampleDist );
	}

	RB_DrawFullscreenPostProcessQuadUnitUV();

	glUseProgramObjectARB( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
	RB_RestorePostProcessTarget(
		previousRenderTexture,
		backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
		backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
	backEnd.glState.forceGlState = true;
	return true;
}

static bool RB_CompositeRVSpecialBlur( void ) {
	if ( !rbRVSpecialBlurPrepared || tr.specialBlurDepthImage == NULL || tr.specialBlurImage == NULL ) {
		return false;
	}

	RB_InitRVSpecialStages();
	if ( !R_ValidateGLSLProgram( &rbRVSpecialMedLabsStage ) ) {
		return false;
	}

	const GLfloat effectRange = Max( tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][4], 0.01f );
	const GLfloat focus = idMath::ClampFloat( 0.0f, 1.0f, tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][5] );
	const GLfloat scroll = static_cast<GLfloat>( backEnd.viewDef->renderView.time ) * 0.001f * 0.25f;
	const GLfloat approachPercent = idMath::ClampFloat( 0.0f, 1.0f, tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][6] );
	const GLfloat approachColor[4] = {
		tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][0],
		tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][1],
		tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][2],
		tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][3]
	};

	backEnd.currentScissor = backEnd.viewDef->scissor;
	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	GL_SelectTexture( 0 );
	tr.specialBlurDepthImage->Bind();
	GL_SelectTexture( 1 );
	tr.specialBlurImage->Bind();
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbRVSpecialMedLabsStage.glslProgramObject );
	if ( rbRVSpecialMedLabsStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbRVSpecialMedLabsStage.shaderTextureLocations[0], 0 );
	}
	if ( rbRVSpecialMedLabsStage.shaderTextureLocations[1] >= 0 ) {
		glUniform1iARB( rbRVSpecialMedLabsStage.shaderTextureLocations[1], 1 );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_RANGE] >= 0 ) {
		glUniform1fARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_RANGE], effectRange );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_FOCUS] >= 0 ) {
		glUniform1fARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_FOCUS], focus );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_SCROLL] >= 0 ) {
		glUniform1fARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_SCROLL], scroll );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_COLOR] >= 0 ) {
		glUniform4fvARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_COLOR], 1, approachColor );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_PERCENT] >= 0 ) {
		glUniform1fARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_PERCENT], approachPercent );
	}

	RB_DrawFullscreenPostProcessQuadUnitUV();

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
	backEnd.glState.forceGlState = true;
	return true;
}

static bool RB_SetRVSpecialOrthoForView( void ) {
	if ( backEnd.viewDef == NULL ) {
		return false;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return false;
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

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0, viewportWidth, viewportHeight, 0, -1, 1 );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	GL_Cull( CT_TWO_SIDED );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	return true;
}

static bool RB_DrawRVSpecialALLight( const idVec3 &origin, float size, const idVec3 &color ) {
	idPlane eye;
	idPlane clip;
	idVec3 ndc;
	idVec3 points[4];
	idVec3 eyePoint;
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	const float distanceScale = 2000.0f;

	R_TransformModelToClip( origin, backEnd.viewDef->worldSpace.modelViewMatrix, backEnd.viewDef->projectionMatrix, eye, clip );
	if ( clip[3] <= 0.0f ) {
		return false;
	}

	const float lightDepth = -eye[2];
	if ( lightDepth <= 0.0f ) {
		return false;
	}

	const idVec3 right = backEnd.viewDef->renderView.viewaxis[1] * size;
	const idVec3 up = backEnd.viewDef->renderView.viewaxis[2] * size;
	points[0] = origin + right + up;
	points[1] = origin - right + up;
	points[2] = origin - right - up;
	points[3] = origin + right - up;

	float x1 = idMath::INFINITY;
	float y1 = idMath::INFINITY;
	float x2 = -idMath::INFINITY;
	float y2 = -idMath::INFINITY;

	for ( int i = 0; i < 4; i++ ) {
		R_TransformModelToClip( points[i], backEnd.viewDef->worldSpace.modelViewMatrix, backEnd.viewDef->projectionMatrix, eye, clip );
		if ( clip[3] <= 0.0f ) {
			return false;
		}

		R_TransformClipToDevice( clip, backEnd.viewDef, ndc );
		const float sx = ( ndc.x * 0.5f + 0.5f ) * viewportWidth;
		const float sy = ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * viewportHeight;
		x1 = Min( x1, sx );
		y1 = Min( y1, sy );
		x2 = Max( x2, sx );
		y2 = Max( y2, sy );
	}

	if ( x2 < 0.0f || y2 < 0.0f || x1 > viewportWidth || y1 > viewportHeight ) {
		return false;
	}

	R_LocalPointToGlobal( backEnd.viewDef->worldSpace.modelViewMatrix, origin, eyePoint );

	const GLfloat lightColor[4] = { color.x, color.y, color.z, 1.0f };
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_DISTANCE_SCALE] >= 0 ) {
		glUniform1fARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_DISTANCE_SCALE], distanceScale );
	}
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_LOC] >= 0 ) {
		glUniform3fvARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_LOC], 1, eyePoint.ToFloatPtr() );
	}
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_COLOR] >= 0 ) {
		glUniform4fvARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_COLOR], 1, lightColor );
	}
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_SIZE] >= 0 ) {
		glUniform1fARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_SIZE], size );
	}
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_MIN_DISTANCE] >= 0 ) {
		glUniform1fARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_MIN_DISTANCE], lightDepth );
	}

	const float s1 = x1 / viewportWidth;
	const float s2 = x2 / viewportWidth;
	const float t1 = 1.0f - ( y1 / viewportHeight );
	const float t2 = 1.0f - ( y2 / viewportHeight );

	glBegin( GL_QUADS );
	glTexCoord2f( s1, t1 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 0.0f, 0.0f );
	glVertex2f( x1, y1 );
	glTexCoord2f( s2, t1 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 1.0f, 0.0f );
	glVertex2f( x2, y1 );
	glTexCoord2f( s2, t2 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 1.0f, 1.0f );
	glVertex2f( x2, y2 );
	glTexCoord2f( s1, t2 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 0.0f, 1.0f );
	glVertex2f( x1, y2 );
	glEnd();

	return true;
}

void RB_DrawSpecialEffects( const void *data ) {
	const drawSurfsCommand_t *cmd = (const drawSurfsCommand_t *)data;

	backEnd.viewDef = cmd->viewDef;
	rbRVSpecialBlurPrepared = false;
	rbRVSpecialALPrepared = false;
	rbRVSpecialActiveMask = tr.specialEffectsEnabled;
	if ( r_forceSpecialEffects.GetInteger() > 0 ) {
		rbRVSpecialActiveMask = r_forceSpecialEffects.GetInteger();
	}

	if ( backEnd.viewDef == NULL || backEnd.viewDef->renderWorld == NULL || backEnd.viewDef->numDrawSurfs <= 0 ) {
		return;
	}
	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	if ( ( rbRVSpecialActiveMask & SPECIAL_EFFECT_BLUR ) != 0 && RB_EnsureRVSpecialBlurResources() ) {
		rbRVSpecialBlurPrepared = RB_CaptureRVSpecialDepth(
			tr.specialBlurDepthRenderTexture,
			tr.specialBlurDepthRenderTexture->GetWidth(),
			tr.specialBlurDepthRenderTexture->GetHeight(),
			false,
			Max( tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][7], 1.0f ) );
	}

	if ( ( rbRVSpecialActiveMask & SPECIAL_EFFECT_AL ) != 0 && RB_EnsureRVSpecialALResources() ) {
		rbRVSpecialALPrepared = RB_CaptureRVSpecialDepth(
			tr.specialALDepthRenderTexture,
			tr.specialALDepthRenderTexture->GetWidth(),
			tr.specialALDepthRenderTexture->GetHeight(),
			true,
			2000.0f );
	}
}

static void RB_DisplaySpecialEffects( const viewEntity_t *viewEnts, bool prePass ) {
	if ( backEnd.viewDef == NULL || !glConfig.GLSLProgramAvailable ) {
		return;
	}

	if ( prePass ) {
		// Legacy blur is authored as a fullscreen 2D overlay. The 3D pass only captures
		// its depth mask; the blur image itself is generated from the resolved scene when
		// the later HUD/UI view starts.
		if ( viewEnts == NULL && ( rbRVSpecialActiveMask & SPECIAL_EFFECT_BLUR ) != 0 ) {
			bool restoredView = false;
			if ( RB_PrepareRVSpecialBlurImage() ) {
				restoredView |= RB_CompositeRVSpecialBlur();
			}
			if ( restoredView ) {
				RB_RVSpecialRestoreDrawingView();
			}
		}
		return;
	}

	bool restoredView = false;

	if ( viewEnts != NULL && ( rbRVSpecialActiveMask & SPECIAL_EFFECT_AL ) != 0 && rbRVSpecialALPrepared && tr.primaryWorld != NULL ) {
		RB_InitRVSpecialStages();
		if ( R_ValidateGLSLProgram( &rbRVSpecialALStage ) && RB_SetRVSpecialOrthoForView() ) {
			GL_SelectTexture( 0 );
			tr.specialALDepthImage->Bind();
			GL_SelectTexture( 1 );
			tr.specialALLightImage->Bind();
			GL_SelectTexture( 0 );

			glUseProgramObjectARB( (GLhandleARB)rbRVSpecialALStage.glslProgramObject );
			if ( rbRVSpecialALStage.shaderTextureLocations[0] >= 0 ) {
				glUniform1iARB( rbRVSpecialALStage.shaderTextureLocations[0], 0 );
			}
			if ( rbRVSpecialALStage.shaderTextureLocations[1] >= 0 ) {
				glUniform1iARB( rbRVSpecialALStage.shaderTextureLocations[1], 1 );
			}

			for ( int i = 0; i < tr.primaryWorld->lightDefs.Num(); i++ ) {
				idRenderLightLocal *light = tr.primaryWorld->lightDefs[i];
				if ( light == NULL ) {
					continue;
				}

				idVec3 lightColor( light->parms.shaderParms[0], light->parms.shaderParms[1], light->parms.shaderParms[2] );
				if ( lightColor.LengthSqr() <= idMath::FLOAT_EPSILON ) {
					continue;
				}
				lightColor.Normalize();

				RB_DrawRVSpecialALLight( light->globalLightOrigin, 300.0f, lightColor );
			}

			glUseProgramObjectARB( 0 );
			GL_SelectTexture( 1 );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
			globalImages->BindNull();
			restoredView = true;
		}
	}

	if ( restoredView ) {
		RB_RVSpecialRestoreDrawingView();
	}
}

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

		// Fallback for materials that reference captured scene buffers but were not sorted as
		// post-process. Offscreen render-texture passes manage their own captures and must not
		// overwrite them here after clearing the destination render target.
		if ( !backEnd.currentRenderCopied && RB_AutomaticCurrentRenderCaptureAllowed() && RB_StageUsesCurrentRender( pStage ) ) {
			RB_CaptureCurrentRenderImage(
				backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
		}
		if ( !backEnd.currentDepthCopied && RB_AutomaticCurrentRenderCaptureAllowed() && RB_StageUsesCurrentDepth( pStage ) ) {
			RB_CaptureCurrentDepthImage(
				backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
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
					if ( location < 0 ) {
						continue;
					}

					if ( RB_BindStockGLSLShaderParm( newStage->shaderParmBindings[i], location ) ) {
						continue;
					}

					const int numRegisters = newStage->shaderParmNumRegisters[i];
					if ( numRegisters <= 0 ) {
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

		// Dynamic reflection/refraction stages exist only to refresh offscreen render targets.
		// The captured images are sampled by later stages via _reflectionRender/_refractionRender.
		if ( pStage->texture.dynamic == DI_REFLECTION_RENDER
			|| pStage->texture.dynamic == DI_REFRACTION_RENDER ) {
			continue;
		}

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

		bool needsCurrentDepth = false;
		for ( int surfIndex = 0; surfIndex < numDrawSurfs; surfIndex++ ) {
			if ( RB_MaterialUsesCurrentDepth( drawSurfs[surfIndex]->material ) ) {
				needsCurrentDepth = true;
				break;
			}
		}

		// Copy the current view for any post-process material sampling _currentRender.
		// Do not gate this on viewEntitys: world-only views may still contain post-process surfaces.
		// Offscreen render-texture passes capture _currentRender explicitly and must keep that copy.
		if ( RB_AutomaticCurrentRenderCaptureAllowed() ) {
			RB_CaptureCurrentRenderImage(
				backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
			if ( needsCurrentDepth ) {
				RB_CaptureCurrentDepthImage(
					backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
					backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
			}
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

static void RB_LightGridModelMatrixRows( const float modelMatrix[16], float row0[4], float row1[4], float row2[4] ) {
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

static void RB_LightGridVertexColorParams( const stageVertexColor_t vertexColor, float params[2] ) {
	params[0] = 0.0f;
	params[1] = 1.0f;

	if ( vertexColor == SVC_MODULATE ) {
		params[0] = 1.0f;
		params[1] = 0.0f;
	} else if ( vertexColor == SVC_INVERSE_MODULATE ) {
		params[0] = -1.0f;
		params[1] = 1.0f;
	}
}

static bool RB_SurfaceHasLightGrid( const drawSurf_t *surf, const LightGrid *&lightGrid ) {
	lightGrid = NULL;

	if ( surf == NULL || surf->area == NULL ) {
		return false;
	}
	if ( surf->material == NULL || surf->space == NULL || surf->geo == NULL ) {
		return false;
	}
	if ( !surf->material->ReceivesLighting() || surf->material->IsPortalSky() ) {
		return false;
	}
	if ( surf->material->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	// Keep the indirect light-grid pass on stable world/entity receivers only.
	// Shot-created decals and depth-hacked weapon/effect surfaces use different
	// color/depth paths and can leave this pass binding invalid state after fire.
	if ( surf->decalColorCache != NULL ) {
		return false;
	}
	if ( surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		return false;
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		return false;
	}

	const LightGrid &candidate = surf->area->lightGrid;
	if ( candidate.GridPointCount() <= 0 || !candidate.HasImage() ) {
		return false;
	}
	if ( candidate.lightGridBounds[0] <= 0 || candidate.lightGridBounds[1] <= 0 || candidate.lightGridBounds[2] <= 0 ) {
		return false;
	}

	lightGrid = &candidate;
	return true;
}

static void RB_UpdateLightGridImageResidency( idRenderWorldLocal *world ) {
	if ( world == NULL || world->portalAreas == NULL ) {
		return;
	}

	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		portalArea_t &area = world->portalAreas[ areaIndex ];
		LightGrid &lightGrid = area.lightGrid;
		idImage *irradianceImage = lightGrid.irradianceImage;
		if ( irradianceImage == NULL || !irradianceImage->IsLoaded() ) {
			continue;
		}

		if ( area.viewCount == tr.viewCount ) {
			continue;
		}

		irradianceImage->PurgeImage();
	}
}

static void RB_STD_DrawLightGridSurface( const drawSurf_t *surf, const LightGrid &lightGrid ) {
	const srfTriangles_t *tri = surf->geo;
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	if ( tri == NULL || shader == NULL || regs == NULL ) {
		return;
	}
	if ( tri->numIndexes <= 0 || tri->ambientCache == NULL ) {
		return;
	}

	idImage *irradianceImage = lightGrid.irradianceImage;
	if ( irradianceImage == NULL ) {
		return;
	}

	if ( !irradianceImage->IsLoaded() ) {
		irradianceImage->ActuallyLoadImage( true );
	}
	if ( irradianceImage->IsDefaulted() ) {
		return;
	}

	const int atlasWidth = irradianceImage->GetOpts().width;
	const int atlasHeight = irradianceImage->GetOpts().height;
	if ( atlasWidth <= 0 || atlasHeight <= 0 ) {
		return;
	}

	const shaderStage_t *bumpStage = shader->GetBumpStage();
	idImage *bumpImage = globalImages->flatNormalMap;
	idVec4 bumpMatrix[2];
	bumpMatrix[0].Set( 1.0f, 0.0f, 0.0f, 0.0f );
	bumpMatrix[1].Set( 0.0f, 1.0f, 0.0f, 0.0f );
	if ( bumpStage != NULL && regs[ bumpStage->conditionRegister ] != 0 && !r_skipBump.GetBool() ) {
		R_SetDrawInteraction( bumpStage, regs, &bumpImage, bumpMatrix, NULL );
		if ( bumpImage == NULL ) {
			bumpImage = globalImages->flatNormalMap;
		}
	}

	float row0[4];
	float row1[4];
	float row2[4];
	RB_LightGridModelMatrixRows( surf->space->modelMatrix, row0, row1, row2 );

	const float lightGridOrigin[4] = {
		lightGrid.lightGridOrigin[0], lightGrid.lightGridOrigin[1], lightGrid.lightGridOrigin[2], 0.0f
	};
	const float lightGridSize[4] = {
		lightGrid.lightGridSize[0], lightGrid.lightGridSize[1], lightGrid.lightGridSize[2], 0.0f
	};
	const float lightGridBounds[4] = {
		static_cast<float>( lightGrid.lightGridBounds[0] ),
		static_cast<float>( lightGrid.lightGridBounds[1] ),
		static_cast<float>( lightGrid.lightGridBounds[2] ),
		0.0f
	};
	const float atlasInfo[4] = {
		1.0f / static_cast<float>( atlasWidth ),
		1.0f / static_cast<float>( atlasHeight ),
		static_cast<float>( lightGrid.imageBorderSize ),
		static_cast<float>( Max( lightGrid.imageSingleProbeSize - lightGrid.imageBorderSize, 1 ) ) / static_cast<float>( Max( lightGrid.imageSingleProbeSize, 1 ) )
	};

	const bool useAlphaToCoverage = RB_UseAlphaToCoverage( shader );
	if ( useAlphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

	GL_Cull( shader->GetCullType() );
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
	}
	if ( surf->space->modelDepthHack != 0.0f ) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
	glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
	glEnableClientState( GL_NORMAL_ARRAY );
	glEnableClientState( GL_COLOR_ARRAY );

	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>( &ac->st ) );
	GL_SelectTexture( 1 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
	GL_SelectTexture( 2 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
	GL_SelectTexture( 0 );

	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_S], 1, bumpMatrix[0].ToFloatPtr() );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_T], 1, bumpMatrix[1].ToFloatPtr() );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW0], 1, row0 );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW1], 1, row1 );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW2], 1, row2 );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_LIGHTGRID_ORIGIN], 1, lightGridOrigin );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_LIGHTGRID_SIZE], 1, lightGridSize );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_LIGHTGRID_BOUNDS], 1, lightGridBounds );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_ATLAS_INFO], 1, atlasInfo );

	GL_SelectTextureNoClient( 0 );
	bumpImage->Bind();
	GL_SelectTextureNoClient( 2 );
	irradianceImage->SetSamplerState( TF_LINEAR, TR_CLAMP );
	irradianceImage->Bind();

	for ( int stageIndex = 0; stageIndex < shader->GetNumStages(); stageIndex++ ) {
		const shaderStage_t *diffuseStage = shader->GetStage( stageIndex );
		if ( diffuseStage->lighting != SL_DIFFUSE || regs[ diffuseStage->conditionRegister ] == 0 ) {
			continue;
		}

		idImage *diffuseImage = globalImages->whiteImage;
		idVec4 diffuseMatrix[2];
		float diffuseColor[4];
		R_SetDrawInteraction( diffuseStage, regs, &diffuseImage, diffuseMatrix, diffuseColor );
		if ( diffuseImage == NULL ) {
			diffuseImage = globalImages->whiteImage;
		}

		float vertexColorParams[2];
		RB_LightGridVertexColorParams( diffuseStage->vertexColor, vertexColorParams );

		RB_SetStageVertexColorPointer( surf, stageIndex, ac );
		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_S], 1, diffuseMatrix[0].ToFloatPtr() );
		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_T], 1, diffuseMatrix[1].ToFloatPtr() );
		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_COLOR], 1, diffuseColor );
		glUniform2fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_VERTEX_COLOR_PARAMS], 1, vertexColorParams );

		GL_SelectTextureNoClient( 1 );
		diffuseImage->Bind();

		RB_DrawElementsWithCounters( tri );
	}

	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 0 );
	globalImages->BindNull();

	GL_SelectTexture( 2 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 1 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );

	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		RB_LeaveDepthHack();
	}
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( useAlphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
}

static void RB_STD_LightGridIndirect( void ) {
	if ( !r_useLightGrid.GetBool() || r_skipDiffuse.GetBool() ) {
		return;
	}
	if ( !glConfig.GLSLProgramAvailable || backEnd.viewDef == NULL || !backEnd.viewDef->viewEntitys ) {
		return;
	}

	RB_InitLightGridIndirectStage();
	if ( !R_ValidateGLSLProgram( &rbLightGridIndirectStage ) ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_LightGridIndirect ----------\n" );

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	glEnable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glUseProgramObjectARB( (GLhandleARB)rbLightGridIndirectStage.glslProgramObject );

	for ( int i = 0; i < rbLightGridIndirectStage.numShaderTextures; i++ ) {
		if ( rbLightGridIndirectStage.shaderTextureLocations[i] >= 0 ) {
			glUniform1iARB( rbLightGridIndirectStage.shaderTextureLocations[i], i );
		}
	}

	RB_UpdateLightGridImageResidency( backEnd.viewDef->renderWorld );

	backEnd.currentSpace = NULL;
	for ( int i = 0; i < backEnd.viewDef->numDrawSurfs; i++ ) {
		drawSurf_t *surf = backEnd.viewDef->drawSurfs[i];
		if ( surf == NULL || surf->material == NULL ) {
			continue;
		}
		if ( surf->material->GetSort() >= SS_POST_PROCESS || surf->material->SuppressInSubview() ) {
			continue;
		}

		const LightGrid *lightGrid = NULL;
		if ( !RB_SurfaceHasLightGrid( surf, lightGrid ) ) {
			continue;
		}

		RB_SimpleSurfaceSetup( surf );
		RB_STD_DrawLightGridSurface( surf, *lightGrid );
	}

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 0 );
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

	if ( RB_SceneRenderTargetRequested() && RB_EnsureSceneRenderTexture() ) {
		backEnd.renderTexture = rbSceneRenderTexture;
	}

	// If we have a backend rendertexture, assign it here.
	if (backEnd.renderTexture)
	{
		backEnd.renderTexture->MakeCurrent();
	}

	RB_DisplaySpecialEffects( backEnd.viewDef->viewEntitys, true );

	// clear the z buffer, set the projection matrix, etc
	RB_BeginDrawingView();

	// decide how much overbrighting we are going to do
	RB_DetermineLightScale();

	// fill the depth buffer and clear color buffer to black except on
	// subviews
	RB_STD_FillDepthBuffer( drawSurfs, numDrawSurfs );
	RB_DisplaySpecialEffects( backEnd.viewDef->viewEntitys, false );

	// main light renderer
	RB_ARB2_DrawInteractions();

	// disable stencil shadow test
	glStencilFunc( GL_ALWAYS, 128, 255 );

	// add precomputed indirect diffuse from irradiance-volume atlases
	RB_STD_LightGridIndirect();

	// uplight the entire screen to crutch up not having better blending range
	RB_STD_LightScale();

	if ( r_portalsDistanceCull.GetBool() && backEnd.viewDef->viewEntitys && backEnd.viewDef->renderWorld != NULL ) {
		backEnd.viewDef->renderWorld->RenderPortalFades();
	}

	// now draw any non-light dependent shading passes
	int	processed = RB_STD_DrawShaderPasses( drawSurfs, numDrawSurfs );

	// Apply a configurable brightness floor after ambient/material passes.
	RB_STD_ForceAmbient();

	// fob and blend lights
	RB_STD_FogAllLights();

	// Apply SSAO before bloom and tonemapping so indirect shadowing modulates the lit scene.
	RB_STD_SSAO();

	// Draw depth-aware coronas and optional lens ghosts before bloom so they participate in the post stack.
	RB_STD_LensFlare();

	// Apply scene bloom before authored post-process overlays that sample _currentRender.
	RB_STD_Bloom();

	// now draw any post-processing effects using _currentRender
	if ( processed < numDrawSurfs ) {
		RB_STD_DrawShaderPasses( drawSurfs+processed, numDrawSurfs-processed );
	}

	RB_RenderDebugTools( drawSurfs, numDrawSurfs );

	if ( RB_IsSceneRenderTexture( backEnd.renderTexture ) ) {
		RB_PresentSceneRenderTargetToBackBuffer();
	}

// jmarshall - stupid OpenGL
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);
// jmarshall end
}
