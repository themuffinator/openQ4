// Copyright (C) 2026 DarkMatter Productions
//
// Shadowed volumetric lighting (REN-09), OpenGL backend.
//
// RB_ARB2_DrawInteractions hands every interaction light to
// RB_Volumetric_GL_InjectLight right after its last opaque receiver pass,
// while a shadow-mapped light's GLOBAL map and state are still current. The
// light's stages march the view through that map into the scattering target;
// RB_Volumetric_GL_Composite lays the target over the opaque scene before fog
// and translucent surfaces. See VolumetricLighting.h for the policy.

#include "tr_local.h"
#include "RendererCaps.h"
#include "VolumetricLighting.h"

#include <cstring>

enum {
	RB_VOL_PROGRAM_UNSHADOWED,
	RB_VOL_PROGRAM_PROJECTED,
	RB_VOL_PROGRAM_POINT,
	RB_VOL_PROGRAM_COMPOSITE,
	RB_VOL_PROGRAM_COUNT
};

enum {
	RB_VOL_U_DEPTH,
	RB_VOL_U_PROJECTION,
	RB_VOL_U_FALLOFF,
	RB_VOL_U_SHADOW_MAP,
	RB_VOL_U_POINT_SHADOW_MAP,
	RB_VOL_U_SCATTER,
	RB_VOL_U_CLIP_TO_WORLD,
	RB_VOL_U_EYE,
	RB_VOL_U_VIEW_FORWARD,
	RB_VOL_U_TARGET_INFO,
	RB_VOL_U_SCENE_INFO,
	RB_VOL_U_PARAMS0,
	RB_VOL_U_PARAMS1,
	RB_VOL_U_LIGHT_ORIGIN,
	RB_VOL_U_PROJECT_S,
	RB_VOL_U_PROJECT_T,
	RB_VOL_U_PROJECT_Q,
	RB_VOL_U_FALLOFF_S,
	RB_VOL_U_LIGHT_COLOR,
	RB_VOL_U_BOUNDS_MIN,
	RB_VOL_U_BOUNDS_MAX,
	RB_VOL_U_SHADOW_ROW0,
	RB_VOL_U_SHADOW_ROW1,
	RB_VOL_U_SHADOW_ROW2,
	RB_VOL_U_SHADOW_ROW3,
	RB_VOL_U_SHADOW_RECT,
	RB_VOL_U_SHADOW_SPLIT,
	RB_VOL_U_SHADOW_BIAS,
	RB_VOL_U_SHADOW_COUNT,
	RB_VOL_U_POINT_SHADOW,
	RB_VOL_UNIFORM_COUNT
};

static const char *const rbVolumetricUniformNames[RB_VOL_UNIFORM_COUNT] = {
	"uDepth", "uProjection", "uFalloff", "uShadowMap", "uPointShadowMap", "uScatter",
	"uClipToWorld", "uEye", "uViewForward", "uTargetInfo", "uSceneInfo", "uParams0", "uParams1",
	"uLightOrigin", "uProjS", "uProjT", "uProjQ", "uFalloffS", "uLightColor", "uBoundsMin", "uBoundsMax",
	"uShadowRow0", "uShadowRow1", "uShadowRow2", "uShadowRow3", "uShadowAtlasRect", "uShadowSplit",
	"uShadowBias", "uShadowCascadeCount", "uPointShadow"
};

typedef struct rbVolumetricProgram_s {
	GLhandleARB	program;
	GLhandleARB	vertexShader;
	GLhandleARB	fragmentShader;
	int		generation;
	bool		failed;
	GLint		uniforms[RB_VOL_UNIFORM_COUNT];
} rbVolumetricProgram_t;

static rbVolumetricProgram_t	rbVolumetricPrograms[RB_VOL_PROGRAM_COUNT];
// One depth copy and scattering target per view size (VolumetricLighting.h).
static volumetricTargetPool_t	rbVolumetricPool;
static idImage *		rbVolumetricDepthImages[VOLUMETRIC_TARGET_SLOTS];
static idImage *		rbVolumetricScatterImages[VOLUMETRIC_TARGET_SLOTS];
static idRenderTexture *	rbVolumetricScatterTargets[VOLUMETRIC_TARGET_SLOTS];

// The view being drawn.
typedef struct rbVolumetricView_s {
	const viewDef_t *	view;
	volumetricViewSettings_t settings;
	volumetricViewStats_t	stats;
	bool			began;
	bool			depthCopied;
	bool			targetCleared;
	int			slot;			// this view's pair in the target pool
	int			sceneX;
	int			sceneY;
	int			sceneWidth;
	int			sceneHeight;
	int			targetWidth;
	int			targetHeight;
	int			budgetUsed;
	float			clipToWorld[16];
	float			eye[4];
	float			viewForward[4];
} rbVolumetricView_t;

static rbVolumetricView_t	rbVolumetricView;

static const char *rbVolumetricVertexSource =
	"#version 130\n"
	"void main() {\n"
	"	gl_Position = ftransform();\n"
	"}\n";

// The march, one light stage per draw. Every step evaluates the stage's
// projection and falloff images and, by variant, the light's shadow map.
static const char *rbVolumetricInjectSource =
	"uniform sampler2D uDepth;\n"
	"uniform sampler2D uProjection;\n"
	"uniform sampler2D uFalloff;\n"
	"#if defined( VOL_SHADOW_PROJECTED )\n"
	"uniform sampler2D uShadowMap;\n"
	"uniform vec4 uShadowRow0[4];\n"
	"uniform vec4 uShadowRow1[4];\n"
	"uniform vec4 uShadowRow2[4];\n"
	"uniform vec4 uShadowRow3[4];\n"
	"uniform vec4 uShadowAtlasRect[4];\n"
	"uniform float uShadowSplit[4];\n"
	"uniform float uShadowBias[4];\n"
	"uniform float uShadowCascadeCount;\n"
	"#elif defined( VOL_SHADOW_POINT )\n"
	"uniform samplerCube uPointShadowMap;\n"
	"uniform vec4 uPointShadow;\n"	// far, bias, depth mode (1 raw red, 0 packed rg)
	"#endif\n"
	"uniform mat4 uClipToWorld;\n"
	"uniform vec4 uEye;\n"
	"uniform vec4 uViewForward;\n"
	"uniform vec4 uTargetInfo;\n"	// xy target pixel to scene uv
	"uniform vec4 uParams0;\n"	// extinction, scattering, anisotropy, max distance
	"uniform vec4 uParams1;\n"	// steps, noise offset, debug, divisor
	"uniform vec4 uLightOrigin;\n"
	"uniform vec4 uProjS;\n"
	"uniform vec4 uProjT;\n"
	"uniform vec4 uProjQ;\n"
	"uniform vec4 uFalloffS;\n"
	"uniform vec4 uLightColor;\n"
	"uniform vec4 uBoundsMin;\n"
	"uniform vec4 uBoundsMax;\n"
	"float VolumeNoise( vec2 p ) {\n"
	"	return fract( 52.9829189 * fract( dot( p, vec2( 0.06711056, 0.00583715 ) ) ) );\n"
	"}\n"
	"float ShadowVisibility( vec3 p, float viewDepth ) {\n"
	"#if defined( VOL_SHADOW_PROJECTED )\n"
	"	int count = int( uShadowCascadeCount + 0.5 );\n"
	"	int first = 0;\n"
	"	for ( int s = 0; s < 3; s++ ) {\n"
	"		if ( s + 1 < count && viewDepth >= uShadowSplit[s] ) first = s + 1;\n"
	"	}\n"
	"	vec4 p4 = vec4( p, 1.0 );\n"
	// The cascade of this depth first, then the coarser ones whose crop
	// still holds the point (the whole-light map is the last).
	"	for ( int c = 0; c < 4; c++ ) {\n"
	"		if ( c < first ) continue;\n"
	"		if ( c >= count ) break;\n"
	"		vec4 coord = vec4( dot( p4, uShadowRow0[c] ), dot( p4, uShadowRow1[c] ), dot( p4, uShadowRow2[c] ), dot( p4, uShadowRow3[c] ) );\n"
	"		if ( coord.w <= 1.0e-6 ) continue;\n"
	"		vec2 localUv = coord.xy / coord.w * 0.5 + 0.5;\n"
	"		float depth = coord.z;\n"
	"		if ( localUv.x <= 0.0 || localUv.y <= 0.0 || localUv.x >= 1.0 || localUv.y >= 1.0 || depth <= 0.0 || depth >= 1.0 ) continue;\n"
	"		vec2 atlasUv = mix( uShadowAtlasRect[c].xy, uShadowAtlasRect[c].zw, localUv );\n"
	"		float stored = textureLod( uShadowMap, atlasUv, 0.0 ).r;\n"
	"		return depth - uShadowBias[c] <= stored ? 1.0 : 0.0;\n"
	"	}\n"
	"	return 1.0;\n"
	"#elif defined( VOL_SHADOW_POINT )\n"
	"	vec3 v = p - uLightOrigin.xyz;\n"
	"	float depth = length( v ) / uPointShadow.x;\n"
	"	if ( depth <= 0.0 || depth >= 1.0 ) return 1.0;\n"
	"	vec4 encoded = textureLod( uPointShadowMap, v, 0.0 );\n"
	"	float stored = uPointShadow.z > 0.5 ? encoded.r : encoded.r + encoded.g * ( 1.0 / 255.0 );\n"
	"	return depth - uPointShadow.y <= stored ? 1.0 : 0.0;\n"
	"#else\n"
	"	return 1.0;\n"
	"#endif\n"
	"}\n"
	"void main() {\n"
	"	vec2 sceneUV = clamp( gl_FragCoord.xy * uTargetInfo.xy, vec2( 0.0 ), vec2( 1.0 ) );\n"
	"	float sceneDepth = textureLod( uDepth, sceneUV, 0.0 ).r;\n"
	"	vec4 world = uClipToWorld * vec4( sceneUV * 2.0 - 1.0, sceneDepth * 2.0 - 1.0, 1.0 );\n"
	"	vec3 ray = world.xyz / world.w - uEye.xyz;\n"
	"	float sceneDistance = length( ray );\n"
	"	vec3 dir = ray / max( sceneDistance, 1.0e-4 );\n"
	"	if ( sceneDepth >= 0.99999 ) sceneDistance = uParams0.w;\n"
	"	float tEnd = min( sceneDistance, uParams0.w );\n"
	"	vec3 safeDir = dir + vec3( lessThan( abs( dir ), vec3( 1.0e-6 ) ) ) * 1.0e-6;\n"
	"	vec3 inv = 1.0 / safeDir;\n"
	"	vec3 t0 = ( uBoundsMin.xyz - uEye.xyz ) * inv;\n"
	"	vec3 t1 = ( uBoundsMax.xyz - uEye.xyz ) * inv;\n"
	"	vec3 tn = min( t0, t1 );\n"
	"	vec3 tf = max( t0, t1 );\n"
	"	float tNear = max( max( tn.x, tn.y ), max( tn.z, 0.0 ) );\n"
	"	float tFar = min( min( tf.x, tf.y ), min( tf.z, tEnd ) );\n"
	"	if ( tFar <= tNear ) discard;\n"
	"	float steps = uParams1.x;\n"
	"	float dt = ( tFar - tNear ) / steps;\n"
	"	float jitter = fract( VolumeNoise( gl_FragCoord.xy ) + uParams1.y );\n"
	"	float g = uParams0.z;\n"
	"	float g2 = g * g;\n"
	"	float forward = dot( dir, uViewForward.xyz );\n"
	"	vec3 sum = vec3( 0.0 );\n"
	"	for ( int i = 0; i < 16; i++ ) {\n"
	"		if ( float( i ) >= steps ) break;\n"
	"		float t = tNear + ( float( i ) + jitter ) * dt;\n"
	"		vec3 p = uEye.xyz + dir * t;\n"
	"		vec4 p4 = vec4( p, 1.0 );\n"
	"		float q = dot( p4, uProjQ );\n"
	"		if ( q <= 0.0 ) continue;\n"
	"		vec2 projUv = vec2( dot( p4, uProjS ), dot( p4, uProjT ) ) / q;\n"
	"		float fall = dot( p4, uFalloffS );\n"
	"		if ( projUv.x < 0.0 || projUv.y < 0.0 || projUv.x > 1.0 || projUv.y > 1.0 || fall < 0.0 || fall > 1.0 ) continue;\n"
	"		vec3 light = textureLod( uProjection, projUv, 0.0 ).rgb * textureLod( uFalloff, vec2( fall, 0.5 ), 0.0 ).rgb;\n"
	"		if ( light.r + light.g + light.b <= 0.0001 ) continue;\n"
	"		float visibility = ShadowVisibility( p, t * forward );\n"
	"		if ( visibility <= 0.0 ) continue;\n"
	"		float c = dot( normalize( uLightOrigin.xyz - p ), dir );\n"
	"		float phase = ( 1.0 - g2 ) / pow( max( 1.0 + g2 - 2.0 * g * c, 1.0e-4 ), 1.5 );\n"
	"		sum += light * ( visibility * phase * exp( -uParams0.x * t ) );\n"
	"	}\n"
	"	gl_FragColor = vec4( sum * ( dt * uParams0.y ) * uLightColor.rgb, 0.0 );\n"
	"}\n";

// The composite: a depth-aware 3x3 filter of the scattering target, which
// upsamples a half-resolution target and takes the march dither out, over
// the scene attenuated by the medium (blend ONE, SRC_ALPHA).
static const char *rbVolumetricCompositeSource =
	"uniform sampler2D uDepth;\n"
	"uniform sampler2D uScatter;\n"
	"uniform mat4 uClipToWorld;\n"
	"uniform vec4 uEye;\n"
	"uniform vec4 uSceneInfo;\n"	// scene width/height, target width/height
	"uniform vec4 uTargetInfo;\n"	// xy scene viewport origin in the bound target
	"uniform vec4 uParams0;\n"
	"uniform vec4 uParams1;\n"
	"float ViewDistance( vec2 uv ) {\n"
	"	float d = textureLod( uDepth, uv, 0.0 ).r;\n"
	"	if ( d >= 0.99999 ) return uParams0.w;\n"
	"	vec4 world = uClipToWorld * vec4( uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0 );\n"
	"	return length( world.xyz / world.w - uEye.xyz );\n"
	"}\n"
	"void main() {\n"
	"	vec2 pixel = gl_FragCoord.xy - uTargetInfo.xy;\n"
	"	vec2 sceneUV = clamp( pixel / uSceneInfo.xy, vec2( 0.0 ), vec2( 1.0 ) );\n"
	"	float center = ViewDistance( sceneUV );\n"
	"	float divisor = uParams1.w;\n"
	"	vec2 targetPos = pixel / divisor;\n"
	"	vec2 base = floor( targetPos );\n"
	"	vec3 sum = vec3( 0.0 );\n"
	"	float weightSum = 0.0;\n"
	"	for ( int y = -1; y <= 1; y++ ) {\n"
	"		for ( int x = -1; x <= 1; x++ ) {\n"
	"			vec2 texel = clamp( base + vec2( float( x ), float( y ) ), vec2( 0.0 ), uSceneInfo.zw - 1.0 );\n"
	"			vec3 scatter = textureLod( uScatter, ( texel + 0.5 ) / uSceneInfo.zw, 0.0 ).rgb;\n"
	"			float distance = ViewDistance( clamp( ( texel + 0.5 ) * divisor / uSceneInfo.xy, vec2( 0.0 ), vec2( 1.0 ) ) );\n"
	"			vec2 offset = texel + 0.5 - targetPos;\n"
	"			float spatial = exp( -0.5 * dot( offset, offset ) );\n"
	"			float range = exp( -abs( distance - center ) / ( 0.03 * center + 2.0 ) );\n"
	"			float weight = spatial * range + 1.0e-4 * spatial;\n"
	"			sum += scatter * weight;\n"
	"			weightSum += weight;\n"
	"		}\n"
	"	}\n"
	"	vec3 scatter = sum / max( weightSum, 1.0e-5 );\n"
	"	float transmittance = exp( -uParams0.x * min( center, uParams0.w ) );\n"
	"	gl_FragColor = vec4( scatter, uParams1.z > 0.5 ? 0.0 : transmittance );\n"
	"}\n";

static void RB_Volumetric_PrintInfoLog( GLhandleARB object, const char *label, const char *name ) {
	GLint length = 0;
	glGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &length );
	idStr log;
	if ( length > 1 ) {
		char *buffer = (char *)Mem_Alloc( length + 1 );
		glGetInfoLogARB( object, length, NULL, buffer );
		buffer[length] = '\0';
		log = buffer;
		Mem_Free( buffer );
	}
	common->Warning( "GLSL %s failed for %s: %s", label, name, log.c_str() );
}

static void RB_Volumetric_FreeProgram( rbVolumetricProgram_t &program ) {
	if ( program.program != 0 ) {
		if ( program.vertexShader != 0 ) {
			glDetachObjectARB( program.program, program.vertexShader );
		}
		if ( program.fragmentShader != 0 ) {
			glDetachObjectARB( program.program, program.fragmentShader );
		}
		glDeleteObjectARB( program.program );
	}
	if ( program.vertexShader != 0 ) {
		glDeleteObjectARB( program.vertexShader );
	}
	if ( program.fragmentShader != 0 ) {
		glDeleteObjectARB( program.fragmentShader );
	}
	memset( &program, 0, sizeof( program ) );
}

static const rbVolumetricProgram_t *RB_Volumetric_Program( int index ) {
	rbVolumetricProgram_t &program = rbVolumetricPrograms[index];
	if ( program.generation != tr.glContextGeneration ) {
		// A new context owns none of the old objects.
		memset( &program, 0, sizeof( program ) );
		program.generation = tr.glContextGeneration;
	}
	if ( program.program != 0 ) {
		return &program;
	}
	if ( program.failed ) {
		return NULL;
	}
	static const char *const names[RB_VOL_PROGRAM_COUNT] = {
		"builtin/volumetric_inject", "builtin/volumetric_inject_projected",
		"builtin/volumetric_inject_point", "builtin/volumetric_composite"
	};
	static const char *const defines[RB_VOL_PROGRAM_COUNT] = {
		"#version 130\n",
		"#version 130\n#define VOL_SHADOW_PROJECTED 1\n",
		"#version 130\n#define VOL_SHADOW_POINT 1\n",
		"#version 130\n"
	};
	const char *body = index == RB_VOL_PROGRAM_COMPOSITE ? rbVolumetricCompositeSource : rbVolumetricInjectSource;
	program.failed = true;
	program.vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	program.fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	if ( program.vertexShader == 0 || program.fragmentShader == 0 ) {
		RB_Volumetric_FreeProgram( program );
		program.generation = tr.glContextGeneration;
		program.failed = true;
		return NULL;
	}
	const GLcharARB *vertexSource = (const GLcharARB *)rbVolumetricVertexSource;
	const GLcharARB *fragmentSources[2] = { (const GLcharARB *)defines[index], (const GLcharARB *)body };
	glShaderSourceARB( program.vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( program.fragmentShader, 2, fragmentSources, NULL );
	glCompileShaderARB( program.vertexShader );
	glCompileShaderARB( program.fragmentShader );
	GLint status = GL_FALSE;
	glGetObjectParameterivARB( program.vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_Volumetric_PrintInfoLog( program.vertexShader, "vertex shader compile", names[index] );
	} else {
		glGetObjectParameterivARB( program.fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
		if ( status == GL_FALSE ) {
			RB_Volumetric_PrintInfoLog( program.fragmentShader, "fragment shader compile", names[index] );
		}
	}
	if ( status != GL_FALSE ) {
		program.program = glCreateProgramObjectARB();
		glAttachObjectARB( program.program, program.vertexShader );
		glAttachObjectARB( program.program, program.fragmentShader );
		glLinkProgramARB( program.program );
		glGetObjectParameterivARB( program.program, GL_OBJECT_LINK_STATUS_ARB, &status );
		if ( status == GL_FALSE ) {
			RB_Volumetric_PrintInfoLog( program.program, "program link", names[index] );
		}
	}
	if ( status == GL_FALSE ) {
		RB_Volumetric_FreeProgram( program );
		program.generation = tr.glContextGeneration;
		program.failed = true;
		return NULL;
	}
	for ( int i = 0; i < RB_VOL_UNIFORM_COUNT; i++ ) {
		program.uniforms[i] = glGetUniformLocationARB( program.program, rbVolumetricUniformNames[i] );
	}
	program.failed = false;
	common->Printf( "Loaded built-in GLSL program '%s'\n", names[index] );
	return &program;
}

static void RB_Volumetric_Uniform4( const rbVolumetricProgram_t *program, int uniform, const float *value, int count = 1 ) {
	if ( program->uniforms[uniform] >= 0 ) {
		glUniform4fvARB( program->uniforms[uniform], count, value );
	}
}

static void RB_Volumetric_Uniform1( const rbVolumetricProgram_t *program, int uniform, const float *value, int count = 1 ) {
	if ( program->uniforms[uniform] >= 0 ) {
		glUniform1fvARB( program->uniforms[uniform], count, value );
	}
}

static void RB_Volumetric_Sampler( const rbVolumetricProgram_t *program, int uniform, int unit ) {
	if ( program->uniforms[uniform] >= 0 ) {
		glUniform1iARB( program->uniforms[uniform], unit );
	}
}

/*
==================
RB_Volumetric_GL_BackendDecline

The OpenGL limits under which the leaf declines the view.
==================
*/
static const char *RB_Volumetric_GL_BackendDecline( const viewDef_t *view ) {
	if ( !glConfig.GLSLProgramAvailable || !glConfig.GLSL130Available ) {
		// Apple's GL 2.1 context and other GLSL 1.20 drivers.
		return "glsl130-unavailable";
	}
	if ( glConfig.disableARB2Interactions
			|| ( RendererDriverQuirks_LastReport().flags & RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS ) != 0 ) {
		return "apple-gl21-corridor";
	}
	if ( r_skipPostProcess.GetBool() ) {
		return "post-process-skipped";
	}
	if ( view->viewport.x2 < view->viewport.x1 || view->viewport.y2 < view->viewport.y1 ) {
		return "empty-viewport";
	}
	return NULL;
}

void RB_Volumetric_GL_BeginView( const viewDef_t *view ) {
	rbVolumetricView_t &state = rbVolumetricView;
	memset( &state, 0, sizeof( state ) );
	state.view = view;
	R_Volumetric_BuildViewSettings( view, view != NULL ? RB_Volumetric_GL_BackendDecline( view ) : NULL, state.settings );
	R_Volumetric_BeginViewStats( state.stats, state.settings, view );
	state.began = true;
	if ( !state.settings.active ) {
		return;
	}
	state.sceneX = view->viewport.x1;
	state.sceneY = view->viewport.y1;
	state.sceneWidth = view->viewport.x2 - view->viewport.x1 + 1;
	state.sceneHeight = view->viewport.y2 - view->viewport.y1 + 1;
	R_Volumetric_TargetExtent( state.settings, state.sceneWidth, state.sceneHeight, state.targetWidth, state.targetHeight );
	state.slot = R_Volumetric_ChooseTargetSlot( rbVolumetricPool, state.sceneWidth, state.sceneHeight,
		state.settings.resolutionDivisor );

	if ( !R_Volumetric_ViewTransforms( view, state.clipToWorld, state.eye, state.viewForward ) ) {
		state.settings.active = false;
		state.settings.declineReason = "singular-projection";
		state.stats.declineReason = state.settings.declineReason;
		return;
	}
}

void RB_Volumetric_GL_DeclineView( const char *reason ) {
	rbVolumetricView_t &state = rbVolumetricView;
	if ( !state.began || !state.settings.active ) {
		return;
	}
	state.settings.active = false;
	state.settings.declineReason = reason;
	state.stats.declineReason = reason;
}

static bool RB_Volumetric_EnsureResources( rbVolumetricView_t &state ) {
	if ( globalImages == NULL ) {
		return false;
	}
	idImage *&rbVolumetricDepthImage = rbVolumetricDepthImages[state.slot];
	idImage *&rbVolumetricScatterImage = rbVolumetricScatterImages[state.slot];
	idRenderTexture *&rbVolumetricScatterTarget = rbVolumetricScatterTargets[state.slot];
	if ( !state.depthCopied ) {
		idImageOpts depthOpts;
		depthOpts.textureType = TT_2D;
		depthOpts.format = FMT_DEPTH;
		depthOpts.width = state.sceneWidth;
		depthOpts.height = state.sceneHeight;
		depthOpts.numLevels = 1;
		depthOpts.numMSAASamples = 0;
		depthOpts.isPersistant = true;
		if ( rbVolumetricDepthImage == NULL || rbVolumetricDepthImage->GetUploadWidth() != state.sceneWidth
				|| rbVolumetricDepthImage->GetUploadHeight() != state.sceneHeight ) {
			R_Volumetric_NoteTargetAllocation();
		}
		rbVolumetricDepthImage = globalImages->ScratchImage( va( "_volumetricDepth%d", state.slot ), &depthOpts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
		if ( rbVolumetricDepthImage == NULL ) {
			return false;
		}
		// Interactions draw at depth EQUAL without writing, so this copy of
		// the prepass serves every light and the composite. A multisampled
		// scene resolves to one sample.
		rbVolumetricDepthImage->CopyDepthbuffer( state.sceneX, state.sceneY, state.sceneWidth, state.sceneHeight );
		state.depthCopied = true;
	}
	if ( rbVolumetricScatterImage == NULL || rbVolumetricScatterTarget == NULL
			|| rbVolumetricScatterImage->GetUploadWidth() != state.targetWidth
			|| rbVolumetricScatterImage->GetUploadHeight() != state.targetHeight ) {
		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_RGBA16F;
		opts.width = state.targetWidth;
		opts.height = state.targetHeight;
		opts.numLevels = 1;
		opts.numMSAASamples = 0;
		opts.isPersistant = true;
		R_Volumetric_NoteTargetAllocation();
		rbVolumetricScatterImage = globalImages->ScratchImage( va( "_volumetricScatter%d", state.slot ), &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
		if ( rbVolumetricScatterImage == NULL ) {
			return false;
		}
		if ( rbVolumetricScatterTarget == NULL ) {
			rbVolumetricScatterTarget = tr.CreateRenderTexture( rbVolumetricScatterImage, NULL );
		} else if ( rbVolumetricScatterTarget->GetWidth() != state.targetWidth
				|| rbVolumetricScatterTarget->GetHeight() != state.targetHeight ) {
			tr.ResizeRenderTexture( rbVolumetricScatterTarget, state.targetWidth, state.targetHeight );
		}
		if ( rbVolumetricScatterTarget == NULL ) {
			return false;
		}
	}
	return true;
}

// Saved scene state across a pass drawn inside the light loop.
typedef struct rbVolumetricSavedState_s {
	GLint		viewport[4];
	GLint		scissor[4];
	GLboolean	scissorTest;
	GLboolean	stencilTest;
	GLboolean	depthTest;
	int		faceCulling;
} rbVolumetricSavedState_t;

static void RB_Volumetric_BeginPass( rbVolumetricSavedState_t &saved ) {
	glGetIntegerv( GL_VIEWPORT, saved.viewport );
	glGetIntegerv( GL_SCISSOR_BOX, saved.scissor );
	saved.scissorTest = glIsEnabled( GL_SCISSOR_TEST );
	saved.stencilTest = glIsEnabled( GL_STENCIL_TEST );
	saved.depthTest = glIsEnabled( GL_DEPTH_TEST );
	saved.faceCulling = backEnd.glState.faceCulling;

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, 1, 0, 1, -1, 1 );
	glMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_TWO_SIDED );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_SCISSOR_TEST );
}

static void RB_Volumetric_EndPass( const rbVolumetricSavedState_t &saved ) {
	glUseProgramObjectARB( 0 );
	if ( backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		R_SetDefaultDrawAndReadBuffers();
	}
	glViewport( saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3] );
	glScissor( saved.scissor[0], saved.scissor[1], saved.scissor[2], saved.scissor[3] );
	if ( saved.scissorTest ) {
		glEnable( GL_SCISSOR_TEST );
	} else {
		glDisable( GL_SCISSOR_TEST );
	}
	if ( saved.stencilTest ) {
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_STENCIL_TEST );
	}
	if ( saved.depthTest ) {
		glEnable( GL_DEPTH_TEST );
	} else {
		glDisable( GL_DEPTH_TEST );
	}
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	backEnd.glState.faceCulling = -1;
	if ( saved.faceCulling >= CT_FRONT_SIDED && saved.faceCulling <= CT_TWO_SIDED ) {
		GL_Cull( saved.faceCulling );
	}
	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );
}

static void RB_Volumetric_DrawQuad( void ) {
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glBegin( GL_QUADS );
	glVertex2f( 0.0f, 0.0f );
	glVertex2f( 0.0f, 1.0f );
	glVertex2f( 1.0f, 1.0f );
	glVertex2f( 1.0f, 0.0f );
	glEnd();
}

static void RB_Volumetric_BindTexture( int unit, idImage *image ) {
	GL_SelectTexture( unit );
	if ( image != NULL ) {
		image->Bind();
	} else {
		globalImages->BindNull();
	}
}

static void RB_Volumetric_BindTargetAndClear( rbVolumetricView_t &state ) {
	backEnd.renderTexture = rbVolumetricScatterTargets[state.slot];
	rbVolumetricScatterTargets[state.slot]->MakeCurrent();
	glViewport( 0, 0, state.targetWidth, state.targetHeight );
	if ( !state.targetCleared ) {
		GLfloat clearColor[4];
		glGetFloatv( GL_COLOR_CLEAR_VALUE, clearColor );
		glDisable( GL_SCISSOR_TEST );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );
		glEnable( GL_SCISSOR_TEST );
		state.targetCleared = true;
	}
}

/*
==================
RB_Volumetric_GL_InjectLight

Called for every interaction light once its opaque receivers are drawn.
shadow is the map its GLOBAL receiver pass just sampled (NULL for none);
stencilShadowed says its shadows were stencil volumes; mapExpected that it
took the shadow-map path but has no map this view.
==================
*/
void RB_Volumetric_GL_InjectLight( const viewLight_t *vLight, const volumetricShadowParams_t *shadow,
		bool stencilShadowed, bool mapExpected ) {
	rbVolumetricView_t &state = rbVolumetricView;
	if ( !state.began || !state.settings.active || vLight == NULL || vLight->scissorRect.IsEmpty() ) {
		return;
	}
	volumetricLightRecord_t record;
	if ( !R_Volumetric_BuildLightRecord( vLight, backEnd.lightScale, backEnd.overBright, record ) ) {
		if ( vLight->lightShader != NULL && !vLight->lightShader->IsAmbientLight() ) {
			R_Volumetric_CountOutcome( state.stats, VOLUMETRIC_LIGHT_SKIPPED_NO_STAGE );
		}
		return;
	}
	volumetricShadowKind_t kind = shadow != NULL ? shadow->kind : VOLUMETRIC_SHADOW_NONE;
	volumetricLightOutcome_t outcome = R_Volumetric_ClassifyLight( state.settings, state.view, record,
		kind, stencilShadowed, mapExpected, state.budgetUsed );
	if ( !R_Volumetric_OutcomeScatters( outcome ) ) {
		R_Volumetric_CountOutcome( state.stats, outcome );
		return;
	}
	if ( state.settings.debug == 2 ) {
		// the same lights with no shadow term
		kind = VOLUMETRIC_SHADOW_NONE;
	}
	const int programIndex = kind == VOLUMETRIC_SHADOW_PROJECTED ? RB_VOL_PROGRAM_PROJECTED
		: ( kind == VOLUMETRIC_SHADOW_POINT ? RB_VOL_PROGRAM_POINT : RB_VOL_PROGRAM_UNSHADOWED );
	const rbVolumetricProgram_t *program = RB_Volumetric_Program( programIndex );
	if ( program == NULL || ( kind != VOLUMETRIC_SHADOW_NONE && shadow->image == NULL ) ) {
		R_Volumetric_CountOutcome( state.stats, VOLUMETRIC_LIGHT_SKIPPED_FAILED );
		return;
	}
	idRenderTexture *sceneTarget = backEnd.renderTexture;
	if ( !RB_Volumetric_EnsureResources( state ) ) {
		R_Volumetric_CountOutcome( state.stats, VOLUMETRIC_LIGHT_SKIPPED_FAILED );
		return;
	}

	rbVolumetricSavedState_t saved;
	RB_Volumetric_BeginPass( saved );
	RB_Volumetric_BindTargetAndClear( state );
	const int divisor = state.settings.resolutionDivisor;
	const int x1 = vLight->scissorRect.x1 / divisor;
	const int y1 = vLight->scissorRect.y1 / divisor;
	const int x2 = Min( state.targetWidth - 1, ( vLight->scissorRect.x2 + divisor - 1 ) / divisor );
	const int y2 = Min( state.targetHeight - 1, ( vLight->scissorRect.y2 + divisor - 1 ) / divisor );
	glScissor( x1, y1, Max( 0, x2 - x1 + 1 ), Max( 0, y2 - y1 + 1 ) );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS );

	glUseProgramObjectARB( program->program );
	RB_Volumetric_Sampler( program, RB_VOL_U_DEPTH, 0 );
	RB_Volumetric_Sampler( program, RB_VOL_U_PROJECTION, 1 );
	RB_Volumetric_Sampler( program, RB_VOL_U_FALLOFF, 2 );
	RB_Volumetric_Sampler( program, RB_VOL_U_SHADOW_MAP, 3 );
	RB_Volumetric_Sampler( program, RB_VOL_U_POINT_SHADOW_MAP, 3 );
	if ( program->uniforms[RB_VOL_U_CLIP_TO_WORLD] >= 0 ) {
		glUniformMatrix4fvARB( program->uniforms[RB_VOL_U_CLIP_TO_WORLD], 1, GL_FALSE, state.clipToWorld );
	}
	const float targetInfo[4] = {
		static_cast<float>( divisor ) / static_cast<float>( state.sceneWidth ),
		static_cast<float>( divisor ) / static_cast<float>( state.sceneHeight ), 0.0f, 0.0f
	};
	float params0[4], params1[4];
	R_Volumetric_PackViewParams( state.settings, params0, params1 );
	RB_Volumetric_Uniform4( program, RB_VOL_U_EYE, state.eye );
	RB_Volumetric_Uniform4( program, RB_VOL_U_VIEW_FORWARD, state.viewForward );
	RB_Volumetric_Uniform4( program, RB_VOL_U_TARGET_INFO, targetInfo );
	RB_Volumetric_Uniform4( program, RB_VOL_U_PARAMS0, params0 );
	RB_Volumetric_Uniform4( program, RB_VOL_U_PARAMS1, params1 );
	RB_Volumetric_Uniform4( program, RB_VOL_U_LIGHT_ORIGIN, record.origin );
	RB_Volumetric_Uniform4( program, RB_VOL_U_BOUNDS_MIN, record.boundsMin );
	RB_Volumetric_Uniform4( program, RB_VOL_U_BOUNDS_MAX, record.boundsMax );

	RB_Volumetric_BindTexture( 0, rbVolumetricDepthImages[state.slot] );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	RB_Volumetric_BindTexture( 2, record.falloffImage );

	// The shadow map is sampled raw: compare mode off for the pass, then
	// restored as the receivers bound it, since the translucent receivers of
	// this light still read it.
	GLint savedCompare = GL_NONE;
	GLint savedMinFilter = GL_NEAREST;
	GLint savedMagFilter = GL_NEAREST;
	GLenum shadowTarget = GL_TEXTURE_2D;
	if ( kind != VOLUMETRIC_SHADOW_NONE ) {
		shadowTarget = kind == VOLUMETRIC_SHADOW_POINT ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
		RB_Volumetric_BindTexture( 3, shadow->image );
		glGetTexParameteriv( shadowTarget, GL_TEXTURE_COMPARE_MODE, &savedCompare );
		glGetTexParameteriv( shadowTarget, GL_TEXTURE_MIN_FILTER, &savedMinFilter );
		glGetTexParameteriv( shadowTarget, GL_TEXTURE_MAG_FILTER, &savedMagFilter );
		glTexParameteri( shadowTarget, GL_TEXTURE_COMPARE_MODE, GL_NONE );
		glTexParameteri( shadowTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( shadowTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		if ( kind == VOLUMETRIC_SHADOW_PROJECTED ) {
			float rows[4][VOLUMETRIC_MAX_CASCADES * 4];
			for ( int c = 0; c < VOLUMETRIC_MAX_CASCADES; c++ ) {
				for ( int r = 0; r < 4; r++ ) {
					memcpy( &rows[r][c * 4], shadow->rows[c][r], 4 * sizeof( float ) );
				}
			}
			RB_Volumetric_Uniform4( program, RB_VOL_U_SHADOW_ROW0, rows[0], VOLUMETRIC_MAX_CASCADES );
			RB_Volumetric_Uniform4( program, RB_VOL_U_SHADOW_ROW1, rows[1], VOLUMETRIC_MAX_CASCADES );
			RB_Volumetric_Uniform4( program, RB_VOL_U_SHADOW_ROW2, rows[2], VOLUMETRIC_MAX_CASCADES );
			RB_Volumetric_Uniform4( program, RB_VOL_U_SHADOW_ROW3, rows[3], VOLUMETRIC_MAX_CASCADES );
			RB_Volumetric_Uniform4( program, RB_VOL_U_SHADOW_RECT, shadow->atlasRects[0], VOLUMETRIC_MAX_CASCADES );
			RB_Volumetric_Uniform1( program, RB_VOL_U_SHADOW_SPLIT, shadow->splitDepths, VOLUMETRIC_MAX_CASCADES );
			RB_Volumetric_Uniform1( program, RB_VOL_U_SHADOW_BIAS, shadow->depthBias, VOLUMETRIC_MAX_CASCADES );
			const float count = static_cast<float>( shadow->cascadeCount );
			RB_Volumetric_Uniform1( program, RB_VOL_U_SHADOW_COUNT, &count );
		} else {
			const float point[4] = { shadow->pointFar, shadow->pointBias, shadow->pointDepthMode, 0.0f };
			RB_Volumetric_Uniform4( program, RB_VOL_U_POINT_SHADOW, point );
		}
	}

	for ( int i = 0; i < record.stageCount; i++ ) {
		const volumetricLightStage_t &stage = record.stages[i];
		RB_Volumetric_BindTexture( 1, stage.image );
		RB_Volumetric_Uniform4( program, RB_VOL_U_PROJECT_S, stage.projectS );
		RB_Volumetric_Uniform4( program, RB_VOL_U_PROJECT_T, stage.projectT );
		RB_Volumetric_Uniform4( program, RB_VOL_U_PROJECT_Q, stage.projectQ );
		RB_Volumetric_Uniform4( program, RB_VOL_U_FALLOFF_S, stage.falloff );
		RB_Volumetric_Uniform4( program, RB_VOL_U_LIGHT_COLOR, stage.color );
		RB_Volumetric_DrawQuad();
		state.stats.passes++;
	}

	if ( kind != VOLUMETRIC_SHADOW_NONE ) {
		RB_Volumetric_BindTexture( 3, shadow->image );
		glTexParameteri( shadowTarget, GL_TEXTURE_COMPARE_MODE, savedCompare );
		glTexParameteri( shadowTarget, GL_TEXTURE_MIN_FILTER, savedMinFilter );
		glTexParameteri( shadowTarget, GL_TEXTURE_MAG_FILTER, savedMagFilter );
	}
	for ( int unit = 3; unit >= 0; unit-- ) {
		RB_Volumetric_BindTexture( unit, NULL );
	}
	backEnd.renderTexture = sceneTarget;
	RB_Volumetric_EndPass( saved );

	state.budgetUsed++;
	R_Volumetric_CountOutcome( state.stats, outcome );
}

/*
==================
RB_Volumetric_GL_Composite

After the opaque scene and its pre-fog material stages, before fog, blend
lights and translucent surfaces: scene * transmittance + scattering. Always
publishes the view's record.
==================
*/
void RB_Volumetric_GL_Composite( void ) {
	rbVolumetricView_t &state = rbVolumetricView;
	if ( !state.began ) {
		return;
	}
	state.began = false;
	if ( !state.settings.active ) {
		if ( state.settings.requested ) {
			R_Volumetric_PublishViewStats( "OpenGL", state.stats );
		}
		return;
	}
	const rbVolumetricProgram_t *program = RB_Volumetric_Program( RB_VOL_PROGRAM_COMPOSITE );
	idRenderTexture *sceneTarget = backEnd.renderTexture;
	if ( program == NULL || !RB_Volumetric_EnsureResources( state ) ) {
		state.stats.declineReason = "composite-unavailable";
		R_Volumetric_PublishViewStats( "OpenGL", state.stats );
		return;
	}
	rbVolumetricSavedState_t saved;
	RB_Volumetric_BeginPass( saved );
	if ( !state.targetCleared ) {
		// No light scattered: the medium still attenuates the scene.
		RB_Volumetric_BindTargetAndClear( state );
		backEnd.renderTexture = sceneTarget;
		if ( sceneTarget != NULL ) {
			sceneTarget->MakeCurrent();
		} else {
			idRenderTexture::BindNull();
			R_SetDefaultDrawAndReadBuffers();
		}
	}
	glViewport( state.sceneX, state.sceneY, state.sceneWidth, state.sceneHeight );
	glScissor( state.sceneX, state.sceneY, state.sceneWidth, state.sceneHeight );
	GL_State( state.settings.debug != 0
		? ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS )
		: ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_SRC_ALPHA | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS ) );
	glUseProgramObjectARB( program->program );
	RB_Volumetric_Sampler( program, RB_VOL_U_DEPTH, 0 );
	RB_Volumetric_Sampler( program, RB_VOL_U_SCATTER, 1 );
	if ( program->uniforms[RB_VOL_U_CLIP_TO_WORLD] >= 0 ) {
		glUniformMatrix4fvARB( program->uniforms[RB_VOL_U_CLIP_TO_WORLD], 1, GL_FALSE, state.clipToWorld );
	}
	const float sceneInfo[4] = {
		static_cast<float>( state.sceneWidth ), static_cast<float>( state.sceneHeight ),
		static_cast<float>( state.targetWidth ), static_cast<float>( state.targetHeight )
	};
	const float targetInfo[4] = { static_cast<float>( state.sceneX ), static_cast<float>( state.sceneY ), 0.0f, 0.0f };
	float params0[4], params1[4];
	R_Volumetric_PackViewParams( state.settings, params0, params1 );
	RB_Volumetric_Uniform4( program, RB_VOL_U_EYE, state.eye );
	RB_Volumetric_Uniform4( program, RB_VOL_U_SCENE_INFO, sceneInfo );
	RB_Volumetric_Uniform4( program, RB_VOL_U_TARGET_INFO, targetInfo );
	RB_Volumetric_Uniform4( program, RB_VOL_U_PARAMS0, params0 );
	RB_Volumetric_Uniform4( program, RB_VOL_U_PARAMS1, params1 );
	RB_Volumetric_BindTexture( 0, rbVolumetricDepthImages[state.slot] );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	RB_Volumetric_BindTexture( 1, rbVolumetricScatterImages[state.slot] );
	RB_Volumetric_DrawQuad();
	RB_Volumetric_BindTexture( 1, NULL );
	RB_Volumetric_BindTexture( 0, NULL );
	RB_Volumetric_EndPass( saved );
	// The pass leaves blending to the next GL_State.
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

	state.stats.composited = true;
	state.stats.width = state.targetWidth;
	state.stats.height = state.targetHeight;
	R_Volumetric_PublishViewStats( "OpenGL", state.stats );
}

void RB_Volumetric_GL_Shutdown( void ) {
	for ( int i = 0; i < RB_VOL_PROGRAM_COUNT; i++ ) {
		if ( rbVolumetricPrograms[i].generation == tr.glContextGeneration ) {
			RB_Volumetric_FreeProgram( rbVolumetricPrograms[i] );
		} else {
			memset( &rbVolumetricPrograms[i], 0, sizeof( rbVolumetricPrograms[i] ) );
		}
	}
	// The images belong to the image manager.
	for ( int i = 0; i < VOLUMETRIC_TARGET_SLOTS; i++ ) {
		if ( rbVolumetricScatterTargets[i] != NULL ) {
			tr.DestroyRenderTexture( rbVolumetricScatterTargets[i] );
			rbVolumetricScatterTargets[i] = NULL;
		}
		rbVolumetricDepthImages[i] = NULL;
		rbVolumetricScatterImages[i] = NULL;
	}
	memset( &rbVolumetricPool, 0, sizeof( rbVolumetricPool ) );
	memset( &rbVolumetricView, 0, sizeof( rbVolumetricView ) );
}
