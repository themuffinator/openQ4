// Copyright (C) 2026 DarkMatter Productions
#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"
#include "MaterialResourceTable.h"
#include "PBRNativeContract.h"

#include <cmath>

float R_PBRNative_RegisterValue( const drawSurf_t *surf, int registerIndex, float fallback ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL
			|| registerIndex < 0 || registerIndex >= surf->material->GetNumRegisters() ) {
		return fallback;
	}
	const float value = surf->shaderRegisters[ registerIndex ];
	return std::isfinite( value ) ? value : fallback;
}

bool R_PBRNative_ImageReady( const idImage *image, int expectedUsage ) {
	idImage *mutableImage = const_cast<idImage *>( image );
	return image != NULL && image->GetUsage() == expectedUsage
		&& image->IsLoaded() && !image->IsDefaulted()
		&& mutableImage->GetDeviceHandle() != 0;
}

/*
====================
R_PBRNative_HasSingleClassicInteractionTopology

The classic decomposition flushes an interaction whenever it encounters a
second bump, diffuse, or specular stage. A PBR draw evaluates the whole BRDF,
so it may only replace the canonical final submit when the material has exactly
one active classic bump -> diffuse -> specular sequence. Declared duplicates
are rejected even when their current condition is false: changing registers
must never change which decomposition draw owns the complete BRDF.
====================
*/
bool R_PBRNative_HasSingleClassicInteractionTopology( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}

	const idMaterial *material = surf->material;
	const float *registers = surf->shaderRegisters;
	const int registerCount = material->GetNumRegisters();
	int bumpStage = -1;
	int diffuseStage = -1;
	int specularStage = -1;

	for ( int stageIndex = 0; stageIndex < material->GetNumStages(); stageIndex++ ) {
		const shaderStage_t *surfaceStage = material->GetStage( stageIndex );
		if ( surfaceStage == NULL ) {
			return false;
		}

		// A custom-lighting stage is another complete per-light owner. Keep
		// mixed custom/classic materials entirely on their authored path.
		if ( surfaceStage->newStage != NULL && surfaceStage->newStage->customLighting ) {
			return false;
		}

		int *ownerStage = NULL;
		switch ( surfaceStage->lighting ) {
			case SL_BUMP:
				ownerStage = &bumpStage;
				break;
			case SL_DIFFUSE:
				ownerStage = &diffuseStage;
				break;
			case SL_SPECULAR:
				ownerStage = &specularStage;
				break;
			default:
				continue;
		}

		if ( *ownerStage >= 0 || surfaceStage->newStage != NULL
				|| surfaceStage->texture.image == NULL
				|| surfaceStage->conditionRegister < 0
				|| surfaceStage->conditionRegister >= registerCount ) {
			return false;
		}
		const float condition = registers[ surfaceStage->conditionRegister ];
		if ( !std::isfinite( condition ) || condition == 0.0f ) {
			return false;
		}
		*ownerStage = stageIndex;
	}

	return bumpStage >= 0 && diffuseStage > bumpStage && specularStage > diffuseStage;
}

bool R_PBRNative_PerforatedCoverageMatches( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL ) {
		return false;
	}
	const idMaterial *material = surf->material;
	// The depth fill owns alpha testing and per-sample coverage, and native
	// direct draws use EQUAL against that same depth. Only admit a single alpha
	// stage with the PBR albedo's image, sampler and UVs; otherwise a different
	// classic mask would silently change PBR coverage.
	const idImage *albedo = material->GetPBRInfo().albedo.image;
	if ( albedo == NULL ) {
		return false;
	}
	int alphaStages = 0;
	for ( int i = 0; i < material->GetNumStages(); ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( !stage->hasAlphaTest ) {
			continue;
		}
		const idImage *image = stage->texture.image;
		if ( ++alphaStages != 1 || stage->lighting != SL_DIFFUSE
				|| stage->newStage != NULL || image == NULL
				|| idStr::Icmp( image->GetName(), albedo->GetName() ) != 0
				|| image->GetFilter() != albedo->GetFilter()
				|| image->GetRepeat() != albedo->GetRepeat()
				|| stage->texture.texgen != TG_EXPLICIT || stage->texture.hasMatrix
				|| stage->vertexColor != SVC_IGNORE || stage->privatePolygonOffset != 0.0f
				|| R_PBRNative_RegisterValue( surf, stage->conditionRegister, 0.0f ) == 0.0f
				|| R_PBRNative_RegisterValue( surf, stage->color.registers[3], 0.0f ) != 1.0f ) {
			return false;
		}
		const float reference = R_PBRNative_RegisterValue( surf, stage->alphaTestRegister, -1.0f );
		if ( reference < 0.0f || reference > 1.0f ) {
			return false;
		}
	}
	return alphaStages == 1;
}

/*
====================
R_PBRNative_TransparentStage

Source alpha is the one coverage mode the depth fill never resolves, so a
native owner may replace the authored stage only when that stage is provably
the PBR albedo itself: exactly one source-alpha blend stage, the same image,
filter and repeat as the albedo, untransformed explicit coordinates, no vertex
tint or alpha test, and an untinted white color whose alpha register supplies
the coverage scale. Any other ambient stage would compose beside the owner, so
its presence returns the whole material to the classic path.
====================
*/
bool R_PBRNative_TransparentStage( const drawSurf_t *surf, int *stageIndexOut, float *alphaScaleOut ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}
	const idMaterial *material = surf->material;
	if ( material->Coverage() != MC_TRANSLUCENT ) {
		return false;
	}
	const idImage *albedo = material->GetPBRInfo().albedo.image;
	if ( albedo == NULL ) {
		return false;
	}

	int found = -1;
	float alphaScale = 0.0f;
	for ( int i = 0 ; i < material->GetNumStages() ; i++ ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage->lighting != SL_AMBIENT ) {
			continue;	// the bump/diffuse/specular sequence the BRDF replaces
		}
		const int blendBits = stage->drawStateBits
				& ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
		const idImage *image = stage->texture.image;
		if ( found >= 0
				|| blendBits != ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
				|| stage->newStage != NULL || image == NULL
				|| idStr::Icmp( image->GetName(), albedo->GetName() ) != 0
				|| image->GetFilter() != albedo->GetFilter()
				|| image->GetRepeat() != albedo->GetRepeat()
				|| stage->texture.texgen != TG_EXPLICIT || stage->texture.hasMatrix
				|| stage->vertexColor != SVC_IGNORE || stage->hasAlphaTest
				|| stage->privatePolygonOffset != 0.0f
				|| R_PBRNative_RegisterValue( surf, stage->conditionRegister, 0.0f ) == 0.0f ) {
			return false;
		}
		for ( int c = 0 ; c < 3 ; c++ ) {
			if ( R_PBRNative_RegisterValue( surf, stage->color.registers[ c ], 0.0f ) != 1.0f ) {
				return false;
			}
		}
		alphaScale = R_PBRNative_RegisterValue( surf, stage->color.registers[ 3 ], -1.0f );
		if ( !( alphaScale > 0.0f ) || alphaScale > 1.0f ) {
			return false;
		}
		found = i;
	}
	if ( found < 0 ) {
		return false;
	}
	if ( stageIndexOut != NULL ) {
		*stageIndexOut = found;
	}
	if ( alphaScaleOut != NULL ) {
		*alphaScaleOut = alphaScale;
	}
	return true;
}

/*
===============================================================================

Parallax occlusion mapping (REN-11)

A heightMap material's height rides in the alpha of its ORM image (packORMH).
The decision is made here, once per admission, so the OpenGL and Vulkan owners
and every pass of one surface see the same answer: a surface either traces the
same ray in its ambient, environment, per-light and emission draws, or none of
them does. A perforated surface keeps its depth fill's coverage at the flat
texture coordinates, so offsetting its colour would shade texels the depth
fill rejected; it draws flat.

===============================================================================
*/

typedef struct {
	int			frame;
	int			admitted;		// admissions that traced a ray
	int			flat;			// heightMap admissions that drew flat
	int			draws;			// draws that traced a ray, all passes
	const char *lastDecline;
} parallaxFrameStats_t;

static parallaxFrameStats_t g_parallaxStats;
static parallaxFrameStats_t g_parallaxLastStats;

static void R_Parallax_TouchStats( void ) {
	if ( g_parallaxStats.frame != tr.frameCount ) {
		if ( g_parallaxStats.frame != 0 ) {
			g_parallaxLastStats = g_parallaxStats;
		}
		const char *decline = g_parallaxStats.lastDecline;
		memset( &g_parallaxStats, 0, sizeof( g_parallaxStats ) );
		g_parallaxStats.frame = tr.frameCount;
		g_parallaxStats.lastDecline = decline;
	}
}

static void R_Parallax_Decline( const char *reason ) {
	R_Parallax_TouchStats();
	g_parallaxStats.flat++;
	g_parallaxStats.lastDecline = reason;
}

void R_PBRNative_ResolveParallax( const drawSurf_t *surf, int dataFlags, const idImage *dataImage,
		bool &parallax, float &scale, float &bias ) {
	parallax = false;
	scale = 0.0f;
	bias = 0.0f;
	if ( surf == NULL || surf->material == NULL ) {
		return;
	}
	const pbrMaterialInfo_t &info = surf->material->GetPBRInfo();
	if ( !info.parallax || !r_parallaxOcclusion.GetBool() ) {
		return;
	}
	if ( ( dataFlags & 1 ) == 0 || dataImage != info.orm.image ) {
		R_Parallax_Decline( "no packed height" );
		return;
	}
	if ( surf->material->Coverage() == MC_PERFORATED ) {
		R_Parallax_Decline( "perforated coverage" );
		return;
	}
	// Emission draws through its own pass with no tangent frame; tracing every
	// other pass would slide the lit texels off the glow.
	if ( info.emissive.present ) {
		R_Parallax_Decline( "emissive" );
		return;
	}
	const float heightScale = idMath::ClampFloat( 0.0f, 0.25f,
		R_PBRNative_RegisterValue( surf, info.heightScaleRegister, 0.04f ) );
	const float reference = idMath::ClampFloat( 0.0f, 1.0f,
		R_PBRNative_RegisterValue( surf, info.heightReferenceRegister, 1.0f ) );
	if ( !( heightScale > 0.0f ) ) {
		R_Parallax_Decline( "zero heightScale" );
		return;
	}
	R_Parallax_TouchStats();
	g_parallaxStats.admitted++;
	parallax = true;
	scale = heightScale;
	bias = -reference * heightScale;
}

static void R_Parallax_Fade( float fade[4] ) {
	const float end = idMath::ClampFloat( 64.0f, 16384.0f, r_parallaxDistance.GetFloat() );
	fade[0] = end * 0.75f;
	fade[1] = end;
	fade[2] = 0.0f;
	fade[3] = 0.0f;
}

void R_PBRNative_ParallaxParameters( const pbrNativeMaterial_t &material, float params[4], float fade[4] ) {
	params[0] = params[1] = params[2] = params[3] = 0.0f;
	fade[0] = fade[1] = fade[2] = fade[3] = 0.0f;
	if ( !material.parallax ) {
		return;
	}
	params[0] = material.parallaxScale;
	params[1] = material.parallaxBias;
	params[2] = (float)idMath::ClampInt( 8, 64, r_parallaxSamples.GetInteger() );
	params[3] = r_parallaxShadows.GetBool() ? 1.0f : 0.0f;
	R_Parallax_Fade( fade );
}

void R_Parallax_RetailParameters( float scale, float bias, float params[4], float fade[4] ) {
	params[0] = params[1] = params[2] = params[3] = 0.0f;
	fade[0] = fade[1] = fade[2] = fade[3] = 0.0f;
	if ( !r_parallaxOcclusion.GetBool() || !std::isfinite( scale ) || !std::isfinite( bias ) || scale <= 0.0f ) {
		return;
	}
	params[0] = Min( scale, 0.25f );
	params[1] = idMath::ClampFloat( -0.25f, 0.25f, bias );
	params[2] = (float)idMath::ClampInt( 8, 64, r_parallaxSamples.GetInteger() );
	params[3] = r_parallaxShadows.GetBool() ? 1.0f : 0.0f;
	R_Parallax_Fade( fade );
}

void R_Parallax_CountDraw( void ) {
	R_Parallax_TouchStats();
	g_parallaxStats.draws++;
}

void R_Parallax_PrintInfo( void ) {
	// The last complete frame. Counters roll only when something counts, so a
	// frame that drew no parallax surface leaves older counts behind: report
	// those as zero rather than as the current state.
	parallaxFrameStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	if ( g_parallaxStats.frame == tr.frameCount - 1 ) {
		stats = g_parallaxStats;				// the previous frame, complete
	} else if ( g_parallaxStats.frame == tr.frameCount && g_parallaxLastStats.frame == tr.frameCount - 1 ) {
		stats = g_parallaxLastStats;			// this frame is still drawing
	}
	stats.lastDecline = g_parallaxStats.lastDecline;
	common->Printf( "Renderer parallax occlusion: requested=%d samples=%d distance=%.0f shadows=%d admitted=%d flat=%d draws=%d lastDecline=%s\n",
		r_parallaxOcclusion.GetBool() ? 1 : 0, idMath::ClampInt( 8, 64, r_parallaxSamples.GetInteger() ),
		idMath::ClampFloat( 64.0f, 16384.0f, r_parallaxDistance.GetFloat() ), r_parallaxShadows.GetBool() ? 1 : 0,
		stats.admitted, stats.flat, stats.draws, stats.lastDecline != NULL ? stats.lastDecline : "none" );
}

bool R_PBRNative_Material( const drawSurf_t *surf, bool translucentAdmitted, pbrNativeMaterial_t &out ) {
	memset( &out, 0, sizeof( out ) );
	if ( !r_rendererModernQuality.GetBool() || !r_pbrMaterials.GetBool() || r_skipBump.GetBool()
			|| r_skipDiffuse.GetBool() || r_skipSpecular.GetBool()
			|| surf == NULL || surf->material == NULL
			|| !R_PBRNative_HasSingleClassicInteractionTopology( surf ) ) {
		return false;
	}

	const idMaterial *material = surf->material;
	if ( !material->HasPBR() ) {
		return false;
	}
	switch ( material->Coverage() ) {
		case MC_OPAQUE:
			break;
		case MC_PERFORATED:
			if ( !R_PBRNative_PerforatedCoverageMatches( surf ) ) {
				return false;
			}
			break;
		case MC_TRANSLUCENT:
			// A translucent surface is absent from the depth fill, so its draws
			// composite in the material walk instead of adding in the light pass.
			// An owner that cannot own that composite must not take the BRDF
			// either, or the surface would be lit natively and composited classically.
			if ( !translucentAdmitted || !R_PBRNative_TransparentStage( surf, NULL, NULL ) ) {
				return false;
			}
			break;
		default:
			return false;
	}
	const materialResourceTableRecord_t *resourceRecord =
		R_MaterialResourceTable_FindRecordForMaterial( material );
	if ( resourceRecord == NULL
			|| !R_MaterialResourceTable_PBRModernPathEligible( *resourceRecord ) ) {
		return false;
	}
	const pbrMaterialInfo_t &info = material->GetPBRInfo();
	// Quake 4 AGB normals deliberately retain the bump/RXGB upload path.
	// RGB/RG normals bypass that swizzle through material-data storage.
	const int normalUsage =
		info.normalFormat == PBR_NORMAL_QUAKE4_AGB ? TD_BUMP : TD_MATERIAL_DATA;
	if ( !info.enabled || info.workflow != PBR_WORKFLOW_METALLIC_ROUGHNESS
			|| !info.albedo.present
			|| ( info.emissive.present
				&& ( !R_MaterialResourceTable_PBREmissivePathEligible( *resourceRecord )
					|| !R_PBRNative_ImageReady( info.emissive.image, TD_PBR_COLOR ) ) )
			|| !R_PBRNative_ImageReady( info.albedo.image, TD_PBR_COLOR )
			|| ( info.normal.present && ( info.normalFormat < PBR_NORMAL_QUAKE4_AGB
				|| info.normalFormat > PBR_NORMAL_TANGENT_XYZ
				|| !R_PBRNative_ImageReady( info.normal.image, normalUsage ) ) )
			|| ( info.orm.present && !R_PBRNative_ImageReady( info.orm.image, TD_MATERIAL_DATA ) )
			|| ( info.metallic.present && !R_PBRNative_ImageReady( info.metallic.image, TD_MATERIAL_DATA ) )
			|| ( info.roughness.present && !R_PBRNative_ImageReady( info.roughness.image, TD_MATERIAL_DATA ) )
			|| ( info.ao.present && !R_PBRNative_ImageReady( info.ao.image, TD_MATERIAL_DATA ) ) ) {
		return false;
	}

	// Unused slots still need valid images. The flags keep their placeholder
	// images out of the material evaluation.
	out.normalImage = info.normal.present ? info.normal.image : globalImages->flatNormalMap;
	out.albedoImage = info.albedo.image;
	out.dataImage = info.orm.present ? info.orm.image : ( info.roughness.present ? info.roughness.image : globalImages->whiteImage );
	out.metallicImage = info.metallic.present ? info.metallic.image : NULL;
	out.aoImage = info.ao.present ? info.ao.image : NULL;
	out.emissiveImage = info.emissive.present ? info.emissive.image : NULL;
	out.dataFlags = ( info.orm.present ? 1 : 0 ) | ( info.metallic.present ? 2 : 0 ) | ( info.roughness.present ? 4 : 0 );
	out.normalFormat = info.normal.present ? (int)info.normalFormat + 1 : 0;
	out.metallic = idMath::ClampFloat( 0.0f, 1.0f,
		R_PBRNative_RegisterValue( surf, info.metallicRegister, 0.0f ) );
	out.roughness = idMath::ClampFloat( 0.0f, 1.0f,
		R_PBRNative_RegisterValue( surf, info.roughnessRegister, 0.5f ) );
	out.normalScale = idMath::ClampFloat( 0.0f, 4.0f,
		R_PBRNative_RegisterValue( surf, info.normalScaleRegister, 1.0f ) );
	out.ao = idMath::ClampFloat( 0.0f, 1.0f,
		R_PBRNative_RegisterValue( surf, info.aoRegister, 1.0f ) );
	for ( int component = 0; component < 3; ++component ) {
		out.emissiveColor[component] = Max( 0.0f,
			R_PBRNative_RegisterValue( surf, info.emissiveColorRegisters[component], 0.0f ) );
	}
	R_PBRNative_ResolveParallax( surf, out.dataFlags, out.dataImage, out.parallax, out.parallaxScale, out.parallaxBias );
	return true;
}

bool R_PBRNative_IsReplacedEmissionStage( const drawSurf_t *surf, int stageIndex ) {
	if ( surf == NULL || surf->material == NULL || !surf->material->GetPBRInfo().emissive.present ) {
		return false;
	}
	const materialResourceTableRecord_t *record =
		R_MaterialResourceTable_FindRecordForMaterial( surf->material );
	if ( record == NULL ) {
		return false;
	}
	const materialResourceTextureBinding_t *fallback =
		R_MaterialResourceTable_TextureBindingForSemantic( *record, MATERIAL_RESOURCE_TEXTURE_EMISSIVE );
	return fallback != NULL && fallback->stageIndex == stageIndex;
}
