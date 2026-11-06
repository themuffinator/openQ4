// Copyright (C) 2026 DarkMatter Productions
// Native per-surface PBR in the classic OpenGL light loop: Stage B of
// docs/dev/archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md.
//
// OpenGL drew authored PBR materials only when a whole frame qualified for the
// experimental modern path, so ordinary gameplay on the default renderer never
// showed them. This owner works inside the classic loop exactly like native
// Vulkan does: the depth fill, stencil shadows and shadow maps are unchanged;
// each light's single classic bump/diffuse/specular submit becomes one PBR
// draw (unshadowed or stencil-shadowed here, shadow-mapped through the PBR
// variants of the receiver programs in draw_arb2.cpp); and the ambient walk
// adds one environment draw and the typed emission per surface.
//
// Every draw shades with the shared kernel (PBRMath.h) and composes like the
// classic interaction it replaces: its radiance is encoded into the
// display-referred framebuffer on its own (Stage C).

#include <cstring>
#include <vector>

#include "tr_local.h"
#include "draw_pbr.h"
#include "PBRMath.h"
#include "PBRNativeContract.h"
#include "ModernSpecularProbeAtlas.h"
#include "ModernClusteredLighting.h"
#include "ModernLightGridGLSL.h"

idCVar r_glPBR( "r_glPBR", "1", CVAR_RENDERER | CVAR_BOOL,
	"draw admitted PBR materials natively in the classic OpenGL light loop; 0 keeps their classic stages" );

// draw_arb2.cpp
bool RB_GLSLInteractionVertexCache( const drawSurf_t *surf, idDrawVert *&ambientVertexPointer );
// draw_common.cpp: the view's SSAO occlusion field, drawn before lighting
bool RB_SSAOIndirectField( idImage **image, float invSize[2] );
bool RB_GLSLSurfaceUsesGPUPosedGeometry( const drawSurf_t *surf );
bool RB_GLSLInteractionsOwnedByModernPath( void );

static const int GL_PBR_UNIT_NORMAL = 0;
static const int GL_PBR_UNIT_FALLOFF = 1;
static const int GL_PBR_UNIT_PROJECTION = 2;
static const int GL_PBR_UNIT_ALBEDO = 3;
static const int GL_PBR_UNIT_DATA = 4;
// Above the eight tracked units: the shadow receivers already own 5-8.
static const int GL_PBR_UNIT_METALLIC = 9;
static const int GL_PBR_UNIT_AO = 10;
static const int GL_PBR_UNIT_ENVIRONMENT = 11;
static const int GL_PBR_UNIT_PROBE_RECORDS = 12;
static const int GL_PBR_UNIT_PROBE_INDICES = 13;
static const int GL_PBR_UNIT_SCREEN_AO = 14;
static const int GL_PBR_REQUIRED_IMAGE_UNITS = 15;
// The environment program reuses the shadow units for the baked light grid.
static const int GL_PBR_UNIT_BAKED_IRRADIANCE = 5;
static const int GL_PBR_UNIT_BAKED_VISIBILITY = 6;
static const int GL_PBR_UNIT_BAKED_RELOCATION = 7;
// Cluster index pairs are stored row-major in a float texture this wide.
static const int GL_PBR_PROBE_INDEX_WIDTH = 1024;

/*
===============================================================================

GLSL libraries

The receiver programs (glprogs/shadow_interaction and shadow_point_interaction)
compile their own sources with OPENQ4_PBR defined; the vertex library supplies
the orthonormal object-space frame and the fragment library the BRDF. Both are
appended after the program's own declarations, so they may use its uniforms
and varyings by name. They mirror pbr_vertex.glsl and pbr_direct.glsl, the
Vulkan implementation, term for term.

===============================================================================
*/

static const char *glPBRVertexLibrary =
	"\n// openQ4 native PBR vertex frame (draw_pbr.cpp)\n"
	"varying vec3 vPBRTangent0;\n"
	"varying vec3 vPBRTangent1;\n"
	"varying vec3 vPBRNormal;\n"
	"vec3 PBRVertexNormalize( vec3 value, vec3 fallback ) {\n"
	"	float lengthSquared = dot( value, value );\n"
	"	return lengthSquared > 1.0e-8 ? value * inversesqrt( lengthSquared ) : fallback;\n"
	"}\n"
	// Averaged mesh tangents are not necessarily perpendicular to the
	// authored normal; leaving that component in the frame can flip N.V at
	// grazing angles after mapping.
	"void PBRVertexFrame( vec3 sourceNormal, vec3 sourceTangent, vec3 sourceBitangent ) {\n"
	"	vec3 normal = PBRVertexNormalize( sourceNormal, vec3( 0.0, 0.0, 1.0 ) );\n"
	"	vec3 tangent = PBRVertexNormalize( sourceTangent, vec3( 1.0, 0.0, 0.0 ) );\n"
	"	vec3 rawBitangent = PBRVertexNormalize( sourceBitangent, vec3( 0.0, 1.0, 0.0 ) );\n"
	"	tangent = PBRVertexNormalize( tangent - normal * dot( normal, tangent ), vec3( 1.0, 0.0, 0.0 ) );\n"
	"	float handedness = dot( cross( normal, tangent ), rawBitangent ) < 0.0 ? -1.0 : 1.0;\n"
	"	vPBRNormal = normal;\n"
	"	vPBRTangent0 = tangent;\n"
	"	vPBRTangent1 = PBRVertexNormalize( cross( normal, tangent ) * handedness, rawBitangent );\n"
	"}\n";

// Material inputs shared by the per-light and environment programs.
#define GL_PBR_MATERIAL_GLSL \
	"uniform sampler2D uPBRMetallicMap;\n" \
	"uniform sampler2D uPBRAOMap;\n" \
	"uniform vec4 uPBRMaterial;\n"	/* metallic, roughness, normal scale, AO scalar */ \
	"uniform vec4 uPBRLayout;\n"	/* packed ORM, metallic map, roughness map, AO map */ \
	"uniform vec4 uPBRMode;\n"		/* normal encoding, display-referred, specular AA, ambient light */ \
	"uniform vec4 uPBRTransparent;\n"	/* authored stage alpha scale (0: additive), coverage only */ \
	"uniform sampler2D uPBRScreenAOMap;\n" \
	"uniform vec4 uPBRScreenAO;\n"	/* x SSAO field bound, yz 1 / its size */ \
	"varying vec3 vPBRTangent0;\n" \
	"varying vec3 vPBRTangent1;\n" \
	"varying vec3 vPBRNormal;\n" \
	"vec3 PBRSafeNormalize( vec3 value ) {\n" \
	"	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );\n" \
	"}\n" \
	/* Ordered transparency: the albedo supplies the per-texel coverage the */ \
	/* authored stage would have sampled. Additive draws keep zero alpha.   */ \
	"float OpenQ4PBRAlpha() {\n" \
	"	return uPBRTransparent.x > 0.0\n" \
	"		? clamp( texture2D( uDiffuseMap, vDiffuseTexCoord ).a * uPBRTransparent.x, 0.0, 1.0 ) : 0.0;\n" \
	"}\n" \
	/* 0 flat, 1 Quake 4 AGB, 2 tangent RG, 3 tangent XYZ (pbrNormalFormat_t + 1) */ \
	"vec3 PBRMappedNormal( vec2 texCoord ) {\n" \
	"	float encoding = floor( uPBRMode.x + 0.5 );\n" \
	"	if ( encoding < 0.5 ) {\n" \
	"		return vec3( 0.0, 0.0, 1.0 );\n" \
	"	}\n" \
	"	vec4 value = texture2D( uBumpMap, texCoord );\n" \
	"	vec3 normal;\n" \
	"	if ( abs( encoding - 2.0 ) < 0.5 ) {\n" \
	"		vec2 xy = ( value.rg * 2.0 - 1.0 ) * uPBRMaterial.z;\n" \
	"		normal = vec3( xy, sqrt( max( 1.0 - dot( xy, xy ), 0.0 ) ) );\n" \
	"	} else {\n" \
	"		normal = ( encoding > 2.5 ? value.rgb : value.agb ) * 2.0 - 1.0;\n" \
	"		normal.xy *= uPBRMaterial.z;\n" \
	"	}\n" \
	"	float lengthSquared = dot( normal, normal );\n" \
	"	return lengthSquared > 1.0e-8 ? normal * inversesqrt( lengthSquared ) : vec3( 0.0, 0.0, 1.0 );\n" \
	"}\n" \
	"vec3 PBRObjectNormal( vec2 texCoord ) {\n" \
	"	vec3 mapped = PBRMappedNormal( texCoord );\n" \
	"	return PBRSafeNormalize( PBRSafeNormalize( vPBRTangent0 ) * mapped.x\n" \
	"		+ PBRSafeNormalize( vPBRTangent1 ) * mapped.y + PBRSafeNormalize( vPBRNormal ) * mapped.z );\n" \
	"}\n" \
	/* x metallic, y perceptual roughness (unfiltered), z AO */ \
	"vec3 PBRMaterialData( vec2 dataTexCoord ) {\n" \
	"	float metallic = uPBRMaterial.x;\n" \
	"	float roughness = uPBRMaterial.y;\n" \
	"	float ao = uPBRMaterial.w;\n" \
	"	if ( uPBRLayout.x > 0.5 ) {\n" \
	"		vec3 orm = texture2D( uSpecularMap, dataTexCoord ).rgb;\n" \
	"		ao *= orm.r;\n" \
	"		roughness *= orm.g;\n" \
	"		metallic *= orm.b;\n" \
	"	} else {\n" \
	"		if ( uPBRLayout.y > 0.5 ) metallic *= texture2D( uPBRMetallicMap, dataTexCoord ).r;\n" \
	"		if ( uPBRLayout.z > 0.5 ) roughness *= texture2D( uSpecularMap, dataTexCoord ).r;\n" \
	"	}\n" \
	"	if ( uPBRLayout.w > 0.5 ) ao *= texture2D( uPBRAOMap, dataTexCoord ).r;\n" \
	"	return vec3( clamp( metallic, 0.0, 1.0 ), roughness, clamp( ao, 0.0, 1.0 ) );\n" \
	"}\n" \
	/* SSAO occludes indirect light only, as the lesser of its occlusion and */ \
	/* the material AO: both describe the same crevices. Direct light never */ \
	/* reads it. */ \
	"float PBRScreenAO( float ao ) {\n" \
	"	return uPBRScreenAO.x > 0.5 ? min( ao, texture2D( uPBRScreenAOMap, gl_FragCoord.xy * uPBRScreenAO.yz ).r ) : ao;\n" \
	"}\n" \
	"vec3 PBRMultiBounceAOColor( float visibility, vec3 albedo ) {\n" \
	"	return vec3( PBRMultiBounceAO( visibility, albedo.r ),\n" \
	"		PBRMultiBounceAO( visibility, albedo.g ), PBRMultiBounceAO( visibility, albedo.b ) );\n" \
	"}\n" \
	"vec3 PBREnergyCompensationColor( vec3 f0, float specularAlbedo ) {\n" \
	"	return vec3( PBREnergyCompensation( f0.r, specularAlbedo ),\n" \
	"		PBREnergyCompensation( f0.g, specularAlbedo ), PBREnergyCompensation( f0.b, specularAlbedo ) );\n" \
	"}\n" \
	/* Production composition: encode the draw's linear radiance for the */ \
	/* display-referred framebuffer, exactly like the classic surfaces.  */ \
	"vec3 PBRDisplayOutput( vec3 radiance ) {\n" \
	"	if ( uPBRMode.y < 0.5 ) {\n" \
	"		return radiance;\n" \
	"	}\n" \
	"	return vec3( PBRLinearToSRGBExtended( radiance.r ),\n" \
	"		PBRLinearToSRGBExtended( radiance.g ), PBRLinearToSRGBExtended( radiance.b ) );\n" \
	"}\n"

static idStr glPBRFragmentLibrary;

static void RB_GLPBR_BuildFragmentLibrary( void ) {
	if ( glPBRFragmentLibrary.Length() > 0 ) {
		return;
	}
	glPBRFragmentLibrary = "\n// openQ4 native PBR shading (draw_pbr.cpp)\n";
	glPBRFragmentLibrary += OPENQ4_PBR_SCALAR_GLSL;
	glPBRFragmentLibrary += "\n";
	glPBRFragmentLibrary += GL_PBR_MATERIAL_GLSL;
	glPBRFragmentLibrary +=
		// The classic light term is what a white classic surface shows; PBR
		// receives pi times its decoded value as irradiance.
		"vec3 PBRClassicLightIrradianceColor( vec3 lightTerm ) {\n"
		"	return vec3( PBRClassicLightIrradiance( lightTerm.r ),\n"
		"		PBRClassicLightIrradiance( lightTerm.g ), PBRClassicLightIrradiance( lightTerm.b ) );\n"
		"}\n"
		// An authored ambient light stands in for bounced light from every
		// direction: a uniform environment of radiance E / pi, the environment
		// term with prefiltered radiance and irradiance both E / pi
		// (pbr_direct.glsl PBRUniformEnvironmentLight).
		"vec3 PBRUniformEnvironmentLight( vec3 irradiance, vec3 albedo, float metallic, float roughness,\n"
		"		float ao, vec3 n, vec3 v, vec3 vertexNormal ) {\n"
		"	float NoV = PBRShadingNoV( dot( n, v ) );\n"
		"	vec3 f0 = mix( vec3( 0.04 ), albedo, metallic );\n"
		"	vec3 diffuseColor = albedo * ( 1.0 - metallic );\n"
		"	vec3 fresnel = f0 + ( max( vec3( 1.0 - roughness ), f0 ) - f0 ) * PBRFresnelWeight( NoV );\n"
		"	vec3 specular = vec3( PBRUniformEnvironmentSpecular( f0.r, NoV, roughness ),\n"
		"			PBRUniformEnvironmentSpecular( f0.g, NoV, roughness ),\n"
		"			PBRUniformEnvironmentSpecular( f0.b, NoV, roughness ) )\n"
		"		* PBRMultiBounceAOColor( PBRSpecularOcclusion( NoV, ao, roughness ), f0 )\n"
		"		* PBRHorizonOcclusion( dot( reflect( -v, n ), PBRSafeNormalize( vertexNormal ) ) );\n"
		"	vec3 diffuse = ( vec3( 1.0 ) - fresnel ) * diffuseColor * PBRMultiBounceAOColor( ao, diffuseColor );\n"
		"	return ( diffuse + specular ) * irradiance * ( 1.0 / 3.14159265 );\n"
		"}\n"
		"vec3 OpenQ4PBRDirect( vec3 shadow ) {\n"
		// Color uses sRGB storage and decodes before filtering. Data stays linear.
		"	vec3 albedo = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb;\n"
		"	vec3 data = PBRMaterialData( vSpecularTexCoord );\n"
		"	float metallic = data.x;\n"
		"	vec3 radiance = PBRClassicLightIrradianceColor(\n"
		"		texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb\n"
		"		* texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb\n"
		"		* uDiffuseColor.rgb ) * shadow;\n"
		"	float roughness = PBRRoughness( data.y );\n"
		// A flat tangent-space normal still varies across a curved surface:
		// measure the final normal in object space for specular AA.
		"	vec3 objectNormal = PBRObjectNormal( vBumpTexCoord );\n"
		"	vec3 normalDx = dFdx( objectNormal );\n"
		"	vec3 normalDy = dFdy( objectNormal );\n"
		"	if ( uPBRMode.z > 0.5 ) {\n"
		"		roughness = PBRFilteredRoughness( roughness, 0.5 * ( dot( normalDx, normalDx ) + dot( normalDy, normalDy ) ) );\n"
		"	}\n"
		"	vec3 viewDir = PBRSafeNormalize( vViewVector );\n"
		"	if ( uPBRMode.w > 0.5 ) {\n"
		"		return PBRDisplayOutput( PBRUniformEnvironmentLight( radiance, albedo, metallic, roughness, PBRScreenAO( data.z ),\n"
		"			objectNormal, viewDir, vPBRNormal ) * vVertexColor );\n"
		"	}\n"
		"	vec3 lightDir = PBRSafeNormalize( vLightVector );\n"
		"	vec3 halfDir = PBRSafeNormalize( lightDir + viewDir );\n"
		"	float ndotl = max( dot( objectNormal, lightDir ), 0.0 );\n"
		"	float ndotv = PBRShadingNoV( dot( objectNormal, viewDir ) );\n"
		"	float ndoth = max( dot( objectNormal, halfDir ), 0.0 );\n"
		"	float vdoth = max( dot( viewDir, halfDir ), 0.0 );\n"
		"	if ( ndotl <= 0.0 || dot( lightDir + viewDir, lightDir + viewDir ) <= 1.0e-8 ) {\n"
		"		return vec3( 0.0 );\n"
		"	}\n"
		"	float distribution = PBRDistributionGGX( ndoth, roughness );\n"
		"	float visibility = PBRVisibilitySmithGGX( ndotv, ndotl, roughness );\n"
		"	vec3 f0 = mix( vec3( 0.04 ), albedo, metallic );\n"
		"	vec3 fresnel = f0 + ( vec3( 1.0 ) - f0 ) * PBRFresnelWeight( vdoth );\n"
		// Single scattering loses up to 69% of a rough conductor's energy; no
		// per-light pass binds the split-sum table, so the fitted albedo serves.
		"	vec3 specular = distribution * visibility * fresnel\n"
		"		* PBREnergyCompensationColor( f0, PBRSpecularAlbedo( ndotv, roughness ) );\n"
		"	vec3 diffuse = ( vec3( 1.0 ) - fresnel ) * ( 1.0 - metallic ) * albedo * ( 1.0 / 3.14159265 );\n"
		// Authored AO modulates indirect irradiance, never this direct light.
		"	return PBRDisplayOutput( ( diffuse + specular ) * radiance * ndotl * vVertexColor );\n"
		"}\n";
}

const char *RB_GLPBR_ReceiverDefines( bool unshadowed ) {
	return unshadowed ? "#define OPENQ4_PBR 1\n#define OPENQ4_PBR_UNSHADOWED 1\n" : "#define OPENQ4_PBR 1\n";
}

const char *RB_GLPBR_VertexLibrary( void ) {
	return glPBRVertexLibrary;
}

const char *RB_GLPBR_FragmentLibrary( void ) {
	RB_GLPBR_BuildFragmentLibrary();
	return glPBRFragmentLibrary.c_str();
}

/*
===============================================================================

Frame telemetry

===============================================================================
*/

typedef struct {
	int		frame;
	int		admitted;			// admission evaluations that admitted a surface
	int		declined;			// PBR surfaces kept on their classic stages
	int		interactions;		// unshadowed and stencil-shadowed PBR draws
	int		mappedInteractions;	// shadow-mapped PBR receiver draws
	int		environment;		// environment and diagnostic draws
	int		emission;			// native emission draws
	int		transparent;		// translucent surfaces composited at their blend stage
	int		transparentLights;	// light draws replayed through a translucent coverage
	int		baked;				// environment draws that took a baked light grid's diffuse
	const char *lastDecline;
	const char *translucentView;	// why the last view did or did not own translucency
} glPBRFrameStats_t;

static glPBRFrameStats_t g_glPBRStats;
static glPBRFrameStats_t g_glPBRLastStats;

static void RB_GLPBR_TouchStats( void ) {
	if ( g_glPBRStats.frame != backEnd.frameCount ) {
		if ( g_glPBRStats.frame != 0 ) {
			g_glPBRLastStats = g_glPBRStats;
		}
		memset( &g_glPBRStats, 0, sizeof( g_glPBRStats ) );
		g_glPBRStats.frame = backEnd.frameCount;
		g_glPBRStats.lastDecline = "none";
		g_glPBRStats.translucentView = "none";
	}
}

void RB_GLPBR_CountMappedInteraction( void ) {
	RB_GLPBR_TouchStats();
	g_glPBRStats.mappedInteractions++;
}

/*
===============================================================================

Programs

===============================================================================
*/

typedef struct {
	GLhandleARB	program;
	GLhandleARB	vertexShader;
	GLhandleARB	fragmentShader;
	int			generation;
	bool		attempted;
	bool		valid;

	GLint		localLightOrigin;
	GLint		localViewOrigin;
	GLint		lightProjectionS;
	GLint		lightProjectionT;
	GLint		lightProjectionQ;
	GLint		lightFalloffS;
	GLint		bumpMatrixS;
	GLint		bumpMatrixT;
	GLint		diffuseMatrixS;
	GLint		diffuseMatrixT;
	GLint		specularMatrixS;
	GLint		specularMatrixT;
	GLint		diffuseColor;
	GLint		vertexColorParams;
	glPBRReceiverUniforms_t pbr;

	// environment and emission programs only
	GLint		objectToWorld[3];
	GLint		environment;	// x intensity, y diagnostic mode, z atlas ready
	GLint		emissiveColor;
	GLint		textureMatrixS;
	GLint		textureMatrixT;
	GLint		modelMatrixRow[3];
	GLint		probeHeader;
	GLint		bakedGrid;
} glPBRProgram_t;

static glPBRProgram_t g_glPBRUnshadowed;
static glPBRProgram_t g_glPBREnvironment;
static glPBRProgram_t g_glPBREmission;

static void RB_GLPBR_PrintInfoLog( GLhandleARB object, const char *label, const char *name ) {
	GLint logLength = 0;
	glGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &logLength );
	if ( logLength <= 1 ) {
		common->Warning( "GLSL %s error in '%s' (no info log)", label, name );
		return;
	}
	logLength = Min( logLength, 1024 * 1024 );
	char *logBuffer = (char *)Mem_ClearedAlloc( logLength + 1 );
	GLsizei written = 0;
	glGetInfoLogARB( object, logLength, &written, logBuffer );
	logBuffer[ idMath::ClampInt( 0, logLength, written ) ] = '\0';
	common->Warning( "GLSL %s error in '%s':\n%s", label, name, logBuffer );
	Mem_Free( logBuffer );
}

static void RB_GLPBR_FreeProgram( glPBRProgram_t &program ) {
	if ( program.program != 0 ) {
		glDetachObjectARB( program.program, program.vertexShader );
		glDetachObjectARB( program.program, program.fragmentShader );
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

// Compiles one shader from up to four parts: an optional replacement for the
// first (#version) line, the source's own version line, defines, the rest of
// the source and an appended library.
static bool RB_GLPBR_CompileShader( GLhandleARB shader, const char *source, const char *versionLine,
		const char *defines, const char *library, const char *name, const char *label ) {
	const char *versionEnd = strchr( source, '\n' );
	if ( versionEnd == NULL || idStr::Cmpn( source, "#version", 8 ) != 0 ) {
		common->Warning( "GLSL source '%s' has no leading #version line", name );
		return false;
	}
	const char *defineText = defines != NULL ? defines : "";
	const char *libraryText = library != NULL ? library : "";
	const GLcharARB *parts[4] = {
		versionLine != NULL ? versionLine : source,
		defineText,
		versionEnd + 1,
		libraryText
	};
	const GLint lengths[4] = {
		static_cast<GLint>( versionLine != NULL ? strlen( versionLine ) : size_t( versionEnd - source + 1 ) ),
		static_cast<GLint>( strlen( defineText ) ),
		static_cast<GLint>( strlen( versionEnd + 1 ) ),
		static_cast<GLint>( strlen( libraryText ) )
	};
	glShaderSourceARB( shader, 4, parts, lengths );
	glCompileShaderARB( shader );
	GLint status = GL_FALSE;
	glGetObjectParameterivARB( shader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_GLPBR_PrintInfoLog( shader, label, name );
		return false;
	}
	return true;
}

static bool RB_GLPBR_LinkProgram( glPBRProgram_t &program, const char *vertexSource, const char *fragmentSource,
		const char *versionLine, const char *defines, const char *vertexLibrary, const char *fragmentLibrary,
		const char *name ) {
	program.vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	program.fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	if ( !RB_GLPBR_CompileShader( program.vertexShader, vertexSource, versionLine, defines, vertexLibrary, name, "vertex shader compile" )
			|| !RB_GLPBR_CompileShader( program.fragmentShader, fragmentSource, versionLine, defines, fragmentLibrary, name, "fragment shader compile" ) ) {
		return false;
	}
	program.program = glCreateProgramObjectARB();
	glAttachObjectARB( program.program, program.vertexShader );
	glAttachObjectARB( program.program, program.fragmentShader );
	glBindAttribLocationARB( program.program, 8, "attr_TexCoord0" );
	glBindAttribLocationARB( program.program, 9, "attr_Tangent" );
	glBindAttribLocationARB( program.program, 10, "attr_Bitangent" );
	glBindAttribLocationARB( program.program, 11, "attr_Normal" );
	glLinkProgramARB( program.program );
	GLint status = GL_FALSE;
	glGetObjectParameterivARB( program.program, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_GLPBR_PrintInfoLog( program.program, "program link", name );
		return false;
	}
	return true;
}

void RB_GLPBR_LookupReceiverUniforms( GLhandleARB program, glPBRReceiverUniforms_t &uniforms ) {
	uniforms.material = glGetUniformLocationARB( program, "uPBRMaterial" );
	uniforms.layout = glGetUniformLocationARB( program, "uPBRLayout" );
	uniforms.mode = glGetUniformLocationARB( program, "uPBRMode" );
	uniforms.metallicMap = glGetUniformLocationARB( program, "uPBRMetallicMap" );
	uniforms.aoMap = glGetUniformLocationARB( program, "uPBRAOMap" );
	uniforms.transparent = glGetUniformLocationARB( program, "uPBRTransparent" );
	uniforms.screenAO = glGetUniformLocationARB( program, "uPBRScreenAO" );
	uniforms.screenAOMap = glGetUniformLocationARB( program, "uPBRScreenAOMap" );
	// Sampler units are program state: assign them once, at link.
	GLhandleARB previous = glGetHandleARB( GL_PROGRAM_OBJECT_ARB );
	glUseProgramObjectARB( program );
	if ( uniforms.metallicMap >= 0 ) {
		glUniform1iARB( uniforms.metallicMap, GL_PBR_UNIT_METALLIC );
	}
	if ( uniforms.aoMap >= 0 ) {
		glUniform1iARB( uniforms.aoMap, GL_PBR_UNIT_AO );
	}
	if ( uniforms.screenAOMap >= 0 ) {
		glUniform1iARB( uniforms.screenAOMap, GL_PBR_UNIT_SCREEN_AO );
	}
	glUseProgramObjectARB( previous );
}

static void RB_GLPBR_LookupInteractionUniforms( glPBRProgram_t &program ) {
	const GLhandleARB object = program.program;
	program.localLightOrigin = glGetUniformLocationARB( object, "uLocalLightOrigin" );
	program.localViewOrigin = glGetUniformLocationARB( object, "uLocalViewOrigin" );
	program.lightProjectionS = glGetUniformLocationARB( object, "uLightProjectionS" );
	program.lightProjectionT = glGetUniformLocationARB( object, "uLightProjectionT" );
	program.lightProjectionQ = glGetUniformLocationARB( object, "uLightProjectionQ" );
	program.lightFalloffS = glGetUniformLocationARB( object, "uLightFalloffS" );
	program.bumpMatrixS = glGetUniformLocationARB( object, "uBumpMatrixS" );
	program.bumpMatrixT = glGetUniformLocationARB( object, "uBumpMatrixT" );
	program.diffuseMatrixS = glGetUniformLocationARB( object, "uDiffuseMatrixS" );
	program.diffuseMatrixT = glGetUniformLocationARB( object, "uDiffuseMatrixT" );
	program.specularMatrixS = glGetUniformLocationARB( object, "uSpecularMatrixS" );
	program.specularMatrixT = glGetUniformLocationARB( object, "uSpecularMatrixT" );
	program.diffuseColor = glGetUniformLocationARB( object, "uDiffuseColor" );
	program.vertexColorParams = glGetUniformLocationARB( object, "uVertexColorParams" );
	program.objectToWorld[0] = glGetUniformLocationARB( object, "uObjectToWorld0" );
	program.objectToWorld[1] = glGetUniformLocationARB( object, "uObjectToWorld1" );
	program.objectToWorld[2] = glGetUniformLocationARB( object, "uObjectToWorld2" );
	program.environment = glGetUniformLocationARB( object, "uPBREnvironment" );
	program.emissiveColor = glGetUniformLocationARB( object, "uEmissiveColor" );
	program.textureMatrixS = glGetUniformLocationARB( object, "uTextureMatrixS" );
	program.textureMatrixT = glGetUniformLocationARB( object, "uTextureMatrixT" );
	program.modelMatrixRow[0] = glGetUniformLocationARB( object, "uModelMatrixRow0" );
	program.modelMatrixRow[1] = glGetUniformLocationARB( object, "uModelMatrixRow1" );
	program.modelMatrixRow[2] = glGetUniformLocationARB( object, "uModelMatrixRow2" );
	program.probeHeader = glGetUniformLocationARB( object, "uProbeHeader[0]" );
	program.bakedGrid = glGetUniformLocationARB( object, "uBakedGrid[0]" );
	RB_GLPBR_LookupReceiverUniforms( object, program.pbr );

	static const struct { const char *name; int unit; } samplers[] = {
		{ "uBumpMap", GL_PBR_UNIT_NORMAL }, { "uLightFalloffMap", GL_PBR_UNIT_FALLOFF },
		{ "uLightProjectionMap", GL_PBR_UNIT_PROJECTION }, { "uDiffuseMap", GL_PBR_UNIT_ALBEDO },
		{ "uSpecularMap", GL_PBR_UNIT_DATA }, { "uEnvironmentAtlas", GL_PBR_UNIT_ENVIRONMENT },
		{ "uEmissiveMap", 0 }, { "uProbeRecords", GL_PBR_UNIT_PROBE_RECORDS },
		{ "uProbeIndices", GL_PBR_UNIT_PROBE_INDICES },
		// Declared by the shared receiver source but unused when unshadowed;
		// distinct units keep sampler types from colliding.
		{ "uShadowMap", 5 }, { "uTranslucentShadowMapR", 6 }, { "uTranslucentShadowMapG", 7 },
		{ "uTranslucentShadowMapB", 8 },
		// The environment program has no shadow maps: its baked light-grid
		// atlases take those units.
		{ "uBakedIrradiance", GL_PBR_UNIT_BAKED_IRRADIANCE }, { "uBakedVisibility", GL_PBR_UNIT_BAKED_VISIBILITY },
		{ "uBakedRelocation", GL_PBR_UNIT_BAKED_RELOCATION },
	};
	GLhandleARB previous = glGetHandleARB( GL_PROGRAM_OBJECT_ARB );
	glUseProgramObjectARB( object );
	for ( int i = 0; i < (int)( sizeof( samplers ) / sizeof( samplers[0] ) ); ++i ) {
		const GLint location = glGetUniformLocationARB( object, samplers[i].name );
		if ( location >= 0 ) {
			glUniform1iARB( location, samplers[i].unit );
		}
	}
	glUseProgramObjectARB( previous );
}

static bool RB_GLPBR_ProgramCurrent( const glPBRProgram_t &program ) {
	return program.attempted && program.generation == tr.videoRestartCount;
}

// The unshadowed and stencil-shadowed variant of the projected receiver.
static bool RB_GLPBR_LoadUnshadowedProgram( void ) {
	glPBRProgram_t &program = g_glPBRUnshadowed;
	if ( RB_GLPBR_ProgramCurrent( program ) ) {
		return program.valid;
	}
	RB_GLPBR_FreeProgram( program );
	program.attempted = true;
	program.generation = tr.videoRestartCount;
	static const char *baseName = "glprogs/shadow_interaction";
	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( va( "%s.vs", baseName ), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( va( "%s.fs", baseName ), (void **)&fragmentBuffer, NULL );
	bool linked = false;
	if ( vertexBuffer != NULL && fragmentBuffer != NULL ) {
		linked = RB_GLPBR_LinkProgram( program, vertexBuffer, fragmentBuffer, NULL,
			RB_GLPBR_ReceiverDefines( true ), RB_GLPBR_VertexLibrary(), RB_GLPBR_FragmentLibrary(),
			"glprogs/shadow_interaction (PBR, unshadowed)" );
	} else {
		common->Warning( "Couldn't load native PBR receiver sources '%s'", baseName );
	}
	if ( vertexBuffer != NULL ) {
		fileSystem->FreeFile( vertexBuffer );
	}
	if ( fragmentBuffer != NULL ) {
		fileSystem->FreeFile( fragmentBuffer );
	}
	if ( !linked ) {
		const int generation = program.generation;
		RB_GLPBR_FreeProgram( program );
		program.attempted = true;
		program.generation = generation;
		return false;
	}
	RB_GLPBR_LookupInteractionUniforms( program );
	program.valid = true;
	return true;
}

/*
Environment: one draw per owned surface in the ambient walk. A port of
pbr_environment.glsl (EvaluatePBREnvironment) and pbr_debug.glsl: the same
atlas cells (analytic slot 8, irradiance cell 62, split-sum table cell 63),
AO, specular occlusion, horizon occlusion and energy compensation. Diagnostic
views draw their material value here once, as on Vulkan.
*/
static const char *glPBREnvironmentVertex =
	"#version 130\n"
	"attribute vec2 attr_TexCoord0;\n"
	"attribute vec3 attr_Tangent;\n"
	"attribute vec3 attr_Bitangent;\n"
	"attribute vec3 attr_Normal;\n"
	"uniform vec4 uLocalViewOrigin;\n"
	"uniform vec4 uBumpMatrixS;\n"
	"uniform vec4 uBumpMatrixT;\n"
	"uniform vec4 uDiffuseMatrixS;\n"
	"uniform vec4 uDiffuseMatrixT;\n"
	"uniform vec4 uSpecularMatrixS;\n"
	"uniform vec4 uSpecularMatrixT;\n"
	"uniform vec2 uVertexColorParams;\n"
	"uniform vec4 uModelMatrixRow0;\n"
	"uniform vec4 uModelMatrixRow1;\n"
	"uniform vec4 uModelMatrixRow2;\n"
	"varying vec2 vBumpTexCoord;\n"
	"varying vec2 vDiffuseTexCoord;\n"
	"varying vec2 vSpecularTexCoord;\n"
	"varying vec3 vViewVector;\n"
	"varying vec3 vVertexColor;\n"
	"varying vec3 vWorldPosition;\n"
	"void PBRVertexFrame( vec3 normal, vec3 tangent, vec3 bitangent );\n"
	"void main() {\n"
	"	vec4 position = gl_Vertex;\n"
	"	vec4 texCoord = vec4( attr_TexCoord0.xy, 0.0, 1.0 );\n"
	"	vViewVector = uLocalViewOrigin.xyz - position.xyz;\n"
	"	PBRVertexFrame( attr_Normal, attr_Tangent, attr_Bitangent );\n"
	"	vBumpTexCoord = vec2( dot( texCoord, uBumpMatrixS ), dot( texCoord, uBumpMatrixT ) );\n"
	"	vDiffuseTexCoord = vec2( dot( texCoord, uDiffuseMatrixS ), dot( texCoord, uDiffuseMatrixT ) );\n"
	"	vSpecularTexCoord = vec2( dot( texCoord, uSpecularMatrixS ), dot( texCoord, uSpecularMatrixT ) );\n"
	"	vVertexColor = gl_Color.rgb * uVertexColorParams.x + vec3( uVertexColorParams.y );\n"
	"	vWorldPosition = vec3( dot( position, uModelMatrixRow0 ), dot( position, uModelMatrixRow1 ), dot( position, uModelMatrixRow2 ) );\n"
	"	gl_Position = ftransform();\n"
	"}\n";

static idStr glPBREnvironmentFragment;

static const char *RB_GLPBR_EnvironmentFragment( void ) {
	if ( glPBREnvironmentFragment.Length() > 0 ) {
		return glPBREnvironmentFragment.c_str();
	}
	glPBREnvironmentFragment =
		"#version 130\n"
		"uniform sampler2D uBumpMap;\n"
		"uniform sampler2D uDiffuseMap;\n"
		"uniform sampler2D uSpecularMap;\n"
		"uniform sampler2D uEnvironmentAtlas;\n"
		"uniform vec4 uDiffuseColor;\n"
		"uniform vec3 uObjectToWorld0;\n"
		"uniform vec3 uObjectToWorld1;\n"
		"uniform vec3 uObjectToWorld2;\n"
		"uniform vec4 uPBREnvironment;\n"	// intensity, diagnostic mode, atlas ready, analytic weight
		"varying vec2 vBumpTexCoord;\n"
		"varying vec2 vDiffuseTexCoord;\n"
		"varying vec2 vSpecularTexCoord;\n"
		"varying vec3 vViewVector;\n"
		"varying vec3 vVertexColor;\n"
		"varying vec3 vWorldPosition;\n"
		// grid, depth, view origin, world-to-view x/y/z, projection; grid.w
		// is the record count and zero when no authored probe applies
		"uniform vec4 uProbeHeader[7];\n"
		"uniform sampler2D uProbeRecords;\n"	// six RGBA32F texels per record
		"uniform sampler2D uProbeIndices;\n";	// two record indices per cluster, -1 for none
	glPBREnvironmentFragment += OPENQ4_PBR_SCALAR_GLSL;
	glPBREnvironmentFragment += "\n";
	glPBREnvironmentFragment += GL_PBR_MATERIAL_GLSL;
	// The baked light grid, sampled exactly as the modern path and Vulkan do
	// (ModernLightGridGLSL.h). That source maps view-relative positions to
	// the world; this program passes world positions, so its basis is the
	// identity and uBakedGrid[6] carries a zero origin.
	glPBREnvironmentFragment +=
		"#define MODERN_HAS_TEXTURE_TABLE 0\n"
		"struct OpenQ4BakedBasis { vec4 viewToWorldX; vec4 viewToWorldY; vec4 viewToWorldZ; };\n"
		"const OpenQ4BakedBasis uClusterGrid = OpenQ4BakedBasis( vec4( 1.0, 0.0, 0.0, 0.0 ),\n"
		"	vec4( 0.0, 1.0, 0.0, 0.0 ), vec4( 0.0, 0.0, 1.0, 0.0 ) );\n";
	glPBREnvironmentFragment += modernLightGridGLSL;
	glPBREnvironmentFragment +=
		"vec3 PBREnvironmentWorld( vec3 objectDirection ) {\n"
		"	return PBRSafeNormalize( vec3( dot( uObjectToWorld0, objectDirection ),\n"
		"		dot( uObjectToWorld1, objectDirection ), dot( uObjectToWorld2, objectDirection ) ) );\n"
		"}\n"
		"vec3 PBREnvironmentLevel( int slot, vec3 direction, int level ) {\n"
		"	vec3 d = PBRSafeNormalize( direction ), ad = abs( d );\n"
		"	int face;\n"
		"	vec2 uv;\n"
		"	float major;\n"
		"	if ( ad.x >= ad.y && ad.x >= ad.z ) {\n"
		"		major = ad.x;\n"
		"		face = d.x >= 0.0 ? 0 : 1;\n"
		"		uv = vec2( d.x >= 0.0 ? -d.z : d.z, -d.y );\n"
		"	} else if ( ad.y >= ad.z ) {\n"
		"		major = ad.y;\n"
		"		face = d.y >= 0.0 ? 2 : 3;\n"
		"		uv = vec2( d.x, d.y >= 0.0 ? d.z : -d.z );\n"
		"	} else {\n"
		"		major = ad.z;\n"
		"		face = d.z >= 0.0 ? 4 : 5;\n"
		"		uv = vec2( d.z >= 0.0 ? d.x : -d.x, -d.y );\n"
		"	}\n"
		"	uv = clamp( uv / max( major, 1.0e-6 ) * 0.5 + 0.5, 0.0, 1.0 );\n"
		"	int cell = slot * 6 + face;\n"
		"	int size = 256 >> level;\n"
		"	vec2 texel = vec2( ivec2( cell % 8, cell / 8 ) * size ) + 0.5 + uv * float( size - 1 );\n"
		"	return textureLod( uEnvironmentAtlas, texel / float( 2048 >> level ), float( level ) ).rgb;\n"
		"}\n"
		"vec3 PBREnvironmentTile( int cell, vec2 uv, float size ) {\n"
		"	vec2 origin = vec2( cell % 8, cell / 8 ) * 256.0 + 0.5;\n"
		"	return textureLod( uEnvironmentAtlas, ( origin + clamp( uv, 0.0, 1.0 ) * ( size - 1.0 ) ) / 2048.0, 0.0 ).rgb;\n"
		"}\n"
		// Authored probes, exactly as pbr_probes.glsl selects and blends them:
		// the shared CPU top-two records of the fragment's cluster.
		"vec4 PBRProbeField( int record, int field ) {\n"
		"	return texelFetch( uProbeRecords, ivec2( field, record ), 0 );\n"
		"}\n"
		"vec3 PBRProbeToView( vec3 direction ) {\n"
		"	return vec3( dot( uProbeHeader[3].xyz, direction ), dot( uProbeHeader[4].xyz, direction ), dot( uProbeHeader[5].xyz, direction ) );\n"
		"}\n"
		"bool PBRProbeExact( float value, float low, float high ) {\n"
		"	return !isnan( value ) && !isinf( value ) && value >= low && value <= high && floor( value ) == value;\n"
		"}\n"
		"bool PBRProbeFinite( vec3 value ) { return !any( isnan( value ) ) && !any( isinf( value ) ); }\n"
		"bool PBRProbeValid( int record ) {\n"
		"	vec4 positionRadius = PBRProbeField( record, 0 );\n"
		"	vec4 tintIntensity = PBRProbeField( record, 1 );\n"
		"	vec4 axisXPriority = PBRProbeField( record, 2 );\n"
		"	vec4 axisYBlend = PBRProbeField( record, 3 );\n"
		"	vec4 axisZSlot = PBRProbeField( record, 4 );\n"
		"	vec4 identity = PBRProbeField( record, 5 );\n"
		"	vec4 box = PBRProbeField( record, 6 );\n"
		"	if ( ( box.w != 0.0 && box.w != 1.0 ) || ( box.w == 1.0 && ( !PBRProbeFinite( box.xyz )\n"
		"			|| any( lessThanEqual( box.xyz, vec3( 0.0 ) ) ) ) ) ) return false;\n"
		"	if ( !PBRProbeExact( uProbeHeader[1].w, 1.0, 16777215.0 ) || !PBRProbeExact( identity.x, 0.0, 16777215.0 )\n"
		"			|| !PBRProbeExact( identity.y, 1.0, 16777215.0 ) || !PBRProbeExact( identity.z, 1.0, 16777215.0 )\n"
		"			|| identity.w != uProbeHeader[1].w ) return false;\n"
		"	if ( !PBRProbeFinite( positionRadius.xyz ) || isnan( positionRadius.w ) || isinf( positionRadius.w ) || positionRadius.w <= 0.0 ) return false;\n"
		"	if ( !PBRProbeFinite( tintIntensity.rgb ) || any( lessThan( tintIntensity.rgb, vec3( 0.0 ) ) )\n"
		"			|| any( greaterThan( tintIntensity.rgb, vec3( 64.0 ) ) ) || isnan( tintIntensity.w ) || isinf( tintIntensity.w )\n"
		"			|| tintIntensity.w <= 0.0 || tintIntensity.w > 64.0 ) return false;\n"
		"	if ( !PBRProbeFinite( axisXPriority.xyz ) || !PBRProbeFinite( axisYBlend.xyz ) || !PBRProbeFinite( axisZSlot.xyz )\n"
		"			|| !PBRProbeExact( axisXPriority.w, 0.0, 255.0 ) || isnan( axisYBlend.w ) || isinf( axisYBlend.w )\n"
		"			|| axisYBlend.w <= 0.0 || axisYBlend.w > 1.0 || !PBRProbeExact( axisZSlot.w, 0.0, 7.0 ) ) return false;\n"
		"	float x = dot( axisXPriority.xyz, axisXPriority.xyz );\n"
		"	float y = dot( axisYBlend.xyz, axisYBlend.xyz );\n"
		"	float z = dot( axisZSlot.xyz, axisZSlot.xyz );\n"
		"	if ( min( x, min( y, z ) ) <= 0.000001 ) return false;\n"
		"	vec3 axisX = axisXPriority.xyz * inversesqrt( x );\n"
		"	vec3 axisY = axisYBlend.xyz * inversesqrt( y );\n"
		"	vec3 axisZ = axisZSlot.xyz * inversesqrt( z );\n"
		"	return abs( dot( axisX, axisY ) ) < 0.01 && abs( dot( axisX, axisZ ) ) < 0.01\n"
		"		&& abs( dot( axisY, axisZ ) ) < 0.01 && abs( dot( cross( axisX, axisY ), axisZ ) ) > 0.99;\n"
		"}\n"
		"vec3 PBRProbeDiffuse( int slot, vec3 direction ) {\n"
		"	vec3 n = direction / max( abs( direction.x ) + abs( direction.y ) + abs( direction.z ), 1.0e-6 );\n"
		"	vec2 oct = n.xy;\n"
		"	if ( n.z < 0.0 ) oct = ( 1.0 - abs( n.yx ) ) * vec2( n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0 );\n"
		"	return PBREnvironmentTile( 54 + slot, oct * 0.5 + 0.5, 32.0 );\n"
		"}\n"
		"int PBRProbeIndex( int pair ) {\n"
		"	ivec2 size = textureSize( uProbeIndices, 0 );\n"
		"	return int( texelFetch( uProbeIndices, ivec2( pair % size.x, pair / size.x ), 0 ).r );\n"
		"}\n"
		"void PBRProbeBlend( vec3 worldPosition, vec3 worldReflection, vec3 worldNormal, float roughness,\n"
		"		inout vec3 prefiltered, inout vec3 irradiance ) {\n"
		"	if ( !PBRProbeExact( uProbeHeader[0].w, 1.0, 32.0 ) ) return;\n"
		"	vec3 position = PBRProbeToView( worldPosition - uProbeHeader[2].xyz );\n"
		"	if ( !PBRProbeFinite( position ) || position.z <= 0.0 ) return;\n"
		"	vec4 projection = uProbeHeader[6];\n"
		"	vec2 ndc = vec2( -position.x * projection.x, position.y * projection.y ) / position.z - projection.zw;\n"
		"	ivec3 grid = ivec3( max( uProbeHeader[0].xyz, vec3( 1.0 ) ) );\n"
		"	ivec2 tile = clamp( ivec2( floor( ( ndc * 0.5 + 0.5 ) * vec2( grid.xy ) ) ), ivec2( 0 ), grid.xy - 1 );\n"
		"	vec4 depth = uProbeHeader[1];\n"
		"	float z = clamp( position.z, depth.x, depth.y );\n"
		"	int slice = clamp( int( floor( log( z / depth.x ) / depth.z * float( grid.z ) ) ), 0, grid.z - 1 );\n"
		"	int pair = ( ( slice * grid.y + tile.y ) * grid.x + tile.x ) * 2;\n"
		"	ivec2 size = textureSize( uProbeIndices, 0 );\n"
		"	if ( pair < 0 || pair + 1 >= size.x * size.y ) return;\n"
		"	vec3 reflection = PBRProbeToView( worldReflection );\n"
		"	vec3 normal = PBRProbeToView( worldNormal );\n"
		"	vec3 radiance = vec3( 0.0 ), diffuse = vec3( 0.0 );\n"
		"	float weightSum = 0.0;\n"
		"	int first = PBRProbeIndex( pair );\n"
		"	for ( int i = 0; i < 2; ++i ) {\n"
		"		int index = PBRProbeIndex( pair + i );\n"
		"		if ( index < 0 || float( index ) >= uProbeHeader[0].w || index >= 32 || ( i == 1 && index == first ) ) continue;\n"
		"		if ( !PBRProbeValid( index ) ) continue;\n"
		"		vec4 positionRadius = PBRProbeField( index, 0 );\n"
		"		vec4 tintIntensity = PBRProbeField( index, 1 );\n"
		"		vec4 axisXPriority = PBRProbeField( index, 2 );\n"
		"		vec4 axisYBlend = PBRProbeField( index, 3 );\n"
		"		vec4 axisZSlot = PBRProbeField( index, 4 );\n"
		"		vec4 box = PBRProbeField( index, 6 );\n"
		"		mat3 orientation = transpose( mat3( normalize( axisXPriority.xyz ), normalize( axisYBlend.xyz ), normalize( axisZSlot.xyz ) ) );\n"
		"		vec3 localPosition = orientation * ( position - positionRadius.xyz );\n"
		"		float weight = box.w > 0.5\n"
		"			? PBRBoxInfluence( localPosition.x, localPosition.y, localPosition.z, box.x, box.y, box.z, axisYBlend.w )\n"
		"			: clamp( ( positionRadius.w - length( position - positionRadius.xyz ) )\n"
		"				/ max( positionRadius.w * axisYBlend.w, 1.0e-6 ), 0.0, 1.0 );\n"
		"		if ( weight <= 0.0 ) continue;\n"
		"		vec3 localReflection = orientation * reflection;\n"
		// Box-projected parallax: look up where the reflection leaves the room.
		"		if ( box.w > 0.5 ) {\n"
		"			float exitDistance = PBRBoxParallaxDistance( localPosition.x, localPosition.y, localPosition.z,\n"
		"				localReflection.x, localReflection.y, localReflection.z, box.x, box.y, box.z );\n"
		"			if ( exitDistance > 0.0 ) localReflection = localPosition + localReflection * exitDistance;\n"
		"		}\n"
		"		float lod = roughness * 6.0;\n"
		"		int low = int( floor( lod ) ), slot = int( axisZSlot.w );\n"
		"		vec3 tint = tintIntensity.rgb * tintIntensity.w;\n"
		"		radiance += mix( PBREnvironmentLevel( slot, localReflection, low ),\n"
		"			PBREnvironmentLevel( slot, localReflection, min( low + 1, 6 ) ), fract( lod ) ) * tint * weight;\n"
		"		diffuse += PBRProbeDiffuse( slot, orientation * normal ) * tint * weight;\n"
		"		weightSum += weight;\n"
		"	}\n"
		"	if ( weightSum <= 1.0e-6 ) return;\n"
		"	float coverage = clamp( weightSum, 0.0, 1.0 );\n"
		"	prefiltered = mix( prefiltered, radiance / weightSum, coverage );\n"
		"	irradiance = mix( irradiance, diffuse / weightSum, coverage );\n"
		"}\n"
		"vec3 EvaluatePBRDiagnostic( int mode ) {\n"
		"	if ( mode == 7 ) {\n"
		"		return vec3( 0.0, 1.0, 0.0 );\n"
		"	}\n"
		"	if ( mode == 1 ) {\n"
		"		return texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb;\n"
		"	}\n"
		"	if ( mode == 2 ) {\n"
		"		return PBREnvironmentWorld( PBRObjectNormal( vBumpTexCoord ) ) * 0.5 + 0.5;\n"
		"	}\n"
		"	vec3 data = PBRMaterialData( vSpecularTexCoord );\n"
		"	return vec3( mode == 3 ? data.x : mode == 4 ? PBRRoughness( data.y ) : data.z );\n"
		"}\n"
		"void main() {\n"
		"	float alpha = OpenQ4PBRAlpha();\n"
		// A translucent surface owes the frame its coverage before any light:
		// the background keeps 1 - alpha (black composited through the alpha).
		"	if ( uPBRTransparent.y > 0.5 ) {\n"
		"		gl_FragColor = vec4( 0.0, 0.0, 0.0, alpha );\n"
		"		return;\n"
		"	}\n"
		"	int diagnostic = int( uPBREnvironment.y + 0.5 );\n"
		"	if ( diagnostic != 0 ) {\n"
		"		gl_FragColor = vec4( EvaluatePBRDiagnostic( diagnostic ), alpha );\n"
		"		return;\n"
		"	}\n"
		"	vec3 albedo = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;\n"
		"	vec3 data = PBRMaterialData( vSpecularTexCoord );\n"
		"	float metallic = data.x;\n"
		"	float roughness = PBRRoughness( data.y );\n"
		"	float ao = PBRScreenAO( data.z );\n"
		"	vec3 objectNormal = PBRObjectNormal( vBumpTexCoord );\n"
		"	vec3 dx = dFdx( objectNormal ), dy = dFdy( objectNormal );\n"
		"	if ( uPBRMode.z > 0.5 ) {\n"
		"		roughness = PBRFilteredRoughness( roughness, 0.5 * ( dot( dx, dx ) + dot( dy, dy ) ) );\n"
		"	}\n"
		"	vec3 n = PBREnvironmentWorld( objectNormal );\n"
		"	vec3 v = PBREnvironmentWorld( vViewVector );\n"
		"	float NoV = clamp( dot( n, v ), 0.0, 1.0 );\n"
		"	vec3 reflection = reflect( -v, n );\n"
		"	float lod = roughness * 6.0;\n"
		"	int low = int( floor( lod ) );\n"
		// The analytic studio environment lights only the laboratory
		// (r_pbrAnalyticEnvironment); production starts from black and takes
		// its environment from authored probes and baked light grids.
		"	vec3 prefiltered = mix( PBREnvironmentLevel( 8, reflection, low ),\n"
		"		PBREnvironmentLevel( 8, reflection, min( low + 1, 6 ) ), fract( lod ) ) * uPBREnvironment.w;\n"
		"	vec3 octNormal = n / max( abs( n.x ) + abs( n.y ) + abs( n.z ), 1.0e-6 );\n"
		"	vec2 oct = octNormal.xy;\n"
		"	if ( n.z < 0.0 ) {\n"
		"		oct = ( 1.0 - abs( octNormal.yx ) ) * vec2( n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0 );\n"
		"	}\n"
		"	vec3 irradiance = PBREnvironmentTile( 54 + 8, oct * 0.5 + 0.5, 32.0 ) * uPBREnvironment.w;\n"
		"	PBRProbeBlend( vWorldPosition, reflection, n, roughness, prefiltered, irradiance );\n"
		"	vec2 brdf = PBREnvironmentTile( 63, vec2( NoV, roughness ), 128.0 ).rg;\n"
		"	vec3 f0 = mix( vec3( 0.04 ), albedo, metallic );\n"
		"	vec3 fresnel = f0 + ( max( vec3( 1.0 - roughness ), f0 ) - f0 ) * PBRFresnelWeight( NoV );\n"
		// AO is indirect visibility: diffuse takes its multi-bounce form, the
		// specular cone its own occlusion and bounce off F0, faded where the
		// normal map turns the reflection below the geometric surface.
		"	vec3 diffuseColor = ( 1.0 - metallic ) * albedo;\n"
		"	vec3 diffuseAO = PBRMultiBounceAOColor( ao, diffuseColor );\n"
		"	vec3 specularAO = PBRMultiBounceAOColor( PBRSpecularOcclusion( NoV, ao, roughness ), f0 )\n"
		"		* PBRHorizonOcclusion( dot( reflection, PBREnvironmentWorld( vPBRNormal ) ) );\n"
		"	vec3 diffuse = ( 1.0 - fresnel ) * diffuseColor * irradiance * diffuseAO;\n"
		"	vec3 specular = prefiltered * ( f0 * brdf.x + brdf.y )\n"
		"		* PBREnergyCompensationColor( f0, brdf.x + brdf.y ) * specularAO;\n"
		// A baked light grid replaces environment diffuse with its own
		// irradiance and keeps the reflections (pbr_environment.glsl).
		"	vec4 baked = ModernBakedIrradiance( vWorldPosition, n, true );\n"
		"	if ( baked.x >= 0.0 ) {\n"
		"		diffuse = ModernBakedClamp( ( 1.0 - fresnel ) * diffuseColor * baked.rgb * diffuseAO * vVertexColor, baked.w );\n"
		"		gl_FragColor = vec4( PBRDisplayOutput( diffuse + specular * uPBREnvironment.x * vVertexColor ), alpha );\n"
		"		return;\n"
		"	}\n"
		"	gl_FragColor = vec4( PBRDisplayOutput( ( diffuse + specular ) * uPBREnvironment.x * vVertexColor ), alpha );\n"
		"}\n";
	return glPBREnvironmentFragment.c_str();
}

static bool RB_GLPBR_LoadEnvironmentProgram( void ) {
	glPBRProgram_t &program = g_glPBREnvironment;
	if ( RB_GLPBR_ProgramCurrent( program ) ) {
		return program.valid;
	}
	RB_GLPBR_FreeProgram( program );
	program.attempted = true;
	program.generation = tr.videoRestartCount;
	if ( !RB_GLPBR_LinkProgram( program, glPBREnvironmentVertex, RB_GLPBR_EnvironmentFragment(), NULL, NULL,
			RB_GLPBR_VertexLibrary(), NULL, "builtin/pbr_environment" ) ) {
		RB_GLPBR_FreeProgram( program );
		program.attempted = true;
		program.generation = tr.videoRestartCount;
		return false;
	}
	RB_GLPBR_LookupInteractionUniforms( program );
	program.valid = true;
	return true;
}

/*
Emission: the typed linear emission in place of the classic glow stage, the
same image under the stage's own texture coordinates, encoded like every PBR
draw (gui.frag mode 4 on Vulkan). Vertex tint is ignored, as on Vulkan.
*/
static const char *glPBREmissionVertex =
	"#version 110\n"
	"attribute vec2 attr_TexCoord0;\n"
	"uniform vec4 uTextureMatrixS;\n"
	"uniform vec4 uTextureMatrixT;\n"
	"varying vec2 vTexCoord;\n"
	"void main() {\n"
	"	vec4 texCoord = vec4( attr_TexCoord0.xy, 0.0, 1.0 );\n"
	"	vTexCoord = vec2( dot( texCoord, uTextureMatrixS ), dot( texCoord, uTextureMatrixT ) );\n"
	"	gl_Position = ftransform();\n"
	"}\n";

static idStr glPBREmissionFragment;

static const char *RB_GLPBR_EmissionFragment( void ) {
	if ( glPBREmissionFragment.Length() > 0 ) {
		return glPBREmissionFragment.c_str();
	}
	glPBREmissionFragment =
		"#version 110\n"
		"uniform sampler2D uEmissiveMap;\n"
		"uniform vec4 uEmissiveColor;\n"	// rgb linear strength, w display-referred
		"varying vec2 vTexCoord;\n";
	glPBREmissionFragment += OPENQ4_PBR_SCALAR_GLSL;
	glPBREmissionFragment +=
		"\nvoid main() {\n"
		"	vec3 radiance = min( texture2D( uEmissiveMap, vTexCoord ).rgb * uEmissiveColor.rgb, vec3( 65504.0 ) );\n"
		"	if ( uEmissiveColor.w > 0.5 ) {\n"
		"		radiance = vec3( PBRLinearToSRGBExtended( radiance.r ),\n"
		"			PBRLinearToSRGBExtended( radiance.g ), PBRLinearToSRGBExtended( radiance.b ) );\n"
		"	}\n"
		"	gl_FragColor = vec4( radiance, 1.0 );\n"
		"}\n";
	return glPBREmissionFragment.c_str();
}

static bool RB_GLPBR_LoadEmissionProgram( void ) {
	glPBRProgram_t &program = g_glPBREmission;
	if ( RB_GLPBR_ProgramCurrent( program ) ) {
		return program.valid;
	}
	RB_GLPBR_FreeProgram( program );
	program.attempted = true;
	program.generation = tr.videoRestartCount;
	if ( !RB_GLPBR_LinkProgram( program, glPBREmissionVertex, RB_GLPBR_EmissionFragment(), NULL, NULL,
			NULL, NULL, "builtin/pbr_emission" ) ) {
		RB_GLPBR_FreeProgram( program );
		program.attempted = true;
		program.generation = tr.videoRestartCount;
		return false;
	}
	RB_GLPBR_LookupInteractionUniforms( program );
	program.valid = true;
	return true;
}

/*
Authored probes: the shared CPU clustering (R_ModernClusteredLighting_PrepareProbes,
also Vulkan's source) selects the top two records per cluster; each view's
records and index pairs are uploaded as small float textures, since the
GLSL 1.30 environment program has no buffer blocks.
*/
static std::vector<rendererSpecularProbeView_t> g_glPBRProbeViews;
static std::uint64_t	g_glPBRProbeGeneration;
static GLuint			g_glPBRProbeRecordTexture;
static GLuint			g_glPBRProbeIndexTexture;
static const viewDef_t *g_glPBRProbeUploadedView;
static int				g_glPBRProbeUploadedFrame = -1;
static int				g_glPBRProbeFrameViews;

// The environment, emission and translucent programs are GLSL 1.30. Without
// it (Apple's GL 2.1 context stops at 1.20) PBR materials keep their classic
// stages: receivers alone would light them with no indirect light at all.
static bool RB_GLPBR_ContextSupported( void ) {
	return glConfig.GLSLProgramAvailable && glConfig.GLSL130Available;
}

void RB_GLPBR_PrepareFrame( const idScenePacketFrame &frame ) {
	g_glPBRProbeViews.clear();
	g_glPBRProbeUploadedView = NULL;
	g_glPBRProbeFrameViews = 0;
	if ( !r_glPBR.GetBool() || !RB_GLPBR_ContextSupported() || !r_rendererModernQuality.GetBool()
			|| !r_pbrMaterials.GetBool() || !r_pbrIBL.GetBool() || r_pbrIBLIntensity.GetFloat() <= 0.0f ) {
		return;
	}
	// The classic owner begins the atlas frame itself when the modern
	// executor does not run; allocation generates the analytic environment.
	R_ModernSpecularProbeAtlas_BeginFrame( true );
	if ( r_rendererReflectionProbes.GetBool() && R_ModernSpecularProbeAtlas_Ready() ) {
		if ( ++g_glPBRProbeGeneration > 0x00ffffffull ) {
			g_glPBRProbeGeneration = 1;
		}
		rendererClusteredLightingStats_t stats;
		if ( !R_ModernClusteredLighting_PrepareProbes( frame, R_ModernSpecularProbeAtlas_Acquire,
				g_glPBRProbeGeneration, g_glPBRProbeViews, stats ) ) {
			g_glPBRProbeViews.clear();	// the whole set falls back to the analytic environment
		}
	}
	R_ModernSpecularProbeAtlas_FlushUploads( R_ModernSpecularProbeAtlas_CaptureFrame( frame ) );
	if ( !g_glPBRProbeViews.empty() && !R_ModernSpecularProbeAtlas_FrameReady() ) {
		g_glPBRProbeViews.clear();
	}
	g_glPBRProbeFrameViews = static_cast<int>( g_glPBRProbeViews.size() );
}

void RB_GLPBR_AdoptExecutorFrame( void ) {
	g_glPBRProbeViews.clear();
	g_glPBRProbeUploadedView = NULL;
	g_glPBRProbeFrameViews = 0;
	if ( !r_glPBR.GetBool() || !RB_GLPBR_ContextSupported() || !r_rendererModernQuality.GetBool()
			|| !r_pbrMaterials.GetBool() || !r_pbrIBL.GetBool() || r_pbrIBLIntensity.GetFloat() <= 0.0f
			|| !r_rendererReflectionProbes.GetBool() ) {
		return;
	}
	// Authored probes start the modern executor as a sidecar, which builds the
	// probe clusters and fills the atlas this frame; reuse them, never rebuild.
	if ( ++g_glPBRProbeGeneration > 0x00ffffffull ) {
		g_glPBRProbeGeneration = 1;
	}
	if ( !R_ModernClusteredLighting_ExportProbeViews( g_glPBRProbeGeneration, g_glPBRProbeViews )
			|| !R_ModernSpecularProbeAtlas_FrameReady() ) {
		g_glPBRProbeViews.clear();
	}
	g_glPBRProbeFrameViews = static_cast<int>( g_glPBRProbeViews.size() );
}

static const rendererSpecularProbeView_t *RB_GLPBR_ProbeView( const viewDef_t *viewDef ) {
	for ( const rendererSpecularProbeView_t &view : g_glPBRProbeViews ) {
		if ( view.viewDef == viewDef ) {
			return &view;
		}
	}
	return NULL;
}

static bool RB_GLPBR_UploadProbeView( const rendererSpecularProbeView_t &view ) {
	if ( g_glPBRProbeUploadedView == view.viewDef && g_glPBRProbeUploadedFrame == backEnd.frameCount ) {
		return true;
	}
	if ( g_glPBRProbeRecordTexture == 0 ) {
		glGenTextures( 1, &g_glPBRProbeRecordTexture );
	}
	if ( g_glPBRProbeIndexTexture == 0 ) {
		glGenTextures( 1, &g_glPBRProbeIndexTexture );
	}
	if ( g_glPBRProbeRecordTexture == 0 || g_glPBRProbeIndexTexture == 0 ) {
		return false;
	}
	static_assert( sizeof( rendererSpecularProbeRecord_t ) == 7 * 4 * sizeof( float ), "seven texels per probe record" );
	const int pairs = static_cast<int>( view.indices.size() );
	const int width = GL_PBR_PROBE_INDEX_WIDTH;
	const int height = Max( 1, ( pairs + width - 1 ) / width );
	idList<float> indexTexels;
	indexTexels.SetNum( width * height );
	for ( int i = 0; i < width * height; ++i ) {
		indexTexels[i] = i < pairs && view.indices[i] != 0xffffffffu ? static_cast<float>( view.indices[i] ) : -1.0f;
	}
	glActiveTextureARB( GL_TEXTURE0_ARB + GL_PBR_UNIT_PROBE_RECORDS );
	glBindTexture( GL_TEXTURE_2D, g_glPBRProbeRecordTexture );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, 7, RENDERER_CLUSTER_SPECULAR_PROBE_MAX_RECORDS, 0,
		GL_RGBA, GL_FLOAT, view.records );
	glActiveTextureARB( GL_TEXTURE0_ARB + GL_PBR_UNIT_PROBE_INDICES );
	glBindTexture( GL_TEXTURE_2D, g_glPBRProbeIndexTexture );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, indexTexels.Ptr() );
	glActiveTextureARB( GL_TEXTURE0_ARB + Max( 0, backEnd.glState.currenttmu ) );
	g_glPBRProbeUploadedView = view.viewDef;
	g_glPBRProbeUploadedFrame = backEnd.frameCount;
	return true;
}

void RB_GLPBR_Shutdown( void ) {
	g_glPBRProbeViews.clear();
	g_glPBRProbeUploadedView = NULL;
	if ( glDeleteTextures != NULL ) {
		if ( g_glPBRProbeRecordTexture != 0 ) {
			glDeleteTextures( 1, &g_glPBRProbeRecordTexture );
		}
		if ( g_glPBRProbeIndexTexture != 0 ) {
			glDeleteTextures( 1, &g_glPBRProbeIndexTexture );
		}
	}
	g_glPBRProbeRecordTexture = 0;
	g_glPBRProbeIndexTexture = 0;
	if ( glDeleteObjectARB == NULL ) {
		memset( &g_glPBRUnshadowed, 0, sizeof( g_glPBRUnshadowed ) );
		memset( &g_glPBREnvironment, 0, sizeof( g_glPBREnvironment ) );
		memset( &g_glPBREmission, 0, sizeof( g_glPBREmission ) );
		return;
	}
	RB_GLPBR_FreeProgram( g_glPBRUnshadowed );
	RB_GLPBR_FreeProgram( g_glPBREnvironment );
	RB_GLPBR_FreeProgram( g_glPBREmission );
}

/*
===============================================================================

Admission

===============================================================================
*/

static bool g_glPBRNativePass;
static bool g_glPBRTranslucentReplay;	// a blend stage is replaying its lights
static float g_glPBRDrawAlphaScale;		// the replayed stage's alpha scale, else 0
static bool g_glPBRViewOwned;	// the classic path owns this view's lighting
static bool g_glPBRTranslucentView;	// ...and its translucent PBR surfaces

// One-entry cache: a chain draw admits a surface and immediately binds it.
static const drawSurf_t *	g_glPBRCachedSurf;
static int					g_glPBRCachedFrame;
static bool					g_glPBRCachedOwned;
static pbrNativeMaterial_t	g_glPBRCachedMaterial;

/*
A translucent surface never reaches the depth fill, so its classic lighting
is added in the light loop and its authored source-alpha stage composites
later. The native owner does both at once at the stage: the background keeps
1 - alpha and every light adds alpha times its radiance. Replaying a light
after the light loop cannot reproduce its stencil or per-light shadow-map
coverage, so -- exactly as on Vulkan (VK_PBRTransparentViewAdmits) -- a view
whose shadow-casting light reaches a translucent receiver keeps classic
ownership of every translucent surface, lighting included.
*/
static bool RB_GLPBR_TranslucentViewAdmits( const viewDef_t *viewDef, const char *&reason ) {
	if ( !r_rendererModernQuality.GetBool() || !r_pbrMaterials.GetBool()
			|| r_skipBump.GetBool() || r_skipDiffuse.GetBool() || r_skipSpecular.GetBool() ) {
		reason = "disabled";
		return false;
	}
	for ( const viewLight_t *vLight = viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		if ( vLight->translucentInteractions == NULL || vLight->lightShader == NULL ) {
			continue;
		}
		const bool hasCasters = vLight->globalShadows != NULL
				|| vLight->globalShadowMapCasters != NULL
				|| vLight->globalShadowMapDynamicCasters != NULL
				|| vLight->localShadows != NULL
				|| vLight->localShadowMapCasters != NULL
				|| vLight->localShadowMapDynamicCasters != NULL
				|| vLight->shadowMapIncompleteMapMask != 0
				|| vLight->shadowMapPrelightMapMissingMask != 0;
		if ( r_shadows.GetBool() && vLight->lightShader->LightCastsShadows()
				&& ( vLight->lightDef == NULL || !vLight->lightDef->parms.noShadows )
				&& hasCasters ) {
			reason = "shadows";
			return false;
		}
	}
	reason = "ready";
	return true;
}

void RB_GLPBR_BeginView( void ) {
	RB_GLPBR_TouchStats();
	g_glPBRCachedSurf = NULL;
	// The modern visible path owns its own PBR when it takes the light pass.
	g_glPBRViewOwned = backEnd.viewDef != NULL && backEnd.viewDef->viewEntitys != NULL
		&& !RB_GLSLInteractionsOwnedByModernPath();
	const char *reason = "view";
	g_glPBRTranslucentView = g_glPBRViewOwned && RB_GLPBR_TranslucentViewAdmits( backEnd.viewDef, reason );
	g_glPBRStats.translucentView = reason;
}

static bool RB_GLPBR_ResourcesAvailable( void ) {
	if ( !r_glPBR.GetBool() ) {
		return false;
	}
	if ( !RB_GLPBR_ContextSupported() ) {
		static int notedRestart = -1;
		if ( notedRestart != tr.videoRestartCount ) {
			notedRestart = tr.videoRestartCount;
			common->Printf( "OpenGL: native PBR needs GLSL 1.30, which this context lacks; PBR materials keep their classic stages\n" );
		}
		return false;
	}
	return glConfig.maxTextureImageUnits >= GL_PBR_REQUIRED_IMAGE_UNITS
		&& RB_GLPBR_LoadUnshadowedProgram();
}

static bool RB_GLPBR_Translucent( const drawSurf_t *surf ) {
	return surf != NULL && surf->material != NULL && surf->material->Coverage() == MC_TRANSLUCENT;
}

static bool RB_GLPBR_Admit( const drawSurf_t *surf, pbrNativeMaterial_t *materialOut ) {
	if ( surf == NULL || surf->material == NULL || !surf->material->HasPBR() ) {
		return false;
	}
	if ( surf == g_glPBRCachedSurf && g_glPBRCachedFrame == backEnd.frameCount ) {
		if ( materialOut != NULL ) {
			*materialOut = g_glPBRCachedMaterial;
		}
		return g_glPBRCachedOwned;
	}
	RB_GLPBR_TouchStats();
	const char *reason = NULL;
	pbrNativeMaterial_t material;
	memset( &material, 0, sizeof( material ) );
	if ( !g_glPBRViewOwned ) {
		reason = "view";
	} else if ( !RB_GLPBR_ResourcesAvailable() ) {
		reason = "resources";
	} else if ( RB_GLSLSurfaceUsesGPUPosedGeometry( surf ) ) {
		reason = "gpu-posed-geometry";
	} else if ( !R_PBRNative_Material( surf, g_glPBRTranslucentView, material ) ) {
		reason = "material-contract";
	} else if ( RB_GLPBR_Translucent( surf ) && !RB_GLPBR_LoadEnvironmentProgram() ) {
		// Its coverage composite draws with the environment program.
		reason = "resources";
	}
	g_glPBRCachedSurf = surf;
	g_glPBRCachedFrame = backEnd.frameCount;
	g_glPBRCachedOwned = reason == NULL;
	g_glPBRCachedMaterial = material;
	if ( reason != NULL ) {
		g_glPBRStats.declined++;
		g_glPBRStats.lastDecline = reason;
	} else {
		g_glPBRStats.admitted++;
	}
	if ( materialOut != NULL ) {
		*materialOut = material;
	}
	return reason == NULL;
}

bool RB_GLPBR_SurfaceOwned( const drawSurf_t *surf ) {
	return RB_GLPBR_Admit( surf, NULL );
}

bool RB_GLPBR_NativePassActive( void ) {
	return g_glPBRNativePass;
}

void RB_GLPBR_BeginNativePass( void ) {
	g_glPBRNativePass = true;
}

void RB_GLPBR_EndNativePass( void ) {
	g_glPBRNativePass = false;
}

bool RB_GLPBR_DecompositionSkips( const drawSurf_t *surf ) {
	if ( g_glPBRTranslucentReplay ) {
		return !RB_GLPBR_SurfaceOwned( surf );
	}
	if ( g_glPBRNativePass ) {
		// An owned translucent surface draws its lights at its blend stage.
		return !RB_GLPBR_SurfaceOwned( surf ) || RB_GLPBR_Translucent( surf );
	}
	return surf != NULL && surf->material != NULL && surf->material->HasPBR() && RB_GLPBR_SurfaceOwned( surf );
}

bool RB_GLPBR_ChainHasOwned( const drawSurf_t *chain ) {
	for ( const drawSurf_t *surf = chain; surf != NULL; surf = surf->nextOnLight ) {
		if ( surf->material != NULL && surf->material->HasPBR() && !RB_GLPBR_Translucent( surf )
				&& RB_GLPBR_SurfaceOwned( surf ) ) {
			return true;
		}
	}
	return false;
}

/*
===============================================================================

Per-light draws

===============================================================================
*/

static void RB_GLPBR_BindRawUnit( int unit, GLenum target, GLuint texture ) {
	// Units above the eight tracked by idImage::Bind bind directly; restore
	// the tracked active unit afterwards.
	glActiveTextureARB( GL_TEXTURE0_ARB + unit );
	glBindTexture( target, texture );
	glActiveTextureARB( GL_TEXTURE0_ARB + Max( 0, backEnd.glState.currenttmu ) );
}

static void RB_GLPBR_BindImage( int unit, idImage *image ) {
	GL_SelectTextureNoClient( unit );
	image->Bind();
}

static void RB_GLPBR_SetMaterialUniforms( const glPBRReceiverUniforms_t &uniforms, const pbrNativeMaterial_t &material,
		bool ambientLight, float alphaScale = 0.0f, bool coverageOnly = false ) {
	if ( uniforms.transparent >= 0 ) {
		// Program state persists: every draw states its own blend contract.
		glUniform4fARB( uniforms.transparent, alphaScale, coverageOnly ? 1.0f : 0.0f, 0.0f, 0.0f );
	}
	if ( uniforms.material >= 0 ) {
		glUniform4fARB( uniforms.material, material.metallic, material.roughness, material.normalScale, material.ao );
	}
	if ( uniforms.layout >= 0 ) {
		glUniform4fARB( uniforms.layout, ( material.dataFlags & 1 ) ? 1.0f : 0.0f, ( material.dataFlags & 2 ) ? 1.0f : 0.0f,
			( material.dataFlags & 4 ) ? 1.0f : 0.0f, material.aoImage != NULL ? 1.0f : 0.0f );
	}
	if ( uniforms.mode >= 0 ) {
		// The classic framebuffer is display-referred on every OpenGL path
		// this owner serves, with and without HDR tone mapping.
		glUniform4fARB( uniforms.mode, static_cast<float>( material.normalFormat ), 1.0f, 1.0f, ambientLight ? 1.0f : 0.0f );
	}
	if ( uniforms.screenAO >= 0 ) {
		// The field describes the opaque surface in front: a translucent or
		// coverage draw never reads it.
		idImage *field = NULL;
		float invSize[2] = { 0.0f, 0.0f };
		const bool screenAO = alphaScale <= 0.0f && !coverageOnly && RB_SSAOIndirectField( &field, invSize );
		glUniform4fARB( uniforms.screenAO, screenAO ? 1.0f : 0.0f, invSize[0], invSize[1], 0.0f );
		RB_GLPBR_BindRawUnit( GL_PBR_UNIT_SCREEN_AO, GL_TEXTURE_2D,
			( screenAO ? field : globalImages->whiteImage )->GetDeviceHandle() );
	}
}

static void RB_GLPBR_BindMaterialImages( const pbrNativeMaterial_t &material ) {
	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_METALLIC, GL_TEXTURE_2D,
		( material.metallicImage != NULL ? material.metallicImage : globalImages->whiteImage )->GetDeviceHandle() );
	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_AO, GL_TEXTURE_2D,
		( material.aoImage != NULL ? material.aoImage : globalImages->whiteImage )->GetDeviceHandle() );
	RB_GLPBR_BindImage( GL_PBR_UNIT_DATA, material.dataImage );
	RB_GLPBR_BindImage( GL_PBR_UNIT_ALBEDO, material.albedoImage );
	RB_GLPBR_BindImage( GL_PBR_UNIT_NORMAL, material.normalImage );
}

const srfTriangles_t *RB_GLPBR_BindReceiverInteraction( const glPBRReceiverUniforms_t &uniforms, const drawInteraction_t *din ) {
	pbrNativeMaterial_t material;
	RB_GLPBR_Admit( din->surf, &material );
	RB_GLPBR_SetMaterialUniforms( uniforms, material, din->ambientLight, g_glPBRDrawAlphaScale );
	RB_GLPBR_BindMaterialImages( material );
	// Classic light triangles drop faces turned from the light; a PBR surface
	// shades every face its interpolated normals can light (Interaction.cpp).
	return din->surf->pbrLightGeo != NULL && !din->ambientLight ? din->surf->pbrLightGeo : din->surf->geo;
}

void RB_GLPBR_UnbindReceiverUnits( void ) {
	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_METALLIC, GL_TEXTURE_2D, 0 );
	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_AO, GL_TEXTURE_2D, 0 );
	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_SCREEN_AO, GL_TEXTURE_2D, 0 );
}

static void RB_GLPBR_SetVertexColorParams( GLint location, stageVertexColor_t vertexColor ) {
	if ( location < 0 ) {
		return;
	}
	float modulate = 0.0f;
	float add = 1.0f;
	if ( vertexColor == SVC_MODULATE ) {
		modulate = 1.0f;
		add = 0.0f;
	} else if ( vertexColor == SVC_INVERSE_MODULATE ) {
		modulate = -1.0f;
		add = 1.0f;
	}
	glUniform2fARB( location, modulate, add );
}

static void RB_GLPBR_DrawUnshadowedInteraction( const drawInteraction_t *din ) {
	const glPBRProgram_t &program = g_glPBRUnshadowed;
	glUniform4fvARB( program.localLightOrigin, 1, din->localLightOrigin.ToFloatPtr() );
	glUniform4fvARB( program.localViewOrigin, 1, din->localViewOrigin.ToFloatPtr() );
	glUniform4fvARB( program.lightProjectionS, 1, din->lightProjection[0].ToFloatPtr() );
	glUniform4fvARB( program.lightProjectionT, 1, din->lightProjection[1].ToFloatPtr() );
	glUniform4fvARB( program.lightProjectionQ, 1, din->lightProjection[2].ToFloatPtr() );
	glUniform4fvARB( program.lightFalloffS, 1, din->lightProjection[3].ToFloatPtr() );
	glUniform4fvARB( program.bumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr() );
	glUniform4fvARB( program.bumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr() );
	glUniform4fvARB( program.diffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr() );
	glUniform4fvARB( program.diffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr() );
	glUniform4fvARB( program.specularMatrixS, 1, din->specularMatrix[0].ToFloatPtr() );
	glUniform4fvARB( program.specularMatrixT, 1, din->specularMatrix[1].ToFloatPtr() );
	glUniform4fvARB( program.diffuseColor, 1, din->diffuseColor.ToFloatPtr() );
	RB_GLPBR_SetVertexColorParams( program.vertexColorParams, din->vertexColor );

	GL_SelectTextureNoClient( GL_PBR_UNIT_FALLOFF );
	din->lightFalloffImage->Bind();
	GL_SelectTextureNoClient( GL_PBR_UNIT_PROJECTION );
	din->lightImage->Bind();
	const srfTriangles_t *geo = RB_GLPBR_BindReceiverInteraction( program.pbr, din );

	const idMaterial *material = din->surf->material;
	if ( material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * material->GetPolygonOffset() );
	}
	RB_DrawElementsWithCounters( geo );
	if ( material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( g_glPBRTranslucentReplay ) {
		g_glPBRStats.transparentLights++;
	} else {
		g_glPBRStats.interactions++;
	}
}

static void RB_GLPBR_SetSurfaceVertexPointers( idDrawVert *ac ) {
	glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET ) );
	glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
	glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
	glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
	glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );
}

static void RB_GLPBR_EnableVertexArrays( bool enable ) {
	for ( int attribute = 8; attribute <= 11; ++attribute ) {
		if ( enable ) {
			glEnableVertexAttribArrayARB( attribute );
		} else {
			glDisableVertexAttribArrayARB( attribute );
		}
	}
	if ( enable ) {
		glEnableClientState( GL_COLOR_ARRAY );
	} else {
		glDisableClientState( GL_COLOR_ARRAY );
	}
}

static void RB_GLPBR_ReleaseProgramState( void ) {
	RB_GLPBR_UnbindReceiverUnits();
	for ( int unit = GL_PBR_UNIT_DATA; unit >= 0; --unit ) {
		GL_SelectTextureNoClient( unit );
		globalImages->BindNull();
	}
	glUseProgramObjectARB( 0 );
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
}

void RB_GLPBR_DrawInteractionChain( const drawSurf_t *chain ) {
	if ( chain == NULL || r_pbrDebug.GetInteger() != 0 || !RB_GLPBR_ChainHasOwned( chain ) ) {
		// Diagnostic views draw once per surface in the ambient walk.
		return;
	}
	const glPBRProgram_t &program = g_glPBRUnshadowed;
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( program.program );
	RB_GLPBR_EnableVertexArrays( true );
	RB_GLPBR_BeginNativePass();
	for ( const drawSurf_t *surf = chain; surf != NULL; surf = surf->nextOnLight ) {
		if ( !RB_GLPBR_SurfaceOwned( surf ) ) {
			continue;
		}
		idDrawVert *ac = NULL;
		if ( !RB_GLSLInteractionVertexCache( surf, ac ) ) {
			continue;
		}
		RB_GLPBR_SetSurfaceVertexPointers( ac );
		RB_CreateSingleDrawInteractions( surf, RB_GLPBR_DrawUnshadowedInteraction );
	}
	RB_GLPBR_EndNativePass();
	RB_GLPBR_EnableVertexArrays( false );
	RB_GLPBR_ReleaseProgramState();
}

/*
===============================================================================

Ambient walk

===============================================================================
*/

static bool RB_GLPBR_SetEnvironmentMatrices( const glPBRProgram_t &program, const drawSurf_t *surf, stageVertexColor_t &vertexColor ) {
	vertexColor = SVC_IGNORE;
	bool haveDiffuse = false;
	for ( int i = 0; i < surf->material->GetNumStages(); ++i ) {
		const shaderStage_t *stage = surf->material->GetStage( i );
		GLint s = -1, t = -1;
		float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		if ( stage->lighting == SL_BUMP ) {
			s = program.bumpMatrixS;
			t = program.bumpMatrixT;
		} else if ( stage->lighting == SL_DIFFUSE ) {
			s = program.diffuseMatrixS;
			t = program.diffuseMatrixT;
			vertexColor = stage->vertexColor;
		} else if ( stage->lighting == SL_SPECULAR ) {
			s = program.specularMatrixS;
			t = program.specularMatrixT;
		} else {
			continue;
		}
		idImage *unused = NULL;
		idVec4 matrix[2];
		R_SetDrawInteraction( stage, surf->shaderRegisters, &unused, matrix, color );
		if ( s >= 0 ) {
			glUniform4fvARB( s, 1, matrix[0].ToFloatPtr() );
		}
		if ( t >= 0 ) {
			glUniform4fvARB( t, 1, matrix[1].ToFloatPtr() );
		}
		if ( stage->lighting == SL_DIFFUSE && program.diffuseColor >= 0 ) {
			glUniform4fvARB( program.diffuseColor, 1, color );
			haveDiffuse = true;
		}
	}
	return haveDiffuse;
}

static bool RB_GLPBR_EnvironmentAtlasReady( void ) {
	// The modern executor begins the atlas frame when it runs; the classic
	// owner allocates it once (the analytic environment and the split-sum
	// table are generated at allocation) and then only samples it.
	if ( !R_ModernSpecularProbeAtlas_Ready() ) {
		R_ModernSpecularProbeAtlas_BeginFrame( true );
	}
	return R_ModernSpecularProbeAtlas_Ready() && R_ModernSpecularProbeAtlas_Texture() != 0;
}

static int RB_GLPBR_DiagnosticView( void ) {
	const int diagnostic = r_pbrDebug.GetInteger();
	return ( diagnostic >= 1 && diagnostic <= 5 ) || diagnostic == 7 ? diagnostic : 0;
}

/*
Production environment lighting is the map's own: authored probes, and a baked
light grid's irradiance in place of environment diffuse. The analytic studio
environment lights only the laboratory (r_pbrAnalyticEnvironment). A surface
with none of them draws no environment pass at all.
*/
static bool RB_GLPBR_Illuminated( void ) {
	return r_pbrIBL.GetBool() && r_pbrIBLIntensity.GetFloat() > 0.0f
		&& ( r_pbrAnalyticEnvironment.GetBool() || RB_GLPBR_ProbeView( backEnd.viewDef ) != NULL );
}

static const LightGrid *RB_GLPBR_SurfaceGrid( const drawSurf_t *surf ) {
	if ( !r_useLightGrid.GetBool() || r_skipDiffuse.GetBool() || r_pbrDebug.GetInteger() != 0 ) {
		return NULL;
	}
	// The modern receiver contract: no grid (or a rejected one, such as a
	// surface beside a blending portal or a depth-hacked weapon) leaves the
	// environment diffuse, never the classic grid pass.
	const LightGrid *grid = NULL;
	return RB_PrepareModernLightGrid( surf, backEnd.viewDef, grid ) ? grid : NULL;
}

/*
One draw of the environment program over the surface's ambient geometry: the
environment term, a material diagnostic (diagnostic != 0) or, for a
translucent surface, its coverage alone. alphaScale > 0 composites through the
authored coverage under the given blend; zero keeps the additive contract.
*/
static void RB_GLPBR_EnvironmentPass( const drawSurf_t *surf, const pbrNativeMaterial_t &material, idDrawVert *ac,
		int diagnostic, bool coverageOnly, float alphaScale, int stateBits, const LightGrid *grid = NULL ) {
	const glPBRProgram_t &program = g_glPBREnvironment;
	const bool diagnosticView = diagnostic != 0;
	const bool lit = !diagnosticView && !coverageOnly;
	const bool illuminated = lit && RB_GLPBR_Illuminated();
	grid = lit ? grid : NULL;
	GL_State( stateBits );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( program.program );

	idVec3 localView;
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localView );
	glUniform4fARB( program.localViewOrigin, localView.x, localView.y, localView.z, 1.0f );
	for ( int row = 0; row < 3; ++row ) {
		float worldRow[3];
		for ( int column = 0; column < 3; ++column ) {
			// Diagnostic normals use the modern view basis (pbr_debug.glsl).
			worldRow[column] = diagnosticView
				? surf->space->modelViewMatrix[column * 4 + row] * ( row == 1 ? 1.0f : -1.0f )
				: surf->space->modelMatrix[column * 4 + row];
		}
		if ( program.objectToWorld[row] >= 0 ) {
			glUniform3fvARB( program.objectToWorld[row], 1, worldRow );
		}
	}
	for ( int row = 0; row < 3; ++row ) {
		if ( program.modelMatrixRow[row] >= 0 ) {
			const float *m = surf->space->modelMatrix;
			glUniform4fARB( program.modelMatrixRow[row], m[0 * 4 + row], m[1 * 4 + row], m[2 * 4 + row], m[3 * 4 + row] );
		}
	}
	float probeHeader[7][4];
	memset( probeHeader, 0, sizeof( probeHeader ) );
	const rendererSpecularProbeView_t *probeView = illuminated ? RB_GLPBR_ProbeView( backEnd.viewDef ) : NULL;
	if ( probeView != NULL && RB_GLPBR_UploadProbeView( *probeView ) ) {
		memcpy( probeHeader[0], probeView->grid, sizeof( probeHeader[0] ) );
		memcpy( probeHeader[1], probeView->depth, sizeof( probeHeader[1] ) );
		memcpy( probeHeader[2], probeView->viewOrigin, sizeof( probeHeader[2] ) );
		memcpy( probeHeader[3], probeView->worldToView[0], sizeof( probeHeader[3] ) );
		memcpy( probeHeader[4], probeView->worldToView[1], sizeof( probeHeader[4] ) );
		memcpy( probeHeader[5], probeView->worldToView[2], sizeof( probeHeader[5] ) );
		memcpy( probeHeader[6], probeView->projection, sizeof( probeHeader[6] ) );
		RB_GLPBR_BindRawUnit( GL_PBR_UNIT_PROBE_RECORDS, GL_TEXTURE_2D, g_glPBRProbeRecordTexture );
		RB_GLPBR_BindRawUnit( GL_PBR_UNIT_PROBE_INDICES, GL_TEXTURE_2D, g_glPBRProbeIndexTexture );
	} else {
		// grid.w = 0: no authored record applies; keep valid textures bound.
		RB_GLPBR_BindRawUnit( GL_PBR_UNIT_PROBE_RECORDS, GL_TEXTURE_2D, R_ModernSpecularProbeAtlas_Texture() );
		RB_GLPBR_BindRawUnit( GL_PBR_UNIT_PROBE_INDICES, GL_TEXTURE_2D, R_ModernSpecularProbeAtlas_Texture() );
	}
	if ( program.probeHeader >= 0 ) {
		glUniform4fvARB( program.probeHeader, 7, probeHeader[0] );
	}
	stageVertexColor_t vertexColor = SVC_IGNORE;
	RB_GLPBR_SetEnvironmentMatrices( program, surf, vertexColor );
	RB_GLPBR_SetVertexColorParams( program.vertexColorParams, vertexColor );
	RB_GLPBR_SetMaterialUniforms( program.pbr, material, false, alphaScale, coverageOnly );
	if ( program.environment >= 0 ) {
		// A grid-only pass keeps its baked diffuse and adds no reflection.
		glUniform4fARB( program.environment,
			illuminated || diagnosticView ? idMath::ClampFloat( 0.0f, 4.0f, r_pbrIBLIntensity.GetFloat() ) : 0.0f,
			diagnosticView ? static_cast<float>( diagnostic ) : 0.0f, 1.0f, r_pbrAnalyticEnvironment.GetBool() ? 1.0f : 0.0f );
	}
	float bakedGrid[7][4];
	memset( bakedGrid, 0, sizeof( bakedGrid ) );
	if ( grid != NULL ) {
		// This program passes world positions: a zero origin.
		const float worldOrigin[3] = { 0.0f, 0.0f, 0.0f };
		RB_LightGridBakedParams( *grid, worldOrigin, bakedGrid );
		RB_GLPBR_BindImage( GL_PBR_UNIT_BAKED_IRRADIANCE, grid->irradianceImage );
		RB_GLPBR_BindImage( GL_PBR_UNIT_BAKED_VISIBILITY, grid->visibilityImage );
		RB_GLPBR_BindImage( GL_PBR_UNIT_BAKED_RELOCATION, grid->probeImage );
	}
	if ( program.bakedGrid >= 0 ) {
		// Program state persists: a draw without a grid disables it.
		glUniform4fvARB( program.bakedGrid, 7, bakedGrid[0] );
	}
	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_ENVIRONMENT, GL_TEXTURE_2D, illuminated || grid != NULL ? R_ModernSpecularProbeAtlas_Texture() : 0 );
	RB_GLPBR_BindMaterialImages( material );

	RB_GLPBR_EnableVertexArrays( true );
	RB_GLPBR_SetSurfaceVertexPointers( ac );
	RB_DrawElementsWithCounters( surf->geo );
	RB_GLPBR_EnableVertexArrays( false );

	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_ENVIRONMENT, GL_TEXTURE_2D, 0 );
	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_PROBE_RECORDS, GL_TEXTURE_2D, 0 );
	RB_GLPBR_BindRawUnit( GL_PBR_UNIT_PROBE_INDICES, GL_TEXTURE_2D, 0 );
	if ( grid != NULL ) {
		// Tracked units: unbinding also disables them for the fixed-function
		// stages that follow in the walk.
		for ( int unit = GL_PBR_UNIT_BAKED_RELOCATION; unit >= GL_PBR_UNIT_BAKED_IRRADIANCE; --unit ) {
			GL_SelectTextureNoClient( unit );
			globalImages->BindNull();
		}
		g_glPBRStats.baked++;
	}
	RB_GLPBR_ReleaseProgramState();
	g_glPBRStats.environment++;
}

void RB_GLPBR_DrawEnvironment( const drawSurf_t *surf ) {
	const int diagnostic = RB_GLPBR_DiagnosticView();
	if ( surf == NULL || surf->material == NULL || !surf->material->HasPBR()
			|| ( diagnostic == 0 && r_pbrDebug.GetInteger() != 0 )
			|| surf->material->GetSort() >= SS_POST_PROCESS || RB_GLPBR_Translucent( surf )
			|| !RB_GLPBR_SurfaceOwned( surf ) ) {
		// A translucent owner composites everything at its blend stage.
		return;
	}
	const bool illuminated = diagnostic == 0 && RB_GLPBR_Illuminated();
	const LightGrid *grid = diagnostic == 0 ? RB_GLPBR_SurfaceGrid( surf ) : NULL;
	if ( diagnostic == 0 && !illuminated && grid == NULL ) {
		return;
	}
	if ( !RB_GLPBR_LoadEnvironmentProgram() || ( illuminated && !RB_GLPBR_EnvironmentAtlasReady() ) ) {
		return;
	}
	pbrNativeMaterial_t material;
	RB_GLPBR_Admit( surf, &material );
	idDrawVert *ac = NULL;
	if ( !RB_GLSLInteractionVertexCache( surf, ac ) ) {
		return;
	}
	const int previousTmu = backEnd.glState.currenttmu;
	RB_GLPBR_EnvironmentPass( surf, material, ac, diagnostic, false, 0.0f,
		GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | GLS_DEPTHFUNC_EQUAL, grid );
	// The ambient walk set the fixed-function texcoord pointer for its stages.
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
	if ( previousTmu >= 0 ) {
		GL_SelectTexture( previousTmu );
	}
}

/*
Translucent owners. The light loop drew nothing for this surface; here, in
its authored stage's sort position, the background keeps 1 - alpha, then each
light that reaches it and the environment add alpha times their encoded
radiance: dst * (1 - a) + a * sum. Lights are matched by the ambient surface
identity, since a light can carry a clipped copy of the surface.
*/
static const srfTriangles_t *RB_GLPBR_SurfaceIdentity( const drawSurf_t *surf ) {
	return surf->geo->ambientSurface != NULL ? surf->geo->ambientSurface : surf->geo;
}

static void RB_GLPBR_RestoreWalkSurface( const drawSurf_t *surf, idDrawVert *ac ) {
	// The light draws leave their own scissor and depth range; the walk keeps
	// drawing this surface's remaining stages with its own.
	if ( surf->space != backEnd.currentSpace ) {
		glLoadMatrixf( surf->space->modelViewMatrix );
		backEnd.currentSpace = surf->space;
	}
	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
	}
	if ( surf->space->modelDepthHack != 0.0f ) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}
	if ( surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surf->material->GetPolygonOffset() );
	}
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
}

static void RB_GLPBR_ReplayTransparentLights( const drawSurf_t *surf, float alphaScale ) {
	const glPBRProgram_t &program = g_glPBRUnshadowed;
	viewLight_t *savedLight = backEnd.vLight;
	float savedLightTextureMatrix[16];
	memcpy( savedLightTextureMatrix, backEnd.lightTextureMatrix, sizeof( savedLightTextureMatrix ) );
	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | GLS_DEPTHFUNC_LESS );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( program.program );
	RB_GLPBR_EnableVertexArrays( true );
	g_glPBRTranslucentReplay = true;
	g_glPBRDrawAlphaScale = alphaScale;
	const srfTriangles_t *identity = RB_GLPBR_SurfaceIdentity( surf );
	for ( viewLight_t *vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		if ( vLight->lightShader == NULL || vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
			continue;
		}
		for ( const drawSurf_t *lightSurf = vLight->translucentInteractions; lightSurf != NULL;
				lightSurf = lightSurf->nextOnLight ) {
			if ( lightSurf->geo == NULL || lightSurf->space != surf->space || lightSurf->material != surf->material
					|| RB_GLPBR_SurfaceIdentity( lightSurf ) != identity ) {
				continue;
			}
			idDrawVert *lightVerts = NULL;
			if ( !RB_GLSLInteractionVertexCache( lightSurf, lightVerts ) ) {
				continue;
			}
			backEnd.vLight = vLight;
			RB_GLPBR_SetSurfaceVertexPointers( lightVerts );
			RB_CreateSingleDrawInteractions( lightSurf, RB_GLPBR_DrawUnshadowedInteraction );
		}
	}
	g_glPBRDrawAlphaScale = 0.0f;
	g_glPBRTranslucentReplay = false;
	RB_GLPBR_EnableVertexArrays( false );
	RB_GLPBR_ReleaseProgramState();
	backEnd.vLight = savedLight;
	memcpy( backEnd.lightTextureMatrix, savedLightTextureMatrix, sizeof( savedLightTextureMatrix ) );
}

bool RB_GLPBR_DrawTransparentStage( const drawSurf_t *surf, int stageIndex ) {
	if ( !RB_GLPBR_Translucent( surf ) || !surf->material->HasPBR() || !RB_GLPBR_SurfaceOwned( surf ) ) {
		return false;
	}
	int transparentStage = -1;
	float alphaScale = 0.0f;
	if ( !R_PBRNative_TransparentStage( surf, &transparentStage, &alphaScale ) || transparentStage != stageIndex ) {
		return false;
	}
	pbrNativeMaterial_t material;
	RB_GLPBR_Admit( surf, &material );
	idDrawVert *ac = NULL;
	if ( !RB_GLSLInteractionVertexCache( surf, ac ) ) {
		return false;
	}
	const int previousTmu = backEnd.glState.currenttmu;
	// Coverage must blend, never become multisample coverage.
	const bool alphaToCoverage = glIsEnabled( GL_SAMPLE_ALPHA_TO_COVERAGE ) != GL_FALSE;
	if ( alphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
	const int diagnostic = RB_GLPBR_DiagnosticView();
	const int depthBits = GLS_DEPTHMASK | GLS_DEPTHFUNC_LESS;
	// Opacity belongs to the full ambient mesh, whichever lights reach it. A
	// material diagnostic composites its value; every other view black.
	RB_GLPBR_EnvironmentPass( surf, material, ac, diagnostic, diagnostic == 0, alphaScale,
		GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | depthBits );
	if ( r_pbrDebug.GetInteger() == 0 ) {
		if ( !r_skipInteractions.GetBool() && !r_skipTranslucent.GetBool() ) {
			RB_GLPBR_ReplayTransparentLights( surf, alphaScale );
			RB_GLPBR_RestoreWalkSurface( surf, ac );
		}
		if ( RB_GLPBR_Illuminated() && RB_GLPBR_EnvironmentAtlasReady() ) {
			RB_GLPBR_EnvironmentPass( surf, material, ac, 0, false, alphaScale,
				GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE | depthBits );
		}
	}
	RB_GLPBR_RestoreWalkSurface( surf, ac );
	if ( alphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
	if ( previousTmu >= 0 ) {
		GL_SelectTexture( previousTmu );
	}
	g_glPBRStats.transparent++;
	return true;
}

bool RB_GLPBR_DrawEmissionStage( const drawSurf_t *surf, int stageIndex ) {
	if ( surf == NULL || surf->material == NULL || !surf->material->HasPBR()
			|| !R_PBRNative_IsReplacedEmissionStage( surf, stageIndex ) || !RB_GLPBR_SurfaceOwned( surf ) ) {
		return false;
	}
	const int diagnostic = r_pbrDebug.GetInteger();
	if ( diagnostic != 0 && diagnostic != 6 ) {
		// Material diagnostics replace the surface; emission has its own view.
		return true;
	}
	pbrNativeMaterial_t material;
	RB_GLPBR_Admit( surf, &material );
	if ( material.emissiveImage == NULL || !RB_GLPBR_LoadEmissionProgram() ) {
		return false;
	}
	const shaderStage_t *stage = surf->material->GetStage( stageIndex );
	const glPBRProgram_t &program = g_glPBREmission;
	idDrawVert *ac = NULL;
	if ( !RB_GLSLInteractionVertexCache( surf, ac ) ) {
		return false;
	}

	const int previousTmu = backEnd.glState.currenttmu;
	// The classic glow stage's own blend, depth and mask state.
	GL_State( stage->drawStateBits );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( program.program );
	idVec4 matrix[2];
	idImage *unused = NULL;
	R_SetDrawInteraction( stage, surf->shaderRegisters, &unused, matrix, NULL );
	glUniform4fvARB( program.textureMatrixS, 1, matrix[0].ToFloatPtr() );
	glUniform4fvARB( program.textureMatrixT, 1, matrix[1].ToFloatPtr() );
	glUniform4fARB( program.emissiveColor, material.emissiveColor[0], material.emissiveColor[1],
		material.emissiveColor[2], 1.0f );
	RB_GLPBR_BindImage( 0, material.emissiveImage );

	glEnableVertexAttribArrayARB( 8 );
	glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );
	RB_DrawElementsWithCounters( surf->geo );
	glDisableVertexAttribArrayARB( 8 );

	RB_GLPBR_ReleaseProgramState();
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
	if ( previousTmu >= 0 ) {
		GL_SelectTexture( previousTmu );
	}
	g_glPBRStats.emission++;
	return true;
}

idImage *RB_GLPBR_DepthCoverageImage( const drawSurf_t *surf, const shaderStage_t *stage ) {
	// A perforated owner's alpha test reads the PBR albedo itself (the
	// contract proves it is the same image, sampler and coordinates), so the
	// depth fill's coverage is exactly the coverage its draws shade.
	if ( surf == NULL || stage == NULL || !stage->hasAlphaTest || surf->material == NULL
			|| surf->material->Coverage() != MC_PERFORATED || !surf->material->HasPBR() ) {
		return NULL;
	}
	if ( backEnd.viewDef == NULL || backEnd.viewDef->viewEntitys == NULL ) {
		return NULL;
	}
	pbrNativeMaterial_t material;
	if ( !RB_GLPBR_Admit( surf, &material ) ) {
		return NULL;
	}
	return material.albedoImage;
}

void RB_GLPBR_PrintInfo( void ) {
	const glPBRFrameStats_t &stats = g_glPBRLastStats.frame != 0 ? g_glPBRLastStats : g_glPBRStats;
	common->Printf( "OpenGL: native PBR: enabled=%d program=%d environment=%d emission=%d admitted=%d declined=%d interactions=%d mapped=%d environmentDraws=%d emissionDraws=%d transparent=%d transparentLights=%d baked=%d analytic=%d probeViews=%d lastDecline=%s translucentView=%s\n",
		r_glPBR.GetBool() && r_pbrMaterials.GetBool() && r_rendererModernQuality.GetBool() ? 1 : 0,
		g_glPBRUnshadowed.valid ? 1 : 0, g_glPBREnvironment.valid ? 1 : 0, g_glPBREmission.valid ? 1 : 0,
		stats.admitted, stats.declined, stats.interactions, stats.mappedInteractions,
		stats.environment, stats.emission, stats.transparent, stats.transparentLights, stats.baked,
		r_pbrAnalyticEnvironment.GetBool() ? 1 : 0, g_glPBRProbeFrameViews,
		stats.lastDecline != NULL ? stats.lastDecline : "none",
		stats.translucentView != NULL ? stats.translucentView : "none" );
}
