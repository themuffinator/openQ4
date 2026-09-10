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
#include "RendererResourceSettings.h"

#include <cstdlib>

/*

Any errors during parsing just set MF_DEFAULTED and return, rather than throwing
a hard error. This will cause the material to fall back to default material,
but otherwise let things continue.

Each material may have a set of calculations that must be evaluated before
drawing with it.

Every expression that a material uses can be evaluated at one time, which
will allow for perfect common subexpression removal when I get around to
writing it.

Without this, scrolling an entire surface could result in evaluating the
same texture matrix calculations a half dozen times.

  Open question: should I allow arbitrary per-vertex color, texCoord, and vertex
  calculations to be specified in the material code?

  Every stage will definately have a valid image pointer.

  We might want the ability to change the sort value based on conditionals,
  but it could be a hassle to implement,

*/

// keep all of these on the stack, when they are static it makes material parsing non-reentrant
typedef struct mtrParsingData_s {
	bool			registerIsTemporary[MAX_EXPRESSION_REGISTERS];
	float			shaderRegisters[MAX_EXPRESSION_REGISTERS];
	expOp_t			shaderOps[MAX_EXPRESSION_OPS];
	shaderStage_t	parseStages[MAX_SHADER_STAGES];

	bool			registersAreConstant;
	bool			forceOverlays;
} mtrParsingData_t;

static void R_ResetSpecularProbeMaterialInfo( specularProbeMaterialInfo_t &info ) {
	memset( &info, 0, sizeof( info ) );
	info.cubeConvention = SPECULAR_PROBE_CUBE_NONE;
	info.tint[0] = 1.0f;
	info.tint[1] = 1.0f;
	info.tint[2] = 1.0f;
	info.intensity = 1.0f;
	info.blendFraction = 0.25f;
}

// `glslPrograms` is an authored material-capability condition, while
// glConfig.GLSLProgramAvailable also advertises the OpenGL renderer's broad
// post-processing and modern-feature surface. Vulkan implements the stock
// material-program families natively without claiming those unrelated paths.
static bool R_MaterialGLSLProgramsAvailable() {
#ifdef OPENQ4_RENDERER_VK_MODULE
	return true;
#else
	return glConfig.GLSLProgramAvailable;
#endif
}

// Like glslPrograms, fragmentPrograms is an authored material capability.
// Vulkan supplies native implementations for the stock ARB/FP20 material
// families, so declarations must select their programmable branch even
// though no OpenGL ARB program capability is advertised by this backend.
static bool R_MaterialFragmentProgramsAvailable() {
#ifdef OPENQ4_RENDERER_VK_MODULE
	return true;
#else
	return glConfig.ARBFragmentProgramAvailable;
#endif
}

struct q4ImplicitGuiAtlasMaterialPrefix_t {
	const char *prefix;
	int length;
};

static const q4ImplicitGuiAtlasMaterialPrefix_t Q4_IMPLICIT_GUI_ATLAS_NOPICMIP_PREFIXES[] = {
	{ "gfx/guis/", sizeof( "gfx/guis/" ) - 1 },
	{ "fonts/", sizeof( "fonts/" ) - 1 },
};

static bool R_MaterialNameStartsWith( const char *name, const q4ImplicitGuiAtlasMaterialPrefix_t &prefix ) {
	return name != NULL && !idStr::Icmpn( name, prefix.prefix, prefix.length );
}

static bool R_MaterialNeedsImplicitGuiAtlasNoPicMip( const char *materialName ) {
	for ( int i = 0; i < static_cast<int>( sizeof( Q4_IMPLICIT_GUI_ATLAS_NOPICMIP_PREFIXES ) / sizeof( Q4_IMPLICIT_GUI_ATLAS_NOPICMIP_PREFIXES[0] ) ); ++i ) {
		if ( R_MaterialNameStartsWith( materialName, Q4_IMPLICIT_GUI_ATLAS_NOPICMIP_PREFIXES[i] ) ) {
			return true;
		}
	}
	return false;
}

static bool R_IsQ4FontDataImageName( const char *name ) {
	if ( name == NULL || idStr::Icmpn( name, "fonts/", 6 ) != 0 ) {
		return false;
	}

	idStr extension;
	idStr nameStr = name;
	nameStr.ExtractFileExtension( extension );
	return !extension.Icmp( "fontdat" );
}

static void R_ResolveQ4SpecialImageName( const char *name, idStr &resolved ) {
	resolved = name != NULL ? name : "";
	if ( name == NULL ) {
		return;
	}

	if ( !idStr::Icmp( name, "DepthTexture" ) ) {
		if ( globalImages->GetImage( "DepthTexture" ) != NULL ) {
			resolved = "DepthTexture";
			return;
		}
		resolved = "_currentDepth";
		return;
	}

	if ( !idStr::Icmp( name, "BlurTexture1" ) ) {
		if ( globalImages->GetImage( "BlurTexture1" ) != NULL ) {
			resolved = "BlurTexture1";
			return;
		}
		resolved = "_currentRender";
		return;
	}

	if ( !idStr::Icmp( name, "ambientNormalMap" ) ) {
		resolved = "_ambient";
		return;
	}
	if ( !idStr::Icmp( name, "normalCubeMap" ) ) {
		resolved = "_normalCubeMap";
		return;
	}
	if ( !idStr::Icmp( name, "specularTableImage" ) ) {
		resolved = "_specularTable";
		return;
	}

	if ( R_IsQ4FontDataImageName( name ) ) {
		resolved.StripFileExtension();
	}
}

static idImage *R_LoadMaterialImage( const char *name, textureFilter_t filter, textureRepeat_t repeat,
	textureUsage_t usage, cubeFiles_t cubeMap = CF_2D, bool allowDownSize = true, unsigned int flags = 0 ) {
	idStr resolvedName;
	R_ResolveQ4SpecialImageName( name, resolvedName );
	return globalImages->ImageFromFile( resolvedName.c_str(), filter, repeat, usage, cubeMap, allowDownSize, flags );
}

static int R_FindMD5RVertexProgramForStageProgram( const char *programName ) {
	if ( programName == NULL || programName[0] == '\0' ) {
		return 0;
	}

	idStr md5rProgram = "md5r";
	md5rProgram += programName;

	idStr md5rPath = "glprogs/";
	md5rPath += md5rProgram;
	md5rPath.BackSlashesToSlashes();
	if ( fileSystem->ReadFile( md5rPath.c_str(), NULL, NULL ) == -1 ) {
		return 0;
	}

	return R_FindARBProgram( GL_VERTEX_PROGRAM_ARB, md5rProgram.c_str() );
}

static bool R_IsQ4LightImageNamespace( const char *name ) {
	return name != NULL
		&& ( idStr::Icmpn( name, "lights/", 7 ) == 0
			|| idStr::Icmpn( name, "gfx/lights/", 11 ) == 0 );
}

static textureUsage_t R_DefaultStageUsageForMaterial( const char *materialName ) {
	// Retail Quake 4 routes projected-light materials from both stock light
	// namespaces through its light-oriented image bucket. openQ4 maps that
	// behavior onto TD_LIGHT so projection cookies avoid the generic texture path.
	if ( R_IsQ4LightImageNamespace( materialName ) ) {
		return TD_LIGHT;
	}
	return TD_DEFAULT;
}

static textureUsage_t R_ApplyMaterialHighQualityUsage( textureUsage_t usage, bool forceHighQuality ) {
	// Retail Quake 4 routes authored "highquality"/"uncompressed" image hints
	// through a distinct usage bucket. openQ4 keeps that separate identity so
	// those stages do not collapse onto generic caches, while still letting the
	// modern loader keep its higher-quality uncompressed binary-image pipeline.
	if ( forceHighQuality || !image_ignoreHighQuality.GetBool() ) {
		return TD_HIGH_QUALITY;
	}
	return usage;
}

static unsigned int R_ApplyMaterialNoMipFlags( unsigned int flags ) {
	// Retail only promoted "nomips" while resource builds were active. openQ4's
	// equivalent build switch is the boolean com_makingBuild cvar.
	if ( com_makingBuild.GetBool() ) {
		flags |= IMAGEFLAG_NOMIPS;
	}
	return flags;
}

static bool R_IsFilterBlendStage( const shaderStage_t &stage ) {
	const int blendBits = stage.drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	return blendBits == ( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO ) ||
		blendBits == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR );
}

static bool R_IsSceneCaptureImage( const idImage *image ) {
	if ( image == NULL ) {
		return false;
	}

	if ( image == globalImages->currentRenderImage
		|| image == globalImages->originalCurrentRenderImage
		|| image == globalImages->currentDepthImage ) {
		return true;
	}

	const char *name = image->GetName();
	if ( name == NULL ) {
		return false;
	}

	return idStr::Icmpn( name, "_currentRender", 14 ) == 0
		|| idStr::Icmpn( name, "_currentDepth", 13 ) == 0;
}


/*
=============
idMaterial::CommonInit
=============
*/
void idMaterial::CommonInit() {
	desc = "<none>";
	renderBump = "";
	portalDistanceNear = 262144.0f;
	portalDistanceFar = 262144.0f;
	contentFlags = CONTENTS_SOLID;
	surfaceFlags = SURFTYPE_NONE;
	materialFlags = 0;
	sort = SS_BAD;
	coverage = MC_BAD;
	cullType = CT_FRONT_SIDED;
	deform = DFRM_NONE;
	numOps = 0;
	ops = NULL;
	numRegisters = 0;
	expressionRegisters = NULL;
	constantRegisters = NULL;
	numStages = 0;
	numAmbientStages = 0;
	hasCustomGLSLLightingStage = false;
	stages = NULL;
	editorImage = NULL;
	lightFalloffImage = NULL;
	shouldCreateBackSides = false;
	entityGui = 0;
	fogLight = false;
	blendLight = false;
	ambientLight = false;
	noFog = false;
	hasSubview = false;
	allowOverlays = true;
	unsmoothedTangents = false;
	gui = NULL;
	memset( deformRegisters, 0, sizeof( deformRegisters ) );
	deformDecl = NULL;
	editorAlpha = 1.0;
	spectrum = 0;
	polygonOffset = 0;
	suppressInSubview = false;
	refCount = 0;
	portalSky = false;
// jmarshall - quake 4
	materialType = nullptr;
	materialTypeArray = nullptr;
	materialTypeArrayName.Clear();
	MTAWidth = 0;
	MTAHeight = 0;
	useCount = 0;
	globalUseCount = 0;
	portalImage = nullptr;
// jmarshall end
	memset( &pbrInfo, 0, sizeof( pbrInfo ) );
	pbrInfo.workflow = PBR_WORKFLOW_NONE;
	pbrInfo.normalFormat = PBR_NORMAL_UNSPECIFIED;
	pbrInfo.metallicRegister = -1;
	pbrInfo.roughnessRegister = -1;
	pbrInfo.aoRegister = -1;
	pbrInfo.normalScaleRegister = -1;
	pbrInfo.emissiveColorRegisters[0] = -1;
	pbrInfo.emissiveColorRegisters[1] = -1;
	pbrInfo.emissiveColorRegisters[2] = -1;
	pbrInfo.autoLegacyFallback = true;
	R_ResetSpecularProbeMaterialInfo( specularProbeInfo );

	decalInfo.stayTime = 10000;
	decalInfo.maxAngle = 0.1f;
}

/*
=============
idMaterial::idMaterial
=============
*/
idMaterial::idMaterial() : imagePolicyIdentity(R_ImagePolicyNewResourceIdentity()) {
	CommonInit();

	// we put this here instead of in CommonInit, because
	// we don't want it cleared when a material is purged
	surfaceArea = 0;
}

/*
=============
idMaterial::~idMaterial
=============
*/
idMaterial::~idMaterial() {
	R_ImagePolicyResourceDestroyed();
}

/*
==============
idMaterial::GetMaterialType
==============
*/
const rvDeclMatType* idMaterial::GetMaterialType( idVec2& tc ) const {
	if ( materialTypeArray == NULL || MTAWidth <= 0 || MTAHeight <= 0 ) {
		return materialType;
	}

	idVec2 wrapped = tc;
	wrapped.x = fmodf( wrapped.x, 1.0f );
	wrapped.y = fmodf( wrapped.y, 1.0f );
	if ( wrapped.x < 0.0f ) {
		wrapped.x += 1.0f;
	}
	if ( wrapped.y < 0.0f ) {
		wrapped.y += 1.0f;
	}

	const int x = idMath::ClampInt( 0, MTAWidth - 1, idMath::FtoiFast( wrapped.x * MTAWidth ) );
	const int y = idMath::ClampInt( 0, MTAHeight - 1, idMath::FtoiFast( wrapped.y * MTAHeight ) );
	const rvDeclMatType *resolvedMaterialType = declManager->MaterialTypeByIndex( materialTypeArray[ y * MTAWidth + x ], true );
	return resolvedMaterialType != NULL ? resolvedMaterialType : materialType;
}

/*
===============
idMaterial::FreeData
===============
*/
void idMaterial::FreeData() {
	if ( !R_ImagePolicyContentMutation() ) return;
	int i;

	if ( stages ) {
		// delete any idCinematic textures
		for ( i = 0; i < numStages; i++ ) {
			if ( stages[i].texture.cinematic != NULL ) {
				delete stages[i].texture.cinematic;
				stages[i].texture.cinematic = NULL;
			}
			if ( stages[i].newStage != NULL ) {
				// the handles are only valid in the GL context generation that compiled
				// them; after a full restart they may alias live objects in the new
				// context (partial restarts keep the context, so the generation matches)
				if ( stages[i].newStage->glslProgramObject != 0
						&& stages[i].newStage->glslProgramGeneration == tr.glContextGeneration
						&& glConfig.isInitialized ) {
					if ( stages[i].newStage->glslVertexShaderObject != 0 ) {
						glDetachObjectARB(
							(GLhandleARB)stages[i].newStage->glslProgramObject,
							(GLhandleARB)stages[i].newStage->glslVertexShaderObject );
						glDeleteObjectARB( (GLhandleARB)stages[i].newStage->glslVertexShaderObject );
					}
					if ( stages[i].newStage->glslFragmentShaderObject != 0 ) {
						glDetachObjectARB(
							(GLhandleARB)stages[i].newStage->glslProgramObject,
							(GLhandleARB)stages[i].newStage->glslFragmentShaderObject );
						glDeleteObjectARB( (GLhandleARB)stages[i].newStage->glslFragmentShaderObject );
					}
					glDeleteObjectARB( (GLhandleARB)stages[i].newStage->glslProgramObject );
				}
				Mem_Free( stages[i].newStage );
				stages[i].newStage = NULL;
			}
		}
		R_StaticFree( stages );
		stages = NULL;
	}
	if ( expressionRegisters != NULL ) {
		R_StaticFree( expressionRegisters );
		expressionRegisters = NULL;
	}
	if ( constantRegisters != NULL ) {
		R_StaticFree( constantRegisters );
		constantRegisters = NULL;
	}
	if ( ops != NULL ) {
		R_StaticFree( ops );
		ops = NULL;
	}
	if ( materialTypeArray != NULL ) {
		Mem_Free( materialTypeArray );
		materialTypeArray = NULL;
	}
	materialTypeArrayName.Clear();
	MTAWidth = 0;
	MTAHeight = 0;
	// Purged declarations remain queryable before their next parse. Do not
	// leave stale PBR image pointers or register indices visible in that state.
	memset( &pbrInfo, 0, sizeof( pbrInfo ) );
	pbrInfo.workflow = PBR_WORKFLOW_NONE;
	pbrInfo.normalFormat = PBR_NORMAL_UNSPECIFIED;
	pbrInfo.metallicRegister = -1;
	pbrInfo.roughnessRegister = -1;
	pbrInfo.aoRegister = -1;
	pbrInfo.normalScaleRegister = -1;
	pbrInfo.emissiveColorRegisters[0] = -1;
	pbrInfo.emissiveColorRegisters[1] = -1;
	pbrInfo.emissiveColorRegisters[2] = -1;
	pbrInfo.autoLegacyFallback = true;
	R_ResetSpecularProbeMaterialInfo( specularProbeInfo );
}

/*
==============
idMaterial::GetEditorImage
==============
*/
idImage *idMaterial::GetEditorImage( void ) const {
	if ( editorImage ) {
		return editorImage;
	}

	// if we don't have an editorImageName, use the first stage image
	if ( !editorImageName.Length()) {
		// _D3XP :: First check for a diffuse image, then use the first
		if ( numStages && stages ) {
			int i;
			for( i = 0; i < numStages; i++ ) {
				if ( stages[i].lighting == SL_DIFFUSE ) {
					editorImage = stages[i].texture.image;
					break;
				}
			}
			if ( !editorImage ) {
				editorImage = stages[0].texture.image;
			}
		} else if ( pbrInfo.enabled && pbrInfo.albedo.image != NULL ) {
			editorImage = pbrInfo.albedo.image;
		} else {
			editorImage = globalImages->defaultImage;
		}
	} else {
		// look for an explicit one
		editorImage = globalImages->ImageFromFile( editorImageName, TF_DEFAULT, TR_REPEAT, TD_DEFAULT );
	}

	if ( !editorImage ) {
		editorImage = globalImages->defaultImage;
	}

	return editorImage;
}


// info parms
typedef struct {
	char	*name;
	int		clearSolid, surfaceFlags, contents;
} infoParm_t;

static infoParm_t	infoParms[] = {
	// game relevant attributes
	{"solid",		0,	0,	CONTENTS_SOLID },		// may need to override a clearSolid
	{"water",		1,	0,	CONTENTS_WATER },		// used for water
	{"lava",		1,	0,	CONTENTS_LAVA },		// used for lava, damages what swims in it
	{"slime",		1,	0,	CONTENTS_SLIME },		// used for slime, damages what swims in it
	{"playerclip",	0,	0,	CONTENTS_PLAYERCLIP },	// solid to players
	{"monsterclip",	0,	0,	CONTENTS_MONSTERCLIP },	// solid to monsters
	{"moveableclip",0,	0,	CONTENTS_MOVEABLECLIP },// solid to moveable entities
	{"ikclip",		0,	0,	CONTENTS_IKCLIP },		// solid to IK
	{"blood",		0,	0,	CONTENTS_BLOOD },		// used to detect blood decals
	{"trigger",		0,	0,	CONTENTS_TRIGGER },		// used for triggers
	{"projectileclip",	0,	0,	CONTENTS_PROJECTILECLIP },	// projectiles only
	{"aassolid",	0,	0,	CONTENTS_AAS_SOLID },	// solid for AAS
	{"aasobstacle",	0,	0,	CONTENTS_AAS_OBSTACLE },// used to compile an obstacle into AAS that can be enabled/disabled
	{"flashlight_trigger",	0,	0,	CONTENTS_FLASHLIGHT_TRIGGER }, // used for triggers that are activated by the flashlight
	{"sightclip",	0,	0,	CONTENTS_SIGHTCLIP },	// blocks sight for actors and cameras
	{"shotclip",	0,	0,	CONTENTS_PROJECTILE },	// blocks hitscan shots
	{"largeshotclip",	0,	0,	CONTENTS_LARGESHOTCLIP },	// blocks large shots
	{"notacticalfeatures",	0,	0,	CONTENTS_NOTACTICALFEATURES },	// don't place tactical features here
	{"vehicleclip",	0,	0,	CONTENTS_VEHICLECLIP },	// solid to vehicles
	{"flyclip",	0,	0,	CONTENTS_FLYCLIP },		// solid to vehicles
	{"itemclip",	0,	0,	CONTENTS_ITEMCLIP },	// item collision
	{"nonsolid",	1,	0,	0 },					// clears the solid flag
	{"nullNormal",	0,	SURF_NULLNORMAL,0 },		// renderbump will draw as 0x80 0x80 0x80

	// utility relevant attributes
	{"areaportal",	1,	0,	CONTENTS_AREAPORTAL },	// divides areas
	{"qer_nocarve",	1,	0,	CONTENTS_NOCSG},		// don't cut brushes in editor

	{"discrete",	1,	SURF_DISCRETE,	0 },		// surfaces should not be automatically merged together or
													// clipped to the world,
													// because they represent discrete objects like gui shaders
													// mirrors, or autosprites
	{"noFragment",	0,	SURF_NOFRAGMENT,	0 },

	{"slick",		0,	SURF_SLICK,		0 },
	{"collision",	0,	SURF_COLLISION,	0 },
	{"noimpact",	0,	SURF_NOIMPACT,	0 },		// don't make impact explosions or marks
	{"nodamage",	0,	SURF_NODAMAGE,	0 },		// no falling damage when hitting
	{"bounce",		0,	SURF_BOUNCE,	0 },		// projectiles bounce off this surface
	{"ladder",		0,	SURF_LADDER,	0 },		// climbable
	{"nosteps",		0,	SURF_NOSTEPS,	0 },		// no footsteps

	// material types for particle, sound, footstep feedback
	{"metal",		0,  SURFTYPE_METAL,		0 },	// metal
	{"stone",		0,  SURFTYPE_STONE,		0 },	// stone
	{"flesh",		0,  SURFTYPE_FLESH,		0 },	// flesh
	{"wood",		0,  SURFTYPE_WOOD,		0 },	// wood
	{"cardboard",	0,	SURFTYPE_CARDBOARD,	0 },	// cardboard
	{"liquid",		0,	SURFTYPE_LIQUID,	0 },	// liquid
	{"glass",		0,	SURFTYPE_GLASS,		0 },	// glass
	{"plastic",		0,	SURFTYPE_PLASTIC,	0 },	// plastic
	{"ricochet",	0,	SURFTYPE_RICOCHET,	0 },	// behaves like metal but causes a ricochet sound

	// unassigned surface types
	{"surftype10",	0,	SURFTYPE_10,	0 },
	{"surftype11",	0,	SURFTYPE_11,	0 },
	{"surftype12",	0,	SURFTYPE_12,	0 },
	{"surftype13",	0,	SURFTYPE_13,	0 },
	{"surftype14",	0,	SURFTYPE_14,	0 },
	{"surftype15",	0,	SURFTYPE_15,	0 },
};

static const int numInfoParms = sizeof(infoParms) / sizeof (infoParms[0]);


/*
===============
idMaterial::CheckSurfaceParm

See if the current token matches one of the surface parm bit flags
===============
*/
bool idMaterial::CheckSurfaceParm( idToken *token ) {

	for ( int i = 0 ; i < numInfoParms ; i++ ) {
		if ( !token->Icmp( infoParms[i].name ) ) {
			if ( infoParms[i].surfaceFlags & SURF_TYPE_MASK ) {
				// ensure we only have one surface type set
				surfaceFlags &= ~SURF_TYPE_MASK;
			}
			surfaceFlags |= infoParms[i].surfaceFlags;
			contentFlags |= infoParms[i].contents;
			if ( infoParms[i].clearSolid ) {
				contentFlags &= ~CONTENTS_SOLID;
			}
			return true;
		}
	}
	return false;
}

/*
===============
idMaterial::MatchToken

Sets defaultShader and returns false if the next token doesn't match
===============
*/
bool idMaterial::MatchToken( idLexer &src, const char *match ) {
	if ( !src.ExpectTokenString( match ) ) {
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	return true;
}

/*
=================
idMaterial::ParseSort
=================
*/
void idMaterial::ParseSort( idLexer &src ) {
	idToken token;

	if ( !src.ReadTokenOnLine( &token ) ) {
		src.Warning( "missing sort parameter" );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	if ( !token.Icmp( "subview" ) ) {
		sort = SS_SUBVIEW;
	} else if ( !token.Icmp( "gui" ) ) {
		sort = SS_GUI;
	} else if ( !token.Icmp( "opaque" ) ) {
		sort = SS_OPAQUE;
	}else if ( !token.Icmp( "decal" ) ) {
		sort = SS_DECAL;
	} else if ( !token.Icmp( "far" ) ) {
		sort = SS_FAR;
	} else if ( !token.Icmp( "medium" ) ) {
		sort = SS_MEDIUM;
	} else if ( !token.Icmp( "close" ) ) {
		sort = SS_CLOSE;
	} else if ( !token.Icmp( "almostNearest" ) ) {
		sort = SS_ALMOST_NEAREST;
	} else if ( !token.Icmp( "nearest" ) ) {
		sort = SS_NEAREST;
	} else if ( !token.Icmp( "postProcess" ) ) {
		sort = SS_POST_PROCESS;
	} else if ( !token.Icmp( "portalSky" ) ) {
		sort = SS_PORTAL_SKY;
	} else {
		sort = atof( token );
	}
}

/*
=================
idMaterial::ParseDecalInfo
=================
*/
void idMaterial::ParseDecalInfo( idLexer &src ) {
	idToken token;

	decalInfo.stayTime = src.ParseFloat() * 1000;

	if ( !src.ReadToken( &token ) ) {
		return;
	}

	// Quake 4 syntax: "decalInfo <staySeconds>, <maxAngle>".
	if ( token == "," ) {
		decalInfo.maxAngle = src.ParseFloat();
		return;
	}

	// Legacy Doom 3 syntax compatibility:
	// "decalInfo <staySeconds> <fadeSeconds> ( <start rgba> ) ( <end rgba> )".
	if ( token.type == TT_NUMBER || token == "." || token == "-" ) {
		float dummy[4];
		src.Parse1DMatrix( 4, dummy );
		src.Parse1DMatrix( 4, dummy );
		return;
	}

	src.UnreadToken( &token );
}

/*
=============
idMaterial::GetExpressionConstant
=============
*/
int idMaterial::GetExpressionConstant( float f ) {
	int		i;

	for ( i = EXP_REG_NUM_PREDEFINED ; i < numRegisters ; i++ ) {
		if ( !pd->registerIsTemporary[i] && pd->shaderRegisters[i] == f ) {
			return i;
		}
	}
	if ( numRegisters == MAX_EXPRESSION_REGISTERS ) {
		common->Warning( "GetExpressionConstant: material '%s' hit MAX_EXPRESSION_REGISTERS", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return 0;
	}
	pd->registerIsTemporary[i] = false;
	pd->shaderRegisters[i] = f;
	numRegisters++;

	return i;
}

/*
=============
idMaterial::GetExpressionTemporary
=============
*/
int idMaterial::GetExpressionTemporary( void ) {
	if ( numRegisters == MAX_EXPRESSION_REGISTERS ) {
		common->Warning( "GetExpressionTemporary: material '%s' hit MAX_EXPRESSION_REGISTERS", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return 0;
	}
	pd->registerIsTemporary[numRegisters] = true;
	numRegisters++;
	return numRegisters - 1;
}

/*
=============
idMaterial::GetExpressionOp
=============
*/
expOp_t	*idMaterial::GetExpressionOp( void ) {
	if ( numOps == MAX_EXPRESSION_OPS ) {
		common->Warning( "GetExpressionOp: material '%s' hit MAX_EXPRESSION_OPS", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return &pd->shaderOps[0];
	}

	return &pd->shaderOps[numOps++];
}

/*
=================
idMaterial::EmitOp
=================
*/
int idMaterial::EmitOp( int a, int b, expOpType_t opType ) {
	expOp_t	*op;

	// optimize away identity operations
	if ( opType == OP_TYPE_ADD ) {
		if ( !pd->registerIsTemporary[a] && pd->shaderRegisters[a] == 0 ) {
			return b;
		}
		if ( !pd->registerIsTemporary[b] && pd->shaderRegisters[b] == 0 ) {
			return a;
		}
		if ( !pd->registerIsTemporary[a] && !pd->registerIsTemporary[b] ) {
			return GetExpressionConstant( pd->shaderRegisters[a] + pd->shaderRegisters[b] );
		}
	}
	if ( opType == OP_TYPE_MULTIPLY ) {
		if ( !pd->registerIsTemporary[a] && pd->shaderRegisters[a] == 1 ) {
			return b;
		}
		if ( !pd->registerIsTemporary[a] && pd->shaderRegisters[a] == 0 ) {
			return a;
		}
		if ( !pd->registerIsTemporary[b] && pd->shaderRegisters[b] == 1 ) {
			return a;
		}
		if ( !pd->registerIsTemporary[b] && pd->shaderRegisters[b] == 0 ) {
			return b;
		}
		if ( !pd->registerIsTemporary[a] && !pd->registerIsTemporary[b] ) {
			return GetExpressionConstant( pd->shaderRegisters[a] * pd->shaderRegisters[b] );
		}
	}

	op = GetExpressionOp();
	op->opType = opType;
	op->a = a;
	op->b = b;
	op->c = GetExpressionTemporary();

	return op->c;
}

/*
=================
idMaterial::ParseEmitOp
=================
*/
int idMaterial::ParseEmitOp( idLexer &src, int a, expOpType_t opType, int priority ) {
	int		b;

	b = ParseExpressionPriority( src, priority );
	return EmitOp( a, b, opType );
}

/*
=================
idMaterial::ParseTerm

Returns a register index
=================
*/
int idMaterial::ParseTerm( idLexer &src ) {
	idToken token;
	int		a, b;

	src.ReadToken( &token );

	if ( token == "(" ) {
		a = ParseExpression( src );
		MatchToken( src, ")" );
		return a;
	}

	if ( !token.Icmp( "time" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_TIME;
	}
	if ( !token.Icmp( "parm0" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM0;
	}
	if ( !token.Icmp( "parm1" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM1;
	}
	if ( !token.Icmp( "parm2" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM2;
	}
	if ( !token.Icmp( "parm3" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM3;
	}
	if ( !token.Icmp( "parm4" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM4;
	}
	if ( !token.Icmp( "DecalLife" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM4;
	}
	if ( !token.Icmp( "parm5" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM5;
	}
	if ( !token.Icmp( "DecalSpawn" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM5;
	}
	if ( !token.Icmp( "VertexRandomizer" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_VERTEX_RANDOM;
	}
	if ( !token.Icmp( "parm6" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM6;
	}
	if ( !token.Icmp( "parm7" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM7;
	}
	if ( !token.Icmp( "parm8" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM8;
	}
	if ( !token.Icmp( "parm9" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM9;
	}
	if ( !token.Icmp( "parm10" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM10;
	}
	if ( !token.Icmp( "parm11" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_PARM11;
	}
	if ( !token.Icmp( "global0" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_GLOBAL0;
	}
	if ( !token.Icmp( "global1" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_GLOBAL1;
	}
	if ( !token.Icmp( "global2" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_GLOBAL2;
	}
	if ( !token.Icmp( "global3" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_GLOBAL3;
	}
	if ( !token.Icmp( "global4" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_GLOBAL4;
	}
	if ( !token.Icmp( "global5" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_GLOBAL5;
	}
	if ( !token.Icmp( "global6" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_GLOBAL6;
	}
	if ( !token.Icmp( "global7" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_GLOBAL7;
	}
	if ( !token.Icmp( "fragmentPrograms" ) ) {
		return GetExpressionConstant( R_MaterialFragmentProgramsAvailable() ? 1.0f : 0.0f );
	}
	if ( !token.Icmp( "glslPrograms" ) ) {
		// Retail Quake 4 keeps glslPrograms as a runtime capability opcode so
		// authored branches evaluate against the active backend state instead of
		// being folded to a parse-time constant.
		pd->registersAreConstant = false;
		return EmitOp( 0, 0, OP_TYPE_GLSL_ENABLED );
	}
	if ( !token.Icmp( "POTCorrectionX" ) ) {
		const int width = Max( 1, glConfig.vidWidth );
		return GetExpressionConstant( width / (float)MakePowerOfTwo( width ) );
	}
	if ( !token.Icmp( "POTCorrectionY" ) ) {
		const int height = Max( 1, glConfig.vidHeight );
		return GetExpressionConstant( height / (float)MakePowerOfTwo( height ) );
	}
	if ( !token.Icmp( "VideoWidth" ) ) {
		return GetExpressionConstant( (float)glConfig.vidWidth );
	}
	if ( !token.Icmp( "VideoHeight" ) ) {
		return GetExpressionConstant( (float)glConfig.vidHeight );
	}
	if ( !token.Icmp( "IsMultiplayer" ) ) {
		return GetExpressionConstant( session->IsMultiplayer() ? 1.0f : 0.0f );
	}

	if ( !token.Icmp( "sound" ) ) {
		pd->registersAreConstant = false;
		return EmitOp( 0, 0, OP_TYPE_SOUND );
	}

	// parse negative numbers
	if ( token == "-" ) {
		src.ReadToken( &token );
		if ( token.type == TT_NUMBER || token == "." ) {
			return GetExpressionConstant( -(float) token.GetFloatValue() );
		}
		src.Warning( "Bad negative number '%s'", token.c_str() );
		SetMaterialFlag( MF_DEFAULTED );
		return 0;
	}

	if ( token.type == TT_NUMBER || token == "." || token == "-" ) {
		return GetExpressionConstant( (float) token.GetFloatValue() );
	}

	// see if it is a table name
	const idDeclTable *table = static_cast<const idDeclTable *>( declManager->FindType( DECL_TABLE, token.c_str(), false ) );
	if ( !table ) {
		src.Warning( "Bad term '%s'", token.c_str() );
		SetMaterialFlag( MF_DEFAULTED );
		return 0;
	}

	// parse a table expression
	MatchToken( src, "[" );

	b = ParseExpression( src );

	MatchToken( src, "]" );

	return EmitOp( table->Index(), b, OP_TYPE_TABLE );
}

/*
=================
idMaterial::ParseExpressionPriority

Returns a register index
=================
*/
#define	TOP_PRIORITY 4
int idMaterial::ParseExpressionPriority( idLexer &src, int priority ) {
	idToken token;
	int		a;

	if ( priority == 0 ) {
		return ParseTerm( src );
	}

	a = ParseExpressionPriority( src, priority - 1 );

	if ( TestMaterialFlag( MF_DEFAULTED ) ) {	// we have a parse error
		return 0;
	}

	if ( !src.ReadToken( &token ) ) {
		// we won't get EOF in a real file, but we can
		// when parsing from generated strings
		return a;
	}

	if ( priority == 1 && token == "*" ) {
		return ParseEmitOp( src, a, OP_TYPE_MULTIPLY, priority );
	}
	if ( priority == 1 && token == "/" ) {
		return ParseEmitOp( src, a, OP_TYPE_DIVIDE, priority );
	}
	if ( priority == 1 && token == "%" ) {	// implied truncate both to integer
		return ParseEmitOp( src, a, OP_TYPE_MOD, priority );
	}
	if ( priority == 2 && token == "+" ) {
		return ParseEmitOp( src, a, OP_TYPE_ADD, priority );
	}
	if ( priority == 2 && token == "-" ) {
		return ParseEmitOp( src, a, OP_TYPE_SUBTRACT, priority );
	}
	if ( priority == 3 && token == ">" ) {
		return ParseEmitOp( src, a, OP_TYPE_GT, priority );
	}
	if ( priority == 3 && token == ">=" ) {
		return ParseEmitOp( src, a, OP_TYPE_GE, priority );
	}
	if ( priority == 3 && token == "<" ) {
		return ParseEmitOp( src, a, OP_TYPE_LT, priority );
	}
	if ( priority == 3 && token == "<=" ) {
		return ParseEmitOp( src, a, OP_TYPE_LE, priority );
	}
	if ( priority == 3 && token == "==" ) {
		return ParseEmitOp( src, a, OP_TYPE_EQ, priority );
	}
	if ( priority == 3 && token == "!=" ) {
		return ParseEmitOp( src, a, OP_TYPE_NE, priority );
	}
	if ( priority == 4 && token == "&&" ) {
		return ParseEmitOp( src, a, OP_TYPE_AND, priority );
	}
	if ( priority == 4 && token == "||" ) {
		return ParseEmitOp( src, a, OP_TYPE_OR, priority );
	}

	// assume that anything else terminates the expression
	// not too robust error checking...

	src.UnreadToken( &token );

	return a;
}

/*
=================
idMaterial::ParseExpression

Returns a register index
=================
*/
int idMaterial::ParseExpression( idLexer &src ) {
	return ParseExpressionPriority( src, TOP_PRIORITY );
}


/*
===============
idMaterial::ClearStage
===============
*/
void idMaterial::ClearStage( shaderStage_t *ss ) {
	ss->drawStateBits = 0;
	ss->mStageRegisterStart = numRegisters;
	ss->mNumStageRegisters = 0;
	ss->mStageOpsStart = numOps;
	ss->mNumStageOps = 0;
	ss->conditionRegister = GetExpressionConstant( 1 );
	ss->alphaTestMode = GL_GREATER;
	ss->alphaTestRegister = GetExpressionConstant( 0.5f );
	ss->color.registers[0] =
	ss->color.registers[1] =
	ss->color.registers[2] =
	ss->color.registers[3] = GetExpressionConstant( 1 );
}

/*
===============
idMaterial::NameToSrcBlendMode
===============
*/
int idMaterial::NameToSrcBlendMode( const idStr &name ) {
	if ( !name.Icmp( "GL_ONE" ) ) {
		return GLS_SRCBLEND_ONE;
	} else if ( !name.Icmp( "GL_ZERO" ) ) {
		return GLS_SRCBLEND_ZERO;
	} else if ( !name.Icmp( "GL_DST_COLOR" ) ) {
		return GLS_SRCBLEND_DST_COLOR;
	} else if ( !name.Icmp( "GL_ONE_MINUS_DST_COLOR" ) ) {
		return GLS_SRCBLEND_ONE_MINUS_DST_COLOR;
	} else if ( !name.Icmp( "GL_SRC_ALPHA" ) ) {
		return GLS_SRCBLEND_SRC_ALPHA;
	} else if ( !name.Icmp( "GL_ONE_MINUS_SRC_ALPHA" ) ) {
		return GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA;
	} else if ( !name.Icmp( "GL_DST_ALPHA" ) ) {
		return GLS_SRCBLEND_DST_ALPHA;
	} else if ( !name.Icmp( "GL_ONE_MINUS_DST_ALPHA" ) ) {
		return GLS_SRCBLEND_ONE_MINUS_DST_ALPHA;
	} else if ( !name.Icmp( "GL_SRC_COLOR" ) ) {
		return GLS_SRCBLEND_SRC_COLOR;
	} else if ( !name.Icmp( "GL_ONE_MINUS_SRC_COLOR" ) ) {
		return GLS_SRCBLEND_ONE_MINUS_SRC_COLOR;
	} else if ( !name.Icmp( "GL_SRC_ALPHA_SATURATE" ) ) {
		return GLS_SRCBLEND_ALPHA_SATURATE;
	}

	common->Warning( "unknown blend mode '%s' in material '%s'", name.c_str(), GetName() );
	SetMaterialFlag( MF_DEFAULTED );

	return GLS_SRCBLEND_ONE;
}

/*
===============
idMaterial::NameToDstBlendMode
===============
*/
int idMaterial::NameToDstBlendMode( const idStr &name ) {
	if ( !name.Icmp( "GL_ONE" ) ) {
		return GLS_DSTBLEND_ONE;
	} else if ( !name.Icmp( "GL_ZERO" ) ) {
		return GLS_DSTBLEND_ZERO;
	} else if ( !name.Icmp( "GL_SRC_ALPHA" ) ) {
		return GLS_DSTBLEND_SRC_ALPHA;
	} else if ( !name.Icmp( "GL_ONE_MINUS_SRC_ALPHA" ) ) {
		return GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	} else if ( !name.Icmp( "GL_DST_ALPHA" ) ) {
		return GLS_DSTBLEND_DST_ALPHA;
	} else if ( !name.Icmp( "GL_ONE_MINUS_DST_ALPHA" ) ) {
		return GLS_DSTBLEND_ONE_MINUS_DST_ALPHA;
	} else if ( !name.Icmp( "GL_SRC_COLOR" ) ) {
		return GLS_DSTBLEND_SRC_COLOR;
	} else if ( !name.Icmp( "GL_ONE_MINUS_SRC_COLOR" ) ) {
		return GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
	}

	common->Warning( "unknown blend mode '%s' in material '%s'", name.c_str(), GetName() );
	SetMaterialFlag( MF_DEFAULTED );

	return GLS_DSTBLEND_ONE;
}

/*
================
idMaterial::ParseBlend
================
*/
void idMaterial::ParseBlend( idLexer &src, shaderStage_t *stage ) {
	idToken token;
	int		srcBlend, dstBlend;

	if ( !src.ReadToken( &token ) ) {
		return;
	}

	// blending combinations
	if ( !token.Icmp( "blend" ) ) {
		stage->drawStateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
		return;
	}
	if ( !token.Icmp( "add" ) ) {
		stage->drawStateBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
		return;
	}
	if ( !token.Icmp( "filter" ) || !token.Icmp( "modulate" ) ) {
		stage->drawStateBits = GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
		return;
	}
	if (  !token.Icmp( "none" ) ) {
		// none is used when defining an alpha mask that doesn't draw
		stage->drawStateBits = GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE;
		return;
	}
	if ( !token.Icmp( "bumpmap" ) ) {
		stage->lighting = SL_BUMP;
		return;
	}
	if ( !token.Icmp( "diffusemap" ) ) {
		stage->lighting = SL_DIFFUSE;
		return;
	}
	if ( !token.Icmp( "specularmap" ) ) {
		stage->lighting = SL_SPECULAR;
		return;
	}

	srcBlend = NameToSrcBlendMode( token );

	MatchToken( src, "," );
	if ( !src.ReadToken( &token ) ) {
		return;
	}
	dstBlend = NameToDstBlendMode( token );

	stage->drawStateBits = srcBlend | dstBlend;
}

/*
================
idMaterial::ParseVertexParm

If there is a single value, it will be repeated across all elements
If there are two values, 3 = 0.0, 4 = 1.0
if there are three values, 4 = 1.0
================
*/
void idMaterial::ParseVertexParm( idLexer &src, newShaderStage_t *newStage ) {
	idToken				token;

	src.ReadTokenOnLine( &token );
	int	parm = token.GetIntValue();
	if ( !token.IsNumeric() || parm < 0 || parm >= MAX_VERTEX_PARMS ) {
		common->Warning( "bad vertexParm number\n" );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}
	if ( parm >= newStage->numVertexParms ) {
		newStage->numVertexParms = parm+1;
	}

	newStage->vertexParms[parm][0] = ParseExpression( src );

	src.ReadTokenOnLine( &token );
	if ( !token[0] || token.Icmp( "," ) ) {
		newStage->vertexParms[parm][1] =
		newStage->vertexParms[parm][2] =
		newStage->vertexParms[parm][3] = newStage->vertexParms[parm][0];
		return;
	}

	newStage->vertexParms[parm][1] = ParseExpression( src );

	src.ReadTokenOnLine( &token );
	if ( !token[0] || token.Icmp( "," ) ) {
		newStage->vertexParms[parm][2] = GetExpressionConstant( 0 );
		newStage->vertexParms[parm][3] = GetExpressionConstant( 1 );
		return;
	}

	newStage->vertexParms[parm][2] = ParseExpression( src );

	src.ReadTokenOnLine( &token );
	if ( !token[0] || token.Icmp( "," ) ) {
		newStage->vertexParms[parm][3] = GetExpressionConstant( 1 );
		return;
	}

	newStage->vertexParms[parm][3] = ParseExpression( src );
}

/*
================
idMaterial::ParseFragmentParm

If there is a single value, it will be repeated across all elements
If there are two values, 3 = 0.0, 4 = 1.0
if there are three values, 4 = 1.0
================
*/
void idMaterial::ParseFragmentParm( idLexer &src, newShaderStage_t *newStage ) {
	idToken				token;

	src.ReadTokenOnLine( &token );
	int	parm = token.GetIntValue();
	if ( !token.IsNumeric() || parm < 0 || parm >= MAX_FRAGMENT_PARMS ) {
		common->Warning( "bad fragmentParm number\n" );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}
	if ( parm >= newStage->numFragmentParms ) {
		newStage->numFragmentParms = parm+1;
	}

	newStage->fragmentParms[parm][0] = ParseExpression( src );

	src.ReadTokenOnLine( &token );
	if ( !token[0] || token.Icmp( "," ) ) {
		newStage->fragmentParms[parm][1] =
		newStage->fragmentParms[parm][2] =
		newStage->fragmentParms[parm][3] = newStage->fragmentParms[parm][0];
		return;
	}

	newStage->fragmentParms[parm][1] = ParseExpression( src );

	src.ReadTokenOnLine( &token );
	if ( !token[0] || token.Icmp( "," ) ) {
		newStage->fragmentParms[parm][2] = GetExpressionConstant( 0 );
		newStage->fragmentParms[parm][3] = GetExpressionConstant( 1 );
		return;
	}

	newStage->fragmentParms[parm][2] = ParseExpression( src );

	src.ReadTokenOnLine( &token );
	if ( !token[0] || token.Icmp( "," ) ) {
		newStage->fragmentParms[parm][3] = GetExpressionConstant( 1 );
		return;
	}

	newStage->fragmentParms[parm][3] = ParseExpression( src );
}


/*
================
idMaterial::ParseFragmentMap
================
*/
void idMaterial::ParseFragmentMap( idLexer &src, newShaderStage_t *newStage ) {
	const char			*str;
	textureFilter_t		tf;
	textureRepeat_t		trp;
	textureUsage_t		td;
	cubeFiles_t			cubeMap;
	bool				allowPicmip;
	unsigned int		imageFlags;
	idToken				token;

	tf = TF_DEFAULT;
	trp = TR_REPEAT;
	td = TD_DEFAULT;
	allowPicmip = true;
	imageFlags = 0;
	cubeMap = CF_2D;

	src.ReadTokenOnLine( &token );
	int	unit = token.GetIntValue();
	if ( !token.IsNumeric() || unit < 0 || unit >= MAX_FRAGMENT_IMAGES ) {
		common->Warning( "bad fragmentMap number\n" );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	// unit 1 is the normal map.. make sure it gets flagged as the proper depth
	if ( unit == 1 ) {
		td = TD_BUMP;
	}

	if ( unit >= newStage->numFragmentProgramImages ) {
		newStage->numFragmentProgramImages = unit+1;
	}

	while( 1 ) {
		src.ReadTokenOnLine( &token );

		if ( !token.Icmp( "cubeMap" ) ) {
			cubeMap = CF_NATIVE;
			continue;
		}
		if ( !token.Icmp( "cameraCubeMap" ) ) {
			cubeMap = CF_CAMERA;
			continue;
		}
		if ( !token.Icmp( "nearest" ) ) {
			tf = TF_NEAREST;
			continue;
		}
		if ( !token.Icmp( "linear" ) ) {
			tf = TF_LINEAR;
			continue;
		}
		if ( !token.Icmp( "clamp" ) ) {
			trp = TR_CLAMP;
			continue;
		}
		if ( !token.Icmp( "noclamp" ) ) {
			trp = TR_REPEAT;
			continue;
		}
		if ( !token.Icmp( "zeroclamp" ) ) {
			trp = TR_CLAMP_TO_ZERO;
			continue;
		}
		if ( !token.Icmp( "alphazeroclamp" ) ) {
			trp = TR_CLAMP_TO_ZERO_ALPHA;
			continue;
		}
		if ( !token.Icmp( "mirroredrepeat" ) ) {
			trp = TR_MIRRORED_REPEAT;
			continue;
		}
		// NV_texture_shader's DSDT qualifier selected a signed two-channel
		// displacement texture format. Modern backends sample the authored
		// image directly; retain the bump-map usage hint and consume the
		// qualifier so it cannot be mistaken for the image program.
		if ( !token.Icmp( "dsdt" ) ) {
			td = TD_BUMP;
			continue;
		}
		if ( !token.Icmp( "forceHighQuality" ) ) {
			td = R_ApplyMaterialHighQualityUsage( td, true );
			continue;
		}

		if ( !token.Icmp( "uncompressed" ) || !token.Icmp( "highquality" ) ) {
			td = R_ApplyMaterialHighQualityUsage( td, false );
			continue;
		}
		if ( !token.Icmp( "nopicmip" ) ) {
			allowPicmip = false;
			continue;
		}
		if ( !token.Icmp( "nomips" ) ) {
			imageFlags = R_ApplyMaterialNoMipFlags( imageFlags );
			continue;
		}

		if ( !token.Icmp( "lightfalloffImage" ) ) {
			newStage->fragmentProgramBindings[unit] = LEGACY_FRAGMENT_BINDING_LIGHT_FALLOFF;
			newStage->fragmentProgramImages[unit] = NULL;
			return;
		}
		if ( !token.Icmp( "lightImage" ) ) {
			newStage->fragmentProgramBindings[unit] = LEGACY_FRAGMENT_BINDING_LIGHT_IMAGE;
			newStage->fragmentProgramImages[unit] = NULL;
			return;
		}
		if ( !token.Icmp( "ambientNormalMap" ) ) {
			newStage->fragmentProgramBindings[unit] = LEGACY_FRAGMENT_BINDING_AMBIENT_NORMAL_MAP;
			newStage->fragmentProgramImages[unit] = globalImages->ambientNormalMap;
			return;
		}
		if ( !token.Icmp( "normalCubeMap" ) ) {
			newStage->fragmentProgramBindings[unit] = LEGACY_FRAGMENT_BINDING_NORMAL_CUBE_MAP;
			newStage->fragmentProgramImages[unit] = globalImages->normalCubeMapImage;
			return;
		}
		if ( !token.Icmp( "specularTableImage" ) ) {
			newStage->fragmentProgramBindings[unit] = LEGACY_FRAGMENT_BINDING_SPECULAR_TABLE;
			newStage->fragmentProgramImages[unit] = globalImages->specularTableImage;
			return;
		}

		// assume anything else is the image name
		src.UnreadToken( &token );
		break;
	}
	str = R_ParsePastImageProgram( src );

	newStage->fragmentProgramBindings[unit] = LEGACY_FRAGMENT_BINDING_NONE;
	newStage->fragmentProgramImages[unit] =
		R_LoadMaterialImage( str, tf, trp, td, cubeMap, allowPicmip, imageFlags );
	if ( !newStage->fragmentProgramImages[unit] ) {
		newStage->fragmentProgramImages[unit] = globalImages->defaultImage;
	}
}

/*
================
idMaterial::ParseShaderParm

GLSL shader parameter parser used by Quake 4 style "shaderParm" tokens.
================
*/
static glslShaderParmBinding_t R_ParseGLSLShaderParmBinding( const idToken &token ) {
	if ( !token.Icmp( "localLightOrigin" ) || !token.Icmp( "lightOrigin" ) ) {
		return GLSL_SHADERPARM_LOCAL_LIGHT_ORIGIN;
	}
	if ( !token.Icmp( "localViewOrigin" ) ) {
		return GLSL_SHADERPARM_LOCAL_VIEW_ORIGIN;
	}
	if ( !token.Icmp( "lightProjectS" ) || !token.Icmp( "lightProjectionS" ) || !token.Icmp( "lightProject_s" ) ) {
		return GLSL_SHADERPARM_LIGHT_PROJECT_S;
	}
	if ( !token.Icmp( "lightProjectT" ) || !token.Icmp( "lightProjectionT" ) || !token.Icmp( "lightProject_t" ) ) {
		return GLSL_SHADERPARM_LIGHT_PROJECT_T;
	}
	if ( !token.Icmp( "lightProjectQ" ) || !token.Icmp( "lightProjectionQ" ) || !token.Icmp( "lightProject_q" ) ) {
		return GLSL_SHADERPARM_LIGHT_PROJECT_Q;
	}
	if ( !token.Icmp( "lightFalloffS" ) || !token.Icmp( "lightFalloff_s" ) ) {
		return GLSL_SHADERPARM_LIGHT_FALLOFF_S;
	}
	if ( !token.Icmp( "bumpMatrixS" ) || !token.Icmp( "bumpMatrix_s" ) ) {
		return GLSL_SHADERPARM_BUMP_MATRIX_S;
	}
	if ( !token.Icmp( "bumpMatrixT" ) || !token.Icmp( "bumpMatrix_t" ) ) {
		return GLSL_SHADERPARM_BUMP_MATRIX_T;
	}
	if ( !token.Icmp( "diffuseMatrixS" ) || !token.Icmp( "diffuseMatrix_s" ) ) {
		return GLSL_SHADERPARM_DIFFUSE_MATRIX_S;
	}
	if ( !token.Icmp( "diffuseMatrixT" ) || !token.Icmp( "diffuseMatrix_t" ) ) {
		return GLSL_SHADERPARM_DIFFUSE_MATRIX_T;
	}
	if ( !token.Icmp( "specularMatrixS" ) || !token.Icmp( "specularMatrix_s" ) ) {
		return GLSL_SHADERPARM_SPECULAR_MATRIX_S;
	}
	if ( !token.Icmp( "specularMatrixT" ) || !token.Icmp( "specularMatrix_t" ) ) {
		return GLSL_SHADERPARM_SPECULAR_MATRIX_T;
	}
	if ( !token.Icmp( "colorModulate" ) ) {
		return GLSL_SHADERPARM_COLOR_MODULATE;
	}
	if ( !token.Icmp( "colorAdd" ) ) {
		return GLSL_SHADERPARM_COLOR_ADD;
	}
	if ( !token.Icmp( "diffuseColor" ) || !token.Icmp( "diffuse" ) ) {
		return GLSL_SHADERPARM_DIFFUSE_COLOR;
	}
	if ( !token.Icmp( "specularColor" ) || !token.Icmp( "specular" ) ) {
		return GLSL_SHADERPARM_SPECULAR_COLOR;
	}
	if ( !token.Icmp( "viewOrigin" ) ) {
		return GLSL_SHADERPARM_VIEW_ORIGIN;
	}
	if ( !token.Icmp( "colorMatrix0" ) ) {
		return GLSL_SHADERPARM_COLOR_MATRIX0;
	}
	if ( !token.Icmp( "colorMatrix1" ) ) {
		return GLSL_SHADERPARM_COLOR_MATRIX1;
	}
	if ( !token.Icmp( "colorMatrix2" ) ) {
		return GLSL_SHADERPARM_COLOR_MATRIX2;
	}
	if ( !token.Icmp( "projectionRow0" ) || !token.Icmp( "projectionMatrix0" ) ) {
		return GLSL_SHADERPARM_PROJECTION_ROW_0;
	}
	if ( !token.Icmp( "projectionRow1" ) || !token.Icmp( "projectionMatrix1" ) ) {
		return GLSL_SHADERPARM_PROJECTION_ROW_1;
	}
	if ( !token.Icmp( "projectionRow2" ) || !token.Icmp( "projectionMatrix2" ) ) {
		return GLSL_SHADERPARM_PROJECTION_ROW_2;
	}
	if ( !token.Icmp( "projectionRow3" ) || !token.Icmp( "projectionMatrix3" ) ) {
		return GLSL_SHADERPARM_PROJECTION_ROW_3;
	}
	if ( !token.Icmp( "modelRow0" ) || !token.Icmp( "modelMatrix0" ) ) {
		return GLSL_SHADERPARM_MODEL_ROW_0;
	}
	if ( !token.Icmp( "modelRow1" ) || !token.Icmp( "modelMatrix1" ) ) {
		return GLSL_SHADERPARM_MODEL_ROW_1;
	}
	if ( !token.Icmp( "modelRow2" ) || !token.Icmp( "modelMatrix2" ) ) {
		return GLSL_SHADERPARM_MODEL_ROW_2;
	}
	if ( !token.Icmp( "gaussianSampleOffsets" ) ) {
		return GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS;
	}
	if ( !token.Icmp( "gaussianSampleOffsetsHorizontal" ) ) {
		return GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS_HORIZONTAL;
	}
	if ( !token.Icmp( "gaussianSampleOffsetsVertical" ) ) {
		return GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS_VERTICAL;
	}
	if ( !token.Icmp( "gaussianSampleWeights" ) ) {
		return GLSL_SHADERPARM_GAUSSIAN_SAMPLE_WEIGHTS;
	}
	if ( !token.Icmp( "gaussianSampleWeights2" ) ) {
		return GLSL_SHADERPARM_GAUSSIAN_SAMPLE_WEIGHTS2;
	}
	if ( !token.Icmp( "postProcessInvTexSize" ) ) {
		return GLSL_SHADERPARM_POSTPROCESS_INV_TEX_SIZE;
	}
	if ( !token.Icmp( "postProcessTexSize" ) ) {
		return GLSL_SHADERPARM_POSTPROCESS_TEX_SIZE;
	}
	if ( !token.Icmp( "postProcessSourceColorSpace" ) ) {
		return GLSL_SHADERPARM_POSTPROCESS_SOURCE_COLOR_SPACE;
	}
	if ( !token.Icmp( "postProcessSMAAQuality" ) ) {
		return GLSL_SHADERPARM_POSTPROCESS_SMAA_QUALITY;
	}
	if ( !token.Icmp( "currentRenderViewportOrigin" ) ) {
		return GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_ORIGIN;
	}
	if ( !token.Icmp( "currentRenderViewportSize" ) ) {
		return GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_SIZE;
	}
	if ( !token.Icmp( "currentRenderTextureScale" ) ) {
		return GLSL_SHADERPARM_CURRENT_RENDER_TEXTURE_SCALE;
	}

	return GLSL_SHADERPARM_REGISTERS;
}

void idMaterial::ParseShaderParm( idLexer &src, newShaderStage_t *newStage ) {
	idToken token;

	if ( !newStage->glslProgram ) {
		common->Warning( "shaderParm specified before glslProgram in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	if ( newStage->numShaderParms >= MAX_GLSL_SHADER_PARMS ) {
		common->Warning( "material '%s' exceeded MAX_GLSL_SHADER_PARMS", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	if ( !src.ReadTokenOnLine( &token ) ) {
		common->Warning( "missing shaderParm name in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	const int slot = newStage->numShaderParms++;
	idStr::Copynz( newStage->shaderParmNames[slot], token.c_str(), MAX_GLSL_SHADER_PARM_NAME );
	newStage->shaderParmBindings[slot] = GLSL_SHADERPARM_REGISTERS;
	newStage->shaderParmNumRegisters[slot] = 0;
	memset( newStage->shaderParmRegisters[slot], 0, sizeof( newStage->shaderParmRegisters[slot] ) );

	if ( !src.ReadTokenOnLine( &token ) ) {
		common->Warning( "missing shaderParm value for '%s' in material '%s'",
			newStage->shaderParmNames[slot], GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	const glslShaderParmBinding_t binding = R_ParseGLSLShaderParmBinding( token );
	if ( binding != GLSL_SHADERPARM_REGISTERS ) {
		newStage->shaderParmBindings[slot] = binding;
		return;
	}

	src.UnreadToken( &token );

	int count = 0;
	while ( count < 4 ) {
		newStage->shaderParmRegisters[slot][count] = ParseExpression( src );
		count++;

		if ( count == 4 ) {
			break;
		}

		if ( !src.ReadTokenOnLine( &token ) ) {
			break;
		}

		if ( token != "," ) {
			src.UnreadToken( &token );
			break;
		}
	}

	newStage->shaderParmNumRegisters[slot] = count;
}

/*
================
idMaterial::ParseShaderTexture

GLSL shader texture parser used by Quake 4 style "shaderTexture" tokens.
================
*/
void idMaterial::ParseShaderTexture( idLexer &src, newShaderStage_t *newStage ) {
	const char			*str;
	textureFilter_t		tf;
	textureRepeat_t		trp;
	textureUsage_t		td;
	cubeFiles_t			cubeMap;
	bool				allowPicmip;
	unsigned int		imageFlags;
	bool				explicitFilter;
	bool				explicitRepeat;
	idToken				token;

	if ( !newStage->glslProgram ) {
		common->Warning( "shaderTexture specified before glslProgram in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	if ( newStage->numShaderTextures >= MAX_FRAGMENT_IMAGES ) {
		common->Warning( "material '%s' exceeded shaderTexture limit (%d)", GetName(), MAX_FRAGMENT_IMAGES );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	if ( !src.ReadTokenOnLine( &token ) ) {
		common->Warning( "missing shaderTexture name in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}

	const int slot = newStage->numShaderTextures++;
	idStr::Copynz( newStage->shaderTextureNames[slot], token.c_str(), MAX_GLSL_SHADER_PARM_NAME );
	newStage->shaderTextureBindings[slot] = GLSL_SHADERTEXTURE_IMAGE;

	tf = TF_DEFAULT;
	trp = TR_REPEAT;
	td = TD_DEFAULT;
	allowPicmip = true;
	imageFlags = 0;
	explicitFilter = false;
	explicitRepeat = false;
	cubeMap = CF_2D;

	while( 1 ) {
		if ( !src.ReadTokenOnLine( &token ) ) {
			common->Warning( "missing shaderTexture image for '%s' in material '%s'",
				newStage->shaderTextureNames[slot], GetName() );
			SetMaterialFlag( MF_DEFAULTED );
			return;
		}

		if ( !token.Icmp( "cubeMap" ) ) {
			cubeMap = CF_NATIVE;
			continue;
		}
		if ( !token.Icmp( "cameraCubeMap" ) ) {
			cubeMap = CF_CAMERA;
			continue;
		}
		if ( !token.Icmp( "nearest" ) ) {
			tf = TF_NEAREST;
			explicitFilter = true;
			continue;
		}
		if ( !token.Icmp( "linear" ) ) {
			tf = TF_LINEAR;
			explicitFilter = true;
			continue;
		}
		if ( !token.Icmp( "clamp" ) ) {
			trp = TR_CLAMP;
			explicitRepeat = true;
			continue;
		}
		if ( !token.Icmp( "noclamp" ) ) {
			trp = TR_REPEAT;
			explicitRepeat = true;
			continue;
		}
		if ( !token.Icmp( "zeroclamp" ) ) {
			trp = TR_CLAMP_TO_ZERO;
			explicitRepeat = true;
			continue;
		}
		if ( !token.Icmp( "alphazeroclamp" ) ) {
			trp = TR_CLAMP_TO_ZERO_ALPHA;
			explicitRepeat = true;
			continue;
		}
		if ( !token.Icmp( "mirroredrepeat" ) ) {
			trp = TR_MIRRORED_REPEAT;
			explicitRepeat = true;
			continue;
		}
		if ( !token.Icmp( "forceHighQuality" ) ) {
			td = R_ApplyMaterialHighQualityUsage( td, true );
			continue;
		}
		if ( !token.Icmp( "uncompressed" ) || !token.Icmp( "highquality" ) ) {
			td = R_ApplyMaterialHighQualityUsage( td, false );
			continue;
		}
		if ( !token.Icmp( "nopicmip" ) ) {
			allowPicmip = false;
			continue;
		}
		if ( !token.Icmp( "nomips" ) ) {
			imageFlags = R_ApplyMaterialNoMipFlags( imageFlags );
			continue;
		}

		if ( !token.Icmp( "lightfalloffImage" ) ) {
			newStage->shaderTextureBindings[slot] = GLSL_SHADERTEXTURE_LIGHT_FALLOFF;
			newStage->shaderTextureImages[slot] = NULL;
			newStage->shaderTextureFilters[slot] = tf;
			newStage->shaderTextureRepeats[slot] = trp;
			return;
		}
		if ( !token.Icmp( "lightImage" ) ) {
			newStage->shaderTextureBindings[slot] = GLSL_SHADERTEXTURE_LIGHT_IMAGE;
			newStage->shaderTextureImages[slot] = NULL;
			newStage->shaderTextureFilters[slot] = tf;
			newStage->shaderTextureRepeats[slot] = trp;
			return;
		}
		if ( !token.Icmp( "ambientNormalMap" ) ) {
			newStage->shaderTextureBindings[slot] = GLSL_SHADERTEXTURE_AMBIENT_NORMAL_MAP;
			newStage->shaderTextureImages[slot] = globalImages->ambientNormalMap ? globalImages->ambientNormalMap : globalImages->defaultImage;
			newStage->shaderTextureFilters[slot] = explicitFilter ? tf : newStage->shaderTextureImages[slot]->GetFilter();
			newStage->shaderTextureRepeats[slot] = explicitRepeat ? trp : newStage->shaderTextureImages[slot]->GetRepeat();
			return;
		}
		if ( !token.Icmp( "normalCubeMap" ) ) {
			newStage->shaderTextureBindings[slot] = GLSL_SHADERTEXTURE_NORMAL_CUBE_MAP;
			newStage->shaderTextureImages[slot] = globalImages->normalCubeMapImage ? globalImages->normalCubeMapImage : globalImages->defaultImage;
			newStage->shaderTextureFilters[slot] = explicitFilter ? tf : newStage->shaderTextureImages[slot]->GetFilter();
			newStage->shaderTextureRepeats[slot] = explicitRepeat ? trp : newStage->shaderTextureImages[slot]->GetRepeat();
			return;
		}
		if ( !token.Icmp( "specularTableImage" ) ) {
			newStage->shaderTextureBindings[slot] = GLSL_SHADERTEXTURE_SPECULAR_TABLE;
			newStage->shaderTextureImages[slot] = globalImages->specularTableImage ? globalImages->specularTableImage : globalImages->defaultImage;
			newStage->shaderTextureFilters[slot] = explicitFilter ? tf : newStage->shaderTextureImages[slot]->GetFilter();
			newStage->shaderTextureRepeats[slot] = explicitRepeat ? trp : newStage->shaderTextureImages[slot]->GetRepeat();
			return;
		}

		// assume anything else is the image name
		src.UnreadToken( &token );
		break;
	}

	str = R_ParsePastImageProgram( src );
	newStage->shaderTextureImages[slot] =
		R_LoadMaterialImage( str, tf, trp, td, cubeMap, allowPicmip, imageFlags );
	if ( !newStage->shaderTextureImages[slot] ) {
		newStage->shaderTextureImages[slot] = globalImages->defaultImage;
	}

	const idImage *image = newStage->shaderTextureImages[slot];
	newStage->shaderTextureFilters[slot] = explicitFilter ? tf : image->GetFilter();
	newStage->shaderTextureRepeats[slot] = explicitRepeat ? trp : image->GetRepeat();
}

/*
===============
idMaterial::MultiplyTextureMatrix
===============
*/
void idMaterial::MultiplyTextureMatrix( textureStage_t *ts, int registers[2][3] ) {
	int		old[2][3];

	if ( !ts->hasMatrix ) {
		ts->hasMatrix = true;
		memcpy( ts->matrix, registers, sizeof( ts->matrix ) );
		return;
	}

	memcpy( old, ts->matrix, sizeof( old ) );

	// multiply the two maticies
	ts->matrix[0][0] = EmitOp(
							EmitOp( old[0][0], registers[0][0], OP_TYPE_MULTIPLY ),
							EmitOp( old[0][1], registers[1][0], OP_TYPE_MULTIPLY ), OP_TYPE_ADD );
	ts->matrix[0][1] = EmitOp(
							EmitOp( old[0][0], registers[0][1], OP_TYPE_MULTIPLY ),
							EmitOp( old[0][1], registers[1][1], OP_TYPE_MULTIPLY ), OP_TYPE_ADD );
	ts->matrix[0][2] = EmitOp( 
							EmitOp(
								EmitOp( old[0][0], registers[0][2], OP_TYPE_MULTIPLY ),
								EmitOp( old[0][1], registers[1][2], OP_TYPE_MULTIPLY ), OP_TYPE_ADD ),
							old[0][2], OP_TYPE_ADD );

	ts->matrix[1][0] = EmitOp(
							EmitOp( old[1][0], registers[0][0], OP_TYPE_MULTIPLY ),
							EmitOp( old[1][1], registers[1][0], OP_TYPE_MULTIPLY ), OP_TYPE_ADD );
	ts->matrix[1][1] = EmitOp(
							EmitOp( old[1][0], registers[0][1], OP_TYPE_MULTIPLY ),
							EmitOp( old[1][1], registers[1][1], OP_TYPE_MULTIPLY ), OP_TYPE_ADD );
	ts->matrix[1][2] = EmitOp( 
							EmitOp(
								EmitOp( old[1][0], registers[0][2], OP_TYPE_MULTIPLY ),
								EmitOp( old[1][1], registers[1][2], OP_TYPE_MULTIPLY ), OP_TYPE_ADD ),
							old[1][2], OP_TYPE_ADD );

}

/*
=================
idMaterial::ParseStage

An open brace has been parsed


{
	if <expression>
	map <imageprogram>
	"nearest" "linear" "clamp" "zeroclamp" "uncompressed" "highquality" "nopicmip"
	scroll, scale, rotate
}

=================
*/
void idMaterial::ParseStage( idLexer &src, const textureRepeat_t trpDefault ) {
	idToken				token;
	const char			*str;
	shaderStage_t		*ss;
	textureStage_t		*ts;
	textureFilter_t		tf;
	textureRepeat_t		trp;
	textureUsage_t		td;
	cubeFiles_t			cubeMap;
	bool				allowPicmip;
	unsigned int		imageFlags;
	char				imageName[MAX_IMAGE_NAME];
	int					a, b;
	int					matrix[2][3];
	newShaderStage_t	newStage;
	bool				stageHasShaderTokens;

	if ( numStages >= MAX_SHADER_STAGES ) {
		SetMaterialFlag( MF_DEFAULTED );
		common->Warning( "material '%s' exceeded %i stages", GetName(), MAX_SHADER_STAGES );
		// bail before pd->parseStages[numStages] writes past the array;
		// the caller already consumed the opening brace
		src.SkipBracedSection( false );
		return;
	}

	tf = TF_DEFAULT;
	trp = trpDefault;
	td = R_DefaultStageUsageForMaterial( GetName() );
	allowPicmip = true;
	imageFlags = 0;
	cubeMap = CF_2D;

	imageName[0] = 0;
	stageHasShaderTokens = false;

	memset( &newStage, 0, sizeof( newStage ) );

	ss = &pd->parseStages[numStages];
	ts = &ss->texture;

	// Parse-stage structs are reused from a stack aggregate. Clear every field
	// first so stale stage state cannot leak into materials that omit tokens.
	memset( ss, 0, sizeof( *ss ) );

	ClearStage( ss );

	while ( 1 ) {
		if ( TestMaterialFlag( MF_DEFAULTED ) ) {	// we have a parse error
			return;
		}
		if ( !src.ExpectAnyToken( &token ) ) {
			SetMaterialFlag( MF_DEFAULTED );
			return;
		}

		// the close brace for the entire material ends the draw block
		if ( token == "}" ) {
			break;
		}

		//BSM Nerve: Added for stage naming in the material editor
		if( !token.Icmp( "name") ) {
			src.SkipRestOfLine();
			continue;
		}

		// Quake 4 materials can gate stages with an if expression (e.g. glslPrograms == 1)
		if ( !token.Icmp( "if" ) ) {
			ss->conditionRegister = ParseExpression( src );
			continue;
		}

		// image options
		if ( !token.Icmp( "blend" ) ) {
			ParseBlend( src, ss );
			continue;
		}

		if (  !token.Icmp( "map" ) ) {
			str = R_ParsePastImageProgram( src );
			idStr::Copynz( imageName, str, sizeof( imageName ) );
			continue;
		}

// jmarshall: quake 4 materials
		if (!token.Icmp("nomips")) {
			imageFlags = R_ApplyMaterialNoMipFlags( imageFlags );
			continue;
		}
// jmarshall end

		if (  !token.Icmp( "remoteRenderMap" ) ) {
			ts->dynamic = DI_REMOTE_RENDER;
			ts->width = src.ParseInt();
			ts->height = src.ParseInt();
			continue;
		}

		if (  !token.Icmp( "mirrorRenderMap" ) ) {
			ts->dynamic = DI_MIRROR_RENDER;
			ts->width = src.ParseInt();
			ts->height = src.ParseInt();
			ts->texgen = TG_SCREEN;
			continue;
		}

		if ( !token.Icmp( "reflectionRenderMap" ) ) {
			ts->dynamic = DI_REFLECTION_RENDER;
			ts->width = src.ParseInt();
			ts->height = src.ParseInt();
			continue;
		}

		if ( !token.Icmp( "refractionRenderMap" ) ) {
			ts->dynamic = DI_REFRACTION_RENDER;
			ts->width = src.ParseInt();
			ts->height = src.ParseInt();
			ts->texgen = TG_SCREEN;
			continue;
		}

		if (  !token.Icmp( "xrayRenderMap" ) ) {
			ts->dynamic = DI_XRAY_RENDER;
			ts->width = src.ParseInt();
			ts->height = src.ParseInt();
			ts->texgen = TG_SCREEN;
			continue;
		}
		if (  !token.Icmp( "screen" ) ) {
			ts->texgen = TG_SCREEN;
			continue;
		}
		if (  !token.Icmp( "screen2" ) ) {
			ts->texgen = TG_SCREEN2;
			continue;
		}
		if (  !token.Icmp( "glassWarp" ) ) {
			ts->texgen = TG_GLASSWARP;
			continue;
		}

		if ( !token.Icmp( "videomap" ) ) {
			// note that videomaps will always be in clamp mode, so texture
			// coordinates had better be in the 0 to 1 range
			if ( !src.ReadToken( &token ) ) {
				common->Warning( "missing parameter for 'videoMap' keyword in material '%s'", GetName() );
				continue;
			}
			bool loop = false;
			if ( !token.Icmp( "loop" ) ) {
				loop = true;
				if ( !src.ReadToken( &token ) ) {
					common->Warning( "missing parameter for 'videoMap' keyword in material '%s'", GetName() );
					continue;
				}
			}
			ts->cinematic = idCinematic::Alloc();
			ts->cinematic->InitFromFile( token.c_str(), loop );
			continue;
		}

		if ( !token.Icmp( "soundmap" ) ) {
			if ( !src.ReadToken( &token ) ) {
				common->Warning( "missing parameter for 'soundmap' keyword in material '%s'", GetName() );
				continue;
			}
			ts->cinematic = new idSndWindow();
			ts->cinematic->InitFromFile( token.c_str(), true );
			continue;
		}

		if ( !token.Icmp( "cubeMap" ) ) {
			str = R_ParsePastImageProgram( src );
			idStr::Copynz( imageName, str, sizeof( imageName ) );
			cubeMap = CF_NATIVE;
			continue;
		}

		if ( !token.Icmp( "cameraCubeMap" ) ) {
			str = R_ParsePastImageProgram( src );
			idStr::Copynz( imageName, str, sizeof( imageName ) );
			cubeMap = CF_CAMERA;
			continue;
		}

		if ( !token.Icmp( "ignoreAlphaTest" ) ) {
			ss->ignoreAlphaTest = true;
			continue;
		}
		if ( !token.Icmp( "nearest" ) ) {
			tf = TF_NEAREST;
			continue;
		}
		if ( !token.Icmp( "linear" ) ) {
			tf = TF_LINEAR;
			continue;
		}
		if ( !token.Icmp( "clamp" ) ) {
			trp = TR_CLAMP;
			continue;
		}
		if ( !token.Icmp( "noclamp" ) ) {
			trp = TR_REPEAT;
			continue;
		}
		if ( !token.Icmp( "zeroclamp" ) ) {
			trp = TR_CLAMP_TO_ZERO;
			continue;
		}
		if ( !token.Icmp( "alphazeroclamp" ) ) {
			trp = TR_CLAMP_TO_ZERO_ALPHA;
			continue;
		}
		if ( !token.Icmp( "mirroredrepeat" ) ) {
			trp = TR_MIRRORED_REPEAT;
			continue;
		}
		if ( !token.Icmp( "uncompressed" ) || !token.Icmp( "highquality" ) ) {
			td = R_ApplyMaterialHighQualityUsage( td, false );
			continue;
		}
		if ( !token.Icmp( "forceHighQuality" ) ) {
			td = R_ApplyMaterialHighQualityUsage( td, true );
			continue;
		}
		if ( !token.Icmp( "nopicmip" ) ) {
			allowPicmip = false;
			continue;
		}
		if ( !token.Icmp( "vertexColor" ) ) {
			ss->vertexColor = SVC_MODULATE;
			continue;
		}
		if ( !token.Icmp( "inverseVertexColor" ) ) {
			ss->vertexColor = SVC_INVERSE_MODULATE;
			continue;
		}

		// privatePolygonOffset
		else if ( !token.Icmp( "privatePolygonOffset" ) ) {
			if ( !src.ReadTokenOnLine( &token ) ) {
				ss->privatePolygonOffset = 1;
				continue;
			}
			// explict larger (or negative) offset
			src.UnreadToken( &token );
			ss->privatePolygonOffset = src.ParseFloat();
			continue;
		}

		// texture coordinate generation
		if ( !token.Icmp( "texGen" ) ) {
			src.ExpectAnyToken( &token );
			if ( !token.Icmp( "normal" ) ) {
				ts->texgen = TG_DIFFUSE_CUBE;
			} else if ( !token.Icmp( "reflect" ) ) {
				ts->texgen = TG_REFLECT_CUBE;
			} else if ( !token.Icmp( "skybox" ) ) {
				ts->texgen = TG_SKYBOX_CUBE;
			} else if ( !token.Icmp( "wobbleSky" ) ) {
				ts->texgen = TG_WOBBLESKY_CUBE;
				texGenRegisters[0] = ParseExpression( src );
				texGenRegisters[1] = ParseExpression( src );
				texGenRegisters[2] = ParseExpression( src );
			} else if ( !token.Icmp( "potCorrection" ) ) {
				// Retail Quake 4 only needs explicit POT correction on hardware
				// without NPOT textures; otherwise the stage stays on base coords.
				if ( !glConfig.textureNonPowerOfTwoAvailable ) {
					ts->texgen = TG_POT_CORRECTION;
				}
			} else {
				common->Warning( "bad texGen '%s' in material %s", token.c_str(), GetName() );
				SetMaterialFlag( MF_DEFAULTED );
			}
			continue;
		}
		if ( !token.Icmp( "scroll" ) || !token.Icmp( "translate" ) ) {
			a = ParseExpression( src );
			MatchToken( src, "," );
			b = ParseExpression( src );
			matrix[0][0] = GetExpressionConstant( 1 );
			matrix[0][1] = GetExpressionConstant( 0 );
			matrix[0][2] = a;
			matrix[1][0] = GetExpressionConstant( 0 );
			matrix[1][1] = GetExpressionConstant( 1 );
			matrix[1][2] = b;

			MultiplyTextureMatrix( ts, matrix );
			continue;
		}
		if ( !token.Icmp( "scale" ) ) {
			a = ParseExpression( src );
			MatchToken( src, "," );
			b = ParseExpression( src );
			// this just scales without a centering
			matrix[0][0] = a;
			matrix[0][1] = GetExpressionConstant( 0 );
			matrix[0][2] = GetExpressionConstant( 0 );
			matrix[1][0] = GetExpressionConstant( 0 );
			matrix[1][1] = b;
			matrix[1][2] = GetExpressionConstant( 0 );

			MultiplyTextureMatrix( ts, matrix );
			continue;
		}
		if ( !token.Icmp( "centerScale" ) ) {
			a = ParseExpression( src );
			MatchToken( src, "," );
			b = ParseExpression( src );
			// this subtracts 0.5, then scales, then adds 0.5
			matrix[0][0] = a;
			matrix[0][1] = GetExpressionConstant( 0 );
			matrix[0][2] = EmitOp( GetExpressionConstant( 0.5 ), EmitOp( GetExpressionConstant( 0.5 ), a, OP_TYPE_MULTIPLY ), OP_TYPE_SUBTRACT );
			matrix[1][0] = GetExpressionConstant( 0 );
			matrix[1][1] = b;
			matrix[1][2] = EmitOp( GetExpressionConstant( 0.5 ), EmitOp( GetExpressionConstant( 0.5 ), b, OP_TYPE_MULTIPLY ), OP_TYPE_SUBTRACT );

			MultiplyTextureMatrix( ts, matrix );
			continue;
		}
		if ( !token.Icmp( "shear" ) ) {
			a = ParseExpression( src );
			MatchToken( src, "," );
			b = ParseExpression( src );
			// this subtracts 0.5, then shears, then adds 0.5
			matrix[0][0] = GetExpressionConstant( 1 );
			matrix[0][1] = a;
			matrix[0][2] = EmitOp( GetExpressionConstant( -0.5 ), a, OP_TYPE_MULTIPLY );
			matrix[1][0] = b;
			matrix[1][1] = GetExpressionConstant( 1 );
			matrix[1][2] = EmitOp( GetExpressionConstant( -0.5 ), b, OP_TYPE_MULTIPLY );

			MultiplyTextureMatrix( ts, matrix );
			continue;
		}
		if ( !token.Icmp( "rotate" ) ) {
			const idDeclTable *table;
			int		sinReg, cosReg;

			// in cycles
			a = ParseExpression( src );

			table = static_cast<const idDeclTable *>( declManager->FindType( DECL_TABLE, "sinTable", false ) );
			if ( !table ) {
				common->Warning( "no sinTable for rotate defined" );
				SetMaterialFlag( MF_DEFAULTED );
				return;
			}
			sinReg = EmitOp( table->Index(), a, OP_TYPE_TABLE );

			table = static_cast<const idDeclTable *>( declManager->FindType( DECL_TABLE, "cosTable", false ) );
			if ( !table ) {
				common->Warning( "no cosTable for rotate defined" );
				SetMaterialFlag( MF_DEFAULTED );
				return;
			}
			cosReg = EmitOp( table->Index(), a, OP_TYPE_TABLE );

			// this subtracts 0.5, then rotates, then adds 0.5
			matrix[0][0] = cosReg;
			matrix[0][1] = EmitOp( GetExpressionConstant( 0 ), sinReg, OP_TYPE_SUBTRACT );
			matrix[0][2] = EmitOp( EmitOp( EmitOp( GetExpressionConstant( -0.5 ), cosReg, OP_TYPE_MULTIPLY ), 
										EmitOp( GetExpressionConstant( 0.5 ), sinReg, OP_TYPE_MULTIPLY ), OP_TYPE_ADD ),
										GetExpressionConstant( 0.5 ), OP_TYPE_ADD );

			matrix[1][0] = sinReg;
			matrix[1][1] = cosReg;
			matrix[1][2] = EmitOp( EmitOp( EmitOp( GetExpressionConstant( -0.5 ), sinReg, OP_TYPE_MULTIPLY ), 
										EmitOp( GetExpressionConstant( -0.5 ), cosReg, OP_TYPE_MULTIPLY ), OP_TYPE_ADD ),
										GetExpressionConstant( 0.5 ), OP_TYPE_ADD );

			MultiplyTextureMatrix( ts, matrix );
			continue;
		}

		// color mask options
		if ( !token.Icmp( "maskRed" ) ) {
			ss->drawStateBits |= GLS_REDMASK;
			continue;
		}		
		if ( !token.Icmp( "maskGreen" ) ) {
			ss->drawStateBits |= GLS_GREENMASK;
			continue;
		}		
		if ( !token.Icmp( "maskBlue" ) ) {
			ss->drawStateBits |= GLS_BLUEMASK;
			continue;
		}		
		if ( !token.Icmp( "maskAlpha" ) ) {
			ss->drawStateBits |= GLS_ALPHAMASK;
			continue;
		}		
		if ( !token.Icmp( "maskColor" ) ) {
			ss->drawStateBits |= GLS_COLORMASK;
			continue;
		}		
		if ( !token.Icmp( "maskDepth" ) ) {
			ss->drawStateBits |= GLS_DEPTHMASK;
			continue;
		}		
		if ( !token.Icmp( "alphaTest" ) ) {
			ss->hasAlphaTest = true;
			ss->alphaTestRegister = ParseExpression( src );
			if ( !ss->hasAlphaFunc ) {
				ss->alphaTestMode = GL_GREATER;
			}
			coverage = MC_PERFORATED;
			continue;
		}		
		if ( !token.Icmp( "alphaFunc" ) ) {
			ss->hasAlphaFunc = true;
			ss->hasAlphaTest = true;
			ss->alphaTestMode = GL_GREATER;
			if ( src.ReadTokenOnLine( &token ) ) {
				if ( !token.Icmp( "less" ) ) {
					ss->alphaTestMode = GL_LESS;
				} else if ( !token.Icmp( "equal" ) ) {
					ss->alphaTestMode = GL_EQUAL;
				} else if ( !token.Icmp( "greater" ) ) {
					ss->alphaTestMode = GL_GREATER;
				} else {
					src.Warning( "unknown alphaFunc '%s' in material '%s'", token.c_str(), GetName() );
				}
			}
			coverage = MC_PERFORATED;
			continue;
		}

		// shorthand for 2D modulated
		if ( !token.Icmp( "colored" ) ) {
			ss->color.registers[0] = EXP_REG_PARM0;
			ss->color.registers[1] = EXP_REG_PARM1;
			ss->color.registers[2] = EXP_REG_PARM2;
			ss->color.registers[3] = EXP_REG_PARM3;
			pd->registersAreConstant = false;
			continue;
		}

		if ( !token.Icmp( "color" ) ) {
			ss->color.registers[0] = ParseExpression( src );
			MatchToken( src, "," );
			ss->color.registers[1] = ParseExpression( src );
			MatchToken( src, "," );
			ss->color.registers[2] = ParseExpression( src );
			MatchToken( src, "," );
			ss->color.registers[3] = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "red" ) ) {
			ss->color.registers[0] = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "green" ) ) {
			ss->color.registers[1] = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "blue" ) ) {
			ss->color.registers[2] = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "alpha" ) ) {
			ss->color.registers[3] = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "rgb" ) ) {
			ss->color.registers[0] = ss->color.registers[1] = 
				ss->color.registers[2] = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "rgba" ) ) {
			ss->color.registers[0] = ss->color.registers[1] = 
				ss->color.registers[2] = ss->color.registers[3] = ParseExpression( src );
			continue;
		}

		if ( !token.Icmp( "if" ) ) {
			ss->conditionRegister = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "program" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				newStage.vertexProgram = R_FindARBProgram( GL_VERTEX_PROGRAM_ARB, token.c_str() );
				newStage.fragmentProgram = R_FindARBProgram( GL_FRAGMENT_PROGRAM_ARB, token.c_str() );
				newStage.md5rVertexProgram = R_FindMD5RVertexProgramForStageProgram( token.c_str() );
			}
			continue;
		}
		if ( !token.Icmp( "fragmentProgram" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				newStage.fragmentProgram = R_FindARBProgram( GL_FRAGMENT_PROGRAM_ARB, token.c_str() );
			}
			continue;
		}
		// Quake 4's NV20 guide fallback spells the fragment-program
		// directive fp20Program. It has the same material-stage role and
		// binding ABI as fragmentProgram on modern renderers.
		if ( !token.Icmp( "fp20Program" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				newStage.fragmentProgram = R_FindARBProgram( GL_FRAGMENT_PROGRAM_ARB, token.c_str() );
			}
			continue;
		}
		if ( !token.Icmp( "vertexProgram" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				newStage.vertexProgram = R_FindARBProgram( GL_VERTEX_PROGRAM_ARB, token.c_str() );
				newStage.md5rVertexProgram = R_FindMD5RVertexProgramForStageProgram( token.c_str() );
			}
			continue;
		}
		if ( !token.Icmp( "megaTexture" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				//newStage.megaTexture = new idMegaTexture;
				//if ( !newStage.megaTexture->InitFromMegaFile( token.c_str() ) ) {
				//	delete newStage.megaTexture;
				//	SetMaterialFlag( MF_DEFAULTED );
				//	continue;
				//}
				//newStage.vertexProgram = R_FindARBProgram( GL_VERTEX_PROGRAM_ARB, "megaTexture.vfp" );
				//newStage.fragmentProgram = R_FindARBProgram( GL_FRAGMENT_PROGRAM_ARB, "megaTexture.vfp" );
				continue;
			}
		}


		if ( !token.Icmp( "vertexParm" ) ) {
			ParseVertexParm( src, &newStage );
			continue;
		}
		if ( !token.Icmp( "fragmentParm" ) ) {
			ParseFragmentParm( src, &newStage );
			continue;
		}

		if (  !token.Icmp( "fragmentMap" ) ) {	
			ParseFragmentMap( src, &newStage );
			continue;
		}

		// Quake 4 GLSL material tokens (ignored if GLSL isn't supported)
		if ( !token.Icmp( "glslProgram" ) ) {
			stageHasShaderTokens = true;
			if ( src.ReadTokenOnLine( &token ) ) {
				if ( !newStage.glslProgram ) {
					newStage.glslProgram = true;
					idStr::Copynz( newStage.glslProgramName, token.c_str(), sizeof( newStage.glslProgramName ) );
				} else {
					common->Warning( "multiple glslProgram declarations in material '%s'", GetName() );
					SetMaterialFlag( MF_DEFAULTED );
					return;
				}
			} else {
				common->Warning( "missing glslProgram name in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return;
			}
			continue;
		}
		if ( !token.Icmp( "shaderParm" ) ) {
			stageHasShaderTokens = true;
			ParseShaderParm( src, &newStage );
			continue;
		}
		if ( !token.Icmp( "shaderTexture" ) ) {
			stageHasShaderTokens = true;
			ParseShaderTexture( src, &newStage );
			continue;
		}
		if ( !token.Icmp( "customLighting" ) ) {
			stageHasShaderTokens = true;
			newStage.customLighting = true;
			continue;
		}

// jmarshall - make this more informative
		src.Warning( "unknown token '%s' in material '%s'", token.c_str(), GetName() );
// jmarshall end
		SetMaterialFlag( MF_DEFAULTED );
		return;
	}


	// if we are using newStage, allocate a copy of it
	if ( newStage.fragmentProgram || newStage.vertexProgram || newStage.glslProgram ) {
		ss->newStage = (newShaderStage_t *)Mem_Alloc( sizeof( newStage ) );
		*(ss->newStage) = newStage;
	}

	ss->mNumStageRegisters = numRegisters - ss->mStageRegisterStart;
	ss->mNumStageOps = numOps - ss->mStageOpsStart;

	// successfully parsed a stage
	if (idStr::Icmpn(GetName(),"_retained/",10) == 0 &&
		(ss->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)) {
		ss->drawStateBits |= GLS_ALPHA_COVERAGE;
	}
	numStages++;

	// select a compressed depth based on what the stage is
	if ( td == TD_DEFAULT ) {
		switch( ss->lighting ) {
		case SL_BUMP:
			td = TD_BUMP;
			break;
		case SL_DIFFUSE:
			td = TD_DIFFUSE;
			break;
		case SL_SPECULAR:
			td = TD_SPECULAR;
			break;
		default:
			break;
		}
	}

	// now load the image with all the parms we parsed
	if ( imageName[0] ) {
		if ( ss->lighting == SL_AMBIENT
			&& R_IsFilterBlendStage( *ss )
			&& TestMaterialFlag( MF_POLYGONOFFSET )
			&& ( td == TD_DEFAULT || td == TD_HIGH_QUALITY ) ) {
			imageFlags |= IMAGEFLAG_FILTER_NEUTRAL_ALPHA;
		}
		ts->image = R_LoadMaterialImage( imageName, tf, trp, td, cubeMap, allowPicmip, imageFlags );
		if ( !ts->image ) {
			ts->image = globalImages->defaultImage;
		}
	} else if ( !ts->cinematic && !ts->dynamic && !ss->newStage ) {
		if ( !stageHasShaderTokens ) {
			common->Warning( "material '%s' had stage with no image", GetName() );
		}
		ts->image = globalImages->defaultImage;
	}
}

static bool R_IsUnsupportedPBRImageProgramToken( const idToken &token ) {
	return R_IsMutableRenderImageName( token.c_str() )
		|| !token.Icmp( "videoMap" ) || !token.Icmp( "soundMap" )
		|| !token.Icmp( "mirrorRenderMap" ) || !token.Icmp( "remoteRenderMap" )
		|| !token.Icmp( "reflectionRenderMap" ) || !token.Icmp( "refractionRenderMap" )
		|| !token.Icmp( "xrayRenderMap" )
		|| !token.Icmp( "cameraCubeMap" ) || !token.Icmp( "cubeMap" )
		|| !token.Icmp( "program" ) || !token.Icmp( "vertexProgram" )
		|| !token.Icmp( "fragmentProgram" ) || !token.Icmp( "fp20Program" )
		|| !token.Icmp( "glslProgram" ) || !token.Icmp( "vertexParm" )
		|| !token.Icmp( "fragmentParm" ) || !token.Icmp( "fragmentMap" )
		|| !token.Icmp( "shaderParm" ) || !token.Icmp( "shaderTexture" )
		|| !token.Icmp( "customLighting" )
		// Stage-only state is not an image name. Reject it here as well as
		// when nested inside an otherwise valid image program. Keep image
		// program operators such as add() and scale() available.
		|| !token.Icmp( "blend" ) || !token.Icmp( "map" )
		|| !token.Icmp( "screen" ) || !token.Icmp( "screen2" ) || !token.Icmp( "glassWarp" )
		|| !token.Icmp( "texGen" ) || !token.Icmp( "if" )
		|| !token.Icmp( "alphaTest" ) || !token.Icmp( "alphaFunc" )
		|| !token.Icmp( "scroll" ) || !token.Icmp( "translate" )
		|| !token.Icmp( "centerScale" ) || !token.Icmp( "shear" ) || !token.Icmp( "rotate" )
		|| !token.Icmp( "vertexColor" ) || !token.Icmp( "inverseVertexColor" )
		|| !token.Icmp( "color" ) || !token.Icmp( "colored" )
		|| !token.Icmp( "red" ) || !token.Icmp( "green" ) || !token.Icmp( "blue" )
		|| !token.Icmp( "alpha" ) || !token.Icmp( "rgb" ) || !token.Icmp( "rgba" )
		|| !token.Icmp( "maskRed" ) || !token.Icmp( "maskGreen" )
		|| !token.Icmp( "maskBlue" ) || !token.Icmp( "maskAlpha" )
		|| !token.Icmp( "maskColor" ) || !token.Icmp( "maskDepth" )
		|| !token.Icmp( "privatePolygonOffset" ) || !token.Icmp( "polygonOffset" )
		|| !token.Icmp( "ignoreAlphaTest" );
}

/*
================
idMaterial::ParsePBRImage

Parses one static image-program reference from a PBR metadata line.  Dynamic
render/video maps and arbitrary shader state deliberately remain classic-stage
features until a modern pass owns their complete lifetime and fallback rules.
================
*/
bool idMaterial::ParsePBRImage( idLexer &src, pbrMaterialTexture_t &target, const int usage, const textureRepeat_t trpDefault ) {
	if ( target.present ) {
		src.Warning( "duplicate PBR image semantic in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	textureFilter_t filter = TF_DEFAULT;
	textureRepeat_t repeat = trpDefault;
	bool allowPicmip = true;
	bool noMips = false;
	bool highQuality = false;
	bool forceHighQuality = false;
	unsigned int imageFlags = 0;
	idToken token;
	bool haveImageToken = false;

	while ( src.ReadTokenOnLine( &token ) ) {
		if ( !token.Icmp( "nearest" ) ) {
			filter = TF_NEAREST;
			continue;
		}
		if ( !token.Icmp( "linear" ) ) {
			filter = TF_LINEAR;
			continue;
		}
		if ( !token.Icmp( "clamp" ) ) {
			repeat = TR_CLAMP;
			continue;
		}
		if ( !token.Icmp( "noclamp" ) ) {
			repeat = TR_REPEAT;
			continue;
		}
		if ( !token.Icmp( "zeroclamp" ) ) {
			repeat = TR_CLAMP_TO_ZERO;
			continue;
		}
		if ( !token.Icmp( "alphazeroclamp" ) ) {
			repeat = TR_CLAMP_TO_ZERO_ALPHA;
			continue;
		}
		if ( !token.Icmp( "mirroredrepeat" ) ) {
			repeat = TR_MIRRORED_REPEAT;
			continue;
		}
		if ( !token.Icmp( "nopicmip" ) ) {
			allowPicmip = false;
			continue;
		}
		if ( !token.Icmp( "nomips" ) ) {
			noMips = true;
			imageFlags = R_ApplyMaterialNoMipFlags( imageFlags );
			continue;
		}
		if ( !token.Icmp( "forceHighQuality" ) ) {
			highQuality = true;
			forceHighQuality = true;
			continue;
		}
		if ( !token.Icmp( "uncompressed" ) || !token.Icmp( "highquality" ) ) {
			// TD_PBR_COLOR and TD_MATERIAL_DATA are already lossless RGBA8
			// identities. Retain the authoring hint for any generated classic
			// fallback without discarding the PBR colour-vs-data mip semantics.
			highQuality = true;
			continue;
		}

		if ( R_IsUnsupportedPBRImageProgramToken( token ) ) {
			src.Warning( "dynamic or cube image token '%s' is not supported in the PBR block for '%s'", token.c_str(), GetName() );
			SetMaterialFlag( MF_DEFAULTED );
			return false;
		}

		src.UnreadToken( &token );
		haveImageToken = true;
		break;
	}

	if ( !haveImageToken ) {
		src.Warning( "PBR image semantic expects an image program in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	const char *parsedName = R_ParsePastImageProgram( src );
	if ( parsedName == NULL || parsedName[0] == '\0' ) {
		src.Warning( "PBR image semantic has an empty image program in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	// R_ParsePastImageProgram accepts arbitrary leaf names, so validating only
	// the first authored token would let a dynamic/render/program token hide
	// inside add(), scale(), or another nested image operation. Re-lex the
	// canonical expression and reject forbidden tokens at every nesting depth.
	idLexer imageProgram;
	imageProgram.LoadMemory( parsedName, idLib::SizeToInt( strlen( parsedName ), "ParsePBRImage validation" ), "pbrImageProgram" );
	imageProgram.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
	idToken imageProgramToken;
	while ( imageProgram.ReadToken( &imageProgramToken ) ) {
		if ( R_IsUnsupportedPBRImageProgramToken( imageProgramToken ) ) {
			src.Warning( "dynamic or cube image token '%s' is not supported in the PBR block for '%s'", imageProgramToken.c_str(), GetName() );
			imageProgram.FreeSource();
			SetMaterialFlag( MF_DEFAULTED );
			return false;
		}
	}
	imageProgram.FreeSource();

	idStr imageName = parsedName;
	target.image = R_LoadMaterialImage( imageName.c_str(), filter, repeat,
		static_cast<textureUsage_t>( usage ), CF_2D, allowPicmip, imageFlags );
	if ( target.image == NULL ) {
		target.image = globalImages->defaultImage;
	}
	if ( R_IsMutableRenderImage( target.image ) ) {
		src.Warning( "mutable render image '%s' is not supported in the PBR block for '%s'", imageName.c_str(), GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	target.filter = static_cast<int>( filter );
	target.repeat = static_cast<int>( repeat );
	target.allowPicmip = allowPicmip;
	target.noMips = noMips;
	target.highQuality = highQuality;
	target.forceHighQuality = forceHighQuality;
	target.present = true;
	return true;
}

/*
================
idMaterial::ParsePBRBlock
================
*/
bool idMaterial::ParsePBRBlock( idLexer &src, const textureRepeat_t trpDefault ) {
	if ( pbrInfo.enabled ) {
		src.Warning( "multiple PBR blocks in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	if ( !src.ExpectTokenString( "{" ) ) {
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	pbrInfo.enabled = true;
	// Allocate PBR-only defaults only after an authored PBR block opts in. Doing
	// this in ParseMaterial's common prologue would shift expression-register
	// indices for every retail material even though no PBR metadata is present.
	pbrInfo.metallicRegister = GetExpressionConstant( 0.0f );
	pbrInfo.roughnessRegister = GetExpressionConstant( 0.5f );
	pbrInfo.aoRegister = GetExpressionConstant( 1.0f );
	pbrInfo.normalScaleRegister = GetExpressionConstant( 1.0f );
	pbrInfo.emissiveColorRegisters[0] = GetExpressionConstant( 0.0f );
	pbrInfo.emissiveColorRegisters[1] = GetExpressionConstant( 0.0f );
	pbrInfo.emissiveColorRegisters[2] = GetExpressionConstant( 0.0f );
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}

		if ( !token.Icmp( "workflow" ) ) {
			idToken value;
			if ( !src.ReadTokenOnLine( &value ) ) {
				src.Warning( "PBR workflow expects a value in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			if ( !value.Icmp( "metallicRoughness" ) ) {
				pbrInfo.workflow = PBR_WORKFLOW_METALLIC_ROUGHNESS;
			} else if ( !value.Icmp( "specularGlossiness" ) ) {
				pbrInfo.workflow = PBR_WORKFLOW_SPECULAR_GLOSSINESS;
			} else {
				src.Warning( "unknown PBR workflow '%s' in material '%s'", value.c_str(), GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			continue;
		}

		if ( !token.Icmp( "normalFormat" ) ) {
			idToken value;
			if ( !src.ReadTokenOnLine( &value ) ) {
				src.Warning( "PBR normalFormat expects a value in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			if ( !value.Icmp( "quake4AGB" ) ) {
				pbrInfo.normalFormat = PBR_NORMAL_QUAKE4_AGB;
			} else if ( !value.Icmp( "tangentRG" ) ) {
				pbrInfo.normalFormat = PBR_NORMAL_TANGENT_RG;
			} else if ( !value.Icmp( "tangentXYZ" ) ) {
				pbrInfo.normalFormat = PBR_NORMAL_TANGENT_XYZ;
			} else {
				src.Warning( "unknown PBR normalFormat '%s' in material '%s'", value.c_str(), GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			continue;
		}

		if ( !token.Icmp( "albedoMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.albedo, TD_PBR_COLOR, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "normalMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.normal, TD_BUMP, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "ormMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.orm, TD_MATERIAL_DATA, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "metallicMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.metallic, TD_MATERIAL_DATA, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "roughnessMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.roughness, TD_MATERIAL_DATA, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "aoMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.ao, TD_MATERIAL_DATA, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "emissiveMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.emissive, TD_PBR_COLOR, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "legacyBumpMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.legacyBump, TD_BUMP, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "legacyDiffuseMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.legacyDiffuse, TD_DIFFUSE, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "legacySpecularMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.legacySpecular, TD_SPECULAR, trpDefault ) ) return false;
			continue;
		}
		if ( !token.Icmp( "legacyEmissiveMap" ) ) {
			if ( !ParsePBRImage( src, pbrInfo.legacyEmissive, TD_PBR_COLOR, trpDefault ) ) return false;
			continue;
		}

		if ( !token.Icmp( "metallic" ) ) {
			pbrInfo.metallicRegister = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "roughness" ) ) {
			pbrInfo.roughnessRegister = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "ao" ) ) {
			pbrInfo.aoRegister = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "normalScale" ) ) {
			pbrInfo.normalScaleRegister = ParseExpression( src );
			continue;
		}
		if ( !token.Icmp( "emissiveColor" ) ) {
			for ( int i = 0; i < 3; ++i ) {
				pbrInfo.emissiveColorRegisters[i] = ParseExpression( src );
				if ( i < 2 ) {
					idToken separator;
					if ( src.ReadToken( &separator ) && separator != "," ) {
						src.UnreadToken( &separator );
					}
				}
			}
			continue;
		}
		if ( !token.Icmp( "autoLegacyFallback" ) ) {
			idToken value;
			if ( !src.ReadTokenOnLine( &value ) || ( value != "0" && value != "1" ) ) {
				src.Warning( "PBR autoLegacyFallback expects 0 or 1 in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			pbrInfo.autoLegacyFallback = value == "1";
			continue;
		}

		src.Warning( "unknown PBR parameter '%s' in material '%s'", token.c_str(), GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	if ( token != "}" ) {
		src.Warning( "unterminated PBR block in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	if ( pbrInfo.workflow == PBR_WORKFLOW_NONE ) {
		src.Warning( "PBR material '%s' has no workflow", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	if ( !pbrInfo.albedo.present ) {
		src.Warning( "PBR material '%s' has no albedoMap", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	if ( pbrInfo.normal.present && pbrInfo.normalFormat == PBR_NORMAL_UNSPECIFIED ) {
		src.Warning( "PBR normalMap in '%s' requires normalFormat", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	if ( pbrInfo.orm.present && ( pbrInfo.metallic.present || pbrInfo.roughness.present || pbrInfo.ao.present ) ) {
		src.Warning( "PBR material '%s' cannot combine ormMap with separate metallic/roughness/AO maps", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	pbrInfo.hasExplicitLegacyFallback = pbrInfo.legacyBump.present ||
		pbrInfo.legacyDiffuse.present || pbrInfo.legacySpecular.present ||
		pbrInfo.legacyEmissive.present;
	return true;
}

static bool R_ReadSpecularProbeNumbers( idLexer &src, const char *field,
		const char *materialName, float *values, int valueCount ) {
	for ( int index = 0; index < valueCount; ++index ) {
		idToken value;
		if ( !src.ReadTokenOnLine( &value ) || !value.IsNumeric() ) {
			src.Warning( "openQ4SpecularProbe %s expects %d numeric value%s in material '%s'",
				field, valueCount, valueCount == 1 ? "" : "s", materialName );
			return false;
		}
		values[index] = value.GetFloatValue();
		if ( !std::isfinite( values[index] ) ) {
			src.Warning( "openQ4SpecularProbe %s is not finite in material '%s'", field, materialName );
			return false;
		}
	}
	idToken extra;
	if ( src.ReadTokenOnLine( &extra ) ) {
		src.Warning( "openQ4SpecularProbe %s has an unexpected value '%s' in material '%s'",
			field, extra.c_str(), materialName );
		return false;
	}
	return true;
}

/*
================
idMaterial::ParseSpecularProbeBlock

Parses renderer-only metadata for an authored reflection/specular probe.  The
contract is deliberately static and bounded: expressions, render targets,
image programs, and stage state are rejected.  A complete temporary record is
committed only after the closing brace and every required field validate, so a
malformed block can never expose partially authored probe state.
================
*/
bool idMaterial::ParseSpecularProbeBlock( idLexer &src ) {
	if ( specularProbeInfo.enabled ) {
		src.Warning( "multiple openQ4SpecularProbe blocks in material '%s'", GetName() );
		R_ResetSpecularProbeMaterialInfo( specularProbeInfo );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	if ( !src.ExpectTokenString( "{" ) ) {
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	specularProbeMaterialInfo_t parsed;
	R_ResetSpecularProbeMaterialInfo( parsed );
	bool cubeSeen = false;
	bool tintSeen = false;
	bool intensitySeen = false;
	bool blendSeen = false;
	bool prioritySeen = false;
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}

		if ( !token.Icmp( "cubeMap" ) || !token.Icmp( "cameraCubeMap" ) ) {
			if ( cubeSeen ) {
				src.Warning( "openQ4SpecularProbe in material '%s' has multiple cube maps", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			idToken imageName;
			idToken extra;
			if ( !src.ReadTokenOnLine( &imageName ) || imageName.type == TT_NUMBER
					|| imageName.type == TT_PUNCTUATION || src.ReadTokenOnLine( &extra ) ) {
				src.Warning( "openQ4SpecularProbe %s expects one static image name in material '%s'",
					token.c_str(), GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			if ( R_IsUnsupportedPBRImageProgramToken( imageName )
					|| R_IsMutableRenderImageName( imageName.c_str() ) ) {
				src.Warning( "mutable or dynamic probe image '%s' is not supported in material '%s'",
					imageName.c_str(), GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}

			const bool cameraConvention = !token.Icmp( "cameraCubeMap" );
			parsed.cubeConvention = cameraConvention
				? SPECULAR_PROBE_CUBE_CAMERA : SPECULAR_PROBE_CUBE_NATIVE;
			parsed.cubeImage = R_LoadMaterialImage( imageName.c_str(), TF_LINEAR,
				TR_CLAMP, TD_HIGH_QUALITY,
				cameraConvention ? CF_CAMERA : CF_NATIVE, false, IMAGEFLAG_NOMIPS );
			if ( parsed.cubeImage == NULL || R_IsMutableRenderImage( parsed.cubeImage )
					|| ( parsed.cubeImage->IsLoaded()
						&& ( parsed.cubeImage->IsDefaulted()
							|| parsed.cubeImage->GetOpts().textureType != TT_CUBIC ) ) ) {
				src.Warning( "probe cube image '%s' is unavailable or invalid in material '%s'",
					imageName.c_str(), GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			cubeSeen = true;
			continue;
		}

		if ( !token.Icmp( "tint" ) ) {
			if ( tintSeen || !R_ReadSpecularProbeNumbers( src, "tint", GetName(), parsed.tint, 3 ) ) {
				src.Warning( "openQ4SpecularProbe tint is duplicated or invalid in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			for ( int component = 0; component < 3; ++component ) {
				if ( parsed.tint[component] < 0.0f || parsed.tint[component] > 64.0f ) {
					src.Warning( "openQ4SpecularProbe tint must be in [0,64] in material '%s'", GetName() );
					SetMaterialFlag( MF_DEFAULTED );
					return false;
				}
			}
			tintSeen = true;
			continue;
		}

		if ( !token.Icmp( "intensity" ) ) {
			if ( intensitySeen || !R_ReadSpecularProbeNumbers( src, "intensity", GetName(), &parsed.intensity, 1 )
					|| parsed.intensity <= 0.0f || parsed.intensity > 64.0f ) {
				src.Warning( "openQ4SpecularProbe intensity must be in (0,64] in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			intensitySeen = true;
			continue;
		}

		if ( !token.Icmp( "blendFraction" ) ) {
			if ( blendSeen || !R_ReadSpecularProbeNumbers( src, "blendFraction", GetName(), &parsed.blendFraction, 1 )
					|| parsed.blendFraction <= 0.0f || parsed.blendFraction > 1.0f ) {
				src.Warning( "openQ4SpecularProbe blendFraction must be in (0,1] in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			blendSeen = true;
			continue;
		}

		if ( !token.Icmp( "priority" ) ) {
			idToken value;
			idToken extra;
			if ( prioritySeen || !src.ReadTokenOnLine( &value )
					|| value.type != TT_NUMBER || !( value.subtype & TT_INTEGER )
					|| src.ReadTokenOnLine( &extra ) ) {
				src.Warning( "openQ4SpecularProbe priority expects one integer in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			char *end = NULL;
			const long parsedPriority = std::strtol( value.c_str(), &end, 10 );
			if ( end == value.c_str() || end == NULL || *end != '\0'
					|| parsedPriority < 0 || parsedPriority > 255 ) {
				src.Warning( "openQ4SpecularProbe priority must be in [0,255] in material '%s'", GetName() );
				SetMaterialFlag( MF_DEFAULTED );
				return false;
			}
			parsed.priority = static_cast<int>( parsedPriority );
			prioritySeen = true;
			continue;
		}

		src.Warning( "unknown openQ4SpecularProbe parameter '%s' in material '%s'", token.c_str(), GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	if ( token != "}" ) {
		src.Warning( "unterminated openQ4SpecularProbe block in material '%s'", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}
	if ( !cubeSeen || parsed.cubeImage == NULL ) {
		src.Warning( "openQ4SpecularProbe material '%s' has no cubeMap", GetName() );
		SetMaterialFlag( MF_DEFAULTED );
		return false;
	}

	parsed.enabled = true;
	specularProbeInfo = parsed;
	return true;
}

/*
================
idMaterial::AddPBRLegacyFallbackStages
================
*/
void idMaterial::AddPBRLegacyFallbackStages( const textureRepeat_t trpDefault ) {
	if ( !pbrInfo.enabled ) {
		return;
	}

	bool hasBump = false;
	bool hasDiffuse = false;
	bool hasSpecular = false;
	bool hasAmbient = false;
	for ( int i = 0; i < numStages; ++i ) {
		hasBump |= pd->parseStages[i].lighting == SL_BUMP;
		hasDiffuse |= pd->parseStages[i].lighting == SL_DIFFUSE;
		hasSpecular |= pd->parseStages[i].lighting == SL_SPECULAR;
		hasAmbient |= pd->parseStages[i].lighting == SL_AMBIENT;
	}
	pbrInfo.hasAuthoredClassicFallback = hasBump && hasDiffuse;

	auto addStage = [&]( const char *blendName, const pbrMaterialTexture_t &texture ) -> bool {
		if ( texture.image == NULL || numStages >= MAX_SHADER_STAGES ) {
			SetMaterialFlag( MF_DEFAULTED );
			return false;
		}
		idStr buffer;
		buffer = "blend ";
		buffer.Append( blendName );
		buffer.Append( "\n" );
		switch ( static_cast<textureFilter_t>( texture.filter ) ) {
		case TF_NEAREST: buffer.Append( "nearest\n" ); break;
		case TF_LINEAR: buffer.Append( "linear\n" ); break;
		default: break;
		}
		const textureRepeat_t repeat = static_cast<textureRepeat_t>( texture.repeat );
		if ( repeat != trpDefault ) {
			switch ( repeat ) {
			case TR_REPEAT: buffer.Append( "noclamp\n" ); break;
			case TR_MIRRORED_REPEAT: buffer.Append( "mirroredrepeat\n" ); break;
			case TR_CLAMP: buffer.Append( "clamp\n" ); break;
			case TR_CLAMP_TO_ZERO: buffer.Append( "zeroclamp\n" ); break;
			case TR_CLAMP_TO_ZERO_ALPHA: buffer.Append( "alphazeroclamp\n" ); break;
			default: break;
			}
		}
		if ( !texture.allowPicmip ) buffer.Append( "nopicmip\n" );
		if ( texture.noMips ) buffer.Append( "nomips\n" );
		if ( texture.forceHighQuality ) {
			buffer.Append( "forceHighQuality\n" );
		} else if ( texture.highQuality ) {
			buffer.Append( "highquality\n" );
		}
		buffer.Append( "map " );
		buffer.Append( texture.image->GetName() );
		buffer.Append( "\n}\n" );
		idLexer generated;
		generated.LoadMemory( buffer.c_str(), buffer.Length(), "pbrLegacyFallback" );
		generated.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
		ParseStage( generated, trpDefault );
		generated.FreeSource();
		return !TestMaterialFlag( MF_DEFAULTED );
	};
	auto defaultTexture = [&]( idImage *image ) -> pbrMaterialTexture_t {
		pbrMaterialTexture_t texture;
		memset( &texture, 0, sizeof( texture ) );
		texture.image = image;
		texture.present = image != NULL;
		texture.filter = static_cast<int>( TF_DEFAULT );
		texture.repeat = static_cast<int>( trpDefault );
		texture.allowPicmip = true;
		return texture;
	};

	if ( !hasBump && pbrInfo.legacyBump.present ) {
		if ( !addStage( "bumpmap", pbrInfo.legacyBump ) ) return;
		hasBump = true;
		pbrInfo.usesGeneratedLegacyFallback = true;
	}
	if ( !hasDiffuse && pbrInfo.legacyDiffuse.present ) {
		if ( !addStage( "diffusemap", pbrInfo.legacyDiffuse ) ) return;
		hasDiffuse = true;
		pbrInfo.usesGeneratedLegacyFallback = true;
	}
	if ( !hasSpecular && pbrInfo.legacySpecular.present ) {
		if ( !addStage( "specularmap", pbrInfo.legacySpecular ) ) return;
		hasSpecular = true;
		pbrInfo.usesGeneratedLegacyFallback = true;
	}
	if ( !hasAmbient && pbrInfo.legacyEmissive.present ) {
		if ( !addStage( "add", pbrInfo.legacyEmissive ) ) return;
		hasAmbient = true;
		pbrInfo.usesGeneratedLegacyFallback = true;
	}

	// An authored bump+diffuse interaction is already a complete classic
	// fallback. Classic specular is optional, and a PBR emissive map must not
	// silently inject an ambient stage into that authored path.
	const bool hasUsableClassicInteraction = hasBump && hasDiffuse;
	const bool allowApproximate = !hasUsableClassicInteraction
		&& pbrInfo.autoLegacyFallback
		&& r_pbrGeneratedLegacyFallback.GetBool();
	if ( allowApproximate ) {
		if ( !hasBump ) {
			// The classic Quake 4 interaction decoder understands only the A/G
			// normal convention. Tangent RG/XYZ sources stay available to the
			// future PBR shader, but their classic fallback must remain neutral.
			const bool classicNormalCompatible = pbrInfo.normal.present
				&& pbrInfo.normalFormat == PBR_NORMAL_QUAKE4_AGB;
			const pbrMaterialTexture_t texture = classicNormalCompatible
				? pbrInfo.normal
				: defaultTexture( globalImages->flatNormalMap );
			if ( !addStage( "bumpmap", texture ) ) return;
			hasBump = true;
			pbrInfo.usesGeneratedLegacyFallback = true;
			pbrInfo.usesApproximateLegacyFallback = true;
		}
		if ( !hasDiffuse ) {
			const pbrMaterialTexture_t texture = pbrInfo.albedo.present ? pbrInfo.albedo : defaultTexture( globalImages->whiteImage );
			if ( !addStage( "diffusemap", texture ) ) return;
			hasDiffuse = true;
			pbrInfo.usesGeneratedLegacyFallback = true;
			pbrInfo.usesApproximateLegacyFallback = true;
		}
		if ( !hasSpecular ) {
			if ( !addStage( "specularmap", defaultTexture( globalImages->blackImage ) ) ) return;
			hasSpecular = true;
			pbrInfo.usesGeneratedLegacyFallback = true;
			pbrInfo.usesApproximateLegacyFallback = true;
		}
		if ( !hasAmbient && pbrInfo.emissive.present ) {
			if ( !addStage( "add", pbrInfo.emissive ) ) return;
			pbrInfo.usesGeneratedLegacyFallback = true;
			pbrInfo.usesApproximateLegacyFallback = true;
		}
	}

	if ( pbrInfo.usesApproximateLegacyFallback ) {
		common->Warning( "PBR material '%s' uses an approximate generated classic fallback", GetName() );
	}
}

/*
===============
idMaterial::ParseDeform
===============
*/
void idMaterial::ParseDeform( idLexer &src ) {
	idToken token;

	if ( !src.ExpectAnyToken( &token ) ) {
		return;
	}

	if ( !token.Icmp( "sprite" ) ) {
		deform = DFRM_SPRITE;
		cullType = CT_TWO_SIDED;
		SetMaterialFlag( MF_NOSHADOWS );
		return;
	}
	if ( !token.Icmp( "rectsprite" ) ) {
		deform = DFRM_RECTSPRITE;
		cullType = CT_TWO_SIDED;
		SetMaterialFlag( MF_NOSHADOWS );
		return;
	}
	if ( !token.Icmp( "tube" ) ) {
		deform = DFRM_TUBE;
		cullType = CT_TWO_SIDED;
		SetMaterialFlag( MF_NOSHADOWS );
		return;
	}
	if ( !token.Icmp( "flare" ) ) {
		deform = DFRM_FLARE;
		cullType = CT_TWO_SIDED;
		deformRegisters[0] = ParseExpression( src );
		SetMaterialFlag( MF_NOSHADOWS );
		return;
	}
	if ( !token.Icmp( "expand" ) ) {
		deform = DFRM_EXPAND;
		deformRegisters[0] = ParseExpression( src );
		return;
	}
	if ( !token.Icmp( "move" ) ) {
		deform = DFRM_MOVE;
		deformRegisters[0] = ParseExpression( src );
		return;
	}
	if ( !token.Icmp( "turbulent" ) ) {
		deform = DFRM_TURB;

		if ( !src.ExpectAnyToken( &token ) ) {
			src.Warning( "deform particle missing particle name" );
			SetMaterialFlag( MF_DEFAULTED );
			return;
		}
		deformDecl = declManager->FindType( DECL_TABLE, token.c_str(), true );

		deformRegisters[0] = ParseExpression( src );
		deformRegisters[1] = ParseExpression( src );
		deformRegisters[2] = ParseExpression( src );
		return;
	}
	if ( !token.Icmp( "eyeBall" ) ) {
		deform = DFRM_EYEBALL;
		return;
	}
	
	src.Warning( "Bad deform type '%s'", token.c_str() );
	SetMaterialFlag( MF_DEFAULTED );
}


/*
==============
idMaterial::AddImplicitStages

If a material has diffuse or specular stages without any
bump stage, add an implicit _flat bumpmap stage.

If a material has a bump stage but no diffuse or specular
stage, add a _white diffuse stage.

It is valid to have either a diffuse or specular without the other.

It is valid to have a reflection map and a bump map for bumpy reflection
==============
*/
void idMaterial::AddImplicitStages( const textureRepeat_t trpDefault /* = TR_REPEAT  */ ) {
	char	buffer[1024];
	idLexer		newSrc;
	bool hasDiffuse = false;
	bool hasSpecular = false;
	bool hasBump = false;
	bool hasReflection = false;

	for ( int i = 0 ; i < numStages ; i++ ) {
		if ( pd->parseStages[i].lighting == SL_BUMP ) {
			hasBump = true;
		}
		if ( pd->parseStages[i].lighting == SL_DIFFUSE ) {
			hasDiffuse = true;
		}
		if ( pd->parseStages[i].lighting == SL_SPECULAR ) {
			hasSpecular = true;
		}
		if ( pd->parseStages[i].texture.texgen == TG_REFLECT_CUBE ) {
			hasReflection = true;
		}
	}

	// if it doesn't have an interaction at all, don't add anything
	if ( !hasBump && !hasDiffuse && !hasSpecular ) {
		return;
	}

	if ( numStages == MAX_SHADER_STAGES ) {
		return;
	}

	if ( !hasBump ) {
		idStr::snPrintf( buffer, sizeof( buffer ), "blend bumpmap\nmap _flat\n}\n" );
		newSrc.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "idMaterial::AddImplicitStages" ), "bumpmap" );
		newSrc.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
		ParseStage( newSrc, trpDefault );
		newSrc.FreeSource();
	}

	if ( !hasDiffuse && !hasSpecular && !hasReflection ) {
		idStr::snPrintf( buffer, sizeof( buffer ), "blend diffusemap\nmap _white\n}\n" );
		newSrc.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "idMaterial::AddImplicitStages" ), "diffusemap" );
		newSrc.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
		ParseStage( newSrc, trpDefault );
		newSrc.FreeSource();
	}

}

/*
===============
idMaterial::SortInteractionStages

The renderer expects bump, then diffuse, then specular
There can be multiple bump maps, followed by additional
diffuse and specular stages, which allows cross-faded bump mapping.

Ambient stages can be interspersed anywhere, but they are
ignored during interactions, and all the interaction
stages are ignored during ambient drawing.
===============
*/
void idMaterial::SortInteractionStages() {
	int		j;

	for ( int i = 0 ; i < numStages ; i = j ) {
		// find the next bump map
		for ( j = i + 1 ; j < numStages ; j++ ) {
			if ( pd->parseStages[j].lighting == SL_BUMP ) {
				// if the very first stage wasn't a bumpmap,
				// this bumpmap is part of the first group
				if ( pd->parseStages[i].lighting != SL_BUMP ) {
					continue;
				}
				break;
			}
		}

		// bubble sort everything bump / diffuse / specular
		for ( int l = 1 ; l < j-i ; l++ ) {
			for ( int k = i ; k < j-l ; k++ ) {
				if ( pd->parseStages[k].lighting > pd->parseStages[k+1].lighting ) {
					shaderStage_t	temp;

					temp = pd->parseStages[k];
					pd->parseStages[k] = pd->parseStages[k+1];
					pd->parseStages[k+1] = temp;
				}
			}
		}
	}
}

/*
=================
idMaterial::ParseMaterial

The current text pointer is at the explicit text definition of the
Parse it into the global material variable. Later functions will optimize it.

If there is any error during parsing, defaultShader will be set.
=================
*/
void idMaterial::ParseMaterial( idLexer &src ) {
	idToken		token;
	int			s;
	char		buffer[1024];
	const char	*str;
	idLexer		newSrc;
	int			i;

	s = 0;

	numOps = 0;
	numRegisters = EXP_REG_NUM_PREDEFINED;	// leave space for the parms to be copied in
	for ( i = 0 ; i < numRegisters ; i++ ) {
		pd->registerIsTemporary[i] = true;		// they aren't constants that can be folded
	}
	numStages = 0;

	textureRepeat_t	trpDefault = TR_REPEAT;		// allow a global setting for repeat

	while ( 1 ) {
		if ( TestMaterialFlag( MF_DEFAULTED ) ) {	// we have a parse error
			return;
		}
		if ( !src.ExpectAnyToken( &token ) ) {
			SetMaterialFlag( MF_DEFAULTED );
			return;
		}

		// end of material definition
		if ( token == "}" ) {
			break;
		}
		else if ( !token.Icmp( "qer_editorimage") ) {
			src.ReadTokenOnLine( &token );
			editorImageName = token.c_str();
			src.SkipRestOfLine();
			continue;
		}
// jmarshall - quake 4 materials.
		else if (!token.Icmp("materialImage")) {
			src.ReadTokenOnLine(&token);
			idStr hitImage = token;
			materialTypeArray = declManager->GetMaterialTypeArray( hitImage.c_str(), MTAWidth, MTAHeight );
			materialTypeArrayName = token;
			continue;
		}
// jmarshall end
		// description
		else if ( !token.Icmp( "description") ) {
			src.ReadTokenOnLine( &token );
			desc = token.c_str();
			continue;
		}
		// check for the surface / content bit flags
		else if ( CheckSurfaceParm( &token ) ) {
			continue;
		}


		// polygonOffset
		else if ( !token.Icmp( "polygonOffset" ) ) {
			SetMaterialFlag( MF_POLYGONOFFSET );
			if ( !src.ReadTokenOnLine( &token ) ) {
				polygonOffset = 1;
				continue;
			}
			// explict larger (or negative) offset
			polygonOffset = token.GetFloatValue();
			continue;
		}
		// noshadow
		else if ( !token.Icmp( "noShadows" ) ) {
			SetMaterialFlag( MF_NOSHADOWS | MF_NOSHADOWS_EXPLICIT );
			continue;
		}
// jmarshall - possible legacy optimisations that aren't needed for current hardware.
		else if (!token.Icmp("notfix")) {
			surfaceFlags |= SURF_NO_T_FIX;
			continue;
		}
		else if (!token.Icmp("sightClip")) {
			// Unknown what this is used for.
			continue;
		}
		else if (!token.Icmp("sky")) {
			SetMaterialFlag( MF_SKY );
			continue;
		}
		else if (!token.Icmp("needCurrentRender")) {
			SetMaterialFlag( MF_NEED_CURRENT_RENDER );
			continue;
		}
// jmarshall end
		else if ( !token.Icmp( "suppressInSubview" ) ) {
			suppressInSubview = true;
			continue;
		}
// jmarshall
		else if (!token.Icmp("materialType")) {
			src.ReadToken(&token);
			materialType = declManager->FindMaterialType(token);
			if ( materialType != NULL && materialType->IsImplicit() ) {
				common->Warning( "UNKNOWN: materialType '%s' in '%s'", token.c_str(), GetName() );
			}
			continue;
		}
// jmarshall end
		else if ( !token.Icmp( "portalDistanceNear" ) ) {
			portalDistanceNear = src.ParseFloat();
			continue;
		}
		else if ( !token.Icmp( "portalDistanceFar" ) ) {
			portalDistanceFar = src.ParseFloat();
			continue;
		}
		else if ( !token.Icmp( "portalImage" ) ) {
			if ( !src.ReadTokenOnLine( &token ) ) {
				src.Warning( "missing parameter for 'portalImage' in '%s'", GetName() );
				continue;
			}
			portalImage = R_LoadMaterialImage( token.c_str(), TF_DEFAULT, TR_REPEAT, TD_DEFAULT );
			src.SkipRestOfLine();
			continue;
		}
		else if ( !token.Icmp( "portalSky" ) ) {
			portalSky = true;
			continue;
		}
		// noSelfShadow
		else if ( !token.Icmp( "noSelfShadow" ) ) {
			SetMaterialFlag( MF_NOSELFSHADOW );
			continue;
		}
		// noPortalFog
		else if ( !token.Icmp( "noPortalFog" ) ) {
			SetMaterialFlag( MF_NOPORTALFOG );
			continue;
		}
		// forceShadows allows nodraw surfaces to cast shadows
		else if ( !token.Icmp( "forceShadows" ) ) {
			SetMaterialFlag( MF_FORCESHADOWS );
			continue;
		}
		// overlay / decal suppression
		else if ( !token.Icmp( "noOverlays" ) ) {
			allowOverlays = false;
			continue;
		}
		// moster blood overlay forcing for alpha tested or translucent surfaces
		else if ( !token.Icmp( "forceOverlays" ) ) {
			pd->forceOverlays = true;
			continue;
		}
		// translucent
		else if ( !token.Icmp( "translucent" ) ) {
			coverage = MC_TRANSLUCENT;
			continue;
		}
		// global zero clamp
		else if ( !token.Icmp( "zeroclamp" ) ) {
			trpDefault = TR_CLAMP_TO_ZERO;
			continue;
		}
		// global clamp
		else if ( !token.Icmp( "clamp" ) ) {
			trpDefault = TR_CLAMP;
			continue;
		}
		// global clamp
		else if ( !token.Icmp( "alphazeroclamp" ) ) {
			trpDefault = TR_CLAMP_TO_ZERO;
			continue;
		}
		// global mirrored repeat
		else if ( !token.Icmp( "mirroredrepeat" ) ) {
			trpDefault = TR_MIRRORED_REPEAT;
			continue;
		}
		// forceOpaque is used for skies-behind-windows
		else if ( !token.Icmp( "forceOpaque" ) ) {
			coverage = MC_OPAQUE;
			continue;
		}
		// twoSided
		else if ( !token.Icmp( "twoSided" ) ) {
			cullType = CT_TWO_SIDED;
			// twoSided implies no-shadows, because the shadow
			// volume would be coplanar with the surface, giving depth fighting
			// we could make this no-self-shadows, but it may be more important
			// to receive shadows from no-self-shadow monsters
			SetMaterialFlag( MF_NOSHADOWS );
		}
		// backSided
		else if ( !token.Icmp( "backSided" ) ) {
			cullType = CT_BACK_SIDED;
			// the shadow code doesn't handle this, so just disable shadows.
			// We could fix this in the future if there was a need.
			SetMaterialFlag( MF_NOSHADOWS );
		}
		// foglight
		else if ( !token.Icmp( "fogLight" ) ) {
			fogLight = true;
			continue;
		}
		// blendlight
		else if ( !token.Icmp( "blendLight" ) ) {
			blendLight = true;
			continue;
		}
		// ambientLight
		else if ( !token.Icmp( "ambientLight" ) ) {
			ambientLight = true;
			continue;
		}
		// mirror
		else if ( !token.Icmp( "mirror" ) ) {
			sort = SS_SUBVIEW;
			coverage = MC_OPAQUE;
			continue;
		}
		// noFog
		else if ( !token.Icmp( "noFog" ) ) {
			noFog = true;
			continue;
		}
		// unsmoothedTangents
		else if ( !token.Icmp( "unsmoothedTangents" ) ) {
			unsmoothedTangents = true;
			continue;
		}
		// lightFallofImage <imageprogram>
		// specifies the image to use for the third axis of projected
		// light volumes
		else if ( !token.Icmp( "lightFalloffImage" ) ) {
			str = R_ParsePastImageProgram( src );
			idStr	copy;

			copy = str;	// so other things don't step on it
			lightFalloffImage = R_LoadMaterialImage( copy, TF_DEFAULT, TR_CLAMP, TD_LIGHT );
			continue;
		}
		// guisurf <guifile> | guisurf entity
		// an entity guisurf must have an idUserInterface
		// specified in the renderEntity
		else if ( !token.Icmp( "guisurf" ) ) {
			src.ReadTokenOnLine( &token );
			if ( !token.Icmp( "entity" ) ) {
				entityGui = 1;
			} else if ( !token.Icmp( "entity2" ) ) {
				entityGui = 2;
			} else if ( !token.Icmp( "entity3" ) ) {
				entityGui = 3;
			} else {
				gui = uiManager->FindGui( token.c_str(), true );
			}
			continue;
		}
		// sort
		else if ( !token.Icmp( "sort" ) ) {
			ParseSort( src );
			continue;
		}
		// spectrum <integer>
		else if ( !token.Icmp( "spectrum" ) ) {
			src.ReadTokenOnLine( &token );
			spectrum = atoi( token.c_str() );
			continue;
		}
		// deform < sprite | tube | flare >
		else if ( !token.Icmp( "deform" ) ) {
			ParseDeform( src );
			continue;
		}
		// decalInfo <staySeconds> [, <maxAngle>] (legacy Doom 3 syntax still accepted)
		else if ( !token.Icmp( "decalInfo" ) ) {
			ParseDecalInfo( src );
			continue;
		}
		// renderbump <args...>
		else if ( !token.Icmp( "renderbump") ) {
			src.ParseRestOfLine( renderBump );
			continue;
		}
		// Quake 4 material guide directives are metadata/preprocessor hints.
		// Treat them as no-op at runtime so shipped materials don't default.
		else if ( !token.Icmp( "inlineGuide" ) || !token.Icmp( "guide" ) ) {
			idToken guideName;
			if ( src.ReadToken( &guideName ) ) {
				if ( src.CheckTokenString( "(" ) ) {
					int parenDepth = 1;
					idToken guideToken;
					while ( parenDepth > 0 && src.ReadToken( &guideToken ) ) {
						if ( guideToken == "(" ) {
							parenDepth++;
						} else if ( guideToken == ")" ) {
							parenDepth--;
						}
					}
				}
			}
			continue;
		}
		else if ( !token.Icmp( "pbr" ) || !token.Icmp( "physicallyBased" ) ) {
			if ( !ParsePBRBlock( src, trpDefault ) ) {
				return;
			}
			continue;
		}
		else if ( !token.Icmp( "openQ4SpecularProbe" ) ) {
			if ( !ParseSpecularProbeBlock( src ) ) {
				return;
			}
			continue;
		}
		// diffusemap for stage shortcut
		else if ( !token.Icmp( "diffusemap" ) ) {
			str = R_ParsePastImageProgram( src );
			if ( str[0] == '\0' ) {
				src.Warning( "diffusemap expects an image program in '%s'", GetName() );
				continue;
			}
			idStr::snPrintf( buffer, sizeof( buffer ), "blend diffusemap\nmap %s\n}\n", str);
			newSrc.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "idMaterial::ParseMaterial" ), "diffusemap" );
			newSrc.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
			ParseStage( newSrc, trpDefault );
			newSrc.FreeSource();
			continue;
		}
		// specularmap for stage shortcut
		else if ( !token.Icmp( "specularmap" ) ) {
			str = R_ParsePastImageProgram( src );
			if ( str[0] == '\0' ) {
				src.Warning( "specularmap expects an image program in '%s'", GetName() );
				continue;
			}
			idStr::snPrintf( buffer, sizeof( buffer ), "blend specularmap\nmap %s\n}\n", str);
			newSrc.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "idMaterial::ParseMaterial" ), "specularmap" );
			newSrc.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
			ParseStage( newSrc, trpDefault );
			newSrc.FreeSource();
			continue;
		}
		// normalmap for stage shortcut
		else if ( !token.Icmp( "bumpmap" ) ) {
			str = R_ParsePastImageProgram( src );
			if ( str[0] == '\0' ) {
				src.Warning( "bumpmap expects an image program in '%s'", GetName() );
				continue;
			}
			idStr::snPrintf( buffer, sizeof( buffer ), "blend bumpmap\nmap %s\n}\n", str );
			newSrc.LoadMemory( buffer, idLib::SizeToInt( strlen( buffer ), "idMaterial::ParseMaterial" ), "bumpmap" );
			newSrc.SetFlags( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
			ParseStage( newSrc, trpDefault );
			newSrc.FreeSource();
			continue;
		}
		// DECAL_MACRO for backwards compatibility with the preprocessor macros
		else if ( !token.Icmp( "DECAL_MACRO" ) ) {
			// polygonOffset
			SetMaterialFlag( MF_POLYGONOFFSET );
			polygonOffset = 1;

			// notfix
			surfaceFlags |= SURF_NO_T_FIX;
			contentFlags &= ~CONTENTS_SOLID;

			// sort decal
			sort = SS_DECAL;

			// noShadows
			SetMaterialFlag( MF_NOSHADOWS | MF_NOSHADOWS_EXPLICIT );
			continue;
		}
		else if ( token == "{" ) {
			// create the new stage
			ParseStage( src, trpDefault );
			continue;
		}
		else {
// jmarshall - make this feedback more informative.
			src.Warning( "unknown general material parameter '%s' in '%s'", token.c_str(), GetName() );
// jmarshall end
			SetMaterialFlag( MF_DEFAULTED );
			return;
		}
	}

	// PBR metadata never mutates an authored classic stage.  For a PBR-only
	// declaration, explicitly requested or development-only generated stages
	// are added before the ordinary classic interaction completion/sort pass.
	AddPBRLegacyFallbackStages( trpDefault );
	if ( TestMaterialFlag( MF_DEFAULTED ) ) {
		return;
	}

	// add _flat or _white stages if needed
	AddImplicitStages();

	// A PBR declaration is still valid metadata when it deliberately omits a
	// classic fallback, but the current renderer cannot draw it until a native
	// PBR lighting path exists.  Preserve that state explicitly instead of
	// letting a zero-stage declaration disappear from authoring diagnostics.
	if ( pbrInfo.enabled ) {
		bool hasBump = false;
		bool hasDiffuse = false;
		for ( int i = 0; i < numStages; ++i ) {
			hasBump |= pd->parseStages[i].lighting == SL_BUMP;
			hasDiffuse |= pd->parseStages[i].lighting == SL_DIFFUSE;
		}
		pbrInfo.legacyFallbackMissing = !( hasBump && hasDiffuse );
		if ( pbrInfo.legacyFallbackMissing ) {
			common->Warning( "PBR material '%s' has no usable classic bump+diffuse fallback", GetName() );
		}
	}

	// order the diffuse / bump / specular stages properly
	SortInteractionStages();

	// if we need to do anything with normals (lighting or environment mapping)
	// and two sided lighting was asked for, flag
	// shouldCreateBackSides() and change culling back to single sided,
	// so we get proper tangent vectors on both sides

	// we can't just call ReceivesLighting(), because the stages are still
	// in temporary form
	if ( cullType == CT_TWO_SIDED ) {
		for ( i = 0 ; i < numStages ; i++ ) {
			const newShaderStage_t *newStage = pd->parseStages[i].newStage;
			const bool customLighting =
				newStage != NULL && newStage->customLighting;
			if ( pd->parseStages[i].lighting != SL_AMBIENT
					|| pd->parseStages[i].texture.texgen != TG_EXPLICIT
					|| customLighting ) {
				if ( cullType == CT_TWO_SIDED ) {
					cullType = CT_FRONT_SIDED;
					shouldCreateBackSides = true;
				}
				break;
			}
		}
	}

	// currently a surface can only have one unique texgen for all the stages on old hardware
	texgen_t firstGen = TG_EXPLICIT;
	for ( i = 0; i < numStages; i++ ) {
		if ( pd->parseStages[i].texture.texgen != TG_EXPLICIT ) {
			if ( firstGen == TG_EXPLICIT ) {
				firstGen = pd->parseStages[i].texture.texgen;
			} else if ( firstGen != pd->parseStages[i].texture.texgen ) {
				common->Warning( "material '%s' has multiple stages with a texgen", GetName() );
				break;
			}
		}
	}
}

/*
=========================
idMaterial::SetGui
=========================
*/
void idMaterial::SetGui( const char *_gui ) const {
	gui = uiManager->FindGui( _gui, true, false, true );
}

/*
=========================
idMaterial::Parse

Parses the current material definition and finds all necessary images.
=========================
*/
bool idMaterial::Parse( const char *text, const int textLength ) {
	if ( !R_ImagePolicyContentMutation() ) return false;
	idLexer	src;
	idToken	token;
	mtrParsingData_t parsingData;

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_LEXER_FLAGS );
	src.SkipUntilString( "{" );

	// reset to the unparsed state
	CommonInit();

	memset( &parsingData, 0, sizeof( parsingData ) );

	pd = &parsingData;	// this is only valid during parse

	// parse it
	ParseMaterial( src );

	// if we are doing an fs_copyfiles, also reference the editorImage
	if ( cvarSystem->GetCVarInteger( "fs_copyFiles" ) ) {
		GetEditorImage();
	}

	//
	// count non-lit stages
	numAmbientStages = 0;
	int i;
	for ( i = 0 ; i < numStages ; i++ ) {
		const newShaderStage_t *newStage = pd->parseStages[i].newStage;
		const bool customLighting =
			newStage != NULL && newStage->customLighting;
		if ( pd->parseStages[i].lighting == SL_AMBIENT && !customLighting ) {
			numAmbientStages++;
		}
	}

	// see if there is a subview stage
	if ( sort == SS_SUBVIEW ) {
		hasSubview = true;
	} else {
		hasSubview = false;
		for ( i = 0 ; i < numStages ; i++ ) {
			if ( pd->parseStages[i].texture.dynamic ) {
				hasSubview = true;
			}
		}
	}

	// automatically determine coverage if not explicitly set
	if ( coverage == MC_BAD ) {
		// automatically set MC_TRANSLUCENT if we don't have any interaction stages and 
		// the first stage is blended and not an alpha test mask or a subview
		if ( !numStages ) {
			// non-visible
			coverage = MC_TRANSLUCENT;
		} else if ( numStages != numAmbientStages ) {
			// we have an interaction draw
			coverage = MC_OPAQUE;
		} else if ( 
			( pd->parseStages[0].drawStateBits & GLS_DSTBLEND_BITS ) != GLS_DSTBLEND_ZERO ||
			( pd->parseStages[0].drawStateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_DST_COLOR ||
			( pd->parseStages[0].drawStateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_ONE_MINUS_DST_COLOR ||
			( pd->parseStages[0].drawStateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_DST_ALPHA ||
			( pd->parseStages[0].drawStateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_ONE_MINUS_DST_ALPHA
			) {
			// blended with the destination
				coverage = MC_TRANSLUCENT;
		} else {
			coverage = MC_OPAQUE;
		}
	}

	// translucent automatically implies noshadows
	if ( coverage == MC_TRANSLUCENT ) {
		SetMaterialFlag( MF_NOSHADOWS );
	} else {
		// mark the contents as opaque
		contentFlags |= CONTENTS_OPAQUE;
	}

	// if we are translucent, draw with an alpha in the editor
	if ( coverage == MC_TRANSLUCENT ) {
		editorAlpha = 0.5;
	} else {
		editorAlpha = 1.0;
	}

	// the sorts can make reasonable defaults
	if ( sort == SS_BAD ) {
		if ( TestMaterialFlag(MF_POLYGONOFFSET) ) {
			sort = SS_DECAL;
		} else if ( coverage == MC_TRANSLUCENT ) {
			sort = SS_MEDIUM;
		} else {
			sort = SS_OPAQUE;
		}
	}

	// anything that references a captured scene buffer such as _currentRender or
	// _currentDepth will automatically get sort = SS_POST_PROCESS and coverage = MC_TRANSLUCENT.
	// Retail Quake 4's needCurrentRender flag preserves authored sort and asks the
	// backend to copy the scene just before this material draws.

	if ( !TestMaterialFlag( MF_NEED_CURRENT_RENDER ) ) {
		for ( i = 0 ; i < numStages ; i++ ) {
			shaderStage_t	*pStage = &pd->parseStages[i];
			if ( R_IsSceneCaptureImage( pStage->texture.image ) ) {
				if ( sort != SS_PORTAL_SKY ) {
					sort = SS_POST_PROCESS;
					coverage = MC_TRANSLUCENT;
				}
				break;
			}
			if ( pStage->newStage ) {
				for ( int j = 0 ; j < pStage->newStage->numFragmentProgramImages ; j++ ) {
					if ( R_IsSceneCaptureImage( pStage->newStage->fragmentProgramImages[j] ) ) {
						if ( sort != SS_PORTAL_SKY ) {
							sort = SS_POST_PROCESS;
							coverage = MC_TRANSLUCENT;
						}
						i = numStages;
						break;
					}
				}
				for ( int j = 0 ; j < pStage->newStage->numShaderTextures ; j++ ) {
					if ( R_IsSceneCaptureImage( pStage->newStage->shaderTextureImages[j] ) ) {
						if ( sort != SS_PORTAL_SKY ) {
							sort = SS_POST_PROCESS;
							coverage = MC_TRANSLUCENT;
						}
						i = numStages;
						break;
					}
				}
			}
		}
	}

	// set the drawStateBits depth flags
	for ( i = 0 ; i < numStages ; i++ ) {
		shaderStage_t	*pStage = &pd->parseStages[i];
		if ( sort == SS_POST_PROCESS ) {
			// post-process effects fill the depth buffer as they draw, so only the
			// topmost post-process effect is rendered
			pStage->drawStateBits |= GLS_DEPTHFUNC_LESS;
		} else if ( coverage == MC_TRANSLUCENT || pStage->ignoreAlphaTest ) {
			// translucent surfaces can extend past the exactly marked depth buffer
			pStage->drawStateBits |= GLS_DEPTHFUNC_LESS | GLS_DEPTHMASK;
		} else {
			// opaque and perforated surfaces must exactly match the depth buffer,
			// which gets alpha test correct
			pStage->drawStateBits |= GLS_DEPTHFUNC_EQUAL | GLS_DEPTHMASK;
		}
	}

	// determine if this surface will accept overlays / decals

	if ( pd->forceOverlays ) {
		// explicitly flaged in material definition
		allowOverlays = true;
	} else {
		if ( !IsDrawn() ) {
			allowOverlays = false;
		}
		if ( Coverage() != MC_OPAQUE ) {
			allowOverlays = false;
		}
		if ( GetSurfaceFlags() & SURF_NOIMPACT ) {
			allowOverlays = false;
		}
	}

	// Match Quake 4: distance-cull portal settings implicitly use a black fade image.
	if ( ( portalDistanceNear < 262144.0f || portalDistanceFar < 262144.0f ) && !portalImage ) {
		portalImage = globalImages->blackImage;
	}

	// add a tiny offset to the sort orders, so that different materials
	// that have the same sort value will at least sort consistantly, instead
	// of flickering back and forth
/* this messed up in-game guis
	if ( sort != SS_SUBVIEW ) {
		int	hash, l;

		l = name.Length();
		hash = 0;
		for ( int i = 0 ; i < l ; i++ ) {
			hash ^= name[i];
		}
		sort += hash * 0.01;
	}
*/

	if (numStages) {
		stages = (shaderStage_t *)R_StaticAlloc( numStages * sizeof( stages[0] ) );
		memcpy( stages, pd->parseStages, numStages * sizeof( stages[0] ) );
	}

	hasCustomGLSLLightingStage = false;
	for ( i = 0 ; i < numStages ; i++ ) {
		const newShaderStage_t *newStage = stages[i].newStage;
		if ( newStage != NULL && newStage->customLighting && newStage->glslProgram ) {
			hasCustomGLSLLightingStage = true;
			break;
		}
	}

	if ( numOps ) {
		ops = (expOp_t *)R_StaticAlloc( numOps * sizeof( ops[0] ) );
		memcpy( ops, pd->shaderOps, numOps * sizeof( ops[0] ) );
	}

	if ( numRegisters ) {
		expressionRegisters = (float *)R_StaticAlloc( numRegisters * sizeof( expressionRegisters[0] ) );
		memcpy( expressionRegisters, pd->shaderRegisters, numRegisters * sizeof( expressionRegisters[0] ) );
	}

	// see if the registers are completely constant, and don't need to be evaluated
	// per-surface
	CheckForConstantRegisters();

	pd = NULL;	// the pointer will be invalid after exiting this function

	// finish things up
	if ( TestMaterialFlag( MF_DEFAULTED ) ) {
		MakeDefault();
		return false;
	}
	return true;
}

/*
===============
idMaterial::Parse
===============
*/
bool idMaterial::Parse( const char *text, const int textLength, bool noCaching ) {
	(void)noCaching;
	return Parse( text, textLength );
}

/*
===============
idMaterial::Validate
===============
*/
bool idMaterial::Validate( const char *psText, int iTextLength, idStr &strReportTo ) const {
	(void)strReportTo;

	idDecl *decl = declManager->AllocateDecl( DECL_MATERIAL );
	const bool valid = DeclManager_ValidateParsedDecl( decl, DECL_MATERIAL, decl != NULL && decl->Parse( psText, iTextLength, false ) );
	DeclManager_FreeAllocatedDecl( decl );
	return valid;
}

/*
===================
idMaterial::Print
===================
*/
char *opNames[] = {
	"OP_TYPE_ADD",
	"OP_TYPE_SUBTRACT",
	"OP_TYPE_MULTIPLY",
	"OP_TYPE_DIVIDE",
	"OP_TYPE_MOD",
	"OP_TYPE_TABLE",
	"OP_TYPE_GT",
	"OP_TYPE_GE",
	"OP_TYPE_LT",
	"OP_TYPE_LE",
	"OP_TYPE_EQ",
	"OP_TYPE_NE",
	"OP_TYPE_AND",
	"OP_TYPE_OR",
	"OP_TYPE_SOUND",
	"OP_TYPE_GLSL_ENABLED"
};

static_assert(
	( sizeof( opNames ) / sizeof( opNames[0] ) ) == ( OP_TYPE_GLSL_ENABLED + 1 ),
	"opNames must stay in sync with expOpType_t" );

void idMaterial::Print() const {
	int			i;

	const int registerCount = GetNumRegisters();
	for ( i = EXP_REG_NUM_PREDEFINED ; i < registerCount ; i++ ) {
		common->Printf( "register %i: %f\n", i, expressionRegisters[i] );
	}
	common->Printf( "\n" );
	for ( i = 0 ; i < numOps ; i++ ) {
		const expOp_t *op = &ops[i];
		if ( op->opType == OP_TYPE_TABLE ) {
			common->Printf( "%i = %s[ %i ]\n", op->c, declManager->DeclByIndex( DECL_TABLE, op->a )->GetName(), op->b );
		} else {
			common->Printf( "%i = %i %s %i\n", op->c, op->a, opNames[ op->opType ], op->b );
		}
	}
	if ( pbrInfo.enabled ) {
		common->Printf(
			"PBR: workflow=%d normalFormat=%d albedo=%s normal=%s orm=%s generatedFallback=%d approximateFallback=%d missingFallback=%d\n",
			static_cast<int>( pbrInfo.workflow ),
			static_cast<int>( pbrInfo.normalFormat ),
			pbrInfo.albedo.image != NULL ? pbrInfo.albedo.image->GetName() : "<none>",
			pbrInfo.normal.image != NULL ? pbrInfo.normal.image->GetName() : "<none>",
			pbrInfo.orm.image != NULL ? pbrInfo.orm.image->GetName() : "<none>",
			pbrInfo.usesGeneratedLegacyFallback ? 1 : 0,
			pbrInfo.usesApproximateLegacyFallback ? 1 : 0,
			pbrInfo.legacyFallbackMissing ? 1 : 0 );
	}
	if ( specularProbeInfo.enabled && specularProbeInfo.cubeImage != NULL ) {
		common->Printf( "Specular probe: cubeMap=%s\n", specularProbeInfo.cubeImage->GetName() );
	}
}

/*
===============
idMaterial::Save
===============
*/
bool idMaterial::Save( const char *fileName ) {
	return ReplaceSourceFileText();
}

/*
===============
idMaterial::AddReference
===============
*/
void idMaterial::AddReference() {
	refCount++;

	for ( int i = 0; i < numStages; i++ ) {
		shaderStage_t *s = &stages[i];

		if ( s->texture.image ) {
			s->texture.image->AddReference();
		}
	}

	if ( portalImage ) {
		portalImage->AddReference();
	}

	pbrMaterialTexture_t *pbrTextures[] = {
		&pbrInfo.albedo, &pbrInfo.normal, &pbrInfo.orm, &pbrInfo.metallic,
		&pbrInfo.roughness, &pbrInfo.ao, &pbrInfo.emissive,
		&pbrInfo.legacyBump, &pbrInfo.legacyDiffuse,
		&pbrInfo.legacySpecular, &pbrInfo.legacyEmissive
	};
	for ( unsigned int i = 0; i < sizeof( pbrTextures ) / sizeof( pbrTextures[0] ); ++i ) {
		if ( pbrTextures[i]->present && pbrTextures[i]->image != NULL ) {
			pbrTextures[i]->image->AddReference();
		}
	}
	if ( specularProbeInfo.enabled && specularProbeInfo.cubeImage != NULL ) {
		specularProbeInfo.cubeImage->AddReference();
	}
}

/*
===============
idMaterial::ResolveUse
===============
*/
void idMaterial::ResolveUse() {
	for ( int i = 0; i < numStages; i++ ) {
		shaderStage_t *stage = &stages[i];
		if ( stage->newStage != NULL ) {
			for ( int j = 0; j < stage->newStage->numFragmentProgramImages; j++ ) {
				if ( stage->newStage->fragmentProgramImages[j] != NULL ) {
					stage->newStage->fragmentProgramImages[j]->AddUseCount( useCount );
				}
			}
			for ( int j = 0; j < MAX_FRAGMENT_IMAGES; j++ ) {
				if ( stage->newStage->shaderTextureImages[j] != NULL ) {
					stage->newStage->shaderTextureImages[j]->AddUseCount( useCount );
				}
			}
			continue;
		}

		if ( stage->texture.image != NULL ) {
			stage->texture.image->AddUseCount( useCount );
		}
	}

	if ( lightFalloffImage != NULL ) {
		lightFalloffImage->AddUseCount( useCount );
	}
	if ( portalImage != NULL ) {
		portalImage->AddUseCount( useCount );
	}

	pbrMaterialTexture_t *pbrTextures[] = {
		&pbrInfo.albedo, &pbrInfo.normal, &pbrInfo.orm, &pbrInfo.metallic,
		&pbrInfo.roughness, &pbrInfo.ao, &pbrInfo.emissive,
		&pbrInfo.legacyBump, &pbrInfo.legacyDiffuse,
		&pbrInfo.legacySpecular, &pbrInfo.legacyEmissive
	};
	for ( unsigned int i = 0; i < sizeof( pbrTextures ) / sizeof( pbrTextures[0] ); ++i ) {
		if ( pbrTextures[i]->present && pbrTextures[i]->image != NULL ) {
			pbrTextures[i]->image->AddUseCount( useCount );
		}
	}
	if ( specularProbeInfo.enabled && specularProbeInfo.cubeImage != NULL ) {
		specularProbeInfo.cubeImage->AddUseCount( useCount );
	}
}

/*
===============
idMaterial::EvaluateRegisters

Parameters are taken from the localSpace and the renderView,
then all expressions are evaluated, leaving the material registers
set to their apropriate values.
===============
*/
void idMaterial::EvaluateRegisters( float *registers, const float shaderParms[MAX_ENTITY_SHADER_PARMS],
									const viewDef_t *view, idSoundEmitter *soundEmitter ) const {
	int		i, b;
	expOp_t	*op;

	// copy the material constants
	for ( i = EXP_REG_NUM_PREDEFINED ; i < numRegisters ; i++ ) {
		registers[i] = expressionRegisters[i];
	}

	// copy the local and global parameters
	registers[EXP_REG_TIME] = view->floatTime;
	registers[EXP_REG_PARM0] = shaderParms[0];
	registers[EXP_REG_PARM1] = shaderParms[1];
	registers[EXP_REG_PARM2] = shaderParms[2];
	registers[EXP_REG_PARM3] = shaderParms[3];
	registers[EXP_REG_PARM4] = shaderParms[4];
	registers[EXP_REG_PARM5] = shaderParms[5];
	registers[EXP_REG_PARM6] = shaderParms[6];
	registers[EXP_REG_PARM7] = shaderParms[7];
	registers[EXP_REG_PARM8] = shaderParms[8];
	registers[EXP_REG_PARM9] = shaderParms[9];
	registers[EXP_REG_PARM10] = shaderParms[10];
	registers[EXP_REG_PARM11] = shaderParms[11];
	registers[EXP_REG_GLOBAL0] = view->renderView.shaderParms[0];
	registers[EXP_REG_GLOBAL1] = view->renderView.shaderParms[1];
	registers[EXP_REG_GLOBAL2] = view->renderView.shaderParms[2];
	registers[EXP_REG_GLOBAL3] = view->renderView.shaderParms[3];
	registers[EXP_REG_GLOBAL4] = view->renderView.shaderParms[4];
	registers[EXP_REG_GLOBAL5] = view->renderView.shaderParms[5];
	registers[EXP_REG_GLOBAL6] = view->renderView.shaderParms[6];
	registers[EXP_REG_GLOBAL7] = view->renderView.shaderParms[7];
	registers[EXP_REG_VERTEX_RANDOM] = shaderParms[SHADERPARM_DIVERSITY];

	op = ops;
	for ( i = 0 ; i < numOps ; i++, op++ ) {
		switch( op->opType ) {
		case OP_TYPE_ADD:
			registers[op->c] = registers[op->a] + registers[op->b];
			break;
		case OP_TYPE_SUBTRACT:
			registers[op->c] = registers[op->a] - registers[op->b];
			break;
		case OP_TYPE_MULTIPLY:
			registers[op->c] = registers[op->a] * registers[op->b];
			break;
		case OP_TYPE_DIVIDE:
			registers[op->c] = registers[op->a] / registers[op->b];
			break;
		case OP_TYPE_MOD:
			b = (int)registers[op->b];
			b = b != 0 ? b : 1;
			registers[op->c] = (int)registers[op->a] % b;
			break;
		case OP_TYPE_TABLE:
			{
				const idDeclTable *table = static_cast<const idDeclTable *>( declManager->DeclByIndex( DECL_TABLE, op->a ) );
				registers[op->c] = table->TableLookup( registers[op->b] );
			}
			break;
		case OP_TYPE_SOUND:
			if ( soundEmitter ) {
				registers[op->c] = soundEmitter->CurrentAmplitude();
			} else {
				registers[op->c] = 0;
			}
			break;
		case OP_TYPE_GLSL_ENABLED:
			registers[op->c] =
					R_MaterialGLSLProgramsAvailable() ? 1.0f : 0.0f;
			break;
		case OP_TYPE_GT:
			registers[op->c] = registers[ op->a ] > registers[op->b];
			break;
		case OP_TYPE_GE:
			registers[op->c] = registers[ op->a ] >= registers[op->b];
			break;
		case OP_TYPE_LT:
			registers[op->c] = registers[ op->a ] < registers[op->b];
			break;
		case OP_TYPE_LE:
			registers[op->c] = registers[ op->a ] <= registers[op->b];
			break;
		case OP_TYPE_EQ:
			registers[op->c] = registers[ op->a ] == registers[op->b];
			break;
		case OP_TYPE_NE:
			registers[op->c] = registers[ op->a ] != registers[op->b];
			break;
		case OP_TYPE_AND:
			registers[op->c] = registers[ op->a ] && registers[op->b];
			break;
		case OP_TYPE_OR:
			registers[op->c] = registers[ op->a ] || registers[op->b];
			break;
		default:
			common->FatalError( "R_EvaluateExpression: bad opcode" );
		}
	}

}

/*
===============
idMaterial::EvaluateStageRegisters

Decal submission in Quake 4 evaluates only the expression ops owned by the
current stage, with explicit parm inputs and no view-global shader parms.
===============
*/
void idMaterial::EvaluateStageRegisters( int stageIndex, float *registers,
										const float shaderParms[MAX_ENTITY_SHADER_PARMS], float floatTime ) const {
	int		i, b;
	expOp_t	*op;

	if ( stages == NULL || stageIndex < 0 || stageIndex >= numStages ) {
		return;
	}

	const shaderStage_t *stage = &stages[stageIndex];

	// copy the material constants
	for ( i = EXP_REG_NUM_PREDEFINED ; i < numRegisters ; i++ ) {
		registers[i] = expressionRegisters[i];
	}

	// seed the explicit stage-local parameters; decal stages do not consult the
	// current view's global shader parm block when evaluating their baked colors
	registers[EXP_REG_TIME] = floatTime;
	registers[EXP_REG_PARM0] = shaderParms[0];
	registers[EXP_REG_PARM1] = shaderParms[1];
	registers[EXP_REG_PARM2] = shaderParms[2];
	registers[EXP_REG_PARM3] = shaderParms[3];
	registers[EXP_REG_PARM4] = shaderParms[4];
	registers[EXP_REG_PARM5] = shaderParms[5];
	registers[EXP_REG_PARM6] = shaderParms[6];
	registers[EXP_REG_PARM7] = shaderParms[7];
	registers[EXP_REG_PARM8] = shaderParms[8];
	registers[EXP_REG_PARM9] = shaderParms[9];
	registers[EXP_REG_PARM10] = shaderParms[10];
	registers[EXP_REG_PARM11] = shaderParms[11];
	registers[EXP_REG_GLOBAL0] = 0.0f;
	registers[EXP_REG_GLOBAL1] = 0.0f;
	registers[EXP_REG_GLOBAL2] = 0.0f;
	registers[EXP_REG_GLOBAL3] = 0.0f;
	registers[EXP_REG_GLOBAL4] = 0.0f;
	registers[EXP_REG_GLOBAL5] = 0.0f;
	registers[EXP_REG_GLOBAL6] = 0.0f;
	registers[EXP_REG_GLOBAL7] = 0.0f;
	registers[EXP_REG_VERTEX_RANDOM] = shaderParms[SHADERPARM_DIVERSITY];

	op = ops + stage->mStageOpsStart;
	for ( i = 0 ; i < stage->mNumStageOps ; i++, op++ ) {
		switch( op->opType ) {
		case OP_TYPE_ADD:
			registers[op->c] = registers[op->a] + registers[op->b];
			break;
		case OP_TYPE_SUBTRACT:
			registers[op->c] = registers[op->a] - registers[op->b];
			break;
		case OP_TYPE_MULTIPLY:
			registers[op->c] = registers[op->a] * registers[op->b];
			break;
		case OP_TYPE_DIVIDE:
			registers[op->c] = registers[op->a] / registers[op->b];
			break;
		case OP_TYPE_MOD:
			b = (int)registers[op->b];
			b = b != 0 ? b : 1;
			registers[op->c] = (int)registers[op->a] % b;
			break;
		case OP_TYPE_TABLE:
			{
				const idDeclTable *table = static_cast<const idDeclTable *>( declManager->DeclByIndex( DECL_TABLE, op->a ) );
				registers[op->c] = table->TableLookup( registers[op->b] );
			}
			break;
		case OP_TYPE_SOUND:
			registers[op->c] = 0.0f;
			break;
		case OP_TYPE_GLSL_ENABLED:
			registers[op->c] =
					R_MaterialGLSLProgramsAvailable() ? 1.0f : 0.0f;
			break;
		case OP_TYPE_GT:
			registers[op->c] = registers[ op->a ] > registers[op->b];
			break;
		case OP_TYPE_GE:
			registers[op->c] = registers[ op->a ] >= registers[op->b];
			break;
		case OP_TYPE_LT:
			registers[op->c] = registers[ op->a ] < registers[op->b];
			break;
		case OP_TYPE_LE:
			registers[op->c] = registers[ op->a ] <= registers[op->b];
			break;
		case OP_TYPE_EQ:
			registers[op->c] = registers[ op->a ] == registers[op->b];
			break;
		case OP_TYPE_NE:
			registers[op->c] = registers[ op->a ] != registers[op->b];
			break;
		case OP_TYPE_AND:
			registers[op->c] = registers[ op->a ] && registers[op->b];
			break;
		case OP_TYPE_OR:
			registers[op->c] = registers[ op->a ] || registers[op->b];
			break;
		default:
			common->FatalError( "R_EvaluateExpression: bad opcode" );
		}
	}
}

/*
=============
idMaterial::Texgen
=============
*/
texgen_t idMaterial::Texgen() const {
	if ( stages ) {
		for ( int i = 0; i < numStages; i++ ) {
			if ( stages[ i ].texture.texgen != TG_EXPLICIT ) {
				return stages[ i ].texture.texgen;
			}
		}
	}
	
	return TG_EXPLICIT;
}

/*
=============
idMaterial::GetImageWidth
=============
*/
int idMaterial::GetImageWidth( void ) const {
	if ( numStages > 0 && stages != NULL && stages[0].texture.image != NULL ) {
		return stages[0].texture.image->GetOpts().width;
	}

	const idImage *image = GetEditorImage();
	return image != NULL ? image->GetOpts().width : 0;
}

/*
=============
idMaterial::GetImageHeight
=============
*/
int idMaterial::GetImageHeight( void ) const {
	if ( numStages > 0 && stages != NULL && stages[0].texture.image != NULL ) {
		return stages[0].texture.image->GetOpts().height;
	}

	const idImage *image = GetEditorImage();
	return image != NULL ? image->GetOpts().height : 0;
}

/*
=============
idMaterial::CinematicLength
=============
*/
int	idMaterial::CinematicLength() const {
	if ( !stages || !stages[0].texture.cinematic ) {
		return 0;
	}
	return stages[0].texture.cinematic->AnimationLength();
}

/*
=============
idMaterial::CinematicStatus
=============
*/
int idMaterial::CinematicStatus( int time ) const {
	if ( !stages || !stages[0].texture.cinematic ) {
		return FMV_IDLE;
	}

	cinData_t cin = stages[0].texture.cinematic->ImageForTime( time );
	return cin.status;
}

/*
=============
idMaterial::UpdateCinematic
=============
*/
void idMaterial::UpdateCinematic( int time ) const {
	if ( !stages || !stages[0].texture.cinematic || !backEnd.viewDef ) {
		return;
	}
	stages[0].texture.cinematic->ImageForTime( time );
}

/*
=============
idMaterial::CloseCinematic
=============
*/
void idMaterial::CloseCinematic( void ) const {
	for( int i = 0; i < numStages; i++ ) {
		if ( stages[i].texture.cinematic ) {
			stages[i].texture.cinematic->Close();
			delete stages[i].texture.cinematic;
			stages[i].texture.cinematic = NULL;
		}
	}
}

/*
=============
idMaterial::ResetCinematicTime
=============
*/
void idMaterial::ResetCinematicTime( int time ) const {
	for( int i = 0; i < numStages; i++ ) {
		if ( stages[i].texture.cinematic ) {
			stages[i].texture.cinematic->ResetTime( time );
		}
	}
}

/*
=============
idMaterial::ConstantRegisters
=============
*/
const float *idMaterial::ConstantRegisters() const {
	if ( !r_useConstantMaterials.GetBool() ) {
		return NULL;
	}
	return constantRegisters;
}

/*
==================
idMaterial::CheckForConstantRegisters

As of 5/2/03, about half of the unique materials loaded on typical
maps are constant, but 2/3 of the surface references are.
This is probably an optimization of dubious value.
==================
*/
static int	c_constant, c_variable;
void idMaterial::CheckForConstantRegisters() {
	if ( !pd->registersAreConstant ) {
		return;
	}

	// evaluate the registers once, and save them 
	constantRegisters = (float *)R_ClearedStaticAlloc( GetNumRegisters() * sizeof( float ) );

	float shaderParms[MAX_ENTITY_SHADER_PARMS];
	memset( shaderParms, 0, sizeof( shaderParms ) );
	viewDef_t	viewDef;
	memset( &viewDef, 0, sizeof( viewDef ) );

	EvaluateRegisters( constantRegisters, shaderParms, &viewDef, 0 );
}

/*
===================
idMaterial::ImageName
===================
*/
const char *idMaterial::ImageName( void ) const {
	if ( numStages == 0 ) {
		if ( pbrInfo.enabled && pbrInfo.albedo.image != NULL ) {
			return pbrInfo.albedo.image->GetName();
		}
		return "_scratch";
	}
	idImage	*image = stages[0].texture.image;
	if ( image ) {
		return image->GetName();
	}
	return "_scratch";
}

/*
===================
idMaterial::SetImageClassifications

Just for image resource tracking.
===================
*/
void idMaterial::SetImageClassifications( int tag ) const {
	for ( int i = 0 ; i < numStages ; i++ ) {
		idImage	*image = stages[i].texture.image;
		if ( image ) {
			//image->SetClassification( tag );
		}
	}
}

/*
=================
idMaterial::Size
=================
*/
size_t idMaterial::Size( void ) const {
	return sizeof( idMaterial );
}

/*
===================
idMaterial::SetDefaultText
===================
*/
bool idMaterial::SetDefaultText( void ) {
	if (idStr::Icmpn(GetName(),"_retainedMask/",14) == 0) {
		const char* slot = GetName()+14;
		if (!*slot) return false;
		for (const char* p = slot; *p; ++p) if (*p < '0' || *p > '9') return false;
		// Multiply premultiplied destination RGBA by sampled mask alpha.
		// A full clipped quad also clears the area outside the mask paths.
		SetText(va("material %s { sort gui twoSided { blend gl_zero, gl_src_alpha vertexColor nopicmip nearest clamp map _retainedLayerImage%s } }",GetName(),slot));
		return true;
	}
	if (idStr::Icmpn(GetName(),"_retainedLayer/",15) == 0) {
		const char* slot = GetName()+15;
		if (!*slot) return false;
		for (const char* p = slot; *p; ++p) if (*p < '0' || *p > '9') return false;
		SetText(va("material %s { sort gui twoSided { blend gl_one, gl_one_minus_src_alpha vertexColor nopicmip nearest clamp map _retainedLayerImage%s } }",GetName(),slot));
		return true;
	}
	if ( idStr::Icmp(GetName(),"_retainedSolid") == 0 ) {
		// RmlUi's native winding differs from legacy GUI quads; UI planes
		// also remain visible under mirrored document transforms.
		SetText("material _retainedSolid { sort gui twoSided { blend gl_one, gl_one_minus_src_alpha vertexColor map _white } }");
		return true;
	}
	// Process-local retained UI image material. Do not mutate the original
	// material's vertex colour/blending or add a networked content declaration.
	// The runtime's first integration path accepts direct images and generated
	// font atlas image identities here, not arbitrary multi-stage materials.
	if ( idStr::Icmpn( GetName(), "_retained/", 10 ) == 0 ) {
		const char* imageName = GetName() + 10;
		if ( !imageName[0] ) return false;
		for ( const char* p = imageName; *p; ++p ) {
			if ( !( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
				*p == '_' || *p == '-' || *p == '/' || *p == '.' ) ) return false;
		}
		idStr generated = va( "material %s { sort gui twoSided { blend blend vertexColor nopicmip linear clamp map \"%s\" } }", GetName(), imageName );
		SetText( generated.c_str() );
		return true;
	}
	// if there exists an image with the same name
	if ( 1 ) { //fileSystem->ReadFile( GetName(), NULL ) != -1 ) {
		char generated[2048];
		const char *noPicMip = R_MaterialNeedsImplicitGuiAtlasNoPicMip( GetName() ) ? "nopicmip\n" : "";
		idStr::snPrintf( generated, sizeof( generated ), 
						"material %s // IMPLICITLY GENERATED\n"
						"{\n"
						"{\n"
						"blend blend\n"
						"colored\n"
						"map \"%s\"\n"
						"clamp\n"
						"%s"
						"}\n"
						"}\n", GetName(), GetName(), noPicMip );
		SetText( generated );
		return true;
	} else {
		return false;
	}
}

/*
===================
idMaterial::DefaultDefinition
===================
*/
const char *idMaterial::DefaultDefinition() const {
	return
		"{\n"
	"\t"	"{\n"
	"\t\t"		"blend\tblend\n"
	"\t\t"		"map\t\t_default\n"
	"\t"	"}\n"
		"}";
}


/*
===================
idMaterial::GetPortalImageName
===================
*/
const char *idMaterial::GetPortalImageName( void ) const {
	if ( portalImage == NULL ) {
		return NULL;
	}
	return portalImage->GetName();
}

/*
===================
idMaterial::GetBumpStage
===================
*/
const shaderStage_t *idMaterial::GetBumpStage( void ) const {
	for ( int i = 0 ; i < numStages ; i++ ) {
		if ( stages[i].lighting == SL_BUMP ) {
			return &stages[i];
		}
	}
	return NULL;
}

/*
===================
idMaterial::HasActiveCustomGLSLLighting
===================
*/
static bool MaterialStageIsActive( const shaderStage_t &stage, const float *registers ) {
	return registers == NULL || registers[stage.conditionRegister] != 0.0f;
}

static bool MaterialStageHasActiveCustomGLSLLighting( const shaderStage_t &stage, const float *registers ) {
	if ( stage.newStage == NULL || !stage.newStage->customLighting || !stage.newStage->glslProgram ) {
		return false;
	}
	return MaterialStageIsActive( stage, registers );
}

static bool MaterialStagesHaveActiveCustomGLSLLighting( const shaderStage_t *stageList, const int stageCount, const float *registers ) {
	if ( stageList == NULL || stageCount <= 0 ) {
		return false;
	}
	for ( int i = 0 ; i < stageCount ; i++ ) {
		if ( MaterialStageHasActiveCustomGLSLLighting( stageList[i], registers ) ) {
			return true;
		}
	}
	return false;
}

bool idMaterial::HasActiveCustomGLSLLighting( const float *registers ) const {
	if ( !hasCustomGLSLLightingStage ) {
		return false;
	}
	return MaterialStagesHaveActiveCustomGLSLLighting( stages, numStages, registers );
}

/*
===================
idMaterial::HasActiveStockLightingInteractions
===================
*/
static bool MaterialStageHasActiveStockBump( const shaderStage_t &stage, const float *registers ) {
	if ( stage.newStage != NULL || stage.lighting != SL_BUMP || stage.texture.image == NULL ) {
		return false;
	}
	return MaterialStageIsActive( stage, registers );
}

static bool MaterialStageHasActiveStockLitStage( const shaderStage_t &stage, const float *registers ) {
	if ( stage.newStage != NULL || stage.texture.image == NULL ) {
		return false;
	}
	if ( stage.lighting != SL_DIFFUSE && stage.lighting != SL_SPECULAR ) {
		return false;
	}
	return MaterialStageIsActive( stage, registers );
}

static bool MaterialStagesHaveActiveStockLightingInteractions( const shaderStage_t *stageList, const int stageCount, const float *registers ) {
	if ( stageList == NULL || stageCount <= 0 ) {
		return false;
	}

	bool haveBump = false;
	bool haveLitStage = false;

	for ( int i = 0 ; i < stageCount ; i++ ) {
		const shaderStage_t &stage = stageList[i];
		if ( MaterialStageHasActiveStockBump( stage, registers ) ) {
			haveBump = true;
		}
		if ( MaterialStageHasActiveStockLitStage( stage, registers ) ) {
			haveLitStage = true;
		}
	}

	return haveBump && haveLitStage;
}

bool idMaterial::HasActiveStockLightingInteractions( const float *registers ) const {
	return MaterialStagesHaveActiveStockLightingInteractions( stages, numStages, registers );
}

/*
===================
idMaterial::CanUseStockShadowMapReceiverForCustomGLSLLighting
===================
*/
bool idMaterial::CanUseStockShadowMapReceiverForCustomGLSLLighting( const float *registers ) const {
	if ( !hasCustomGLSLLightingStage ) {
		return false;
	}
	return MaterialStagesHaveActiveCustomGLSLLighting( stages, numStages, registers )
		&& MaterialStagesHaveActiveStockLightingInteractions( stages, numStages, registers );
}

bool R_MaterialCustomGLSLReceiverHelperSelfTest( void ) {
	float shaderRegisters[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
	idImage *fakeImage = reinterpret_cast< idImage * >( static_cast<uintptr_t>( 1 ) );

	newShaderStage_t customLightingStage;
	memset( &customLightingStage, 0, sizeof( customLightingStage ) );
	customLightingStage.glslProgram = true;
	customLightingStage.customLighting = true;

	shaderStage_t compatibleStages[3];
	memset( compatibleStages, 0, sizeof( compatibleStages ) );
	compatibleStages[0].conditionRegister = 0;
	compatibleStages[0].lighting = SL_BUMP;
	compatibleStages[0].texture.image = fakeImage;
	compatibleStages[1].conditionRegister = 1;
	compatibleStages[1].lighting = SL_DIFFUSE;
	compatibleStages[1].texture.image = fakeImage;
	compatibleStages[2].conditionRegister = 2;
	compatibleStages[2].newStage = &customLightingStage;

	shaderStage_t customOnlyStage;
	memset( &customOnlyStage, 0, sizeof( customOnlyStage ) );
	customOnlyStage.conditionRegister = 2;
	customOnlyStage.newStage = &customLightingStage;

	shaderStage_t inactiveCustomStages[3];
	memcpy( inactiveCustomStages, compatibleStages, sizeof( inactiveCustomStages ) );
	inactiveCustomStages[2].conditionRegister = 3;

	shaderStage_t inactiveStockStages[3];
	memcpy( inactiveStockStages, compatibleStages, sizeof( inactiveStockStages ) );
	inactiveStockStages[1].conditionRegister = 3;

	return MaterialStagesHaveActiveCustomGLSLLighting( compatibleStages, 3, shaderRegisters )
		&& MaterialStagesHaveActiveStockLightingInteractions( compatibleStages, 3, shaderRegisters )
		&& MaterialStagesHaveActiveCustomGLSLLighting( &customOnlyStage, 1, shaderRegisters )
		&& !MaterialStagesHaveActiveStockLightingInteractions( &customOnlyStage, 1, shaderRegisters )
		&& !MaterialStagesHaveActiveCustomGLSLLighting( inactiveCustomStages, 3, shaderRegisters )
		&& MaterialStagesHaveActiveStockLightingInteractions( inactiveCustomStages, 3, shaderRegisters )
		&& MaterialStagesHaveActiveCustomGLSLLighting( inactiveStockStages, 3, shaderRegisters )
		&& !MaterialStagesHaveActiveStockLightingInteractions( inactiveStockStages, 3, shaderRegisters );
}

/*
===================
R_SpecularProbeMaterialParserSelfTest

Uses the intrinsic normalization cubemap so the parser contract can be tested
without loose content.  Invalid declarations must fail as a whole and expose no
probe metadata.
===================
*/
bool R_SpecularProbeMaterialParserSelfTest( void ) {
	static const char validProbe[] =
		"material _specular_probe_selftest_valid {\n"
		" openQ4SpecularProbe {\n"
		"  cubeMap normalCubeMap\n"
		"  tint 0.75 1 1.25\n"
		"  intensity 2\n"
		"  blendFraction 0.4\n"
		"  priority 17\n"
		" }\n"
		" {\n"
		"  map _white\n"
		" }\n"
		"}\n";
	idDecl *validDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( validDecl == NULL ) {
		return false;
	}
	idMaterial *valid = static_cast<idMaterial *>( validDecl );
	bool ok = valid->Parse( validProbe,
		idLib::SizeToInt( sizeof( validProbe ) - 1,
			"R_SpecularProbeMaterialParserSelfTest valid" ) );
	if ( ok ) {
		const specularProbeMaterialInfo_t &info = valid->GetSpecularProbeInfo();
		ok = valid->HasSpecularProbe()
			&& info.cubeImage == globalImages->normalCubeMapImage
			&& info.cubeConvention == SPECULAR_PROBE_CUBE_NATIVE
			&& idMath::Fabs( info.tint[0] - 0.75f ) < 0.0001f
			&& idMath::Fabs( info.tint[1] - 1.0f ) < 0.0001f
			&& idMath::Fabs( info.tint[2] - 1.25f ) < 0.0001f
			&& idMath::Fabs( info.intensity - 2.0f ) < 0.0001f
			&& idMath::Fabs( info.blendFraction - 0.4f ) < 0.0001f
			&& info.priority == 17
			&& valid->GetNumStages() == 1
			&& valid->GetStage( 0 )->texture.image == globalImages->whiteImage;
	}
	DeclManager_FreeAllocatedDecl( validDecl );
	if ( !ok ) {
		common->Printf( "RendererSpecularProbe material parser self-test: valid contract failed\n" );
		return false;
	}

	static const char defaultPolicy[] =
		"material _specular_probe_selftest_defaults {\n"
		" openQ4SpecularProbe {\n"
		"  cubeMap normalCubeMap\n"
		" }\n"
		"}\n";
	idDecl *defaultDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( defaultDecl == NULL ) {
		return false;
	}
	idMaterial *defaults = static_cast<idMaterial *>( defaultDecl );
	ok = defaults->Parse( defaultPolicy,
		idLib::SizeToInt( sizeof( defaultPolicy ) - 1,
			"R_SpecularProbeMaterialParserSelfTest defaults" ) );
	if ( ok ) {
		const specularProbeMaterialInfo_t &info = defaults->GetSpecularProbeInfo();
		ok = info.enabled && info.tint[0] == 1.0f && info.tint[1] == 1.0f
			&& info.tint[2] == 1.0f && info.intensity == 1.0f
			&& info.blendFraction == 0.25f && info.priority == 0;
	}
	DeclManager_FreeAllocatedDecl( defaultDecl );
	if ( !ok ) {
		common->Printf( "RendererSpecularProbe material parser self-test: default policy failed\n" );
		return false;
	}

	auto rejectsProbeDeclaration = []( const char *declaration, const char *label ) -> bool {
		idDecl *decl = declManager->AllocateDecl( DECL_MATERIAL );
		if ( decl == NULL ) {
			return false;
		}
		idMaterial *material = static_cast<idMaterial *>( decl );
		const bool accepted = material->Parse( declaration,
			idLib::SizeToInt( strlen( declaration ), label ) );
		const bool leakedPartialContract = material->HasSpecularProbe();
		DeclManager_FreeAllocatedDecl( decl );
		return !accepted && !leakedPartialContract;
	};
	static const char duplicateBlock[] =
		"material _specular_probe_selftest_duplicate {\n"
		" openQ4SpecularProbe { cubeMap normalCubeMap\n }\n"
		" openQ4SpecularProbe { cubeMap normalCubeMap\n }\n"
		"}\n";
	static const char missingCube[] =
		"material _specular_probe_selftest_missing_cube {\n"
		" openQ4SpecularProbe { intensity 1\n }\n"
		"}\n";
	static const char twoDimensionalImage[] =
		"material _specular_probe_selftest_2d {\n"
		" openQ4SpecularProbe { cubeMap _white\n }\n"
		"}\n";
	static const char mutableImage[] =
		"material _specular_probe_selftest_mutable {\n"
		" openQ4SpecularProbe { cubeMap _currentRender\n }\n"
		"}\n";
	static const char imageProgram[] =
		"material _specular_probe_selftest_program {\n"
		" openQ4SpecularProbe { cubeMap add( normalCubeMap, normalCubeMap )\n }\n"
		"}\n";
	static const char zeroIntensity[] =
		"material _specular_probe_selftest_zero {\n"
		" openQ4SpecularProbe { cubeMap normalCubeMap\n intensity 0\n }\n"
		"}\n";
	static const char fractionalPriority[] =
		"material _specular_probe_selftest_priority {\n"
		" openQ4SpecularProbe { cubeMap normalCubeMap\n priority 1.5\n }\n"
		"}\n";

	return rejectsProbeDeclaration( duplicateBlock, "R_SpecularProbeMaterialParserSelfTest duplicate" )
		&& rejectsProbeDeclaration( missingCube, "R_SpecularProbeMaterialParserSelfTest missing cube" )
		&& rejectsProbeDeclaration( twoDimensionalImage, "R_SpecularProbeMaterialParserSelfTest 2D image" )
		&& rejectsProbeDeclaration( mutableImage, "R_SpecularProbeMaterialParserSelfTest mutable image" )
		&& rejectsProbeDeclaration( imageProgram, "R_SpecularProbeMaterialParserSelfTest image program" )
		&& rejectsProbeDeclaration( zeroIntensity, "R_SpecularProbeMaterialParserSelfTest zero intensity" )
		&& rejectsProbeDeclaration( fractionalPriority, "R_SpecularProbeMaterialParserSelfTest fractional priority" );
}

/*
===================
R_PBRMaterialParserSelfTest

Runtime parser test using only intrinsic images.  It proves metadata parsing,
classic-stage non-interference, explicit fallback generation, and the two
important authoring failures without requiring repository content.
===================
*/
bool R_PBRMaterialParserSelfTest( void ) {
	static const char dualAuthored[] =
		"material _pbr_selftest_dual {\n"
		" bumpmap _flat\n"
		" diffusemap _white\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap heightmap( _white, 1 )\n"
		"  normalMap _flat\n"
		"  normalFormat tangentRG\n"
		"  ormMap smoothnormals( _flat )\n"
		"  emissiveMap _white\n"
		"  metallic 0.25\n"
		"  roughness 0.6\n"
		"  ao 0.9\n"
		"  normalScale 0.75\n"
		"  emissiveColor 0.1 0.2 0.3\n"
		" }\n"
		"}\n";

	idDecl *dualDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( dualDecl == NULL ) {
		return false;
	}
	idMaterial *dual = static_cast<idMaterial *>( dualDecl );
	bool ok = dual->Parse( dualAuthored, idLib::SizeToInt( sizeof( dualAuthored ) - 1, "R_PBRMaterialParserSelfTest dual" ) );
	if ( !ok ) {
		common->Printf( "RendererPBRMaterial parser self-test: dual declaration did not parse\n" );
	}
	if ( ok ) {
		const pbrMaterialInfo_t &info = dual->GetPBRInfo();
		const int registerCount = dual->GetNumRegisters();
		float evaluatedRegisters[MAX_EXPRESSION_REGISTERS];
		float entityParms[MAX_ENTITY_SHADER_PARMS];
		viewDef_t evaluationView;
		memset( evaluatedRegisters, 0, sizeof( evaluatedRegisters ) );
		memset( entityParms, 0, sizeof( entityParms ) );
		memset( &evaluationView, 0, sizeof( evaluationView ) );
		dual->EvaluateRegisters( evaluatedRegisters, entityParms, &evaluationView );
		auto registerMatches = [&]( int index, float expected ) -> bool {
			return index >= 0 && index < registerCount
				&& idMath::Fabs( evaluatedRegisters[index] - expected ) < 0.0001f;
		};
		const idImage *sameAlbedo = info.albedo.image != NULL ? globalImages->GetImageWithParameters(
			info.albedo.image->GetName(),
			static_cast<textureFilter_t>( info.albedo.filter ),
			static_cast<textureRepeat_t>( info.albedo.repeat ),
			TD_PBR_COLOR, CF_2D, info.albedo.allowPicmip, 0 ) : NULL;
		const idImage *sameORM = info.orm.image != NULL ? globalImages->GetImageWithParameters(
			info.orm.image->GetName(),
			static_cast<textureFilter_t>( info.orm.filter ),
			static_cast<textureRepeat_t>( info.orm.repeat ),
			TD_MATERIAL_DATA, CF_2D, info.orm.allowPicmip, 0 ) : NULL;
		ok = dual->HasPBR()
			&& info.workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS
			&& info.normalFormat == PBR_NORMAL_TANGENT_RG
			&& info.albedo.present && info.normal.present && info.orm.present
			&& info.albedo.image->GetUsage() == TD_PBR_COLOR
			&& info.orm.image->GetUsage() == TD_MATERIAL_DATA
			&& sameAlbedo == info.albedo.image
			&& sameORM == info.orm.image
			&& registerMatches( info.metallicRegister, 0.25f )
			&& registerMatches( info.roughnessRegister, 0.6f )
			&& registerMatches( info.aoRegister, 0.9f )
			&& registerMatches( info.normalScaleRegister, 0.75f )
			&& registerMatches( info.emissiveColorRegisters[0], 0.1f )
			&& registerMatches( info.emissiveColorRegisters[1], 0.2f )
			&& registerMatches( info.emissiveColorRegisters[2], 0.3f )
			&& info.hasAuthoredClassicFallback
			&& !info.usesGeneratedLegacyFallback
			&& dual->GetNumStages() == 2;
		if ( !ok ) {
			common->Printf(
				"RendererPBRMaterial parser self-test: dual contract failed has=%d workflow=%d normal=%d maps=%d/%d/%d usage=%d/%d cache=%d/%d stages=%d authored=%d generated=%d registers=%d/%d/%d/%d/%d/%d/%d\n",
				dual->HasPBR() ? 1 : 0,
				static_cast<int>( info.workflow ),
				static_cast<int>( info.normalFormat ),
				info.albedo.present ? 1 : 0,
				info.normal.present ? 1 : 0,
				info.orm.present ? 1 : 0,
				info.albedo.image != NULL ? static_cast<int>( info.albedo.image->GetUsage() ) : -1,
				info.orm.image != NULL ? static_cast<int>( info.orm.image->GetUsage() ) : -1,
				sameAlbedo == info.albedo.image ? 1 : 0,
				sameORM == info.orm.image ? 1 : 0,
				dual->GetNumStages(),
				info.hasAuthoredClassicFallback ? 1 : 0,
				info.usesGeneratedLegacyFallback ? 1 : 0,
				registerMatches( info.metallicRegister, 0.25f ) ? 1 : 0,
				registerMatches( info.roughnessRegister, 0.6f ) ? 1 : 0,
				registerMatches( info.aoRegister, 0.9f ) ? 1 : 0,
				registerMatches( info.normalScaleRegister, 0.75f ) ? 1 : 0,
				registerMatches( info.emissiveColorRegisters[0], 0.1f ) ? 1 : 0,
				registerMatches( info.emissiveColorRegisters[1], 0.2f ) ? 1 : 0,
				registerMatches( info.emissiveColorRegisters[2], 0.3f ) ? 1 : 0 );
		}
	}
	DeclManager_FreeAllocatedDecl( dualDecl );
	if ( !ok ) {
		return false;
	}

	static const char explicitFallback[] =
		"material _pbr_selftest_explicit {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  legacyBumpMap _flat\n"
		"  legacyDiffuseMap nearest clamp nopicmip nomips forceHighQuality makeIntensity( _white )\n"
		"  autoLegacyFallback 0\n"
		" }\n"
		"}\n";
	idDecl *explicitDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( explicitDecl == NULL ) {
		return false;
	}
	idMaterial *explicitMaterial = static_cast<idMaterial *>( explicitDecl );
	const bool oldMakingBuild = com_makingBuild.GetBool();
	com_makingBuild.SetBool( true );
	ok = explicitMaterial->Parse( explicitFallback, idLib::SizeToInt( sizeof( explicitFallback ) - 1, "R_PBRMaterialParserSelfTest explicit" ) );
	com_makingBuild.SetBool( oldMakingBuild );
	if ( ok ) {
		const pbrMaterialInfo_t &info = explicitMaterial->GetPBRInfo();
		const shaderStage_t *diffuseStage = NULL;
		for ( int i = 0; i < explicitMaterial->GetNumStages(); ++i ) {
			const shaderStage_t *stage = explicitMaterial->GetStage( i );
			if ( stage != NULL && stage->lighting == SL_DIFFUSE ) {
				diffuseStage = stage;
				break;
			}
		}
		const idImage *expectedDiffuse = globalImages->GetImageWithParameters(
			info.legacyDiffuse.image != NULL ? info.legacyDiffuse.image->GetName() : "",
			TF_NEAREST,
			TR_CLAMP,
			TD_HIGH_QUALITY,
			CF_2D,
			false,
			IMAGEFLAG_NOMIPS );
		ok = info.hasExplicitLegacyFallback
			&& info.usesGeneratedLegacyFallback
			&& !info.usesApproximateLegacyFallback
			&& info.legacyDiffuse.filter == static_cast<int>( TF_NEAREST )
			&& info.legacyDiffuse.repeat == static_cast<int>( TR_CLAMP )
			&& !info.legacyDiffuse.allowPicmip
			&& info.legacyDiffuse.noMips
			&& info.legacyDiffuse.highQuality
			&& info.legacyDiffuse.forceHighQuality
			&& explicitMaterial->GetNumStages() == 2
			&& diffuseStage != NULL
			&& diffuseStage->texture.image == expectedDiffuse;
	}
	DeclManager_FreeAllocatedDecl( explicitDecl );
	if ( !ok ) {
		return false;
	}

	static const char missingFallback[] =
		"material _pbr_selftest_missing_fallback {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  autoLegacyFallback 0\n"
		" }\n"
		"}\n";
	idDecl *missingDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( missingDecl == NULL ) {
		return false;
	}
	idMaterial *missingMaterial = static_cast<idMaterial *>( missingDecl );
	ok = missingMaterial->Parse( missingFallback, idLib::SizeToInt( sizeof( missingFallback ) - 1, "R_PBRMaterialParserSelfTest missing fallback" ) );
	if ( ok ) {
		const pbrMaterialInfo_t &info = missingMaterial->GetPBRInfo();
		ok = missingMaterial->HasPBR()
			&& info.legacyFallbackMissing
			&& !info.usesGeneratedLegacyFallback
			&& !info.usesApproximateLegacyFallback
			&& missingMaterial->GetNumStages() == 0;
	}
	DeclManager_FreeAllocatedDecl( missingDecl );
	if ( !ok ) {
		return false;
	}

	// A PBR-only declaration is still required to construct the complete ARB2
	// interaction contract when development fallback generation is enabled. Do
	// not settle for validating only the normal stage: every low-end renderer
	// needs the conventional bump/diffuse/specular trio.
	static const char generatedClassicInteractionFallback[] =
		"material _pbr_selftest_generated_classic_interaction {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  normalMap _flat\n"
		"  normalFormat tangentRG\n"
		" }\n"
		"}\n";
	auto validatesGeneratedClassicInteractionFallback = []( const char *declaration, const char *label ) -> bool {
		idDecl *decl = declManager->AllocateDecl( DECL_MATERIAL );
		if ( decl == NULL ) {
			return false;
		}
		idMaterial *material = static_cast<idMaterial *>( decl );
		bool valid = material->Parse( declaration, idLib::SizeToInt( strlen( declaration ), label ) );
		if ( valid ) {
			const pbrMaterialInfo_t &info = material->GetPBRInfo();
			const shaderStage_t *bumpStage = NULL;
			const shaderStage_t *diffuseStage = NULL;
			const shaderStage_t *specularStage = NULL;
			for ( int i = 0; i < material->GetNumStages(); ++i ) {
				const shaderStage_t *stage = material->GetStage( i );
				if ( stage == NULL ) {
					continue;
				}
				if ( stage->lighting == SL_BUMP ) {
					bumpStage = stage;
				} else if ( stage->lighting == SL_DIFFUSE ) {
					diffuseStage = stage;
				} else if ( stage->lighting == SL_SPECULAR ) {
					specularStage = stage;
				}
			}
			valid = info.usesGeneratedLegacyFallback
				&& info.usesApproximateLegacyFallback
				&& !info.legacyFallbackMissing
				&& material->GetNumStages() == 3
				&& bumpStage != NULL && bumpStage->texture.image == globalImages->flatNormalMap
				&& diffuseStage != NULL && diffuseStage->texture.image == info.albedo.image
				&& specularStage != NULL && specularStage->texture.image == globalImages->blackImage;
		}
		DeclManager_FreeAllocatedDecl( decl );
		return valid;
	};

	auto validatesGeneratedNormalFallback = []( const char *declaration, const char *label, bool expectReuse ) -> bool {
		idDecl *decl = declManager->AllocateDecl( DECL_MATERIAL );
		if ( decl == NULL ) {
			return false;
		}
		idMaterial *material = static_cast<idMaterial *>( decl );
		bool valid = material->Parse( declaration, idLib::SizeToInt( strlen( declaration ), label ) );
		if ( valid ) {
			const pbrMaterialInfo_t &info = material->GetPBRInfo();
			const shaderStage_t *bumpStage = NULL;
			for ( int i = 0; i < material->GetNumStages(); ++i ) {
				const shaderStage_t *stage = material->GetStage( i );
				if ( stage != NULL && stage->lighting == SL_BUMP ) {
					bumpStage = stage;
					break;
				}
			}
			valid = info.usesApproximateLegacyFallback
				&& !info.legacyFallbackMissing
				&& info.normal.image != NULL
				&& bumpStage != NULL
				&& ( expectReuse
					? bumpStage->texture.image == info.normal.image
					: bumpStage->texture.image == globalImages->flatNormalMap
						&& info.normal.image != globalImages->flatNormalMap );
		}
		DeclManager_FreeAllocatedDecl( decl );
		return valid;
	};
	static const char tangentNormalFallback[] =
		"material _pbr_selftest_tangent_normal_fallback {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  normalMap makeIntensity( _white )\n"
		"  normalFormat tangentRG\n"
		" }\n"
		"}\n";
	static const char quake4NormalFallback[] =
		"material _pbr_selftest_quake4_normal_fallback {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  normalMap makeIntensity( _white )\n"
		"  normalFormat quake4AGB\n"
		" }\n"
		"}\n";
	const bool oldGeneratedFallback = r_pbrGeneratedLegacyFallback.GetBool();
	r_pbrGeneratedLegacyFallback.SetBool( true );
	const bool generatedFallbacksValid = validatesGeneratedClassicInteractionFallback(
		generatedClassicInteractionFallback, "R_PBRMaterialParserSelfTest complete ARB2 fallback" )
		&& validatesGeneratedNormalFallback(
		tangentNormalFallback, "R_PBRMaterialParserSelfTest tangent fallback", false )
		&& validatesGeneratedNormalFallback(
			quake4NormalFallback, "R_PBRMaterialParserSelfTest quake4 fallback", true );
	r_pbrGeneratedLegacyFallback.SetBool( oldGeneratedFallback );
	if ( !generatedFallbacksValid ) {
		return false;
	}

	static const char missingNormalFormat[] =
		"material _pbr_selftest_bad_normal {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  normalMap _flat\n"
		" }\n"
		"}\n";
	idDecl *badNormalDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( badNormalDecl == NULL ) {
		return false;
	}
	idMaterial *badNormal = static_cast<idMaterial *>( badNormalDecl );
	const bool badNormalAccepted = badNormal->Parse( missingNormalFormat, idLib::SizeToInt( sizeof( missingNormalFormat ) - 1, "R_PBRMaterialParserSelfTest normal" ) );
	DeclManager_FreeAllocatedDecl( badNormalDecl );
	if ( badNormalAccepted ) {
		return false;
	}

	static const char conflictingMaps[] =
		"material _pbr_selftest_bad_maps {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  ormMap _white\n"
		"  roughnessMap _white\n"
		" }\n"
		"}\n";
	idDecl *badMapsDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( badMapsDecl == NULL ) {
		return false;
	}
	idMaterial *badMaps = static_cast<idMaterial *>( badMapsDecl );
	const bool badMapsAccepted = badMaps->Parse( conflictingMaps, idLib::SizeToInt( sizeof( conflictingMaps ) - 1, "R_PBRMaterialParserSelfTest maps" ) );
	DeclManager_FreeAllocatedDecl( badMapsDecl );
	if ( badMapsAccepted ) {
		return false;
	}

	static const char fractionalFallback[] =
		"material _pbr_selftest_bad_fallback_value {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  autoLegacyFallback 0.5\n"
		" }\n"
		"}\n";
	idDecl *fractionalDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( fractionalDecl == NULL ) {
		return false;
	}
	idMaterial *fractionalMaterial = static_cast<idMaterial *>( fractionalDecl );
	const bool fractionalAccepted = fractionalMaterial->Parse( fractionalFallback, idLib::SizeToInt( sizeof( fractionalFallback ) - 1, "R_PBRMaterialParserSelfTest fallback value" ) );
	DeclManager_FreeAllocatedDecl( fractionalDecl );
	if ( fractionalAccepted ) {
		return false;
	}

	auto rejectsPBRDeclaration = []( const char *declaration, const char *label ) -> bool {
		idDecl *decl = declManager->AllocateDecl( DECL_MATERIAL );
		if ( decl == NULL ) {
			return false;
		}
		idMaterial *material = static_cast<idMaterial *>( decl );
		const bool accepted = material->Parse( declaration, idLib::SizeToInt( strlen( declaration ), label ) );
		DeclManager_FreeAllocatedDecl( decl );
		return !accepted;
	};
	static const char dynamicImageToken[] =
		"material _pbr_selftest_dynamic_image {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap reflectionRenderMap\n"
		" }\n"
		"}\n";
	static const char shaderImageToken[] =
		"material _pbr_selftest_shader_image {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap glslProgram\n"
		" }\n"
		"}\n";
	static const char nestedDynamicImageToken[] =
		"material _pbr_selftest_nested_dynamic_image {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap add( _white, reflectionRenderMap )\n"
		" }\n"
		"}\n";
	static const char nestedShaderImageToken[] =
		"material _pbr_selftest_nested_shader_image {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap add( _white, glslProgram )\n"
		" }\n"
		"}\n";
	static const char nestedSceneCaptureToken[] =
		"material _pbr_selftest_nested_scene_capture {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap add( _white, _currentRender )\n"
		" }\n"
		"}\n";
	static const char nestedMutableRenderTargetToken[] =
		"material _pbr_selftest_nested_mutable_target {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap add( _white, _reflectionRender )\n"
		" }\n"
		"}\n";
	static const char nestedStageStateToken[] =
		"material _pbr_selftest_nested_stage_state {\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap add( _white, alphaTest )\n"
		" }\n"
		"}\n";
	const bool pbrRejectionsValid = rejectsPBRDeclaration( dynamicImageToken, "R_PBRMaterialParserSelfTest dynamic image" )
		&& rejectsPBRDeclaration( shaderImageToken, "R_PBRMaterialParserSelfTest shader image" )
		&& rejectsPBRDeclaration( nestedDynamicImageToken, "R_PBRMaterialParserSelfTest nested dynamic image" )
		&& rejectsPBRDeclaration( nestedShaderImageToken, "R_PBRMaterialParserSelfTest nested shader image" )
		&& rejectsPBRDeclaration( nestedSceneCaptureToken, "R_PBRMaterialParserSelfTest nested scene capture" )
		&& rejectsPBRDeclaration( nestedMutableRenderTargetToken, "R_PBRMaterialParserSelfTest nested mutable target" )
		&& rejectsPBRDeclaration( nestedStageStateToken, "R_PBRMaterialParserSelfTest nested stage state" );
	return pbrRejectionsValid && R_SpecularProbeMaterialParserSelfTest();
}

/*
===================
idMaterial::ReloadImages
===================
*/
void idMaterial::ReloadImages( bool force ) const
{
	for ( int i = 0 ; i < numStages ; i++ ) {
		if ( stages[i].newStage ) {
			for ( int j = 0 ; j < stages[i].newStage->numFragmentProgramImages ; j++ ) {
				if ( stages[i].newStage->fragmentProgramImages[j] ) {
					stages[i].newStage->fragmentProgramImages[j]->Reload( force );
				}
			}
			for ( int j = 0 ; j < stages[i].newStage->numShaderTextures ; j++ ) {
				if ( stages[i].newStage->shaderTextureImages[j] ) {
					stages[i].newStage->shaderTextureImages[j]->Reload( force );
				}
			}
		} else if ( stages[i].texture.image ) {
			stages[i].texture.image->Reload( force );
		}
	}

	if ( portalImage ) {
		portalImage->Reload( force );
	}

	const pbrMaterialTexture_t *pbrTextures[] = {
		&pbrInfo.albedo, &pbrInfo.normal, &pbrInfo.orm, &pbrInfo.metallic,
		&pbrInfo.roughness, &pbrInfo.ao, &pbrInfo.emissive,
		&pbrInfo.legacyBump, &pbrInfo.legacyDiffuse,
		&pbrInfo.legacySpecular, &pbrInfo.legacyEmissive
	};
	for ( unsigned int i = 0; i < sizeof( pbrTextures ) / sizeof( pbrTextures[0] ); ++i ) {
		if ( pbrTextures[i]->present && pbrTextures[i]->image != NULL ) {
			pbrTextures[i]->image->Reload( force );
		}
	}
	if ( specularProbeInfo.enabled && specularProbeInfo.cubeImage != NULL ) {
		specularProbeInfo.cubeImage->Reload( force );
	}
}
