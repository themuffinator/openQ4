// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "MaterialResourceTable.h"
#include "GLDebugScope.h"

// open-addressed material-pointer hash so per-draw record lookups stay O(1);
// entries store record index + 1 with 0 meaning empty so a memset clears it
const int MATERIAL_RESOURCE_TABLE_HASH_SIZE = MATERIAL_RESOURCE_TABLE_MAX_RECORDS * 2;

typedef struct materialResourceTableState_s {
	materialResourceTableRecord_t	records[MATERIAL_RESOURCE_TABLE_MAX_RECORDS];
	rendererMaterialPass_t			guiPasses[MATERIAL_RESOURCE_TABLE_MAX_GUI_PASSES];
	int								guiPassCount;
	rendererMaterialPass_t			worldPasses[MATERIAL_RESOURCE_TABLE_MAX_WORLD_PASSES];
	int								worldPassCount;
	unsigned int					tableGeneration;
	materialResourceTableStats_t		stats;
	renderBackendCaps_t				caps;
	renderFeatureSet_t				features;
	int								maxClassicTextureUnits;
	unsigned int					textureArrayTable[MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY];
	int								textureArrayTableCount;
	short							materialHash[MATERIAL_RESOURCE_TABLE_HASH_SIZE];
} materialResourceTableState_t;

static materialResourceTableState_t rg_materialResourceTable;

static std::uint64_t R_MaterialResourceTable_MakeTextureResourceId(
		int recordIndex, int bindingIndex ) {
	if ( rg_materialResourceTable.tableGeneration == 0
			|| recordIndex < 0 || recordIndex >= MATERIAL_RESOURCE_TABLE_MAX_RECORDS
			|| bindingIndex < 0 || bindingIndex >= MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS ) {
		return 0;
	}
	return ( static_cast<std::uint64_t>( rg_materialResourceTable.tableGeneration ) << 32 )
		| ( static_cast<std::uint64_t>( recordIndex + 1 ) << 16 )
		| static_cast<std::uint64_t>( bindingIndex + 1 );
}

static int R_MaterialResourceTable_HashMaterial( const idMaterial *material ) {
	size_t key = reinterpret_cast<size_t>( material ) >> 4;
	key *= 2654435761u;
	return static_cast<int>( key & ( MATERIAL_RESOURCE_TABLE_HASH_SIZE - 1 ) );
}

static void R_MaterialResourceTable_HashInsert( const idMaterial *material, int recordIndex ) {
	if ( material == NULL ) {
		return;
	}
	int slot = R_MaterialResourceTable_HashMaterial( material );
	for ( int probe = 0; probe < MATERIAL_RESOURCE_TABLE_HASH_SIZE; ++probe ) {
		short &entry = rg_materialResourceTable.materialHash[slot];
		if ( entry == 0 ) {
			entry = static_cast<short>( recordIndex + 1 );
			return;
		}
		if ( rg_materialResourceTable.records[entry - 1].material == material ) {
			// the first record added for a material keeps lookup priority
			return;
		}
		slot = ( slot + 1 ) & ( MATERIAL_RESOURCE_TABLE_HASH_SIZE - 1 );
	}
}

const char *MaterialResourceBlendMode_Name( materialResourceBlendMode_t blendMode ) {
	switch ( blendMode ) {
	case MATERIAL_RESOURCE_BLEND_OPAQUE:
		return "opaque";
	case MATERIAL_RESOURCE_BLEND_ALPHA_TEST:
		return "alphaTest";
	case MATERIAL_RESOURCE_BLEND_BLEND:
		return "blend";
	case MATERIAL_RESOURCE_BLEND_ADD:
		return "add";
	case MATERIAL_RESOURCE_BLEND_FILTER:
		return "filter";
	case MATERIAL_RESOURCE_BLEND_GUI:
		return "gui";
	case MATERIAL_RESOURCE_BLEND_POST_PROCESS:
		return "postProcess";
	default:
		return "unknown";
	}
}

const char *MaterialResourceTextureSemantic_Name( materialResourceTextureSemantic_t semantic ) {
	switch ( semantic ) {
	case MATERIAL_RESOURCE_TEXTURE_BUMP:
		return "bump";
	case MATERIAL_RESOURCE_TEXTURE_DIFFUSE:
		return "diffuse";
	case MATERIAL_RESOURCE_TEXTURE_SPECULAR:
		return "specular";
	case MATERIAL_RESOURCE_TEXTURE_EMISSIVE:
		return "emissive";
	case MATERIAL_RESOURCE_TEXTURE_GUI:
		return "gui";
	case MATERIAL_RESOURCE_TEXTURE_POST_PROCESS:
		return "post";
	case MATERIAL_RESOURCE_TEXTURE_ALBEDO:
		return "pbrAlbedo";
	case MATERIAL_RESOURCE_TEXTURE_NORMAL:
		return "pbrNormal";
	case MATERIAL_RESOURCE_TEXTURE_ORM:
		return "pbrORM";
	case MATERIAL_RESOURCE_TEXTURE_METALLIC:
		return "pbrMetallic";
	case MATERIAL_RESOURCE_TEXTURE_ROUGHNESS:
		return "pbrRoughness";
	case MATERIAL_RESOURCE_TEXTURE_AO:
		return "pbrAO";
	case MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR:
		return "pbrEmissive";
	case MATERIAL_RESOURCE_TEXTURE_NONE:
	default:
		return "none";
	}
}

const char *MaterialResourcePBRFallbackReason_Name( materialResourcePBRFallbackReason_t reason ) {
	switch ( reason ) {
	case MATERIAL_RESOURCE_PBR_FALLBACK_NONE:
		return "none";
	case MATERIAL_RESOURCE_PBR_FALLBACK_DISABLED:
		return "disabled";
	case MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_WORKFLOW:
		return "unsupportedWorkflow";
	case MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_MATERIAL_CLASS:
		return "unsupportedMaterialClass";
	case MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_ALBEDO:
		return "missingAlbedo";
	case MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_NORMAL_FORMAT:
		return "missingNormalFormat";
	case MATERIAL_RESOURCE_PBR_FALLBACK_CONFLICTING_LAYOUT:
		return "conflictingLayout";
	case MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_TEXTURE_LAYOUT:
		return "unsupportedTextureLayout";
	case MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_IMAGE:
		return "missingImage";
	case MATERIAL_RESOURCE_PBR_FALLBACK_CLASSIC_FEATURE:
		return "classicFeature";
	case MATERIAL_RESOURCE_PBR_FALLBACK_TOO_MANY_TEXTURES:
		return "tooManyTextures";
	case MATERIAL_RESOURCE_PBR_FALLBACK_SHADER_PATH_UNAVAILABLE:
		return "shaderPathUnavailable";
	default:
		return "unknown";
	}
}

unsigned int MaterialResourceTextureSemantic_Bit( materialResourceTextureSemantic_t semantic ) {
	if ( semantic <= MATERIAL_RESOURCE_TEXTURE_NONE || semantic >= MATERIAL_RESOURCE_TEXTURE_COUNT ) {
		return 0;
	}
	return 1u << static_cast<unsigned int>( semantic );
}

const char *MaterialResourceFallbackReason_Name( materialResourceFallbackReason_t reason ) {
	switch ( reason ) {
	case MATERIAL_RESOURCE_FALLBACK_NONE:
		return "none";
	case MATERIAL_RESOURCE_FALLBACK_MISSING_MATERIAL:
		return "missingMaterial";
	case MATERIAL_RESOURCE_FALLBACK_NO_DRAW_STAGES:
		return "noDrawStages";
	case MATERIAL_RESOURCE_FALLBACK_MISSING_IMAGE:
		return "missingImage";
	case MATERIAL_RESOURCE_FALLBACK_CUSTOM_PROGRAM:
		return "customProgram";
	case MATERIAL_RESOURCE_FALLBACK_CUSTOM_GLSL:
		return "customGLSL";
	case MATERIAL_RESOURCE_FALLBACK_DYNAMIC_IMAGE:
		return "dynamicImage";
	case MATERIAL_RESOURCE_FALLBACK_CURRENT_RENDER_IMAGE:
		return "currentRenderImage";
	case MATERIAL_RESOURCE_FALLBACK_SCREEN_TEXGEN:
		return "screenTexgen";
	case MATERIAL_RESOURCE_FALLBACK_SKY_TEXGEN:
		return "skyTexgen";
	case MATERIAL_RESOURCE_FALLBACK_UNSUPPORTED_TEXGEN:
		return "unsupportedTexgen";
	case MATERIAL_RESOURCE_FALLBACK_NEEDS_CURRENT_RENDER:
		return "needsCurrentRender";
	case MATERIAL_RESOURCE_FALLBACK_STAGE_CONDITION:
		return "stageCondition";
	case MATERIAL_RESOURCE_FALLBACK_STAGE_COLOR:
		return "stageColor";
	case MATERIAL_RESOURCE_FALLBACK_TEXTURE_MATRIX:
		return "textureMatrix";
	case MATERIAL_RESOURCE_FALLBACK_VERTEX_COLOR:
		return "vertexColor";
	case MATERIAL_RESOURCE_FALLBACK_POLYGON_OFFSET:
		return "polygonOffset";
	case MATERIAL_RESOURCE_FALLBACK_TOO_MANY_TEXTURES:
		return "tooManyTextures";
	default:
		return "unknown";
	}
}

static const char *R_MaterialResourceTable_SortGroupName( materialResourceSortGroup_t sortGroup ) {
	switch ( sortGroup ) {
	case MATERIAL_RESOURCE_SORT_SUBVIEW:
		return "subview";
	case MATERIAL_RESOURCE_SORT_GUI:
		return "gui";
	case MATERIAL_RESOURCE_SORT_OPAQUE:
		return "opaque";
	case MATERIAL_RESOURCE_SORT_DECAL:
		return "decal";
	case MATERIAL_RESOURCE_SORT_TRANSLUCENT:
		return "translucent";
	case MATERIAL_RESOURCE_SORT_POST_PROCESS:
		return "postProcess";
	case MATERIAL_RESOURCE_SORT_UNKNOWN:
	default:
		return "unknown";
	}
}

static void R_MaterialResourceTable_SetStatus( const char *status ) {
	idStr::Copynz( rg_materialResourceTable.stats.lastFailure, status ? status : "unknown", sizeof( rg_materialResourceTable.stats.lastFailure ) );
}

static void R_MaterialResourceTable_RecordDebugStringTruncation( const char *source ) {
	rg_materialResourceTable.stats.debugStringTruncations++;
	if ( rg_materialResourceTable.stats.debugStringTruncationSource[0] == '\0' ) {
		idStr::Copynz( rg_materialResourceTable.stats.debugStringTruncationSource, source ? source : "unknown", sizeof( rg_materialResourceTable.stats.debugStringTruncationSource ) );
	}
}

static bool R_MaterialResourceTable_CopyDebugString( char *dest, int destSize, const char *source ) {
	const char *text = source != NULL ? source : "";
	const int sourceLength = static_cast<int>( strlen( text ) );
	idStr::Copynz( dest, text, destSize );
	return sourceLength < destSize;
}

static bool R_MaterialResourceTable_FormatDebugString( char *dest, int destSize, const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	const int result = idStr::vsnPrintf( dest, destSize, fmt, argptr );
	va_end( argptr );
	return result >= 0;
}

static void R_MaterialResourceTable_AddFallback( materialResourceTableRecord_t &record, materialResourceFallbackReason_t reason, unsigned int flag ) {
	record.fallbackFlags |= flag;
	if ( record.fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE ) {
		record.fallbackReason = reason;
	}
}

const char *MaterialResourceGuiPassFailure_Name( materialResourceGuiPassFailure_t reason ) {
	switch ( reason ) {
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE:
		return "none";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_REFERENCED:
		return "notReferenced";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_COMPILED:
		return "notCompiled";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_MISSING_MATERIAL:
		return "missingMaterial";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_DRAWN:
		return "notDrawn";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_NO_AMBIENT_STAGES:
		return "noAmbientStages";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_NEEDS_CURRENT_RENDER:
		return "needsCurrentRender";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_DECAL_SORT:
		return "decalSort";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_POST_PROCESS_SORT:
		return "postProcessSort";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_DYNAMIC_IMAGE:
		return "dynamicImage";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_CINEMATIC_IMAGE:
		return "cinematicImage";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_CURRENT_RENDER_IMAGE:
		return "currentRenderImage";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_CUSTOM_PROGRAM:
		return "customProgram";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_TEXGEN:
		return "unsupportedTexgen";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_ALPHA_TEST:
		return "unsupportedAlphaTest";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_MISSING_IMAGE:
		return "missingImage";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_DEFAULTED_IMAGE:
		return "defaultedImage";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNLOADED_IMAGE:
		return "unloadedImage";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_TEXTURE_BINDING_OVERFLOW:
		return "textureBindingOverflow";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_PASS_POOL_OVERFLOW:
		return "passPoolOverflow";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_INVALID_REGISTER:
		return "invalidRegister";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_STATE:
		return "unsupportedState";
	case MATERIAL_RESOURCE_GUI_PASS_FAILURE_INVALID_PASS:
		return "invalidPass";
	default:
		return "unknown";
	}
}

const char *MaterialResourceWorldPassFailure_Name(
		materialResourceWorldPassFailure_t reason ) {
	return MaterialResourceGuiPassFailure_Name( reason );
}

static void R_MaterialResourceTable_AddPBRFallback( materialResourceTableRecord_t &record, materialResourcePBRFallbackReason_t reason ) {
	if ( record.pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_NONE ) {
		record.pbrFallbackReason = reason;
	}
}

static bool R_MaterialResourceTable_ImageIsPostProcess( const idImage *image ) {
	if ( image == NULL ) {
		return false;
	}
	const char *name = image->GetName();
	if ( name == NULL ) {
		return false;
	}
	return !idStr::Icmp( name, "_currentRender" )
		|| !idStr::Icmp( name, "BlurTexture1" )
		|| !idStr::Icmp( name, "_currentDepth" )
		|| !idStr::Icmp( name, "DepthTexture" );
}

static bool R_MaterialResourceTable_ImageIsSceneCapture( const idImage *image ) {
	return R_IsMutableRenderImage( image );
}

static bool R_MaterialResourceTable_TexgenIsScreenSpace( texgen_t texgen ) {
	return texgen == TG_SCREEN
		|| texgen == TG_SCREEN2
		|| texgen == TG_GLASSWARP
		|| texgen == TG_POT_CORRECTION;
}

static bool R_MaterialResourceTable_TexgenIsCubeOrSky( texgen_t texgen ) {
	return texgen == TG_DIFFUSE_CUBE
		|| texgen == TG_REFLECT_CUBE
		|| texgen == TG_SKYBOX_CUBE
		|| texgen == TG_WOBBLESKY_CUBE;
}

static materialResourceTextureSemantic_t R_MaterialResourceTable_StageSemantic( const shaderStage_t &stage, rendererMaterialClass_t materialClass, bool needsCurrentRender ) {
	if ( R_MaterialResourceTable_ImageIsPostProcess( stage.texture.image ) || needsCurrentRender ) {
		return MATERIAL_RESOURCE_TEXTURE_POST_PROCESS;
	}
	switch ( stage.lighting ) {
	case SL_BUMP:
		return MATERIAL_RESOURCE_TEXTURE_BUMP;
	case SL_DIFFUSE:
		return MATERIAL_RESOURCE_TEXTURE_DIFFUSE;
	case SL_SPECULAR:
		return MATERIAL_RESOURCE_TEXTURE_SPECULAR;
	case SL_AMBIENT:
	default:
		if ( materialClass == RENDER_MATERIAL_GUI ) {
			return MATERIAL_RESOURCE_TEXTURE_GUI;
		}
		if ( materialClass == RENDER_MATERIAL_POST_PROCESS ) {
			return MATERIAL_RESOURCE_TEXTURE_POST_PROCESS;
		}
		return MATERIAL_RESOURCE_TEXTURE_EMISSIVE;
	}
}

static materialResourceSortGroup_t R_MaterialResourceTable_SortGroupForMaterial( const idMaterial *material, rendererMaterialClass_t materialClass ) {
	if ( materialClass == RENDER_MATERIAL_SUBVIEW ) {
		return MATERIAL_RESOURCE_SORT_SUBVIEW;
	}
	if ( materialClass == RENDER_MATERIAL_GUI ) {
		return MATERIAL_RESOURCE_SORT_GUI;
	}
	if ( materialClass == RENDER_MATERIAL_POST_PROCESS ) {
		return MATERIAL_RESOURCE_SORT_POST_PROCESS;
	}
	if ( material == NULL ) {
		return MATERIAL_RESOURCE_SORT_UNKNOWN;
	}
	const float sort = material->GetSort();
	if ( sort == SS_SUBVIEW ) {
		return MATERIAL_RESOURCE_SORT_SUBVIEW;
	}
	if ( sort == SS_GUI || sort == SS_PREGUI ) {
		return MATERIAL_RESOURCE_SORT_GUI;
	}
	if ( sort >= SS_POST_PROCESS ) {
		return MATERIAL_RESOURCE_SORT_POST_PROCESS;
	}
	if ( sort >= SS_DECAL && sort < SS_FAR ) {
		return MATERIAL_RESOURCE_SORT_DECAL;
	}
	if ( sort >= SS_FAR ) {
		return MATERIAL_RESOURCE_SORT_TRANSLUCENT;
	}
	if ( sort >= SS_OPAQUE ) {
		return MATERIAL_RESOURCE_SORT_OPAQUE;
	}
	return MATERIAL_RESOURCE_SORT_UNKNOWN;
}

static int R_MaterialResourceTable_BlendBits( int drawStateBits );
static bool R_MaterialResourceTable_IsAdditiveBlend( int drawStateBits );
static bool R_MaterialResourceTable_IsFilterBlend( int drawStateBits );
static bool R_MaterialResourceTable_IsAlphaBlend( int drawStateBits );

static materialResourceBlendMode_t R_MaterialResourceTable_BlendModeForMaterial( const idMaterial *material, rendererMaterialClass_t materialClass ) {
	if ( materialClass == RENDER_MATERIAL_GUI ) {
		return MATERIAL_RESOURCE_BLEND_GUI;
	}
	if ( materialClass == RENDER_MATERIAL_POST_PROCESS ) {
		return MATERIAL_RESOURCE_BLEND_POST_PROCESS;
	}
	if ( materialClass == RENDER_MATERIAL_PERFORATED ) {
		return MATERIAL_RESOURCE_BLEND_ALPHA_TEST;
	}
	const int stageCount = material != NULL ? material->GetNumStages() : 0;
	if ( stageCount <= 0 ) {
		return materialClass == RENDER_MATERIAL_TRANSLUCENT ? MATERIAL_RESOURCE_BLEND_BLEND : MATERIAL_RESOURCE_BLEND_OPAQUE;
	}

	bool sawAlphaTest = false;
	for ( int i = 0; i < stageCount; ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage != NULL && stage->hasAlphaTest ) {
			sawAlphaTest = true;
			break;
		}
	}
	if ( sawAlphaTest ) {
		return MATERIAL_RESOURCE_BLEND_ALPHA_TEST;
	}

	const shaderStage_t *firstStage = material->GetStage( 0 );
	const int blendBits = firstStage != NULL ? ( firstStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) : 0;
	if ( materialClass == RENDER_MATERIAL_TRANSLUCENT ) {
		if ( blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) ) {
			return MATERIAL_RESOURCE_BLEND_ADD;
		}
		if ( firstStage != NULL && R_MaterialResourceTable_IsFilterBlend( firstStage->drawStateBits ) ) {
			return MATERIAL_RESOURCE_BLEND_FILTER;
		}
		return MATERIAL_RESOURCE_BLEND_BLEND;
	}
	if ( blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) ) {
		return MATERIAL_RESOURCE_BLEND_ADD;
	}
	if ( firstStage != NULL && R_MaterialResourceTable_IsFilterBlend( firstStage->drawStateBits ) ) {
		return MATERIAL_RESOURCE_BLEND_FILTER;
	}
	if ( blendBits == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) ) {
		return MATERIAL_RESOURCE_BLEND_BLEND;
	}
	return MATERIAL_RESOURCE_BLEND_OPAQUE;
}

static bool R_MaterialResourceTable_RecordNeedsSurfaceImage( const materialResourceTableRecord_t &record ) {
	return record.materialClass == RENDER_MATERIAL_OPAQUE
		|| record.materialClass == RENDER_MATERIAL_PERFORATED
		|| record.materialClass == RENDER_MATERIAL_TRANSLUCENT
		|| record.materialClass == RENDER_MATERIAL_GUI
		|| record.materialClass == RENDER_MATERIAL_POST_PROCESS;
}

static int R_MaterialResourceTable_SemanticSlot( materialResourceTextureSemantic_t semantic ) {
	switch ( semantic ) {
	case MATERIAL_RESOURCE_TEXTURE_BUMP:
		return 0;
	case MATERIAL_RESOURCE_TEXTURE_DIFFUSE:
		return 1;
	case MATERIAL_RESOURCE_TEXTURE_SPECULAR:
		return 2;
	case MATERIAL_RESOURCE_TEXTURE_EMISSIVE:
		return 3;
	case MATERIAL_RESOURCE_TEXTURE_GUI:
		return 4;
	case MATERIAL_RESOURCE_TEXTURE_POST_PROCESS:
		return 5;
	case MATERIAL_RESOURCE_TEXTURE_ALBEDO:
	case MATERIAL_RESOURCE_TEXTURE_NORMAL:
	case MATERIAL_RESOURCE_TEXTURE_ORM:
	case MATERIAL_RESOURCE_TEXTURE_METALLIC:
	case MATERIAL_RESOURCE_TEXTURE_ROUGHNESS:
	case MATERIAL_RESOURCE_TEXTURE_AO:
	case MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR:
		// Phase 3 records PBR handles but does not allocate visible shader units.
		// The existing classic and shadow bindings therefore remain untouched.
		return -1;
	default:
		return -1;
	}
}

static const materialResourceTextureBinding_t *R_MaterialResourceTable_FindStageBinding( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic ) {
	const int bindingIndex = ( semantic > MATERIAL_RESOURCE_TEXTURE_NONE && semantic < MATERIAL_RESOURCE_TEXTURE_COUNT ) ? record.semanticBindingIndex[semantic] : -1;
	if ( bindingIndex >= 0 && bindingIndex < record.textureBindingCount && record.textures[bindingIndex].stageIndex >= 0 ) {
		return &record.textures[bindingIndex];
	}
	return NULL;
}

static bool R_MaterialResourceTable_BindingsSampleSameImage(
		const materialResourceTextureBinding_t &a,
		const materialResourceTextureBinding_t &b ) {
	if ( a.image == NULL || b.image == NULL ) {
		return false;
	}
	const char *aName = a.image->GetName();
	const char *bName = b.image->GetName();
	return ( a.image == b.image
			|| ( aName != NULL && bName != NULL && aName[0] != '\0' && !idStr::Icmp( aName, bName ) ) )
		&& a.filter == b.filter
		&& a.repeat == b.repeat;
}

static bool R_MaterialResourceTable_BindingHasIdentityColor(
		const materialResourceTableRecord_t &record,
		const materialResourceTextureBinding_t &binding ) {
	if ( record.material == NULL ) {
		return false;
	}
	const float *constantRegisters = record.material->ConstantRegisters();
	if ( constantRegisters == NULL ) {
		return false;
	}
	for ( int component = 0; component < 4; ++component ) {
		const int registerIndex = binding.colorRegisters[component];
		if ( registerIndex < 0 || registerIndex >= record.material->GetNumRegisters()
				|| constantRegisters[registerIndex] != 1.0f ) {
			return false;
		}
	}
	return true;
}

static const materialResourceTextureBinding_t *R_MaterialResourceTable_FindOnlySourceAlphaStage(
		const materialResourceTableRecord_t &record ) {
	const materialResourceTextureBinding_t *result = NULL;
	for ( int bindingIndex = 0; bindingIndex < record.textureBindingCount; ++bindingIndex ) {
		const materialResourceTextureBinding_t &binding = record.textures[bindingIndex];
		if ( binding.stageIndex < 0 || !R_MaterialResourceTable_IsAlphaBlend( binding.drawStateBits ) ) {
			continue;
		}
		if ( result != NULL ) {
			return NULL;
		}
		result = &binding;
	}
	return result;
}

static bool R_MaterialResourceTable_HasPBRSourceAlphaBlendContract( const materialResourceTableRecord_t &record ) {
	// The clustered owner samples PBR albedo once and applies source alpha from
	// that texel. Admit an authored `blend blend` stage only when it is exactly
	// the same image/sampler, a neutral-color ambient pass, and unmodified
	// explicit UVs. Anything less would silently drop stage semantics.
	if ( !record.hasPBR
			|| record.blendMode != MATERIAL_RESOURCE_BLEND_BLEND
			|| record.blendStageCount != 1
			|| record.additiveStageCount != 0
			|| record.filterStageCount != 0
			|| record.alphaTest ) {
		return false;
	}
	const materialResourceTextureBinding_t *albedo =
		R_MaterialResourceTable_TextureBindingForSemantic( record, MATERIAL_RESOURCE_TEXTURE_ALBEDO );
	const materialResourceTextureBinding_t *alphaStage =
		R_MaterialResourceTable_FindOnlySourceAlphaStage( record );
	return albedo != NULL
		&& alphaStage != NULL
		&& alphaStage->semantic == MATERIAL_RESOURCE_TEXTURE_EMISSIVE
		&& alphaStage->blendEnabled
		&& !alphaStage->depthWrite
		&& !alphaStage->colorMasked
		&& !alphaStage->hasConditionRegister
		&& !alphaStage->hasAlphaTest
		&& !alphaStage->hasTextureMatrix
		&& alphaStage->privatePolygonOffset == 0.0f
		&& alphaStage->texgen == static_cast<int>( TG_EXPLICIT )
		&& alphaStage->vertexColorMode == static_cast<int>( SVC_IGNORE )
		&& R_MaterialResourceTable_BindingsSampleSameImage( *albedo, *alphaStage )
		&& R_MaterialResourceTable_BindingHasIdentityColor( record, *alphaStage );
}

static bool R_MaterialResourceTable_ColorRegistersMatch( const materialResourceTextureBinding_t &a, const materialResourceTextureBinding_t &b ) {
	for ( int i = 0; i < 4; ++i ) {
		if ( a.colorRegisters[i] != b.colorRegisters[i] ) {
			return false;
		}
	}
	return true;
}

static void R_MaterialResourceTable_AddStageColorFallback( materialResourceTableRecord_t &record ) {
	const bool hadFallback = ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_STAGE_COLOR ) != 0;
	R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_STAGE_COLOR, MATERIAL_RESOURCE_FALLBACK_FLAG_STAGE_COLOR );
	if ( !hadFallback ) {
		rg_materialResourceTable.stats.unsupportedFeatures++;
	}
}

static void R_MaterialResourceTable_ValidateStageColorContract( materialResourceTableRecord_t &record ) {
	// The source-alpha PBR contract deliberately keeps its own alpha stage
	// while the direct path samples the matching PBR albedo. Its distinct stage
	// color registers are therefore expected, not an unsupported overlay.
	if ( R_MaterialResourceTable_HasPBRSourceAlphaBlendContract( record ) ) {
		return;
	}
	const materialResourceTextureBinding_t *diffuse = R_MaterialResourceTable_FindStageBinding( record, MATERIAL_RESOURCE_TEXTURE_DIFFUSE );
	const materialResourceTextureBinding_t *emissive = R_MaterialResourceTable_FindStageBinding( record, MATERIAL_RESOURCE_TEXTURE_EMISSIVE );
	if ( diffuse == NULL || emissive == NULL || R_MaterialResourceTable_ColorRegistersMatch( *diffuse, *emissive ) ) {
		return;
	}

	// The modern material programs currently carry one material tint. Stock light
	// cards often combine a neutral diffuse base with a parm-colored additive
	// glow, so let the legacy path preserve the separate glow color/off state.
	R_MaterialResourceTable_AddStageColorFallback( record );
}

static bool R_MaterialResourceTable_RecordHasLitTextureStages( const materialResourceTableRecord_t &record ) {
	return record.hasBump || record.hasDiffuse || record.hasSpecular;
}

static bool R_MaterialResourceTable_AmbientOverlayNeedsLegacyStage( const materialResourceTextureBinding_t &emissive ) {
	if ( emissive.semantic != MATERIAL_RESOURCE_TEXTURE_EMISSIVE || emissive.stageIndex < 0 ) {
		return false;
	}
	if ( R_MaterialResourceTable_BlendBits( emissive.drawStateBits ) == 0 ) {
		return false;
	}
	return R_MaterialResourceTable_IsAdditiveBlend( emissive.drawStateBits )
		|| R_MaterialResourceTable_IsFilterBlend( emissive.drawStateBits )
		|| R_MaterialResourceTable_IsAlphaBlend( emissive.drawStateBits )
		|| emissive.colorMasked
		|| !emissive.depthWrite;
}

static void R_MaterialResourceTable_ValidateAmbientOverlayContract( materialResourceTableRecord_t &record ) {
	// `blend blend` source-alpha PBR declarations are expanded into an opaque
	// interaction plus an emissive-labelled alpha stage by the material parser.
	// Their direct PBR owner consumes that alpha stage as ordered transparency;
	// it is not a separate glow overlay.
	if ( R_MaterialResourceTable_HasPBRSourceAlphaBlendContract( record ) ) {
		return;
	}
	if ( record.materialClass != RENDER_MATERIAL_OPAQUE && record.materialClass != RENDER_MATERIAL_PERFORATED ) {
		return;
	}
	if ( !R_MaterialResourceTable_RecordHasLitTextureStages( record ) ) {
		return;
	}
	const materialResourceTextureBinding_t *emissive = R_MaterialResourceTable_FindStageBinding( record, MATERIAL_RESOURCE_TEXTURE_EMISSIVE );
	if ( emissive == NULL || !R_MaterialResourceTable_AmbientOverlayNeedsLegacyStage( *emissive ) ) {
		return;
	}

	// The modern opaque programs only carry a single emissive texture channel.
	// Authored overlay stages such as multiplayer bright-skin glows still need
	// their own blend/depth/color-mask state to avoid tinting the base material.
	R_MaterialResourceTable_AddStageColorFallback( record );
}

static void R_MaterialResourceTable_InitSelfTestRecord( materialResourceTableRecord_t &record ) {
	memset( &record, 0, sizeof( record ) );
	for ( int i = 0; i < MATERIAL_RESOURCE_TEXTURE_COUNT; ++i ) {
		record.semanticBindingIndex[i] = -1;
	}
	record.fallbackReason = MATERIAL_RESOURCE_FALLBACK_NONE;
}

static bool R_MaterialResourceTable_RunAmbientOverlayContractSelfTest( void ) {
	const int unsupportedStart = rg_materialResourceTable.stats.unsupportedFeatures;

	materialResourceTableRecord_t overlay;
	R_MaterialResourceTable_InitSelfTestRecord( overlay );
	overlay.materialClass = RENDER_MATERIAL_OPAQUE;
	overlay.hasDiffuse = true;
	overlay.textureBindingCount = 1;
	overlay.semanticBindingIndex[MATERIAL_RESOURCE_TEXTURE_EMISSIVE] = 0;
	overlay.textures[0].semantic = MATERIAL_RESOURCE_TEXTURE_EMISSIVE;
	overlay.textures[0].stageIndex = 3;
	overlay.textures[0].drawStateBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
	R_MaterialResourceTable_ValidateAmbientOverlayContract( overlay );
	if ( overlay.fallbackReason != MATERIAL_RESOURCE_FALLBACK_STAGE_COLOR
		|| ( overlay.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_STAGE_COLOR ) == 0
		|| rg_materialResourceTable.stats.unsupportedFeatures != unsupportedStart + 1 ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: additive overlay fallback mismatch\n" );
		return false;
	}

	materialResourceTableRecord_t ambientOnly;
	R_MaterialResourceTable_InitSelfTestRecord( ambientOnly );
	ambientOnly.materialClass = RENDER_MATERIAL_OPAQUE;
	ambientOnly.textureBindingCount = 1;
	ambientOnly.semanticBindingIndex[MATERIAL_RESOURCE_TEXTURE_EMISSIVE] = 0;
	ambientOnly.textures[0].semantic = MATERIAL_RESOURCE_TEXTURE_EMISSIVE;
	ambientOnly.textures[0].stageIndex = 1;
	ambientOnly.textures[0].drawStateBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
	R_MaterialResourceTable_ValidateAmbientOverlayContract( ambientOnly );
	if ( ambientOnly.fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: ambient-only overlay should not fallback\n" );
		return false;
	}

	materialResourceTableRecord_t unblended;
	R_MaterialResourceTable_InitSelfTestRecord( unblended );
	unblended.materialClass = RENDER_MATERIAL_OPAQUE;
	unblended.hasDiffuse = true;
	unblended.textureBindingCount = 1;
	unblended.semanticBindingIndex[MATERIAL_RESOURCE_TEXTURE_EMISSIVE] = 0;
	unblended.textures[0].semantic = MATERIAL_RESOURCE_TEXTURE_EMISSIVE;
	unblended.textures[0].stageIndex = 2;
	R_MaterialResourceTable_ValidateAmbientOverlayContract( unblended );
	if ( unblended.fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE
		|| rg_materialResourceTable.stats.unsupportedFeatures != unsupportedStart + 1 ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: unblended emissive fallback mismatch\n" );
		return false;
	}

	return true;
}

static int R_MaterialResourceTable_BlendBits( int drawStateBits ) {
	return drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
}

static bool R_MaterialResourceTable_IsAdditiveBlend( int drawStateBits ) {
	return R_MaterialResourceTable_BlendBits( drawStateBits ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
}

static bool R_MaterialResourceTable_IsFilterBlend( int drawStateBits ) {
	const int blendBits = R_MaterialResourceTable_BlendBits( drawStateBits );
	return blendBits == ( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO )
		|| blendBits == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR )
		|| blendBits == ( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_SRC_COLOR )
		|| blendBits == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR );
}

static bool R_MaterialResourceTable_IsAlphaBlend( int drawStateBits ) {
	return R_MaterialResourceTable_BlendBits( drawStateBits ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
}

static void R_MaterialResourceTable_UpdateRecordSemanticFlags( materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic, const idImage *image ) {
	const bool present = image != NULL;
	switch ( semantic ) {
	case MATERIAL_RESOURCE_TEXTURE_BUMP:
		record.hasBump |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_DIFFUSE:
		record.hasDiffuse |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_SPECULAR:
		record.hasSpecular |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_EMISSIVE:
		record.hasEmissive |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_GUI:
		record.hasGui |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_POST_PROCESS:
		record.hasPostProcess |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_ALBEDO:
		record.hasPBRAlbedo |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_NORMAL:
		record.hasPBRNormal |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_ORM:
		record.hasPBRORM |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_METALLIC:
		record.hasPBRMetallic |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_ROUGHNESS:
		record.hasPBRRoughness |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_AO:
		record.hasPBRAO |= present;
		break;
	case MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR:
		record.hasPBREmissive |= present;
		break;
	default:
		break;
	}
}

static unsigned int R_MaterialResourceTable_ImageHandleOrZero( const idImage *image ) {
	if ( image != NULL && image->IsLoaded() ) {
		return const_cast<idImage *>( image )->GetDeviceHandle();
	}
	return 0;
}

static int R_MaterialResourceTable_TextureArrayCapacity( void ) {
	if ( !rg_materialResourceTable.stats.textureArraysSupported ) {
		return 0;
	}
	if ( rg_materialResourceTable.caps.maxTextureImageUnits > 0 ) {
		return Min( rg_materialResourceTable.caps.maxTextureImageUnits, MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY );
	}
	return MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY;
}

static int R_MaterialResourceTable_FindTextureArrayTableIndexInternal( unsigned int textureHandle ) {
	if ( textureHandle == 0 ) {
		return -1;
	}
	for ( int i = 0; i < rg_materialResourceTable.textureArrayTableCount; ++i ) {
		if ( rg_materialResourceTable.textureArrayTable[i] == textureHandle ) {
			return i;
		}
	}
	return -1;
}

static int R_MaterialResourceTable_AddTextureArrayTableHandle( unsigned int textureHandle ) {
	if ( textureHandle == 0 ) {
		return -1;
	}
	const int existing = R_MaterialResourceTable_FindTextureArrayTableIndexInternal( textureHandle );
	if ( existing >= 0 ) {
		return existing;
	}
	const int capacity = R_MaterialResourceTable_TextureArrayCapacity();
	if ( rg_materialResourceTable.textureArrayTableCount >= capacity ) {
		rg_materialResourceTable.stats.textureArrayTableOverflows++;
		return -1;
	}
	const int index = rg_materialResourceTable.textureArrayTableCount++;
	rg_materialResourceTable.textureArrayTable[index] = textureHandle;
	return index;
}

static void R_MaterialResourceTable_AddTextureArrayFallbackImage( const idImage *image ) {
	R_MaterialResourceTable_AddTextureArrayTableHandle( R_MaterialResourceTable_ImageHandleOrZero( image ) );
}

static void R_MaterialResourceTable_SeedTextureArrayFallbacks( void ) {
	if ( globalImages == NULL ) {
		return;
	}
	R_MaterialResourceTable_AddTextureArrayFallbackImage( globalImages->defaultImage );
	R_MaterialResourceTable_AddTextureArrayFallbackImage( globalImages->whiteImage );
	R_MaterialResourceTable_AddTextureArrayFallbackImage( globalImages->blackImage );
	R_MaterialResourceTable_AddTextureArrayFallbackImage( globalImages->flatNormalMap );
}

static int R_MaterialResourceTable_FindTextureBindingIndex( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic ) {
	if ( semantic <= MATERIAL_RESOURCE_TEXTURE_NONE || semantic >= MATERIAL_RESOURCE_TEXTURE_COUNT ) {
		return -1;
	}
	const int bindingIndex = record.semanticBindingIndex[semantic];
	return bindingIndex >= 0 && bindingIndex < record.textureBindingCount ? bindingIndex : -1;
}

static bool R_MaterialResourceTable_HasSemanticBinding( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic ) {
	return R_MaterialResourceTable_FindTextureBindingIndex( record, semantic ) >= 0;
}

static bool R_MaterialResourceTable_IsPBRSemantic( materialResourceTextureSemantic_t semantic ) {
	return semantic >= MATERIAL_RESOURCE_TEXTURE_ALBEDO && semantic < MATERIAL_RESOURCE_TEXTURE_COUNT;
}

static int R_MaterialResourceTable_AddTextureBinding(
	materialResourceTableRecord_t &record,
	materialResourceTextureSemantic_t semantic,
	const idImage *image,
	const shaderStage_t *stage,
	int stageIndex ) {
	if ( semantic <= MATERIAL_RESOURCE_TEXTURE_NONE || semantic >= MATERIAL_RESOURCE_TEXTURE_COUNT || image == NULL ) {
		if ( R_MaterialResourceTable_IsPBRSemantic( semantic ) ) {
			R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_IMAGE );
		} else {
			record.hasMissingImage = true;
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_MISSING_IMAGE, MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_IMAGE );
		}
		rg_materialResourceTable.stats.missingImages++;
		return -1;
	}
	// Source-record convenience images remain one-per-semantic. Authored stage
	// bindings do not: two GUI stages sampling the same semantic are distinct
	// ordered operations and must retain their own image/state identity.
	if ( stage == NULL && R_MaterialResourceTable_HasSemanticBinding( record, semantic ) ) {
		R_MaterialResourceTable_UpdateRecordSemanticFlags( record, semantic, image );
		return R_MaterialResourceTable_FindTextureBindingIndex( record, semantic );
	}
	if ( record.textureBindingCount >= MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS ) {
		if ( R_MaterialResourceTable_IsPBRSemantic( semantic ) ) {
			R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_TOO_MANY_TEXTURES );
		} else {
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_TOO_MANY_TEXTURES, MATERIAL_RESOURCE_FALLBACK_FLAG_TOO_MANY_TEXTURES );
		}
		rg_materialResourceTable.stats.unsupportedFeatures++;
		return -1;
	}

	const int bindingIndex = record.textureBindingCount++;
	materialResourceTextureBinding_t &binding = record.textures[bindingIndex];
	memset( &binding, 0, sizeof( binding ) );
	binding.semantic = semantic;
	binding.image = image;
	binding.textureResourceId = R_MaterialResourceTable_MakeTextureResourceId(
		record.tableIndex, bindingIndex );
	binding.textureHandle = image->IsLoaded() ? const_cast<idImage *>( image )->GetDeviceHandle() : 0;
	binding.filter = image->GetFilter();
	binding.repeat = image->GetRepeat();
	binding.classicUnit = R_MaterialResourceTable_SemanticSlot( semantic );
	if ( binding.classicUnit >= rg_materialResourceTable.maxClassicTextureUnits ) {
		if ( R_MaterialResourceTable_IsPBRSemantic( semantic ) ) {
			R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_TOO_MANY_TEXTURES );
		} else {
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_TOO_MANY_TEXTURES, MATERIAL_RESOURCE_FALLBACK_FLAG_TOO_MANY_TEXTURES );
		}
		binding.classicUnit = -1;
	}
	binding.stageIndex = stageIndex;
	binding.stageRegisterStart = stage != NULL ? stage->mStageRegisterStart : 0;
	binding.stageRegisterCount = stage != NULL ? stage->mNumStageRegisters : 0;
	binding.drawStateBits = stage != NULL ? stage->drawStateBits : 0;
	binding.conditionRegister = stage != NULL ? stage->conditionRegister : 0;
	binding.hasConditionRegister = stage != NULL && stage->mNumStageOps > 0;
	binding.hasAlphaTest = stage != NULL && stage->hasAlphaTest;
	binding.alphaTestMode = stage != NULL ? stage->alphaTestMode : 0;
	binding.alphaTestRegister = stage != NULL ? stage->alphaTestRegister : 0;
	binding.texgen = stage != NULL ? static_cast<int>( stage->texture.texgen ) : static_cast<int>( TG_EXPLICIT );
	binding.vertexColorMode = stage != NULL ? static_cast<int>( stage->vertexColor ) : static_cast<int>( SVC_IGNORE );
	binding.privatePolygonOffset = stage != NULL ? stage->privatePolygonOffset : 0.0f;
	binding.blendEnabled = stage != NULL && R_MaterialResourceTable_BlendBits( stage->drawStateBits ) != 0;
	// GLS_DEPTHMASK is an inverse mask: set means depth writes are disabled.
	binding.depthWrite = stage != NULL && ( stage->drawStateBits & GLS_DEPTHMASK ) == 0;
	binding.colorMasked = stage != NULL && ( stage->drawStateBits & GLS_COLORMASK ) != 0;
	for ( int i = 0; i < 4; ++i ) {
		binding.colorRegisters[i] = -1;
	}
	if ( stage != NULL ) {
		memcpy( binding.colorRegisters, stage->color.registers, sizeof( binding.colorRegisters ) );
		memcpy( binding.matrixRegisters, stage->texture.matrix, sizeof( binding.matrixRegisters ) );
		binding.hasTextureMatrix = stage->texture.hasMatrix;
	}
	binding.loaded = image->IsLoaded();
	binding.defaulted = image->IsDefaulted();
	binding.textureArrayCandidate = rg_materialResourceTable.stats.textureArraysSupported && binding.loaded && binding.textureHandle != 0;
	binding.textureArrayLayer = -1;
	binding.textureViewCandidate = rg_materialResourceTable.stats.textureViewsSupported;
	binding.textureViewHandle = 0;
	binding.bindlessSupported = rg_materialResourceTable.stats.bindlessSupported;
	binding.bindlessEnabled = false;
	binding.bindlessHandle = 0;
	const char *semanticName = MaterialResourceTextureSemantic_Name( semantic );
	if ( r_rendererMetrics.GetInteger() >= 2 ) {
		if ( !R_MaterialResourceTable_FormatDebugString(
			binding.debugName,
			sizeof( binding.debugName ),
			"%s:%s",
			semanticName,
			image->GetName() ? image->GetName() : "<unnamed>" ) ) {
			R_MaterialResourceTable_RecordDebugStringTruncation( "texture binding debugName" );
		}
	} else if ( !R_MaterialResourceTable_CopyDebugString( binding.debugName, sizeof( binding.debugName ), semanticName ) ) {
		R_MaterialResourceTable_RecordDebugStringTruncation( "texture binding semantic" );
	}

	if ( record.semanticBindingIndex[semantic] < 0 ) {
		record.semanticBindingIndex[semantic] = bindingIndex;
	}
	const unsigned int semanticBit = MaterialResourceTextureSemantic_Bit( semantic );
	record.textureSemanticMask |= semanticBit;
	if ( binding.textureHandle != 0 ) {
		record.loadedTextureSemanticMask |= semanticBit;
		if ( semantic == MATERIAL_RESOURCE_TEXTURE_DIFFUSE
			|| semantic == MATERIAL_RESOURCE_TEXTURE_EMISSIVE
			|| semantic == MATERIAL_RESOURCE_TEXTURE_GUI
			|| semantic == MATERIAL_RESOURCE_TEXTURE_POST_PROCESS
			|| semantic == MATERIAL_RESOURCE_TEXTURE_ALBEDO
			|| semantic == MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR ) {
			record.renderableColorTextureMask |= semanticBit;
		}
	}
	R_MaterialResourceTable_UpdateRecordSemanticFlags( record, semantic, image );
	if ( binding.defaulted ) {
		record.hasDefaultedImage = true;
		rg_materialResourceTable.stats.defaultedImages++;
	}
	rg_materialResourceTable.stats.textureBindings++;
	if ( binding.classicUnit >= 0 ) {
		rg_materialResourceTable.stats.classicTextureBindings++;
	}
	if ( binding.textureViewCandidate ) {
		rg_materialResourceTable.stats.textureViewDescriptors++;
	}
	return bindingIndex;
}

static void R_MaterialResourceTable_BuildTextureArrayTable( void ) {
	memset( rg_materialResourceTable.textureArrayTable, 0, sizeof( rg_materialResourceTable.textureArrayTable ) );
	rg_materialResourceTable.textureArrayTableCount = 0;
	rg_materialResourceTable.stats.textureArrayTableCapacity = R_MaterialResourceTable_TextureArrayCapacity();
	if ( !rg_materialResourceTable.stats.textureArraysSupported || rg_materialResourceTable.stats.textureArrayTableCapacity <= 0 ) {
		return;
	}

	R_MaterialResourceTable_SeedTextureArrayFallbacks();
	for ( int recordIndex = 0; recordIndex < rg_materialResourceTable.stats.records; ++recordIndex ) {
		materialResourceTableRecord_t &record = rg_materialResourceTable.records[recordIndex];
		// PBR records stay on the legacy/classic owner until a dedicated PBR
		// shader path exists.  Do not let their bindings consume the shared
		// classic-modern texture table and perturb unrelated classic records.
		if ( record.hasPBR ) {
			for ( int bindingIndex = 0; bindingIndex < record.textureBindingCount; ++bindingIndex ) {
				record.textures[bindingIndex].textureArrayCandidate = false;
				record.textures[bindingIndex].textureArrayLayer = -1;
			}
			continue;
		}
		for ( int bindingIndex = 0; bindingIndex < record.textureBindingCount; ++bindingIndex ) {
			materialResourceTextureBinding_t &binding = record.textures[bindingIndex];
			if ( !binding.loaded || binding.textureHandle == 0 ) {
				binding.textureArrayCandidate = false;
				binding.textureArrayLayer = -1;
				continue;
			}
			const int layer = R_MaterialResourceTable_AddTextureArrayTableHandle( binding.textureHandle );
			binding.textureArrayCandidate = layer >= 0;
			binding.textureArrayLayer = layer;
			if ( layer >= 0 ) {
				rg_materialResourceTable.stats.textureArrayDescriptors++;
				rg_materialResourceTable.stats.textureArrayTableDescriptors++;
			}
		}
	}
	rg_materialResourceTable.stats.textureArrayTableTextures = rg_materialResourceTable.textureArrayTableCount;
	rg_materialResourceTable.stats.textureArrayTableReady = rg_materialResourceTable.textureArrayTableCount > 0;
}

static void R_MaterialResourceTable_CountClass( const materialResourceTableRecord_t &record ) {
	switch ( record.materialClass ) {
	case RENDER_MATERIAL_OPAQUE:
		rg_materialResourceTable.stats.opaqueRecords++;
		break;
	case RENDER_MATERIAL_PERFORATED:
		rg_materialResourceTable.stats.perforatedRecords++;
		break;
	case RENDER_MATERIAL_TRANSLUCENT:
		rg_materialResourceTable.stats.translucentRecords++;
		break;
	case RENDER_MATERIAL_GUI:
		rg_materialResourceTable.stats.guiRecords++;
		break;
	case RENDER_MATERIAL_SUBVIEW:
		rg_materialResourceTable.stats.subviewRecords++;
		break;
	case RENDER_MATERIAL_POST_PROCESS:
		rg_materialResourceTable.stats.postProcessRecords++;
		break;
	case RENDER_MATERIAL_SHADOW_ONLY:
		rg_materialResourceTable.stats.shadowOnlyRecords++;
		break;
	default:
		break;
	}
	if ( record.alphaTest ) {
		rg_materialResourceTable.stats.alphaTestRecords++;
	}
	if ( record.blendMode == MATERIAL_RESOURCE_BLEND_BLEND
		|| record.blendMode == MATERIAL_RESOURCE_BLEND_ADD
		|| record.blendMode == MATERIAL_RESOURCE_BLEND_FILTER
		|| record.blendMode == MATERIAL_RESOURCE_BLEND_GUI
		|| record.blendMode == MATERIAL_RESOURCE_BLEND_POST_PROCESS ) {
		rg_materialResourceTable.stats.blendRecords++;
	}
}

static void R_MaterialResourceTable_CountFallbacks( const materialResourceTableRecord_t &record ) {
	if ( record.fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE ) {
		return;
	}
	rg_materialResourceTable.stats.fallbackRecords++;
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_MATERIAL ) != 0 ) {
		rg_materialResourceTable.stats.fallbackMissingMaterial++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_NO_DRAW_STAGES ) != 0 ) {
		rg_materialResourceTable.stats.fallbackNoDrawStages++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_IMAGE ) != 0 ) {
		rg_materialResourceTable.stats.fallbackMissingImage++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_CUSTOM_PROGRAM ) != 0 ) {
		rg_materialResourceTable.stats.fallbackCustomProgram++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_CUSTOM_GLSL ) != 0 ) {
		rg_materialResourceTable.stats.fallbackCustomGLSL++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_DYNAMIC_IMAGE ) != 0 ) {
		rg_materialResourceTable.stats.fallbackDynamicImage++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_CURRENT_RENDER_IMAGE ) != 0 ) {
		rg_materialResourceTable.stats.fallbackCurrentRenderImage++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_SCREEN_TEXGEN ) != 0 ) {
		rg_materialResourceTable.stats.fallbackScreenTexgen++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_SKY_TEXGEN ) != 0 ) {
		rg_materialResourceTable.stats.fallbackSkyTexgen++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_TEXGEN ) != 0 ) {
		rg_materialResourceTable.stats.fallbackUnsupportedTexgen++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_NEEDS_CURRENT_RENDER ) != 0 ) {
		rg_materialResourceTable.stats.fallbackNeedsCurrentRender++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_STAGE_CONDITION ) != 0 ) {
		rg_materialResourceTable.stats.fallbackStageCondition++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_STAGE_COLOR ) != 0 ) {
		rg_materialResourceTable.stats.fallbackStageColor++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_TEXTURE_MATRIX ) != 0 ) {
		rg_materialResourceTable.stats.fallbackTextureMatrix++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_VERTEX_COLOR ) != 0 ) {
		rg_materialResourceTable.stats.fallbackVertexColor++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_POLYGON_OFFSET ) != 0 ) {
		rg_materialResourceTable.stats.fallbackPolygonOffset++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_TOO_MANY_TEXTURES ) != 0 ) {
		rg_materialResourceTable.stats.fallbackTooManyTextures++;
	}
}

static void R_MaterialResourceTable_FinalizeRegisterRange( materialResourceTableRecord_t &record ) {
	record.stageRegisterStart = record.registerCount > 0 ? record.registerCount : 0;
	int stageRegisterEnd = 0;
	for ( int i = 0; i < record.textureBindingCount; ++i ) {
		const materialResourceTextureBinding_t &binding = record.textures[i];
		if ( binding.stageRegisterCount <= 0 ) {
			continue;
		}
		if ( binding.stageRegisterStart < record.stageRegisterStart ) {
			record.stageRegisterStart = binding.stageRegisterStart;
		}
		if ( binding.stageRegisterStart + binding.stageRegisterCount > stageRegisterEnd ) {
			stageRegisterEnd = binding.stageRegisterStart + binding.stageRegisterCount;
		}
	}
	if ( stageRegisterEnd <= record.stageRegisterStart ) {
		record.stageRegisterStart = 0;
		record.stageRegisterCount = 0;
	} else {
		record.stageRegisterCount = stageRegisterEnd - record.stageRegisterStart;
	}
}

template<bool worldDomain>
static void R_MaterialResourceTable_SetPassFailure(
		materialResourceTableRecord_t &record,
		materialResourceGuiPassFailure_t failure,
		int sourceStageIndex ) {
	if ( worldDomain ) {
		record.firstWorldPass = -1;
		record.worldPassCount = 0;
		record.worldPassEligible = false;
		record.worldPassFailure = failure;
		record.worldPassFailureStage = sourceStageIndex;
	} else {
		record.firstGuiPass = -1;
		record.guiPassCount = 0;
		record.guiPassEligible = false;
		record.guiPassFailure = failure;
		record.guiPassFailureStage = sourceStageIndex;
	}
}

static int R_MaterialResourceTable_FindStageBindingIndex(
		const materialResourceTableRecord_t &record, int sourceStageIndex ) {
	for ( int i = 0; i < record.textureBindingCount; ++i ) {
		if ( record.textures[i].stageIndex == sourceStageIndex ) {
			return i;
		}
	}
	return -1;
}

static bool R_MaterialResourceTable_GuiRegisterValid(
		const materialResourceTableRecord_t &record, int registerIndex ) {
	return registerIndex >= 0 && registerIndex < record.registerCount;
}

static bool R_MaterialResourceTable_MapSourceBlendFactor(
		int drawStateBits, rendererBlendFactor_t &factor ) {
	switch ( drawStateBits & GLS_SRCBLEND_BITS ) {
	case GLS_SRCBLEND_ZERO:
		factor = RENDERER_BLEND_ZERO;
		return true;
	case GLS_SRCBLEND_ONE:
		factor = RENDERER_BLEND_ONE;
		return true;
	case GLS_SRCBLEND_DST_COLOR:
		factor = RENDERER_BLEND_DST_COLOR;
		return true;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
		factor = RENDERER_BLEND_ONE_MINUS_DST_COLOR;
		return true;
	case GLS_SRCBLEND_SRC_ALPHA:
		factor = RENDERER_BLEND_SRC_ALPHA;
		return true;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
		factor = RENDERER_BLEND_ONE_MINUS_SRC_ALPHA;
		return true;
	case GLS_SRCBLEND_DST_ALPHA:
		factor = RENDERER_BLEND_DST_ALPHA;
		return true;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
		factor = RENDERER_BLEND_ONE_MINUS_DST_ALPHA;
		return true;
	case GLS_SRCBLEND_ALPHA_SATURATE:
		factor = RENDERER_BLEND_SRC_ALPHA_SATURATE;
		return true;
	case GLS_SRCBLEND_SRC_COLOR:
		factor = RENDERER_BLEND_SRC_COLOR;
		return true;
	case GLS_SRCBLEND_ONE_MINUS_SRC_COLOR:
		factor = RENDERER_BLEND_ONE_MINUS_SRC_COLOR;
		return true;
	default:
		return false;
	}
}

static bool R_MaterialResourceTable_MapDestinationBlendFactor(
		int drawStateBits, rendererBlendFactor_t &factor ) {
	switch ( drawStateBits & GLS_DSTBLEND_BITS ) {
	case GLS_DSTBLEND_ZERO:
		factor = RENDERER_BLEND_ZERO;
		return true;
	case GLS_DSTBLEND_ONE:
		factor = RENDERER_BLEND_ONE;
		return true;
	case GLS_DSTBLEND_SRC_COLOR:
		factor = RENDERER_BLEND_SRC_COLOR;
		return true;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
		factor = RENDERER_BLEND_ONE_MINUS_SRC_COLOR;
		return true;
	case GLS_DSTBLEND_SRC_ALPHA:
		factor = RENDERER_BLEND_SRC_ALPHA;
		return true;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
		factor = RENDERER_BLEND_ONE_MINUS_SRC_ALPHA;
		return true;
	case GLS_DSTBLEND_DST_ALPHA:
		factor = RENDERER_BLEND_DST_ALPHA;
		return true;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
		factor = RENDERER_BLEND_ONE_MINUS_DST_ALPHA;
		return true;
	default:
		return false;
	}
}

static bool R_MaterialResourceTable_MapGuiDepthState(
		int drawStateBits, rendererDepthState_t &depth ) {
	const int depthFunction = drawStateBits & ( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHFUNC_EQUAL );
	depth.testEnabled = false; // generated 2D views disable depth testing as a domain rule
	depth.writeEnabled = ( drawStateBits & GLS_DEPTHMASK ) == 0;
	switch ( depthFunction ) {
	case GLS_DEPTHFUNC_LESS:
		depth.compareOperation = RENDERER_COMPARE_LESS_OR_EQUAL;
		return true;
	case GLS_DEPTHFUNC_EQUAL:
		depth.compareOperation = RENDERER_COMPARE_EQUAL;
		return true;
	case GLS_DEPTHFUNC_ALWAYS:
		depth.compareOperation = RENDERER_COMPARE_ALWAYS;
		return true;
	default:
		return false;
	}
}

static bool R_MaterialResourceTable_MapWorldDepthState(
		int drawStateBits, rendererDepthState_t &depth ) {
	const int depthFunction = drawStateBits
		& ( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHFUNC_EQUAL );
	depth.testEnabled = true;
	depth.writeEnabled = ( drawStateBits & GLS_DEPTHMASK ) == 0;
	switch ( depthFunction ) {
	case GLS_DEPTHFUNC_LESS:
		depth.compareOperation = RENDERER_COMPARE_LESS_OR_EQUAL;
		return true;
	case GLS_DEPTHFUNC_EQUAL:
		depth.compareOperation = RENDERER_COMPARE_EQUAL;
		return true;
	case GLS_DEPTHFUNC_ALWAYS:
		depth.compareOperation = RENDERER_COMPARE_ALWAYS;
		return true;
	default:
		return false;
	}
}

static bool R_MaterialResourceTable_MapCullMode(
		int cullType, rendererCullMode_t &cull ) {
	switch ( cullType ) {
	case CT_FRONT_SIDED:
		cull = RENDERER_CULL_FRONT;
		return true;
	case CT_BACK_SIDED:
		cull = RENDERER_CULL_BACK;
		return true;
	case CT_TWO_SIDED:
		cull = RENDERER_CULL_NONE;
		return true;
	default:
		return false;
	}
}

static bool R_MaterialResourceTable_MapVertexColor(
		stageVertexColor_t source, rendererVertexColorMode_t &destination ) {
	switch ( source ) {
	case SVC_IGNORE:
		destination = RENDERER_VERTEX_COLOR_IGNORE;
		return true;
	case SVC_MODULATE:
		destination = RENDERER_VERTEX_COLOR_MODULATE;
		return true;
	case SVC_INVERSE_MODULATE:
		destination = RENDERER_VERTEX_COLOR_INVERSE_MODULATE;
		return true;
	default:
		return false;
	}
}

static std::uint32_t R_MaterialResourceTable_ColorWriteMask( int drawStateBits ) {
	std::uint32_t mask = RENDERER_COLOR_WRITE_RGBA;
	if ( ( drawStateBits & GLS_REDMASK ) != 0 ) {
		mask &= ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_RED );
	}
	if ( ( drawStateBits & GLS_GREENMASK ) != 0 ) {
		mask &= ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_GREEN );
	}
	if ( ( drawStateBits & GLS_BLUEMASK ) != 0 ) {
		mask &= ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_BLUE );
	}
	if ( ( drawStateBits & GLS_ALPHAMASK ) != 0 ) {
		mask &= ~static_cast<std::uint32_t>( RENDERER_COLOR_WRITE_ALPHA );
	}
	return mask;
}

template<bool worldDomain>
static bool R_MaterialResourceTable_CompileOrderedPasses(
		materialResourceTableRecord_t &record ) {
	const bool domainReferenced = worldDomain
		? record.worldDomainReferenced : record.guiDomainReferenced;
	R_MaterialResourceTable_SetPassFailure<worldDomain>(
		record, domainReferenced
			? MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_COMPILED
			: MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_REFERENCED, -1 );
	if ( !domainReferenced ) {
		return false;
	}
	const idMaterial *material = record.material;
	if ( material == NULL ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
			record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_MISSING_MATERIAL, -1 );
		return false;
	}
	if ( !material->IsDrawn() ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
			record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_DRAWN, -1 );
		return false;
	}
	if ( material->TestMaterialFlag( MF_NEED_CURRENT_RENDER ) ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
			record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_NEEDS_CURRENT_RENDER, -1 );
		return false;
	}
	// Decal-sorted surfaces are conditionally removed by r_skipDecals before
	// the classic ambient walker.  This first sealed contract deliberately
	// carries no mutable debug-CVar disposition, so retain the whole view on
	// the classic path for every sort value in the decal corridor.
	if ( material->GetSort() >= SS_DECAL && material->GetSort() < SS_FAR ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
			record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_DECAL_SORT, -1 );
		return false;
	}
	// The classic walker splits post-process-sorted materials around scene
	// capture and r_skipPostProcess handling.  This first shared domain owns no
	// part of that sequencing, so retain the complete view on the classic path.
	if ( material->GetSort() >= SS_POST_PROCESS ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
			record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_POST_PROCESS_SORT, -1 );
		return false;
	}

	rendererMaterialPassList_t compiled;
	RendererContracts_ResetMaterialPassList( compiled );
	const int stageCount = material->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; ++stageIndex ) {
		const shaderStage_t *stage = material->GetStage( stageIndex );
		if ( stage == NULL || stage->lighting != SL_AMBIENT ) {
			continue;
		}
		if ( stage->newStage != NULL ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_CUSTOM_PROGRAM, stageIndex );
			return false;
		}
		if ( stage->texture.cinematic != NULL ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_CINEMATIC_IMAGE, stageIndex );
			return false;
		}
		if ( stage->texture.dynamic != DI_STATIC ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_DYNAMIC_IMAGE, stageIndex );
			return false;
		}
		if ( stage->texture.texgen != TG_EXPLICIT ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_TEXGEN, stageIndex );
			return false;
		}
		// Classic fixed-function state carries a private stage offset into later
		// stages when the material-level offset remains enabled.  The sealed
		// per-pass model restores each pass independently, so reject this combined
		// authored state until the carry-forward transition is represented.
		if ( record.hasMaterialPolygonOffset
				&& stage->privatePolygonOffset != 0.0f ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_STATE,
				stageIndex );
			return false;
		}
		// shaderStage_t::hasAlphaTest controls the classic depth/coverage pass;
		// RB_STD_T_RenderShaderPasses does not apply it again to ambient color.
		// The world domain proves the established depth packet prerequisite for
		// every perforated/opaque surface, so its color pass must ignore this flag
		// exactly like the classic walker. Generated 2D has no such prerequisite
		// and remains ineligible.
		if ( stage->hasAlphaTest && !worldDomain ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_ALPHA_TEST,
				stageIndex );
			return false;
		}
		if ( stage->texture.image == NULL ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_MISSING_IMAGE, stageIndex );
			return false;
		}
		if ( R_MaterialResourceTable_ImageIsPostProcess( stage->texture.image )
				|| R_MaterialResourceTable_ImageIsSceneCapture( stage->texture.image ) ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_CURRENT_RENDER_IMAGE, stageIndex );
			return false;
		}
		if ( stage->texture.image->IsDefaulted() ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_DEFAULTED_IMAGE, stageIndex );
			return false;
		}
		if ( !stage->texture.image->IsLoaded() ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNLOADED_IMAGE, stageIndex );
			return false;
		}
		const int bindingIndex = R_MaterialResourceTable_FindStageBindingIndex(
			record, stageIndex );
		if ( bindingIndex < 0 || bindingIndex >= record.textureBindingCount
				|| record.textures[bindingIndex].textureResourceId == 0 ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_TEXTURE_BINDING_OVERFLOW, stageIndex );
			return false;
		}

		rendererMaterialPass_t pass = RendererContracts_DefaultMaterialPass();
		pass.sourceStageIndex = stageIndex;
		pass.kind = worldDomain
			? RENDERER_MATERIAL_PASS_SURFACE : RENDERER_MATERIAL_PASS_GUI;
		pass.textureSemantic = RENDERER_TEXTURE_DIFFUSE;
		pass.textureResourceId = record.textures[bindingIndex].textureResourceId;
		if ( !R_MaterialResourceTable_GuiRegisterValid( record, stage->conditionRegister ) ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_INVALID_REGISTER, stageIndex );
			return false;
		}
		pass.condition = RendererContracts_Register( stage->conditionRegister );
		for ( int i = 0; i < 4; ++i ) {
			if ( !R_MaterialResourceTable_GuiRegisterValid( record, stage->color.registers[i] ) ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
					record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_INVALID_REGISTER, stageIndex );
				return false;
			}
			pass.color[i] = RendererContracts_Register( stage->color.registers[i] );
		}
		if ( stage->texture.hasMatrix ) {
			for ( int row = 0; row < 2; ++row ) {
				for ( int column = 0; column < 3; ++column ) {
					const int matrixRegister = stage->texture.matrix[row][column];
					if ( !R_MaterialResourceTable_GuiRegisterValid( record, matrixRegister ) ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
							record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_INVALID_REGISTER, stageIndex );
						return false;
					}
					pass.textureMatrix[row * 3 + column] =
						RendererContracts_Register( matrixRegister );
				}
			}
		}

		const int supportedStateBits = GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS
			| GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK
			| GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHFUNC_EQUAL | GLS_ATEST_BITS;
		if ( ( stage->drawStateBits & ~supportedStateBits ) != 0
				|| !R_MaterialResourceTable_MapSourceBlendFactor(
					stage->drawStateBits, pass.blend.sourceColor )
				|| !R_MaterialResourceTable_MapDestinationBlendFactor(
					stage->drawStateBits, pass.blend.destinationColor )
				|| !( worldDomain
					? R_MaterialResourceTable_MapWorldDepthState(
						stage->drawStateBits, pass.depth )
					: R_MaterialResourceTable_MapGuiDepthState(
						stage->drawStateBits, pass.depth ) )
				|| !R_MaterialResourceTable_MapCullMode( record.cullType, pass.cull )
				|| !R_MaterialResourceTable_MapVertexColor(
					stage->vertexColor, pass.vertexColor ) ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_STATE, stageIndex );
			return false;
		}
		pass.blend.enabled = R_MaterialResourceTable_BlendBits(
			stage->drawStateBits ) != 0;
		pass.blend.destinationAlpha = pass.blend.destinationColor;
		pass.blend.sourceAlpha = pass.blend.sourceColor;
		pass.blend.colorOperation = RENDERER_BLEND_OP_ADD;
		pass.blend.alphaOperation = RENDERER_BLEND_OP_ADD;
		pass.colorWriteMask = R_MaterialResourceTable_ColorWriteMask(
			stage->drawStateBits );

		pass.alphaTestEnabled = false;
		switch ( stage->drawStateBits & GLS_ATEST_BITS ) {
			case 0:
				break;
			case GLS_ATEST_EQ_255:
				pass.alphaTestEnabled = true;
				pass.alphaTestCompareOperation = RENDERER_COMPARE_EQUAL;
				pass.alphaTest = RendererContracts_Constant( 1.0f );
				break;
			case GLS_ATEST_LT_128:
				pass.alphaTestEnabled = true;
				pass.alphaTestCompareOperation = RENDERER_COMPARE_LESS;
				pass.alphaTest = RendererContracts_Constant( 0.5f );
				break;
			case GLS_ATEST_GE_128:
				pass.alphaTestEnabled = true;
				pass.alphaTestCompareOperation = RENDERER_COMPARE_GREATER_OR_EQUAL;
				pass.alphaTest = RendererContracts_Constant( 0.5f );
				break;
			default:
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
					record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_STATE, stageIndex );
				return false;
		}
		pass.texgen = RENDERER_TEXGEN_EXPLICIT;
		const float offsetScale = stage->privatePolygonOffset != 0.0f
			? stage->privatePolygonOffset
			: ( record.hasMaterialPolygonOffset ? record.polygonOffset : 0.0f );
		pass.polygonOffsetEnabled = offsetScale != 0.0f;
		if ( pass.polygonOffsetEnabled ) {
			pass.polygonOffsetFactor = RendererContracts_Constant(
				r_offsetFactor.GetFloat() );
			pass.polygonOffsetUnits = RendererContracts_Constant(
				r_offsetUnits.GetFloat() * offsetScale );
		}
		pass.programFamily = worldDomain
			? RENDERER_PROGRAM_FIXED : RENDERER_PROGRAM_GUI;
		pass.programKey = 0;
		if ( !RendererContracts_AppendMaterialPass( compiled, pass ) ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
				record, compiled.overflowed
					? MATERIAL_RESOURCE_GUI_PASS_FAILURE_PASS_POOL_OVERFLOW
					: MATERIAL_RESOURCE_GUI_PASS_FAILURE_INVALID_PASS,
				stageIndex );
			return false;
		}
	}

	if ( compiled.count == 0 ) {
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
			record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_NO_AMBIENT_STAGES, -1 );
		return false;
	}
	const int poolCount = worldDomain ? rg_materialResourceTable.worldPassCount
		: rg_materialResourceTable.guiPassCount;
	const int poolCapacity = worldDomain ? MATERIAL_RESOURCE_TABLE_MAX_WORLD_PASSES
		: MATERIAL_RESOURCE_TABLE_MAX_GUI_PASSES;
	if ( poolCount < 0 || poolCount > poolCapacity
			|| compiled.count > static_cast<std::uint32_t>(
				poolCapacity - poolCount ) ) {
		if ( worldDomain ) {
			rg_materialResourceTable.stats.worldPassPoolOverflows++;
		} else {
			rg_materialResourceTable.stats.guiPassPoolOverflows++;
		}
		R_MaterialResourceTable_SetPassFailure<worldDomain>(
			record, MATERIAL_RESOURCE_GUI_PASS_FAILURE_PASS_POOL_OVERFLOW, -1 );
		return false;
	}

	if ( worldDomain ) {
		record.firstWorldPass = rg_materialResourceTable.worldPassCount;
		record.worldPassCount = static_cast<int>( compiled.count );
		memcpy( &rg_materialResourceTable.worldPasses[record.firstWorldPass],
			compiled.passes, sizeof( compiled.passes[0] ) * compiled.count );
		rg_materialResourceTable.worldPassCount += record.worldPassCount;
		record.worldPassEligible = true;
		record.worldPassFailure = MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE;
		record.worldPassFailureStage = -1;
	} else {
		record.firstGuiPass = rg_materialResourceTable.guiPassCount;
		record.guiPassCount = static_cast<int>( compiled.count );
		memcpy( &rg_materialResourceTable.guiPasses[record.firstGuiPass],
			compiled.passes, sizeof( compiled.passes[0] ) * compiled.count );
		rg_materialResourceTable.guiPassCount += record.guiPassCount;
		record.guiPassEligible = true;
		record.guiPassFailure = MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE;
		record.guiPassFailureStage = -1;
	}
	return true;
}

static bool R_MaterialResourceTable_CompileGuiPasses(
		materialResourceTableRecord_t &record ) {
	return R_MaterialResourceTable_CompileOrderedPasses<false>( record );
}

static bool R_MaterialResourceTable_CompileWorldPasses(
		materialResourceTableRecord_t &record ) {
	return R_MaterialResourceTable_CompileOrderedPasses<true>( record );
}

static void R_MaterialResourceTable_ScanMaterialStages( materialResourceTableRecord_t &record, const materialResourceRecord_t &sourceRecord ) {
	const idMaterial *material = sourceRecord.material;
	if ( material == NULL ) {
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_MISSING_MATERIAL, MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_MATERIAL );
		return;
	}
	record.stageCount = material->GetNumStages();
	if ( !material->IsDrawn() ) {
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_NO_DRAW_STAGES, MATERIAL_RESOURCE_FALLBACK_FLAG_NO_DRAW_STAGES );
	}
	if ( material->TestMaterialFlag( MF_NEED_CURRENT_RENDER ) ) {
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_NEEDS_CURRENT_RENDER, MATERIAL_RESOURCE_FALLBACK_FLAG_NEEDS_CURRENT_RENDER );
	}

	for ( int i = 0; i < record.stageCount; ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage == NULL ) {
			continue;
		}
		record.evaluatedStageCount++;
		if ( stage->mNumStageOps > 0 ) {
			record.hasConditionRegisters = true;
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_STAGE_CONDITION, MATERIAL_RESOURCE_FALLBACK_FLAG_STAGE_CONDITION );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( stage->texture.hasMatrix ) {
			record.hasTextureMatrix = true;
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_TEXTURE_MATRIX, MATERIAL_RESOURCE_FALLBACK_FLAG_TEXTURE_MATRIX );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( stage->vertexColor != SVC_IGNORE ) {
			record.hasVertexColor = true;
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_VERTEX_COLOR, MATERIAL_RESOURCE_FALLBACK_FLAG_VERTEX_COLOR );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( stage->privatePolygonOffset != 0.0f ) {
			record.hasPrivatePolygonOffset = true;
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_POLYGON_OFFSET, MATERIAL_RESOURCE_FALLBACK_FLAG_POLYGON_OFFSET );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( R_MaterialResourceTable_IsAdditiveBlend( stage->drawStateBits ) ) {
			record.additiveStageCount++;
		} else if ( R_MaterialResourceTable_IsFilterBlend( stage->drawStateBits ) ) {
			record.filterStageCount++;
		} else if ( R_MaterialResourceTable_IsAlphaBlend( stage->drawStateBits ) ) {
			record.blendStageCount++;
		}
		if ( stage->hasAlphaTest ) {
			record.alphaTest = true;
			record.alphaTestMode = stage->alphaTestMode;
			record.alphaTestRegister = stage->alphaTestRegister;
			record.shadowAlphaTest = true;
			record.shadowAlphaTestMode = stage->alphaTestMode;
			record.shadowAlphaTestRegister = stage->alphaTestRegister;
			record.shadowUsesTextureMatrix |= stage->texture.hasMatrix;
			record.shadowUsesVertexColor |= stage->vertexColor != SVC_IGNORE;
		}
		if ( stage->newStage != NULL ) {
			record.hasCustomProgram = true;
			if ( stage->newStage->glslProgram ) {
				record.hasCustomGLSL = true;
				R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_CUSTOM_GLSL, MATERIAL_RESOURCE_FALLBACK_FLAG_CUSTOM_GLSL | MATERIAL_RESOURCE_FALLBACK_FLAG_CUSTOM_PROGRAM );
			} else {
				R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_CUSTOM_PROGRAM, MATERIAL_RESOURCE_FALLBACK_FLAG_CUSTOM_PROGRAM );
			}
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( stage->texture.dynamic != DI_STATIC ) {
			record.hasDynamicImage = true;
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_DYNAMIC_IMAGE, MATERIAL_RESOURCE_FALLBACK_FLAG_DYNAMIC_IMAGE );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( R_MaterialResourceTable_ImageIsSceneCapture( stage->texture.image ) ) {
			record.hasSceneCaptureImage = true;
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_CURRENT_RENDER_IMAGE, MATERIAL_RESOURCE_FALLBACK_FLAG_CURRENT_RENDER_IMAGE | MATERIAL_RESOURCE_FALLBACK_FLAG_NEEDS_CURRENT_RENDER );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( stage->texture.texgen != TG_EXPLICIT ) {
			if ( R_MaterialResourceTable_TexgenIsScreenSpace( stage->texture.texgen ) ) {
				record.hasScreenTexgen = true;
				R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_SCREEN_TEXGEN, MATERIAL_RESOURCE_FALLBACK_FLAG_SCREEN_TEXGEN | MATERIAL_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_TEXGEN );
			} else if ( R_MaterialResourceTable_TexgenIsCubeOrSky( stage->texture.texgen ) ) {
				record.hasSkyTexgen = true;
				R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_SKY_TEXGEN, MATERIAL_RESOURCE_FALLBACK_FLAG_SKY_TEXGEN | MATERIAL_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_TEXGEN );
			} else {
				R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_UNSUPPORTED_TEXGEN, MATERIAL_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_TEXGEN );
			}
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}

		const materialResourceTextureSemantic_t semantic = R_MaterialResourceTable_StageSemantic( *stage, record.materialClass, record.needsCurrentRender );
		const int stageBindingIndex = R_MaterialResourceTable_AddTextureBinding(
			record, semantic, stage->texture.image, stage, i );
		if ( stage->hasAlphaTest && record.shadowAlphaBindingIndex < 0 ) {
			record.shadowAlphaBindingIndex = stageBindingIndex;
		}
	}

	record.shadowCasterSupported = record.castsShadow && record.fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE;
	if ( record.fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE ) {
		record.shadowFallbackFlags |= record.fallbackFlags;
		record.shadowCasterSupported = false;
	}
	if ( record.shadowAlphaTest && record.shadowAlphaBindingIndex < 0 ) {
		record.shadowFallbackFlags |= MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_IMAGE;
		record.shadowCasterSupported = false;
	}
	if ( record.shadowUsesTextureMatrix ) {
		record.shadowFallbackFlags |= MATERIAL_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_TEXGEN;
		record.shadowCasterSupported = false;
	}
	if ( record.shadowUsesVertexColor || record.twoSided ) {
		record.shadowFallbackFlags |= MATERIAL_RESOURCE_FALLBACK_FLAG_CUSTOM_PROGRAM;
		record.shadowCasterSupported = false;
	}
}

static void R_MaterialResourceTable_AddSourceImages( materialResourceTableRecord_t &record, const materialResourceRecord_t &sourceRecord ) {
	if ( sourceRecord.normalImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_BUMP, sourceRecord.normalImage, NULL, -1 );
	}
	if ( sourceRecord.diffuseImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_DIFFUSE, sourceRecord.diffuseImage, NULL, -1 );
	}
	if ( sourceRecord.specularImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_SPECULAR, sourceRecord.specularImage, NULL, -1 );
	}
	if ( sourceRecord.permutation.materialClass == RENDER_MATERIAL_GUI && sourceRecord.diffuseImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_GUI, sourceRecord.diffuseImage, NULL, -1 );
	}
	if ( sourceRecord.permutation.materialClass == RENDER_MATERIAL_POST_PROCESS && sourceRecord.diffuseImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_POST_PROCESS, sourceRecord.diffuseImage, NULL, -1 );
	}
}

static void R_MaterialResourceTable_CountPBR( const materialResourceTableRecord_t &record ) {
	if ( !record.hasPBR ) {
		rg_materialResourceTable.stats.classicRecords++;
		return;
	}
	materialResourceTableStats_t &stats = rg_materialResourceTable.stats;
	stats.pbrRecords++;
	stats.pbrResourceReadyRecords += record.pbrResourceReady ? 1 : 0;
	stats.pbrModernReadyRecords += record.pbrModernReady ? 1 : 0;
	stats.pbrPackedMapRecords += record.pbrPackedMaterialData ? 1 : 0;
	stats.pbrSeparateMapRecords += record.pbrSeparateMaterialData ? 1 : 0;
	stats.pbrAuthoredClassicFallbackRecords += record.pbrHasAuthoredClassicFallback ? 1 : 0;
	stats.pbrExplicitGeneratedFallbackRecords += record.pbrHasExplicitLegacyFallback && record.pbrUsesGeneratedLegacyFallback ? 1 : 0;
	stats.pbrGeneratedFallbackRecords += record.pbrUsesGeneratedLegacyFallback ? 1 : 0;
	stats.pbrApproximateFallbackRecords += record.pbrUsesApproximateLegacyFallback ? 1 : 0;
	stats.pbrMissingLegacyFallbackRecords += record.pbrLegacyFallbackMissing ? 1 : 0;
	stats.pbrMissingAlbedoMapRecords += record.hasPBRAlbedo ? 0 : 1;
	stats.pbrMissingNormalMapRecords += record.hasPBRNormal ? 0 : 1;
	stats.pbrMissingORMMapRecords += record.hasPBRORM ? 0 : 1;
	if ( record.pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_NONE ) {
		return;
	}
	stats.pbrFallbackRecords++;
	switch ( record.pbrFallbackReason ) {
	case MATERIAL_RESOURCE_PBR_FALLBACK_DISABLED:
		stats.pbrFallbackDisabled++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_WORKFLOW:
		stats.pbrFallbackUnsupportedWorkflow++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_MATERIAL_CLASS:
		stats.pbrFallbackUnsupportedMaterialClass++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_ALBEDO:
		stats.pbrFallbackMissingAlbedo++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_NORMAL_FORMAT:
		stats.pbrFallbackMissingNormalFormat++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_CONFLICTING_LAYOUT:
		stats.pbrFallbackConflictingLayout++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_TEXTURE_LAYOUT:
		stats.pbrFallbackUnsupportedTextureLayout++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_IMAGE:
		stats.pbrFallbackMissingImage++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_CLASSIC_FEATURE:
		stats.pbrFallbackClassicFeature++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_TOO_MANY_TEXTURES:
		stats.pbrFallbackTooManyTextures++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_SHADER_PATH_UNAVAILABLE:
		stats.pbrFallbackShaderPathUnavailable++;
		break;
	case MATERIAL_RESOURCE_PBR_FALLBACK_NONE:
	default:
		break;
	}
}

static void R_MaterialResourceTable_AddPBRSourceImages( materialResourceTableRecord_t &record, const materialResourceRecord_t &sourceRecord ) {
	if ( !sourceRecord.hasPBR ) {
		return;
	}
	record.hasPBR = true;
	record.pbrWorkflow = sourceRecord.pbrWorkflow;
	record.pbrNormalFormat = sourceRecord.pbrNormalFormat;
	record.pbrHasAuthoredClassicFallback = sourceRecord.pbrHasAuthoredClassicFallback;
	record.pbrHasExplicitLegacyFallback = sourceRecord.pbrHasExplicitLegacyFallback;
	record.pbrUsesGeneratedLegacyFallback = sourceRecord.pbrUsesGeneratedLegacyFallback;
	record.pbrUsesApproximateLegacyFallback = sourceRecord.pbrUsesApproximateLegacyFallback;
	record.pbrLegacyFallbackMissing = sourceRecord.pbrLegacyFallbackMissing;
	record.pbrMetallicRegister = sourceRecord.pbrMetallicRegister;
	record.pbrRoughnessRegister = sourceRecord.pbrRoughnessRegister;
	record.pbrAORegister = sourceRecord.pbrAORegister;
	record.pbrNormalScaleRegister = sourceRecord.pbrNormalScaleRegister;
	memcpy( record.pbrEmissiveColorRegisters, sourceRecord.pbrEmissiveColorRegisters, sizeof( record.pbrEmissiveColorRegisters ) );

	if ( sourceRecord.pbrAlbedoImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_ALBEDO, sourceRecord.pbrAlbedoImage, NULL, -1 );
	}
	if ( sourceRecord.pbrNormalImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_NORMAL, sourceRecord.pbrNormalImage, NULL, -1 );
	}
	if ( sourceRecord.pbrORMImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_ORM, sourceRecord.pbrORMImage, NULL, -1 );
	}
	if ( sourceRecord.pbrMetallicImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_METALLIC, sourceRecord.pbrMetallicImage, NULL, -1 );
	}
	if ( sourceRecord.pbrRoughnessImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_ROUGHNESS, sourceRecord.pbrRoughnessImage, NULL, -1 );
	}
	if ( sourceRecord.pbrAOImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_AO, sourceRecord.pbrAOImage, NULL, -1 );
	}
	if ( sourceRecord.pbrEmissiveImage != NULL ) {
		R_MaterialResourceTable_AddTextureBinding( record, MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR, sourceRecord.pbrEmissiveImage, NULL, -1 );
	}
	record.pbrPackedMaterialData = sourceRecord.pbrORMImage != NULL;
	record.pbrSeparateMaterialData = sourceRecord.pbrMetallicImage != NULL
		|| sourceRecord.pbrRoughnessImage != NULL
		|| sourceRecord.pbrAOImage != NULL;
}

static bool R_MaterialResourceTable_PBRBindingReady( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic, bool required ) {
	const int index = R_MaterialResourceTable_FindTextureBindingIndex( record, semantic );
	if ( index < 0 ) {
		return !required;
	}
	const materialResourceTextureBinding_t &binding = record.textures[index];
	return binding.image != NULL
		&& binding.loaded
		&& !binding.defaulted
		&& !R_IsMutableRenderImage( binding.image );
}

static void R_MaterialResourceTable_FinalizePBRContract( materialResourceTableRecord_t &record ) {
	if ( !record.hasPBR ) {
		return;
	}

	if ( !record.hasPBRAlbedo ) {
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_ALBEDO );
	}
	if ( record.hasPBRNormal && record.pbrNormalFormat == PBR_NORMAL_UNSPECIFIED ) {
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_NORMAL_FORMAT );
	}
	if ( record.pbrPackedMaterialData && record.pbrSeparateMaterialData ) {
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_CONFLICTING_LAYOUT );
	}
	if ( record.pbrWorkflow != PBR_WORKFLOW_METALLIC_ROUGHNESS ) {
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_WORKFLOW );
	}
	// Translucent PBR materials stay in the ordered forward path. Their blend
	// contract is checked again by the executor before a draw is promoted, so
	// uncommon multi-stage or unsupported blend expressions still fall back to
	// the authored classic stages.
	if ( record.materialClass != RENDER_MATERIAL_OPAQUE
		&& record.materialClass != RENDER_MATERIAL_PERFORATED
		&& record.materialClass != RENDER_MATERIAL_TRANSLUCENT ) {
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_MATERIAL_CLASS );
	}
	const bool sourceAlphaIntent = record.materialClass == RENDER_MATERIAL_TRANSLUCENT
		|| record.blendMode == MATERIAL_RESOURCE_BLEND_BLEND
		|| record.blendStageCount > 0;
	if ( sourceAlphaIntent && !R_MaterialResourceTable_HasPBRSourceAlphaBlendContract( record ) ) {
		// A transparent PBR draw has no correct partial promotion: if its sole
		// alpha stage cannot be proven equivalent to direct albedo sampling, keep
		// the complete material on its classic fallback owner.
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_CLASSIC_FEATURE );
	}
	if ( !R_MaterialResourceTable_PBRBindingReady( record, MATERIAL_RESOURCE_TEXTURE_ALBEDO, true )
		|| !R_MaterialResourceTable_PBRBindingReady( record, MATERIAL_RESOURCE_TEXTURE_NORMAL, record.hasPBRNormal )
		|| !R_MaterialResourceTable_PBRBindingReady( record, MATERIAL_RESOURCE_TEXTURE_ORM, record.hasPBRORM )
		|| !R_MaterialResourceTable_PBRBindingReady( record, MATERIAL_RESOURCE_TEXTURE_METALLIC, record.hasPBRMetallic )
		|| !R_MaterialResourceTable_PBRBindingReady( record, MATERIAL_RESOURCE_TEXTURE_ROUGHNESS, record.hasPBRRoughness )
		|| !R_MaterialResourceTable_PBRBindingReady( record, MATERIAL_RESOURCE_TEXTURE_AO, record.hasPBRAO )
		|| !R_MaterialResourceTable_PBRBindingReady( record, MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR, record.hasPBREmissive ) ) {
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_IMAGE );
	}
	if ( record.fallbackReason != MATERIAL_RESOURCE_FALLBACK_NONE ) {
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_CLASSIC_FEATURE );
	}

	// Resource readiness describes parsed, resident PBR inputs independently of
	// whether this frame is allowed to give them visible ownership.
	record.pbrResourceReady = record.pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_NONE;
	if ( !record.pbrResourceReady ) {
		record.pbrModernReady = false;
		return;
	}
	if ( !r_rendererModernQuality.GetBool() || !r_pbrMaterials.GetBool() ) {
		R_MaterialResourceTable_AddPBRFallback( record, MATERIAL_RESOURCE_PBR_FALLBACK_DISABLED );
		record.pbrModernReady = false;
		return;
	}
	// The direct corridor reserves units 0..6 for albedo, normal, packed ORM or
	// separate metallic/roughness/AO data, and emissive.  The shadow set begins
	// above the material texture-array range, so this remains valid on GL 3.3.
	record.pbrModernReady = true;
}

static void R_MaterialResourceTable_FinalizeShadowContract( materialResourceTableRecord_t &record ) {
	record.shadowFallbackFlags |= record.fallbackFlags;
	record.shadowCasterSupported = record.castsShadow && record.fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE;
	if ( record.shadowAlphaTest && record.shadowAlphaBindingIndex < 0 ) {
		record.shadowFallbackFlags |= MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_IMAGE;
		record.shadowCasterSupported = false;
	}
	if ( record.shadowUsesTextureMatrix ) {
		record.shadowFallbackFlags |= MATERIAL_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_TEXGEN;
		record.shadowCasterSupported = false;
	}
	if ( record.shadowUsesVertexColor || record.twoSided ) {
		record.shadowFallbackFlags |= MATERIAL_RESOURCE_FALLBACK_FLAG_CUSTOM_PROGRAM;
		record.shadowCasterSupported = false;
	}
}

static bool R_MaterialResourceTable_AddRecordFromSource(
		const materialResourceRecord_t &sourceRecord, int sourceIndex,
		bool scanMaterialStages, bool guiDomainReferenced = false,
		bool worldDomainReferenced = false ) {
	if ( rg_materialResourceTable.stats.records >= MATERIAL_RESOURCE_TABLE_MAX_RECORDS ) {
		rg_materialResourceTable.stats.overflow = true;
		R_MaterialResourceTable_SetStatus( "material table overflow" );
		return false;
	}

	materialResourceTableRecord_t &record = rg_materialResourceTable.records[rg_materialResourceTable.stats.records];
	memset( &record, 0, sizeof( record ) );
	for ( int i = 0; i < MATERIAL_RESOURCE_TEXTURE_COUNT; ++i ) {
		record.semanticBindingIndex[i] = -1;
	}
	record.tableIndex = rg_materialResourceTable.stats.records;
	record.tableGeneration = rg_materialResourceTable.tableGeneration;
	record.sourceMaterialRecordIndex = sourceIndex;
	// The scene-packet record owns the stable decl index. Synthetic validation
	// decls allocated through declManager intentionally have no idDeclBase and
	// therefore must never be asked for Index() directly.
	record.materialId = sourceRecord.material != NULL ? sourceRecord.resourceTableIndex : -1;
	record.material = sourceRecord.material;
	if ( !R_MaterialResourceTable_CopyDebugString( record.materialName, sizeof( record.materialName ), sourceRecord.material != NULL ? sourceRecord.material->GetName() : "<missing>" ) ) {
		R_MaterialResourceTable_RecordDebugStringTruncation( "material record name" );
	}
	record.materialClass = static_cast<rendererMaterialClass_t>( sourceRecord.permutation.materialClass );
	record.sortValue = sourceRecord.material != NULL ? sourceRecord.material->GetSort() : 0.0f;
	record.sortGroup = R_MaterialResourceTable_SortGroupForMaterial( sourceRecord.material, record.materialClass );
	record.blendMode = R_MaterialResourceTable_BlendModeForMaterial( sourceRecord.material, record.materialClass );
	record.drawn = sourceRecord.material != NULL ? sourceRecord.material->IsDrawn() : false;
	record.receivesLighting = sourceRecord.material != NULL ? sourceRecord.material->ReceivesLighting() : false;
	record.castsShadow = sourceRecord.material != NULL
		? ( !sourceRecord.material->IsDedicatedCollisionSurface() && sourceRecord.material->SurfaceCastsShadow() )
		: false;
	record.cullType = sourceRecord.material != NULL ? sourceRecord.material->GetCullType() : CT_FRONT_SIDED;
	record.shouldCreateBackSides = sourceRecord.material != NULL && sourceRecord.material->ShouldCreateBackSides();
	record.twoSided = sourceRecord.material != NULL ? ( record.cullType == CT_TWO_SIDED || record.shouldCreateBackSides ) : false;
	record.alphaTest = sourceRecord.permutation.alphaMode == MC_PERFORATED || record.blendMode == MATERIAL_RESOURCE_BLEND_ALPHA_TEST;
	record.alphaTestMode = 0;
	record.alphaTestRegister = 0;
	record.needsCurrentRender = sourceRecord.material != NULL && sourceRecord.material->TestMaterialFlag( MF_NEED_CURRENT_RENDER );
	record.hasGui = sourceRecord.material != NULL && sourceRecord.material->HasGui();
	record.hasSubview = sourceRecord.material != NULL && sourceRecord.material->HasSubview();
	record.hasMaterialPolygonOffset = sourceRecord.material != NULL && sourceRecord.material->TestMaterialFlag( MF_POLYGONOFFSET );
	record.polygonOffset = sourceRecord.material != NULL ? sourceRecord.material->GetPolygonOffset() : 0.0f;
	record.shadowAlphaBindingIndex = -1;
	record.firstGuiPass = -1;
	record.guiDomainReferenced = guiDomainReferenced;
	record.guiPassFailure = guiDomainReferenced
		? MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_COMPILED
		: MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_REFERENCED;
	record.guiPassFailureStage = -1;
	record.firstWorldPass = -1;
	record.worldDomainReferenced = worldDomainReferenced;
	record.worldPassFailure = worldDomainReferenced
		? MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_COMPILED
		: MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_REFERENCED;
	record.worldPassFailureStage = -1;
	record.registerStart = 0;
	record.registerCount = sourceRecord.material != NULL ? sourceRecord.material->GetNumRegisters() : 0;
	record.fallbackReason = MATERIAL_RESOURCE_FALLBACK_NONE;
	if ( record.hasMaterialPolygonOffset ) {
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_POLYGON_OFFSET, MATERIAL_RESOURCE_FALLBACK_FLAG_POLYGON_OFFSET );
		rg_materialResourceTable.stats.unsupportedFeatures++;
	}

	if ( scanMaterialStages ) {
		R_MaterialResourceTable_ScanMaterialStages( record, sourceRecord );
		R_MaterialResourceTable_AddSourceImages( record, sourceRecord );
		if ( record.guiDomainReferenced ) {
			R_MaterialResourceTable_CompileGuiPasses( record );
		}
		if ( record.worldDomainReferenced ) {
			R_MaterialResourceTable_CompileWorldPasses( record );
		}
	} else if ( sourceRecord.material == NULL ) {
		R_MaterialResourceTable_AddSourceImages( record, sourceRecord );
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_MISSING_MATERIAL, MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_MATERIAL );
	} else {
		R_MaterialResourceTable_AddSourceImages( record, sourceRecord );
	}
	R_MaterialResourceTable_AddPBRSourceImages( record, sourceRecord );
	if ( R_MaterialResourceTable_RecordNeedsSurfaceImage( record )
		&& !record.hasDiffuse
		&& !record.hasEmissive
		&& !record.hasGui
		&& !record.hasPostProcess ) {
		record.hasMissingImage = true;
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_MISSING_IMAGE, MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_IMAGE );
		rg_materialResourceTable.stats.missingImages++;
	}
	R_MaterialResourceTable_ValidateStageColorContract( record );
	R_MaterialResourceTable_ValidateAmbientOverlayContract( record );
	R_MaterialResourceTable_FinalizePBRContract( record );
	if ( !scanMaterialStages ) {
		record.shadowCasterSupported = record.castsShadow && record.fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE;
		record.shadowFallbackFlags = record.fallbackFlags;
	}
	R_MaterialResourceTable_FinalizeShadowContract( record );
	R_MaterialResourceTable_FinalizeRegisterRange( record );
	if ( record.guiDomainReferenced ) {
		rg_materialResourceTable.stats.guiDomainReferencedRecords++;
		if ( record.guiPassEligible ) {
			rg_materialResourceTable.stats.guiPassEligibleRecords++;
			rg_materialResourceTable.stats.guiPasses += record.guiPassCount;
		} else {
			rg_materialResourceTable.stats.guiPassFallbackRecords++;
		}
	}
	if ( record.worldDomainReferenced ) {
		rg_materialResourceTable.stats.worldDomainReferencedRecords++;
		if ( record.worldPassEligible ) {
			rg_materialResourceTable.stats.worldPassEligibleRecords++;
			rg_materialResourceTable.stats.worldPasses += record.worldPassCount;
		} else {
			rg_materialResourceTable.stats.worldPassFallbackRecords++;
		}
	}
	R_MaterialResourceTable_CountClass( record );
	R_MaterialResourceTable_CountFallbacks( record );
	R_MaterialResourceTable_CountPBR( record );

	R_MaterialResourceTable_HashInsert( record.material, rg_materialResourceTable.stats.records );
	rg_materialResourceTable.stats.records++;
	return true;
}

static void R_MaterialResourceTable_ResetFrameStats( void ) {
	const bool initialized = rg_materialResourceTable.stats.initialized;
	const bool available = rg_materialResourceTable.stats.available;
	const bool bindlessSupported = rg_materialResourceTable.stats.bindlessSupported;
	const bool textureArraysSupported = rg_materialResourceTable.stats.textureArraysSupported;
	const bool textureViewsSupported = rg_materialResourceTable.stats.textureViewsSupported;
	// records are not cleared wholesale (~2.3 MB): AddRecordFromSource memsets each
	// record before use, and every reader is bounded by stats.records or materialHash,
	// so entries past stats.records hold stale prior-frame data
	memset( rg_materialResourceTable.textureArrayTable, 0, sizeof( rg_materialResourceTable.textureArrayTable ) );
	memset( rg_materialResourceTable.materialHash, 0, sizeof( rg_materialResourceTable.materialHash ) );
	rg_materialResourceTable.textureArrayTableCount = 0;
	rg_materialResourceTable.guiPassCount = 0;
	rg_materialResourceTable.worldPassCount = 0;
	rg_materialResourceTable.tableGeneration++;
	if ( rg_materialResourceTable.tableGeneration == 0 ) {
		// Resource id zero and generation zero are both permanently invalid.
		rg_materialResourceTable.tableGeneration = 1;
	}
	memset( &rg_materialResourceTable.stats, 0, sizeof( rg_materialResourceTable.stats ) );
	rg_materialResourceTable.stats.initialized = initialized;
	rg_materialResourceTable.stats.available = available;
	rg_materialResourceTable.stats.bindlessSupported = bindlessSupported;
	rg_materialResourceTable.stats.bindlessEnabled = false;
	rg_materialResourceTable.stats.textureArraysSupported = textureArraysSupported;
	rg_materialResourceTable.stats.textureViewsSupported = textureViewsSupported;
	R_MaterialResourceTable_SetStatus( available ? "ready" : "unavailable" );
}

void R_MaterialResourceTable_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	memset( &rg_materialResourceTable, 0, sizeof( rg_materialResourceTable ) );
	rg_materialResourceTable.tableGeneration = 1;
	rg_materialResourceTable.caps = caps;
	rg_materialResourceTable.features = features;
	rg_materialResourceTable.maxClassicTextureUnits = caps.maxTextureImageUnits > 0 ? Min( caps.maxTextureImageUnits, MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS ) : MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS;
	rg_materialResourceTable.stats.initialized = true;
	rg_materialResourceTable.stats.available = features.scenePackets;
	rg_materialResourceTable.stats.bindlessSupported = features.bindlessTextures && caps.hasBindlessTexture;
	rg_materialResourceTable.stats.bindlessEnabled = false;
	rg_materialResourceTable.stats.textureArraysSupported = features.gpuDriven && caps.hasTextureArrays && caps.maxTextureImageUnits >= MATERIAL_RESOURCE_TABLE_TEXTURE_ARRAY_CAPACITY;
	rg_materialResourceTable.stats.textureViewsSupported = features.gpuDriven && caps.hasTextureViews;
	rg_materialResourceTable.stats.textureArrayTableCapacity = R_MaterialResourceTable_TextureArrayCapacity();
	R_MaterialResourceTable_SetStatus( rg_materialResourceTable.stats.available ? "ready" : "scene packets unavailable" );
}

void R_MaterialResourceTable_Shutdown( void ) {
	memset( &rg_materialResourceTable, 0, sizeof( rg_materialResourceTable ) );
	R_MaterialResourceTable_SetStatus( "shutdown" );
}

void R_MaterialResourceTable_PrepareFrame( const idScenePacketFrame &packetFrame ) {
	idGLDebugScope scope( "MaterialResourceTable::PrepareFrame" );
	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;
	const int materialRecordCount = packetFrame.NumMaterialRecords();
	const int drawPacketCount = packetFrame.NumDrawPackets();
	bool guiDomainReferenced[SCENE_PACKET_MAX_MATERIAL_RECORDS];
	bool worldDomainReferenced[SCENE_PACKET_MAX_MATERIAL_RECORDS];
	memset( guiDomainReferenced, 0, sizeof( guiDomainReferenced ) );
	memset( worldDomainReferenced, 0, sizeof( worldDomainReferenced ) );
	rg_materialResourceTable.stats.sourceMaterialRecords = materialRecordCount;
	for ( int i = 0; i < drawPacketCount; ++i ) {
		const drawPacket_t &drawPacket = packetFrame.DrawPacket( i );
		if ( drawPacket.materialRecordIndex >= 0 ) {
			rg_materialResourceTable.stats.drawPacketReferences++;
			if ( drawPacket.passCategory == RENDER_PASS_GUI
					&& drawPacket.packetCategory == SCENE_PACKET_CATEGORY_GUI
					&& drawPacket.materialRecordIndex < materialRecordCount ) {
				guiDomainReferenced[drawPacket.materialRecordIndex] = true;
			}
			if ( ( drawPacket.passCategory == RENDER_PASS_AMBIENT
						|| drawPacket.passCategory == RENDER_PASS_GUI )
					&& drawPacket.packetCategory == SCENE_PACKET_CATEGORY_WORLD
					&& drawPacket.materialRecordIndex < materialRecordCount ) {
				worldDomainReferenced[drawPacket.materialRecordIndex] = true;
			}
		}
	}
	if ( !rg_materialResourceTable.stats.available ) {
		return;
	}
	for ( int i = 0; i < materialRecordCount; ++i ) {
		R_MaterialResourceTable_AddRecordFromSource(
			packetFrame.MaterialRecord( i ), i, true, guiDomainReferenced[i],
			worldDomainReferenced[i] );
	}
	R_MaterialResourceTable_BuildTextureArrayTable();
	if ( rg_materialResourceTable.stats.overflow ) {
		R_MaterialResourceTable_SetStatus( "overflow" );
	} else {
		R_MaterialResourceTable_SetStatus( rg_materialResourceTable.stats.records > 0 ? "ready" : "empty" );
	}
}

const materialResourceTableStats_t &R_MaterialResourceTable_Stats( void ) {
	return rg_materialResourceTable.stats;
}

const materialResourceTableRecord_t *R_MaterialResourceTable_RecordForIndex( int tableIndex ) {
	if ( tableIndex < 0 || tableIndex >= rg_materialResourceTable.stats.records ) {
		return NULL;
	}
	return &rg_materialResourceTable.records[tableIndex];
}

const materialResourceTextureBinding_t *R_MaterialResourceTable_TextureBindingForSemantic( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic ) {
	const int bindingIndex = R_MaterialResourceTable_FindTextureBindingIndex( record, semantic );
	return bindingIndex >= 0 ? &record.textures[bindingIndex] : NULL;
}

bool R_MaterialResourceTable_GuiPassEligible(
		const materialResourceTableRecord_t &record ) {
	return record.guiDomainReferenced
		&& record.guiPassEligible
		&& record.guiPassFailure == MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE
		&& record.tableGeneration == rg_materialResourceTable.tableGeneration
		&& record.tableIndex >= 0
		&& record.tableIndex < rg_materialResourceTable.stats.records
		&& &rg_materialResourceTable.records[record.tableIndex] == &record
		&& record.firstGuiPass >= 0
		&& record.guiPassCount > 0
		&& record.guiPassCount
			<= static_cast<int>( RENDERER_CONTRACT_MAX_MATERIAL_PASSES )
		&& record.firstGuiPass <= rg_materialResourceTable.guiPassCount
		&& record.guiPassCount <= rg_materialResourceTable.guiPassCount - record.firstGuiPass;
}

const rendererMaterialPass_t *R_MaterialResourceTable_GuiPasses(
		const materialResourceTableRecord_t &record, int &count ) {
	count = 0;
	if ( !R_MaterialResourceTable_GuiPassEligible( record ) ) {
		return NULL;
	}
	count = record.guiPassCount;
	return &rg_materialResourceTable.guiPasses[record.firstGuiPass];
}

bool R_MaterialResourceTable_CopyGuiPassList(
		const materialResourceTableRecord_t &record,
		rendererMaterialPassList_t &destination ) {
	RendererContracts_ResetMaterialPassList( destination );
	int count = 0;
	const rendererMaterialPass_t *passes = R_MaterialResourceTable_GuiPasses(
		record, count );
	if ( passes == NULL || count <= 0
			|| count > static_cast<int>( RENDERER_CONTRACT_MAX_MATERIAL_PASSES ) ) {
		return false;
	}
	memcpy( destination.passes, passes, sizeof( destination.passes[0] ) * count );
	destination.count = static_cast<std::uint32_t>( count );
	return true;
}

bool R_MaterialResourceTable_WorldPassEligible(
		const materialResourceTableRecord_t &record ) {
	return record.worldDomainReferenced
		&& record.worldPassEligible
		&& record.worldPassFailure == MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE
		&& record.tableGeneration == rg_materialResourceTable.tableGeneration
		&& record.tableIndex >= 0
		&& record.tableIndex < rg_materialResourceTable.stats.records
		&& &rg_materialResourceTable.records[record.tableIndex] == &record
		&& record.firstWorldPass >= 0
		&& record.worldPassCount > 0
		&& record.worldPassCount
			<= static_cast<int>( RENDERER_CONTRACT_MAX_MATERIAL_PASSES )
		&& record.firstWorldPass <= rg_materialResourceTable.worldPassCount
		&& record.worldPassCount
			<= rg_materialResourceTable.worldPassCount - record.firstWorldPass;
}

const rendererMaterialPass_t *R_MaterialResourceTable_WorldPasses(
		const materialResourceTableRecord_t &record, int &count ) {
	count = 0;
	if ( !R_MaterialResourceTable_WorldPassEligible( record ) ) {
		return NULL;
	}
	count = record.worldPassCount;
	return &rg_materialResourceTable.worldPasses[record.firstWorldPass];
}

bool R_MaterialResourceTable_CopyWorldPassList(
		const materialResourceTableRecord_t &record,
		rendererMaterialPassList_t &destination ) {
	RendererContracts_ResetMaterialPassList( destination );
	int count = 0;
	const rendererMaterialPass_t *passes = R_MaterialResourceTable_WorldPasses(
		record, count );
	if ( passes == NULL || count <= 0
			|| count > static_cast<int>( RENDERER_CONTRACT_MAX_MATERIAL_PASSES ) ) {
		return false;
	}
	memcpy( destination.passes, passes,
		sizeof( destination.passes[0] ) * count );
	destination.count = static_cast<std::uint32_t>( count );
	return true;
}

const materialResourceTextureBinding_t *R_MaterialResourceTable_ResolveTextureResource(
		std::uint64_t textureResourceId ) {
	if ( textureResourceId == 0 ) {
		return NULL;
	}
	const unsigned int generation = static_cast<unsigned int>( textureResourceId >> 32 );
	const unsigned int encodedRecord = static_cast<unsigned int>(
		( textureResourceId >> 16 ) & 0xffffu );
	const unsigned int encodedBinding = static_cast<unsigned int>(
		textureResourceId & 0xffffu );
	if ( generation == 0 || generation != rg_materialResourceTable.tableGeneration
			|| encodedRecord == 0 || encodedBinding == 0 ) {
		return NULL;
	}
	const int recordIndex = static_cast<int>( encodedRecord - 1 );
	const int bindingIndex = static_cast<int>( encodedBinding - 1 );
	if ( recordIndex < 0 || recordIndex >= rg_materialResourceTable.stats.records ) {
		return NULL;
	}
	const materialResourceTableRecord_t &record =
		rg_materialResourceTable.records[recordIndex];
	if ( record.tableGeneration != generation || record.tableIndex != recordIndex
			|| bindingIndex < 0 || bindingIndex >= record.textureBindingCount ) {
		return NULL;
	}
	const materialResourceTextureBinding_t &binding = record.textures[bindingIndex];
	return binding.textureResourceId == textureResourceId ? &binding : NULL;
}

const materialResourceTableRecord_t *R_MaterialResourceTable_FindRecordForMaterial( const idMaterial *material ) {
	if ( material == NULL ) {
		return NULL;
	}
	int slot = R_MaterialResourceTable_HashMaterial( material );
	for ( int probe = 0; probe < MATERIAL_RESOURCE_TABLE_HASH_SIZE; ++probe ) {
		const short entry = rg_materialResourceTable.materialHash[slot];
		if ( entry == 0 ) {
			return NULL;
		}
		const materialResourceTableRecord_t &record = rg_materialResourceTable.records[entry - 1];
		if ( record.material == material ) {
			return &record;
		}
		slot = ( slot + 1 ) & ( MATERIAL_RESOURCE_TABLE_HASH_SIZE - 1 );
	}
	return NULL;
}

const unsigned int *R_MaterialResourceTable_TextureArrayTable( int &count ) {
	count = rg_materialResourceTable.textureArrayTableCount;
	return rg_materialResourceTable.textureArrayTable;
}

int R_MaterialResourceTable_TextureArrayTableIndexForHandle( unsigned int textureHandle ) {
	return R_MaterialResourceTable_FindTextureArrayTableIndexInternal( textureHandle );
}

void R_MaterialResourceTable_PrintGfxInfo( void ) {
	const materialResourceTableStats_t &stats = R_MaterialResourceTable_Stats();
	common->Printf(
		"Material resource table: initialized=%d available=%d prepared=%d records=%d classicRecords=%d source=%d draws=%d textures=%d classicTextures=%d arrays=%d table=%d/%d desc=%d overflow=%d views=%d bindless=%d/%d fallback=%d missing=%d unsupported=%d custom=%d/%d dynamic=%d current=%d texgen=%d(screen=%d sky=%d) condition=%d stageColor=%d matrix=%d vertexColor=%d offset=%d debugTrunc=%d source='%s' status='%s'\n",
		stats.initialized ? 1 : 0,
		stats.available ? 1 : 0,
		stats.prepared ? 1 : 0,
		stats.records,
		stats.classicRecords,
		stats.sourceMaterialRecords,
		stats.drawPacketReferences,
		stats.textureBindings,
		stats.classicTextureBindings,
		stats.textureArrayDescriptors,
		stats.textureArrayTableTextures,
		stats.textureArrayTableCapacity,
		stats.textureArrayTableDescriptors,
		stats.textureArrayTableOverflows,
		stats.textureViewDescriptors,
		stats.bindlessEnabled ? 1 : 0,
		stats.bindlessSupported ? 1 : 0,
		stats.fallbackRecords,
		stats.missingImages,
		stats.unsupportedFeatures,
		stats.fallbackCustomProgram,
		stats.fallbackCustomGLSL,
		stats.fallbackDynamicImage,
		stats.fallbackCurrentRenderImage,
		stats.fallbackUnsupportedTexgen,
		stats.fallbackScreenTexgen,
		stats.fallbackSkyTexgen,
		stats.fallbackStageCondition,
		stats.fallbackStageColor,
		stats.fallbackTextureMatrix,
		stats.fallbackVertexColor,
		stats.fallbackPolygonOffset,
		stats.debugStringTruncations,
		stats.debugStringTruncationSource,
		stats.lastFailure );
	common->Printf(
		"PBR material resources: records=%d resourceReady=%d modernReady=%d packed=%d separate=%d fallback=%d disabled=%d workflow=%d class=%d albedo=%d normalFormat=%d conflict=%d layout=%d image=%d classic=%d units=%d shader=%d authored=%d explicitGenerated=%d generated=%d approximate=%d missingFallback=%d mapsMissing=%d/%d/%d\n",
		stats.pbrRecords,
		stats.pbrResourceReadyRecords,
		stats.pbrModernReadyRecords,
		stats.pbrPackedMapRecords,
		stats.pbrSeparateMapRecords,
		stats.pbrFallbackRecords,
		stats.pbrFallbackDisabled,
		stats.pbrFallbackUnsupportedWorkflow,
		stats.pbrFallbackUnsupportedMaterialClass,
		stats.pbrFallbackMissingAlbedo,
		stats.pbrFallbackMissingNormalFormat,
		stats.pbrFallbackConflictingLayout,
		stats.pbrFallbackUnsupportedTextureLayout,
		stats.pbrFallbackMissingImage,
		stats.pbrFallbackClassicFeature,
		stats.pbrFallbackTooManyTextures,
		stats.pbrFallbackShaderPathUnavailable,
		stats.pbrAuthoredClassicFallbackRecords,
		stats.pbrExplicitGeneratedFallbackRecords,
		stats.pbrGeneratedFallbackRecords,
		stats.pbrApproximateFallbackRecords,
		stats.pbrMissingLegacyFallbackRecords,
		stats.pbrMissingAlbedoMapRecords,
		stats.pbrMissingNormalMapRecords,
		stats.pbrMissingORMMapRecords );
	common->Printf(
		"GUI material passes: referenced=%d eligible=%d fallback=%d passes=%d/%d poolOverflow=%d\n",
		stats.guiDomainReferencedRecords,
		stats.guiPassEligibleRecords,
		stats.guiPassFallbackRecords,
		stats.guiPasses,
		MATERIAL_RESOURCE_TABLE_MAX_GUI_PASSES,
		stats.guiPassPoolOverflows );
	common->Printf(
		"World ambient material passes: referenced=%d eligible=%d fallback=%d passes=%d/%d poolOverflow=%d\n",
		stats.worldDomainReferencedRecords,
		stats.worldPassEligibleRecords,
		stats.worldPassFallbackRecords,
		stats.worldPasses,
		MATERIAL_RESOURCE_TABLE_MAX_WORLD_PASSES,
		stats.worldPassPoolOverflows );
}

bool R_MaterialResourceTable_ClassicModernPathEligible( const materialResourceTableRecord_t &record ) {
	return !record.hasPBR;
}

bool R_MaterialResourceTable_PBRModernPathEligible( const materialResourceTableRecord_t &record ) {
	return r_rendererModernQuality.GetBool()
		&& record.hasPBR
		&& record.pbrModernReady
		&& record.pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_NONE;
}

bool R_MaterialResourceTable_PBRTransparentPathEligible( const materialResourceTableRecord_t &record ) {
	return R_MaterialResourceTable_PBRModernPathEligible( record )
		&& R_MaterialResourceTable_HasPBRSourceAlphaBlendContract( record );
}

void R_MaterialResourceTable_DumpLatest( void ) {
	const materialResourceTableStats_t &stats = R_MaterialResourceTable_Stats();
	common->Printf(
		"MaterialResourceTable dump: prepared=%d available=%d records=%d classicRecords=%d source=%d draws=%d textures=%d table=%d/%d desc=%d overflow=%d fallback=%d missing=%d defaulted=%d unsupported=%d debugTrunc=%d source='%s' status='%s'\n",
		stats.prepared ? 1 : 0,
		stats.available ? 1 : 0,
		stats.records,
		stats.classicRecords,
		stats.sourceMaterialRecords,
		stats.drawPacketReferences,
		stats.textureBindings,
		stats.textureArrayTableTextures,
		stats.textureArrayTableCapacity,
		stats.textureArrayTableDescriptors,
		stats.textureArrayTableOverflows,
		stats.fallbackRecords,
		stats.missingImages,
		stats.defaultedImages,
		stats.unsupportedFeatures,
		stats.debugStringTruncations,
		stats.debugStringTruncationSource,
		stats.lastFailure );
	common->Printf(
		"PBR summary: records=%d resourceReady=%d modernReady=%d packed=%d separate=%d fallback=%d approximate=%d missingFallback=%d mapsMissing=%d/%d/%d\n",
		stats.pbrRecords,
		stats.pbrResourceReadyRecords,
		stats.pbrModernReadyRecords,
		stats.pbrPackedMapRecords,
		stats.pbrSeparateMapRecords,
		stats.pbrFallbackRecords,
		stats.pbrApproximateFallbackRecords,
		stats.pbrMissingLegacyFallbackRecords,
		stats.pbrMissingAlbedoMapRecords,
		stats.pbrMissingNormalMapRecords,
		stats.pbrMissingORMMapRecords );
	common->Printf(
		"GUI pass summary: referenced=%d eligible=%d fallback=%d passes=%d/%d poolOverflow=%d\n",
		stats.guiDomainReferencedRecords,
		stats.guiPassEligibleRecords,
		stats.guiPassFallbackRecords,
		stats.guiPasses,
		MATERIAL_RESOURCE_TABLE_MAX_GUI_PASSES,
		stats.guiPassPoolOverflows );
	common->Printf(
		"World ambient pass summary: referenced=%d eligible=%d fallback=%d passes=%d/%d poolOverflow=%d\n",
		stats.worldDomainReferencedRecords,
		stats.worldPassEligibleRecords,
		stats.worldPassFallbackRecords,
		stats.worldPasses,
		MATERIAL_RESOURCE_TABLE_MAX_WORLD_PASSES,
		stats.worldPassPoolOverflows );
	for ( int i = 0; i < stats.records; ++i ) {
		const materialResourceTableRecord_t &record = rg_materialResourceTable.records[i];
		common->Printf(
			"  material[%d] source=%d id=%d name='%s' class=%s blend=%s sort=%s/%.2f cull=%d regs=%d+%d stageRegs=%d+%d stages=%d/%d blendStages=%d additiveStages=%d filterStages=%d alpha=%d textures=%d fallback=%s flags=0x%x current=%d capture=%d gui=%d subview=%d light=%d shadow=%d/%d matrix=%d vertexColor=%d dynamic=%d screenTexgen=%d skyTexgen=%d custom=%d/%d offset=%.2f twoSided=%d backsides=%d shadowFlags=0x%x\n",
			record.tableIndex,
			record.sourceMaterialRecordIndex,
			record.materialId,
			record.materialName,
			RendererMaterialClass_Name( record.materialClass ),
			MaterialResourceBlendMode_Name( record.blendMode ),
			R_MaterialResourceTable_SortGroupName( record.sortGroup ),
			record.sortValue,
			record.cullType,
			record.registerStart,
			record.registerCount,
			record.stageRegisterStart,
			record.stageRegisterCount,
			record.evaluatedStageCount,
			record.stageCount,
			record.blendStageCount,
			record.additiveStageCount,
			record.filterStageCount,
			record.alphaTest ? 1 : 0,
			record.textureBindingCount,
			MaterialResourceFallbackReason_Name( record.fallbackReason ),
			record.fallbackFlags,
			record.needsCurrentRender ? 1 : 0,
			record.hasSceneCaptureImage ? 1 : 0,
			record.hasGui ? 1 : 0,
			record.hasSubview ? 1 : 0,
			record.receivesLighting ? 1 : 0,
			record.castsShadow ? 1 : 0,
			record.shadowCasterSupported ? 1 : 0,
			record.hasTextureMatrix ? 1 : 0,
			record.hasVertexColor ? 1 : 0,
			record.hasDynamicImage ? 1 : 0,
			record.hasScreenTexgen ? 1 : 0,
			record.hasSkyTexgen ? 1 : 0,
			record.hasCustomProgram ? 1 : 0,
			record.hasCustomGLSL ? 1 : 0,
			record.polygonOffset,
			record.twoSided ? 1 : 0,
				record.shouldCreateBackSides ? 1 : 0,
				record.shadowFallbackFlags );
		if ( record.guiDomainReferenced ) {
			common->Printf(
				"    guiPass eligible=%d span=%d+%d failure=%s stage=%d generation=%u\n",
				record.guiPassEligible ? 1 : 0,
				record.firstGuiPass,
				record.guiPassCount,
				MaterialResourceGuiPassFailure_Name( record.guiPassFailure ),
				record.guiPassFailureStage,
				record.tableGeneration );
		}
		if ( record.worldDomainReferenced ) {
			common->Printf(
				"    worldPass eligible=%d span=%d+%d failure=%s stage=%d generation=%u\n",
				record.worldPassEligible ? 1 : 0,
				record.firstWorldPass,
				record.worldPassCount,
				MaterialResourceWorldPassFailure_Name( record.worldPassFailure ),
				record.worldPassFailureStage,
				record.tableGeneration );
		}
		if ( record.hasPBR ) {
			common->Printf(
				"    pbr workflow=%d normalFormat=%d resourceReady=%d modernReady=%d fallback=%s packed=%d separate=%d maps=a%d n%d orm%d m%d r%d ao%d e%d authored=%d explicit=%d generated=%d approximate=%d missingFallback=%d regs=%d/%d/%d/%d emit=%d,%d,%d\n",
				record.pbrWorkflow,
				record.pbrNormalFormat,
				record.pbrResourceReady ? 1 : 0,
				record.pbrModernReady ? 1 : 0,
				MaterialResourcePBRFallbackReason_Name( record.pbrFallbackReason ),
				record.pbrPackedMaterialData ? 1 : 0,
				record.pbrSeparateMaterialData ? 1 : 0,
				record.hasPBRAlbedo ? 1 : 0,
				record.hasPBRNormal ? 1 : 0,
				record.hasPBRORM ? 1 : 0,
				record.hasPBRMetallic ? 1 : 0,
				record.hasPBRRoughness ? 1 : 0,
				record.hasPBRAO ? 1 : 0,
				record.hasPBREmissive ? 1 : 0,
				record.pbrHasAuthoredClassicFallback ? 1 : 0,
				record.pbrHasExplicitLegacyFallback ? 1 : 0,
				record.pbrUsesGeneratedLegacyFallback ? 1 : 0,
				record.pbrUsesApproximateLegacyFallback ? 1 : 0,
				record.pbrLegacyFallbackMissing ? 1 : 0,
				record.pbrMetallicRegister,
				record.pbrRoughnessRegister,
				record.pbrAORegister,
				record.pbrNormalScaleRegister,
				record.pbrEmissiveColorRegisters[0],
				record.pbrEmissiveColorRegisters[1],
				record.pbrEmissiveColorRegisters[2] );
		}
		for ( int bindingIndex = 0; bindingIndex < record.textureBindingCount; ++bindingIndex ) {
			const materialResourceTextureBinding_t &binding = record.textures[bindingIndex];
			common->Printf(
				"    tex[%d] semantic=%s unit=%d image='%s' handle=%u loaded=%d defaulted=%d filter=%d repeat=%d stage=%d regs=%d+%d cond=%d alpha=%d texgen=%d matrix=%d vcolor=%d blend=%d depthWrite=%d offset=%.2f array=%d layer=%d view=%d bindless=%d\n",
				bindingIndex,
				MaterialResourceTextureSemantic_Name( binding.semantic ),
				binding.classicUnit,
				binding.image != NULL ? binding.image->GetName() : "<missing>",
				binding.textureHandle,
				binding.loaded ? 1 : 0,
				binding.defaulted ? 1 : 0,
				static_cast<int>( binding.filter ),
				static_cast<int>( binding.repeat ),
				binding.stageIndex,
				binding.stageRegisterStart,
				binding.stageRegisterCount,
				binding.hasConditionRegister ? 1 : 0,
				binding.hasAlphaTest ? 1 : 0,
				binding.texgen,
				binding.hasTextureMatrix ? 1 : 0,
				binding.vertexColorMode,
				binding.blendEnabled ? 1 : 0,
				binding.depthWrite ? 1 : 0,
				binding.privatePolygonOffset,
				binding.textureArrayCandidate ? 1 : 0,
				binding.textureArrayLayer,
				binding.textureViewCandidate ? 1 : 0,
				binding.bindlessEnabled ? 1 : 0 );
		}
	}
}

static bool R_MaterialResourceTable_RunGuiPassContractSelfTest( void ) {
	if ( globalImages == NULL ) {
		common->Printf( "RendererMaterialResourceTable GUI pass self-test skipped: images unavailable\n" );
		return true;
	}
	static const char declaration[] =
		"material _gui_resource_table_selftest {\n"
		" twoSided\n"
		" polygonOffset 2\n"
		" {\n"
		"  if parm0\n"
		"  blend gl_src_alpha, gl_one_minus_src_alpha\n"
		"  map _white\n"
		"  color parm0, parm1, parm2, parm3\n"
		"  translate parm4, parm5\n"
		"  scale parm6, parm7\n"
		"  vertexColor\n"
		"  maskRed\n"
		"  maskAlpha\n"
		"  maskDepth\n"
		" }\n"
		" {\n"
		"  blend gl_dst_alpha, gl_one\n"
		"  map _black\n"
		"  inverseVertexColor\n"
		"  maskGreen\n"
		" }\n"
		"}\n";

	idDecl *materialDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( materialDecl == NULL ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: GUI declaration allocation\n" );
		return false;
	}
	idMaterial *material = static_cast<idMaterial *>( materialDecl );
	if ( !material->Parse( declaration,
			idLib::SizeToInt( sizeof( declaration ) - 1,
				"GUI resource-table self-test" ) ) ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: GUI declaration parse\n" );
		DeclManager_FreeAllocatedDecl( materialDecl );
		return false;
	}
	// The parser assigns decal sort to material-level polygon offset by default.
	// This fixture isolates the supported offset state from the separately tested
	// decal-sort exclusion, as a 2D draw can reference an otherwise opaque
	// material regardless of where that material was first classified.
	const float parsedSort = material->GetSort();
	material->SetSort( SS_OPAQUE );

	const int savedMaxClassicTextureUnits = rg_materialResourceTable.maxClassicTextureUnits;
	rg_materialResourceTable.maxClassicTextureUnits = Max(
		savedMaxClassicTextureUnits, MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS );
	materialResourceRecord_t source;
	memset( &source, 0, sizeof( source ) );
	source.material = material;
	source.diffuseImage = globalImages->whiteImage;
	source.resourceTableIndex = 300;
	// A material can be deduplicated from a world draw before a GUI draw. The
	// packet reference, not the first observed material class, owns this domain.
	source.permutation.materialClass = RENDER_MATERIAL_OPAQUE;
	source.permutation.alphaMode = MC_PERFORATED;

	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;
	const bool unreferencedAdded = R_MaterialResourceTable_AddRecordFromSource(
		source, 299, true, false );
	const materialResourceTableRecord_t *unreferencedRecord =
		R_MaterialResourceTable_RecordForIndex( 0 );
	const bool unreferencedRejected = unreferencedAdded
		&& unreferencedRecord != NULL
		&& !unreferencedRecord->guiDomainReferenced
		&& !unreferencedRecord->guiPassEligible
		&& unreferencedRecord->firstGuiPass == -1
		&& unreferencedRecord->guiPassCount == 0
		&& unreferencedRecord->guiPassFailure
			== MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_REFERENCED
		&& !unreferencedRecord->worldDomainReferenced
		&& !unreferencedRecord->worldPassEligible
		&& unreferencedRecord->firstWorldPass == -1
		&& unreferencedRecord->worldPassCount == 0
		&& unreferencedRecord->worldPassFailure
			== MATERIAL_RESOURCE_GUI_PASS_FAILURE_NOT_REFERENCED
		&& rg_materialResourceTable.guiPassCount == 0
		&& rg_materialResourceTable.worldPassCount == 0
		&& rg_materialResourceTable.stats.guiDomainReferencedRecords == 0
		&& rg_materialResourceTable.stats.guiPassEligibleRecords == 0
		&& rg_materialResourceTable.stats.guiPassFallbackRecords == 0
		&& rg_materialResourceTable.stats.worldDomainReferencedRecords == 0
		&& rg_materialResourceTable.stats.worldPassEligibleRecords == 0
		&& rg_materialResourceTable.stats.worldPassFallbackRecords == 0;

	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;
	const bool added = R_MaterialResourceTable_AddRecordFromSource(
		source, 300, true, true, true );
	const materialResourceTableRecord_t *record =
		R_MaterialResourceTable_RecordForIndex( 0 );
	int passCount = 0;
	const rendererMaterialPass_t *passes = record != NULL
		? R_MaterialResourceTable_GuiPasses( *record, passCount ) : NULL;
	rendererMaterialPassList_t copiedPasses;
	const bool copied = record != NULL
		&& R_MaterialResourceTable_CopyGuiPassList( *record, copiedPasses );
	int worldPassCount = 0;
	const rendererMaterialPass_t *worldPasses = record != NULL
		? R_MaterialResourceTable_WorldPasses( *record, worldPassCount ) : NULL;
	rendererMaterialPassList_t copiedWorldPasses;
	const bool worldCopied = record != NULL
		&& R_MaterialResourceTable_CopyWorldPassList(
			*record, copiedWorldPasses );
	const materialResourceTextureBinding_t *firstBinding =
		passes != NULL && passCount > 0
			? R_MaterialResourceTable_ResolveTextureResource(
				passes[0].textureResourceId ) : NULL;
	const materialResourceTextureBinding_t *secondBinding =
		passes != NULL && passCount > 1
			? R_MaterialResourceTable_ResolveTextureResource(
				passes[1].textureResourceId ) : NULL;
	const std::uint64_t staleResourceId = passes != NULL && passCount > 0
		? passes[0].textureResourceId : 0;
	const std::uint32_t expectedFirstColorMask = RENDERER_COLOR_WRITE_GREEN
		| RENDERER_COLOR_WRITE_BLUE;
	const bool recordContract = added && record != NULL
		&& record->guiDomainReferenced
		&& R_MaterialResourceTable_GuiPassEligible( *record )
		&& record->guiPassFailure == MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE
		&& record->firstGuiPass == 0 && record->guiPassCount == 2
		&& record->textureBindingCount == 3
		&& record->materialClass == RENDER_MATERIAL_OPAQUE
		&& record->semanticBindingIndex[MATERIAL_RESOURCE_TEXTURE_EMISSIVE] == 0
		&& R_MaterialResourceTable_TextureBindingForSemantic(
			*record, MATERIAL_RESOURCE_TEXTURE_EMISSIVE ) == &record->textures[0];
	const bool orderedResourceContract = recordContract && passes != NULL
		&& passCount == 2 && copied && copiedPasses.count == 2
		&& passes[0].order == 0 && passes[1].order == 1
		&& passes[0].sourceStageIndex == 0 && passes[1].sourceStageIndex == 1
		&& passes[0].kind == RENDERER_MATERIAL_PASS_GUI
		&& passes[1].kind == RENDERER_MATERIAL_PASS_GUI
		&& passes[0].textureSemantic == RENDERER_TEXTURE_DIFFUSE
		&& passes[1].textureSemantic == RENDERER_TEXTURE_DIFFUSE
		&& passes[0].textureResourceId != passes[1].textureResourceId
		&& firstBinding == &record->textures[0]
		&& secondBinding == &record->textures[1]
		&& firstBinding->stageIndex == 0 && secondBinding->stageIndex == 1
		&& !firstBinding->depthWrite;
	const bool registerContract = orderedResourceContract
		&& passes[0].condition.source == RENDERER_REGISTER_INDEX
		&& passes[0].condition.index == material->GetStage( 0 )->conditionRegister
		&& passes[0].color[0].source == RENDERER_REGISTER_INDEX
		&& passes[0].color[0].index == material->GetStage( 0 )->color.registers[0]
		&& passes[0].textureMatrix[0].source == RENDERER_REGISTER_INDEX
		&& passes[0].textureMatrix[5].source == RENDERER_REGISTER_INDEX;
	const bool blendDepthContract = registerContract && passes[0].blend.enabled
		&& passes[0].blend.sourceColor == RENDERER_BLEND_SRC_ALPHA
		&& passes[0].blend.destinationColor == RENDERER_BLEND_ONE_MINUS_SRC_ALPHA
		&& !passes[0].depth.testEnabled && !passes[0].depth.writeEnabled
		&& passes[0].depth.compareOperation == RENDERER_COMPARE_LESS_OR_EQUAL
		&& passes[0].cull == RENDERER_CULL_NONE;
	const bool colorAlphaContract = blendDepthContract
		&& passes[0].colorWriteMask == expectedFirstColorMask
		&& !passes[0].alphaTestEnabled
		&& passes[0].alphaTestCompareOperation == RENDERER_COMPARE_GREATER
		&& passes[0].alphaTest.source == RENDERER_REGISTER_CONSTANT
		&& passes[0].alphaTest.constantValue == 0.5f;
	const bool firstStateContract = colorAlphaContract
		&& passes[0].vertexColor == RENDERER_VERTEX_COLOR_MODULATE
		&& passes[0].polygonOffsetEnabled
		&& passes[0].polygonOffsetFactor.source == RENDERER_REGISTER_CONSTANT
		&& passes[0].polygonOffsetUnits.source == RENDERER_REGISTER_CONSTANT
		&& passes[0].programFamily == RENDERER_PROGRAM_GUI
		&& RendererContracts_ValidateMaterialPass( passes[0] );
	const bool secondStateContract = firstStateContract && passes[1].blend.enabled
		&& passes[1].blend.sourceColor == RENDERER_BLEND_DST_ALPHA
		&& passes[1].blend.destinationColor == RENDERER_BLEND_ONE
		&& passes[1].vertexColor == RENDERER_VERTEX_COLOR_INVERSE_MODULATE
		&& RendererContracts_ValidateMaterialPass( passes[1] );
	const bool worldRecordContract = secondStateContract
		&& record->worldDomainReferenced
		&& R_MaterialResourceTable_WorldPassEligible( *record )
		&& record->worldPassFailure == MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE
		&& record->firstWorldPass == 0 && record->worldPassCount == 2
		&& worldPasses != NULL && worldPassCount == 2
		&& worldCopied && copiedWorldPasses.count == 2;
	rendererDepthState_t expectedWorldDepth[ 2 ];
	const bool worldFixtureDepthMapped = material->GetNumStages() >= 2
		&& R_MaterialResourceTable_MapWorldDepthState(
			material->GetStage( 0 )->drawStateBits, expectedWorldDepth[ 0 ] )
		&& R_MaterialResourceTable_MapWorldDepthState(
			material->GetStage( 1 )->drawStateBits, expectedWorldDepth[ 1 ] );
	const bool worldKindProgramDepthContract = worldRecordContract
		&& worldFixtureDepthMapped
		&& worldPasses[0].order == 0 && worldPasses[1].order == 1
		&& worldPasses[0].sourceStageIndex == 0
		&& worldPasses[1].sourceStageIndex == 1
		&& worldPasses[0].kind == RENDERER_MATERIAL_PASS_SURFACE
		&& worldPasses[1].kind == RENDERER_MATERIAL_PASS_SURFACE
		&& worldPasses[0].programFamily == RENDERER_PROGRAM_FIXED
		&& worldPasses[1].programFamily == RENDERER_PROGRAM_FIXED
		&& worldPasses[0].textureResourceId == passes[0].textureResourceId
		&& worldPasses[1].textureResourceId == passes[1].textureResourceId
		&& worldPasses[0].depth.testEnabled
			== expectedWorldDepth[0].testEnabled
		&& worldPasses[0].depth.writeEnabled
			== expectedWorldDepth[0].writeEnabled
		&& worldPasses[0].depth.compareOperation
			== expectedWorldDepth[0].compareOperation
		&& worldPasses[1].depth.testEnabled
			== expectedWorldDepth[1].testEnabled
		&& worldPasses[1].depth.writeEnabled
			== expectedWorldDepth[1].writeEnabled
		&& worldPasses[1].depth.compareOperation
			== expectedWorldDepth[1].compareOperation
		&& RendererContracts_ValidateMaterialPass( worldPasses[0] )
		&& RendererContracts_ValidateMaterialPass( worldPasses[1] );
	rendererDepthState_t lessDepth;
	rendererDepthState_t equalDepth;
	rendererDepthState_t alwaysDepth;
	const bool worldDepthMappingContract = worldKindProgramDepthContract
		&& R_MaterialResourceTable_MapWorldDepthState(
			GLS_DEPTHFUNC_LESS | GLS_DEPTHMASK, lessDepth )
		&& lessDepth.testEnabled && !lessDepth.writeEnabled
		&& lessDepth.compareOperation == RENDERER_COMPARE_LESS_OR_EQUAL
		&& R_MaterialResourceTable_MapWorldDepthState(
			GLS_DEPTHFUNC_EQUAL, equalDepth )
		&& equalDepth.testEnabled && equalDepth.writeEnabled
		&& equalDepth.compareOperation == RENDERER_COMPARE_EQUAL
		&& R_MaterialResourceTable_MapWorldDepthState(
			GLS_DEPTHFUNC_ALWAYS, alwaysDepth )
		&& alwaysDepth.testEnabled && alwaysDepth.writeEnabled
		&& alwaysDepth.compareOperation == RENDERER_COMPARE_ALWAYS;
	const bool statsContract = worldDepthMappingContract
		&& rg_materialResourceTable.stats.guiPasses == 2
		&& rg_materialResourceTable.stats.guiDomainReferencedRecords == 1
		&& rg_materialResourceTable.stats.guiPassEligibleRecords == 1
		&& rg_materialResourceTable.stats.guiPassFallbackRecords == 0
		&& rg_materialResourceTable.stats.worldPasses == 2
		&& rg_materialResourceTable.stats.worldDomainReferencedRecords == 1
		&& rg_materialResourceTable.stats.worldPassEligibleRecords == 1
		&& rg_materialResourceTable.stats.worldPassFallbackRecords == 0;
	bool ok = unreferencedRejected && statsContract;
	const bool initialEligible = record != NULL && record->guiPassEligible;
	const materialResourceGuiPassFailure_t initialFailure = record != NULL
		? record->guiPassFailure : MATERIAL_RESOURCE_GUI_PASS_FAILURE_MISSING_MATERIAL;
	const int initialBindingCount = record != NULL ? record->textureBindingCount : -1;

	// The authored alpha-test flag belongs to the separate classic depth/coverage
	// walk, not the ambient color walk. Generated 2D has no paired prerequisite
	// and rejects it, while world ambient keeps both color stages and relies on
	// the domain's exact classic depth-packet proof.
	shaderStage_t *alphaStage = material->GetNumStages() > 0
		? const_cast<shaderStage_t *>( material->GetStage( 0 ) ) : NULL;
	bool alphaDomainContract = false;
	if ( alphaStage != NULL ) {
		alphaStage->hasAlphaTest = true;
		R_MaterialResourceTable_ResetFrameStats();
		rg_materialResourceTable.stats.prepared = true;
		const bool alphaAdded = R_MaterialResourceTable_AddRecordFromSource(
			source, 301, true, true, true );
		const materialResourceTableRecord_t *alphaRecord =
			R_MaterialResourceTable_RecordForIndex( 0 );
		alphaDomainContract = alphaAdded && alphaRecord != NULL
			&& !alphaRecord->guiPassEligible
			&& alphaRecord->firstGuiPass == -1
			&& alphaRecord->guiPassCount == 0
			&& alphaRecord->guiPassFailure
				== MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_ALPHA_TEST
			&& alphaRecord->guiPassFailureStage == 0
			&& alphaRecord->worldPassEligible
			&& alphaRecord->firstWorldPass == 0
			&& alphaRecord->worldPassCount == 2
			&& alphaRecord->worldPassFailure
				== MATERIAL_RESOURCE_GUI_PASS_FAILURE_NONE
			&& alphaRecord->worldPassFailureStage == -1
			&& rg_materialResourceTable.guiPassCount == 0
			&& rg_materialResourceTable.worldPassCount == 2;
		alphaStage->hasAlphaTest = false;
	}

	// A private offset layered over a material offset has classic carry-forward
	// semantics that this first independent-pass contract does not encode.
	bool combinedPolygonOffsetRejected = false;
	if ( alphaStage != NULL ) {
		alphaStage->privatePolygonOffset = 3.0f;
		R_MaterialResourceTable_ResetFrameStats();
		rg_materialResourceTable.stats.prepared = true;
		const bool combinedOffsetAdded = R_MaterialResourceTable_AddRecordFromSource(
			source, 305, true, true, true );
		const materialResourceTableRecord_t *combinedOffsetRecord =
			R_MaterialResourceTable_RecordForIndex( 0 );
		combinedPolygonOffsetRejected = combinedOffsetAdded
			&& combinedOffsetRecord != NULL
			&& !combinedOffsetRecord->guiPassEligible
			&& combinedOffsetRecord->firstGuiPass == -1
			&& combinedOffsetRecord->guiPassCount == 0
			&& combinedOffsetRecord->guiPassFailure
				== MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_STATE
			&& combinedOffsetRecord->guiPassFailureStage == 0
			&& !combinedOffsetRecord->worldPassEligible
			&& combinedOffsetRecord->firstWorldPass == -1
			&& combinedOffsetRecord->worldPassCount == 0
			&& combinedOffsetRecord->worldPassFailure
				== MATERIAL_RESOURCE_GUI_PASS_FAILURE_UNSUPPORTED_STATE
			&& combinedOffsetRecord->worldPassFailureStage == 0
			&& rg_materialResourceTable.guiPassCount == 0
			&& rg_materialResourceTable.worldPassCount == 0;
		alphaStage->privatePolygonOffset = 0.0f;
	}

	// Post-process sorting is coupled to scene capture and skip controls in the
	// classic walker.  Prove it cannot leak a partially compiled shared list.
	const float initialSort = material->GetSort();
	material->SetSort( SS_POST_PROCESS );
	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;
	const bool postProcessAdded = R_MaterialResourceTable_AddRecordFromSource(
		source, 303, true, true, true );
	const materialResourceTableRecord_t *postProcessRecord =
		R_MaterialResourceTable_RecordForIndex( 0 );
	const bool postProcessRejected = postProcessAdded && postProcessRecord != NULL
		&& !postProcessRecord->guiPassEligible
		&& postProcessRecord->firstGuiPass == -1
		&& postProcessRecord->guiPassCount == 0
		&& postProcessRecord->guiPassFailure
			== MATERIAL_RESOURCE_GUI_PASS_FAILURE_POST_PROCESS_SORT
		&& postProcessRecord->guiPassFailureStage == -1
		&& !postProcessRecord->worldPassEligible
		&& postProcessRecord->firstWorldPass == -1
		&& postProcessRecord->worldPassCount == 0
		&& postProcessRecord->worldPassFailure
			== MATERIAL_RESOURCE_GUI_PASS_FAILURE_POST_PROCESS_SORT
		&& postProcessRecord->worldPassFailureStage == -1
		&& rg_materialResourceTable.guiPassCount == 0
		&& rg_materialResourceTable.worldPassCount == 0;
	material->SetSort( initialSort );

	// Decal-sort admission would bypass r_skipDecals in the shared consumers.
	// Reject it atomically until that cvar is part of the sealed view contract.
	material->SetSort( SS_DECAL );
	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;
	const bool decalAdded = R_MaterialResourceTable_AddRecordFromSource(
		source, 304, true, true, true );
	const materialResourceTableRecord_t *decalRecord =
		R_MaterialResourceTable_RecordForIndex( 0 );
	const bool decalRejected = decalAdded && decalRecord != NULL
		&& !decalRecord->guiPassEligible
		&& decalRecord->firstGuiPass == -1
		&& decalRecord->guiPassCount == 0
		&& decalRecord->guiPassFailure
			== MATERIAL_RESOURCE_GUI_PASS_FAILURE_DECAL_SORT
		&& decalRecord->guiPassFailureStage == -1
		&& !decalRecord->worldPassEligible
		&& decalRecord->firstWorldPass == -1
		&& decalRecord->worldPassCount == 0
		&& decalRecord->worldPassFailure
			== MATERIAL_RESOURCE_GUI_PASS_FAILURE_DECAL_SORT
		&& decalRecord->worldPassFailureStage == -1
		&& rg_materialResourceTable.guiPassCount == 0
		&& rg_materialResourceTable.worldPassCount == 0;
	material->SetSort( initialSort );

	// A new frame generation invalidates every old opaque resource id. Leave one
	// slot free and prove that a two-stage material fails without committing its
	// first pass or exposing a partially eligible record.
	const bool staleRejected = staleResourceId != 0
		&& R_MaterialResourceTable_ResolveTextureResource( staleResourceId ) == NULL;
	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;
	rg_materialResourceTable.guiPassCount = MATERIAL_RESOURCE_TABLE_MAX_GUI_PASSES - 1;
	const bool overflowAdded = R_MaterialResourceTable_AddRecordFromSource(
		source, 302, true, true );
	const materialResourceTableRecord_t *overflowRecord =
		R_MaterialResourceTable_RecordForIndex( 0 );
	int overflowPassCount = -1;
	const rendererMaterialPass_t *overflowPasses = overflowRecord != NULL
		? R_MaterialResourceTable_GuiPasses( *overflowRecord, overflowPassCount ) : NULL;
	const materialResourceGuiPassFailure_t guiOverflowFailure = overflowRecord != NULL
		? overflowRecord->guiPassFailure
		: MATERIAL_RESOURCE_GUI_PASS_FAILURE_MISSING_MATERIAL;
	const bool guiOverflowContract = overflowAdded && overflowRecord != NULL
		&& !overflowRecord->guiPassEligible
		&& overflowRecord->firstGuiPass == -1
		&& overflowRecord->guiPassCount == 0
		&& overflowRecord->guiPassFailure == MATERIAL_RESOURCE_GUI_PASS_FAILURE_PASS_POOL_OVERFLOW
		&& overflowPasses == NULL && overflowPassCount == 0
		&& rg_materialResourceTable.guiPassCount == MATERIAL_RESOURCE_TABLE_MAX_GUI_PASSES - 1
		&& rg_materialResourceTable.stats.guiPasses == 0
		&& rg_materialResourceTable.stats.guiPassEligibleRecords == 0
		&& rg_materialResourceTable.stats.guiPassFallbackRecords == 1
		&& rg_materialResourceTable.stats.guiPassPoolOverflows == 1;

	// The world ambient pool is separately bounded. Leave one slot free and
	// prove a repeated two-stage material rolls back without affecting the GUI
	// pool or exposing a partial world list.
	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;
	rg_materialResourceTable.worldPassCount =
		MATERIAL_RESOURCE_TABLE_MAX_WORLD_PASSES - 1;
	const bool worldOverflowAdded = R_MaterialResourceTable_AddRecordFromSource(
		source, 306, true, false, true );
	const materialResourceTableRecord_t *worldOverflowRecord =
		R_MaterialResourceTable_RecordForIndex( 0 );
	int worldOverflowPassCount = -1;
	const rendererMaterialPass_t *worldOverflowPasses = worldOverflowRecord != NULL
		? R_MaterialResourceTable_WorldPasses(
			*worldOverflowRecord, worldOverflowPassCount ) : NULL;
	const materialResourceWorldPassFailure_t worldOverflowFailure =
		worldOverflowRecord != NULL ? worldOverflowRecord->worldPassFailure
			: MATERIAL_RESOURCE_GUI_PASS_FAILURE_MISSING_MATERIAL;
	const bool worldOverflowContract = worldOverflowAdded
		&& worldOverflowRecord != NULL
		&& !worldOverflowRecord->worldPassEligible
		&& worldOverflowRecord->firstWorldPass == -1
		&& worldOverflowRecord->worldPassCount == 0
		&& worldOverflowFailure
			== MATERIAL_RESOURCE_GUI_PASS_FAILURE_PASS_POOL_OVERFLOW
		&& worldOverflowPasses == NULL && worldOverflowPassCount == 0
		&& rg_materialResourceTable.worldPassCount
			== MATERIAL_RESOURCE_TABLE_MAX_WORLD_PASSES - 1
		&& rg_materialResourceTable.guiPassCount == 0
		&& rg_materialResourceTable.stats.worldPasses == 0
		&& rg_materialResourceTable.stats.worldPassEligibleRecords == 0
		&& rg_materialResourceTable.stats.worldPassFallbackRecords == 1
		&& rg_materialResourceTable.stats.worldPassPoolOverflows == 1;
	ok &= alphaDomainContract && combinedPolygonOffsetRejected
		&& postProcessRejected && decalRejected && staleRejected
		&& guiOverflowContract && worldOverflowContract;

	if ( !ok ) {
		common->Printf(
			"RendererMaterialResourceTable self-test failed: ordered pass contract unreferenced=%d added=%d record=%d eligible=%d failure=%s guiPasses=%d worldPasses=%d bindings=%d copied=%d/%d resolved=%d/%d groups=%d/%d/%d/%d/%d/%d/%d/%d/%d/%d alphaDomain=%d combinedOffsetRejected=%d postProcessRejected=%d decalRejected=%d stale=%d guiOverflow=%d/%s worldOverflow=%d/%s pool=%d/%d\n",
			unreferencedRejected ? 1 : 0,
			added ? 1 : 0,
			record != NULL ? 1 : 0,
			initialEligible ? 1 : 0,
			MaterialResourceGuiPassFailure_Name( initialFailure ),
			passCount,
			worldPassCount,
			initialBindingCount,
			copied ? 1 : 0,
			worldCopied ? 1 : 0,
			firstBinding != NULL ? 1 : 0,
			secondBinding != NULL ? 1 : 0,
			recordContract ? 1 : 0,
			orderedResourceContract ? 1 : 0,
			registerContract ? 1 : 0,
			blendDepthContract ? 1 : 0,
			colorAlphaContract ? 1 : 0,
			firstStateContract ? 1 : 0,
			secondStateContract ? 1 : 0,
			worldRecordContract ? 1 : 0,
			worldKindProgramDepthContract ? 1 : 0,
			statsContract ? 1 : 0,
			alphaDomainContract ? 1 : 0,
			combinedPolygonOffsetRejected ? 1 : 0,
			postProcessRejected ? 1 : 0,
			decalRejected ? 1 : 0,
			staleRejected ? 1 : 0,
			guiOverflowContract ? 1 : 0,
			MaterialResourceGuiPassFailure_Name( guiOverflowFailure ),
			worldOverflowContract ? 1 : 0,
			MaterialResourceWorldPassFailure_Name( worldOverflowFailure ),
			rg_materialResourceTable.guiPassCount,
			rg_materialResourceTable.worldPassCount );
	}
	for ( int i = 0; i < rg_materialResourceTable.stats.records; ++i ) {
		rg_materialResourceTable.records[i].material = NULL;
	}
	material->SetSort( parsedSort );
	DeclManager_FreeAllocatedDecl( materialDecl );
	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.maxClassicTextureUnits = savedMaxClassicTextureUnits;
	if ( ok ) {
		common->Printf( "RendererMaterialResourceTable ordered GUI/world pass contract self-test passed\n" );
	}
	return ok;
}

static bool R_MaterialResourceTable_RunPBRContractSelfTest( void ) {
	if ( globalImages == NULL ) {
		common->Printf( "RendererMaterialResourceTable PBR self-test skipped: images unavailable\n" );
		return true;
	}
	static const char declaration[] =
		"material _pbr_resource_table_selftest {\n"
		" bumpmap _flat\n"
		" diffusemap _white\n"
		" specularmap _black\n"
		" pbr {\n"
		"  workflow metallicRoughness\n"
		"  albedoMap _white\n"
		"  normalMap _flat\n"
		"  normalFormat tangentRG\n"
		"  ormMap _white\n"
		"  metallic 0.2\n"
		"  roughness 0.7\n"
		"  ao 1.0\n"
		" }\n"
		"}\n";

	idDecl *materialDecl = declManager->AllocateDecl( DECL_MATERIAL );
	if ( materialDecl == NULL ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: PBR declaration allocation\n" );
		return false;
	}
	idMaterial *material = static_cast<idMaterial *>( materialDecl );
	if ( !material->Parse( declaration, idLib::SizeToInt( sizeof( declaration ) - 1, "PBR resource-table self-test" ) ) ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: PBR declaration parse\n" );
		DeclManager_FreeAllocatedDecl( materialDecl );
		return false;
	}
	const int savedMaxClassicTextureUnits = rg_materialResourceTable.maxClassicTextureUnits;
	// Vulkan can invoke this CPU-only contract before the classic material table
	// is initialized. Scope the same minimum unit budget used by this synthetic
	// bump/diffuse/specular material, then restore the live backend state.
	rg_materialResourceTable.maxClassicTextureUnits = Max( savedMaxClassicTextureUnits, 3 );
	const pbrMaterialInfo_t &pbr = material->GetPBRInfo();
	materialResourceRecord_t source;
	memset( &source, 0, sizeof( source ) );
	source.material = material;
	source.diffuseImage = globalImages->whiteImage;
	source.normalImage = globalImages->flatNormalMap;
	source.specularImage = globalImages->blackImage;
	source.hasPBR = true;
	source.pbrWorkflow = static_cast<int>( pbr.workflow );
	source.pbrNormalFormat = static_cast<int>( pbr.normalFormat );
	source.pbrAlbedoImage = pbr.albedo.image;
	source.pbrNormalImage = pbr.normal.image;
	source.pbrORMImage = pbr.orm.image;
	source.pbrHasAuthoredClassicFallback = pbr.hasAuthoredClassicFallback;
	source.pbrHasExplicitLegacyFallback = pbr.hasExplicitLegacyFallback;
	source.pbrUsesGeneratedLegacyFallback = pbr.usesGeneratedLegacyFallback;
	source.pbrUsesApproximateLegacyFallback = pbr.usesApproximateLegacyFallback;
	source.pbrLegacyFallbackMissing = pbr.legacyFallbackMissing;
	source.pbrMetallicRegister = pbr.metallicRegister;
	source.pbrRoughnessRegister = pbr.roughnessRegister;
	source.pbrAORegister = pbr.aoRegister;
	source.pbrNormalScaleRegister = pbr.normalScaleRegister;
	memcpy( source.pbrEmissiveColorRegisters, pbr.emissiveColorRegisters, sizeof( source.pbrEmissiveColorRegisters ) );
	source.permutation.materialClass = RENDER_MATERIAL_OPAQUE;
	source.permutation.alphaMode = MC_OPAQUE;
	source.resourceTableIndex = 200;

	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;
	const bool added = R_MaterialResourceTable_AddRecordFromSource( source, 200, true );

	materialResourceRecord_t separateSource = source;
	separateSource.pbrORMImage = NULL;
	separateSource.pbrMetallicImage = globalImages->whiteImage;
	separateSource.pbrRoughnessImage = globalImages->whiteImage;
	separateSource.pbrAOImage = globalImages->whiteImage;
	separateSource.resourceTableIndex = 201;
	const bool separateAdded = R_MaterialResourceTable_AddRecordFromSource( separateSource, 201, true );

	materialResourceRecord_t scalarSource = source;
	scalarSource.pbrNormalImage = NULL;
	scalarSource.pbrNormalFormat = PBR_NORMAL_UNSPECIFIED;
	scalarSource.pbrORMImage = NULL;
	scalarSource.resourceTableIndex = 202;
	const bool scalarAdded = R_MaterialResourceTable_AddRecordFromSource( scalarSource, 202, true );

	materialResourceRecord_t explicitSource = source;
	explicitSource.pbrHasAuthoredClassicFallback = false;
	explicitSource.pbrHasExplicitLegacyFallback = true;
	explicitSource.pbrUsesGeneratedLegacyFallback = true;
	explicitSource.pbrUsesApproximateLegacyFallback = false;
	explicitSource.resourceTableIndex = 203;
	const bool explicitAdded = R_MaterialResourceTable_AddRecordFromSource( explicitSource, 203, true );

	materialResourceRecord_t unsupportedSource = source;
	unsupportedSource.pbrWorkflow = PBR_WORKFLOW_SPECULAR_GLOSSINESS;
	unsupportedSource.resourceTableIndex = 204;
	const bool unsupportedAdded = R_MaterialResourceTable_AddRecordFromSource( unsupportedSource, 204, true );

	materialResourceRecord_t missingFallbackSource = source;
	missingFallbackSource.pbrHasAuthoredClassicFallback = false;
	missingFallbackSource.pbrLegacyFallbackMissing = true;
	missingFallbackSource.resourceTableIndex = 205;
	const bool missingFallbackAdded = R_MaterialResourceTable_AddRecordFromSource( missingFallbackSource, 205, true );

	materialResourceRecord_t missingAlbedoSource = source;
	missingAlbedoSource.pbrAlbedoImage = NULL;
	missingAlbedoSource.resourceTableIndex = 206;
	const bool missingAlbedoAdded = R_MaterialResourceTable_AddRecordFromSource( missingAlbedoSource, 206, true );

	idImageOpts mutableImageOpts = globalImages->whiteImage->GetOpts();
	idImage *mutableImage = globalImages->ScratchImage(
		"_pbr_resource_table_mutable_selftest",
		&mutableImageOpts,
		TF_DEFAULT,
		TR_REPEAT,
		TD_PBR_COLOR );
	materialResourceRecord_t mutableImageSource = source;
	mutableImageSource.pbrAlbedoImage = mutableImage;
	mutableImageSource.resourceTableIndex = 207;
	const bool mutableImageAdded = mutableImage != NULL
		&& R_MaterialResourceTable_AddRecordFromSource( mutableImageSource, 207, true );

	// Authored legacy-map metadata is not an explicit-generated fallback when
	// the complete classic interaction already made stage generation redundant.
	materialResourceRecord_t redundantExplicitSource = source;
	redundantExplicitSource.pbrHasExplicitLegacyFallback = true;
	redundantExplicitSource.pbrUsesGeneratedLegacyFallback = false;
	redundantExplicitSource.resourceTableIndex = 208;
	const bool redundantExplicitAdded = R_MaterialResourceTable_AddRecordFromSource( redundantExplicitSource, 208, true );

	// A source-alpha PBR surface is valid only through the ordered transparent
	// forward path; retain it here so that path can perform blend validation.
	materialResourceRecord_t transparentSource = source;
	transparentSource.permutation.materialClass = RENDER_MATERIAL_TRANSLUCENT;
	transparentSource.permutation.alphaMode = MC_TRANSLUCENT;
	transparentSource.resourceTableIndex = 209;
	const bool transparentAdded = R_MaterialResourceTable_AddRecordFromSource( transparentSource, 209, true );

	R_MaterialResourceTable_BuildTextureArrayTable();
	const materialResourceTableRecord_t *record = R_MaterialResourceTable_RecordForIndex( 0 );
	const materialResourceTableRecord_t *separateRecord = R_MaterialResourceTable_RecordForIndex( 1 );
	const materialResourceTableRecord_t *scalarRecord = R_MaterialResourceTable_RecordForIndex( 2 );
	const materialResourceTableRecord_t *explicitRecord = R_MaterialResourceTable_RecordForIndex( 3 );
	const materialResourceTableRecord_t *unsupportedRecord = R_MaterialResourceTable_RecordForIndex( 4 );
	const materialResourceTableRecord_t *missingFallbackRecord = R_MaterialResourceTable_RecordForIndex( 5 );
	const materialResourceTableRecord_t *missingAlbedoRecord = R_MaterialResourceTable_RecordForIndex( 6 );
	const materialResourceTableRecord_t *mutableImageRecord = R_MaterialResourceTable_RecordForIndex( 7 );
	const materialResourceTableRecord_t *redundantExplicitRecord = R_MaterialResourceTable_RecordForIndex( 8 );
	const materialResourceTableRecord_t *transparentRecord = R_MaterialResourceTable_RecordForIndex( 9 );
	const materialResourceTableStats_t &stats = R_MaterialResourceTable_Stats();
	const bool pbrEnabled = r_rendererModernQuality.GetBool() && r_pbrMaterials.GetBool();
	const materialResourcePBRFallbackReason_t expectedReason = pbrEnabled
		? MATERIAL_RESOURCE_PBR_FALLBACK_NONE
		: MATERIAL_RESOURCE_PBR_FALLBACK_DISABLED;
	const materialResourcePBRFallbackReason_t expectedSeparateReason = pbrEnabled
		? MATERIAL_RESOURCE_PBR_FALLBACK_NONE
		: MATERIAL_RESOURCE_PBR_FALLBACK_DISABLED;
	materialResourceTableRecord_t sourceAlphaContract;
	R_MaterialResourceTable_InitSelfTestRecord( sourceAlphaContract );
	sourceAlphaContract.material = material;
	sourceAlphaContract.hasPBR = true;
	sourceAlphaContract.pbrModernReady = pbrEnabled;
	sourceAlphaContract.pbrFallbackReason = expectedReason;
	sourceAlphaContract.blendMode = MATERIAL_RESOURCE_BLEND_BLEND;
	sourceAlphaContract.blendStageCount = 1;
	const materialResourceTextureBinding_t *sourceAlphaAlbedo = record != NULL
		? R_MaterialResourceTable_TextureBindingForSemantic( *record, MATERIAL_RESOURCE_TEXTURE_ALBEDO )
		: NULL;
	const shaderStage_t *identityStage = material->GetNumStages() > 0 ? material->GetStage( 0 ) : NULL;
	if ( sourceAlphaAlbedo != NULL && identityStage != NULL ) {
		sourceAlphaContract.textureBindingCount = 2;
		sourceAlphaContract.semanticBindingIndex[MATERIAL_RESOURCE_TEXTURE_ALBEDO] = 0;
		sourceAlphaContract.semanticBindingIndex[MATERIAL_RESOURCE_TEXTURE_EMISSIVE] = 1;
		sourceAlphaContract.textures[0] = *sourceAlphaAlbedo;
		sourceAlphaContract.textures[0].semantic = MATERIAL_RESOURCE_TEXTURE_ALBEDO;
		materialResourceTextureBinding_t &alphaStage = sourceAlphaContract.textures[1];
		alphaStage = *sourceAlphaAlbedo;
		alphaStage.semantic = MATERIAL_RESOURCE_TEXTURE_EMISSIVE;
		alphaStage.stageIndex = 0;
		alphaStage.drawStateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK;
		alphaStage.blendEnabled = true;
		alphaStage.depthWrite = false;
		alphaStage.colorMasked = false;
		alphaStage.hasConditionRegister = false;
		alphaStage.hasAlphaTest = false;
		alphaStage.hasTextureMatrix = false;
		alphaStage.privatePolygonOffset = 0.0f;
		alphaStage.texgen = static_cast<int>( TG_EXPLICIT );
		alphaStage.vertexColorMode = static_cast<int>( SVC_IGNORE );
		memcpy( alphaStage.colorRegisters, identityStage->color.registers, sizeof( alphaStage.colorRegisters ) );
	}
	const bool sourceAlphaContractAccepted = R_MaterialResourceTable_PBRTransparentPathEligible( sourceAlphaContract ) == pbrEnabled;
	materialResourceTableRecord_t complexSourceAlpha = sourceAlphaContract;
	complexSourceAlpha.additiveStageCount = 1;
	const bool sourceAlphaComplexBlendRejected = !R_MaterialResourceTable_PBRTransparentPathEligible( complexSourceAlpha );
	materialResourceTableRecord_t mismatchedAlbedoSourceAlpha = sourceAlphaContract;
	mismatchedAlbedoSourceAlpha.textures[1].image = globalImages->blackImage;
	const bool sourceAlphaMismatchedAlbedoRejected = !R_MaterialResourceTable_PBRTransparentPathEligible( mismatchedAlbedoSourceAlpha );
	materialResourceTableRecord_t wrongPassSourceAlpha = sourceAlphaContract;
	wrongPassSourceAlpha.textures[1].semantic = MATERIAL_RESOURCE_TEXTURE_DIFFUSE;
	const bool sourceAlphaWrongPassRejected = !R_MaterialResourceTable_PBRTransparentPathEligible( wrongPassSourceAlpha );
	materialResourceTableRecord_t tintedSourceAlpha = sourceAlphaContract;
	tintedSourceAlpha.textures[1].colorRegisters[0] = EXP_REG_PARM0;
	const bool sourceAlphaTintedRejected = !R_MaterialResourceTable_PBRTransparentPathEligible( tintedSourceAlpha );
	materialResourceTableRecord_t transformedUVSourceAlpha = sourceAlphaContract;
	transformedUVSourceAlpha.textures[1].texgen = static_cast<int>( TG_SCREEN );
	const bool sourceAlphaTransformedUVRejected = !R_MaterialResourceTable_PBRTransparentPathEligible( transformedUVSourceAlpha );
	bool pbrBindingsExcludedFromClassicTable = stats.records == 10;
	for ( int recordIndex = 0; recordIndex < stats.records; ++recordIndex ) {
		const materialResourceTableRecord_t *testRecord = R_MaterialResourceTable_RecordForIndex( recordIndex );
		pbrBindingsExcludedFromClassicTable &= testRecord != NULL && testRecord->hasPBR;
		if ( testRecord != NULL ) {
			for ( int i = 0; i < testRecord->textureBindingCount; ++i ) {
				pbrBindingsExcludedFromClassicTable &= !testRecord->textures[i].textureArrayCandidate
					&& testRecord->textures[i].textureArrayLayer == -1;
			}
		}
	}
	const bool ok = added && separateAdded && scalarAdded && explicitAdded
		&& unsupportedAdded && missingFallbackAdded && missingAlbedoAdded && mutableImageAdded && redundantExplicitAdded && transparentAdded
		&& record != NULL
		&& record->hasPBR
		&& record->pbrResourceReady
		&& record->pbrModernReady == pbrEnabled
		&& !R_MaterialResourceTable_ClassicModernPathEligible( *record )
		&& R_MaterialResourceTable_PBRModernPathEligible( *record ) == pbrEnabled
		&& record->pbrFallbackReason == expectedReason
		&& record->pbrPackedMaterialData
		&& !record->pbrSeparateMaterialData
		&& record->pbrHasAuthoredClassicFallback
		&& R_MaterialResourceTable_TextureBindingForSemantic( *record, MATERIAL_RESOURCE_TEXTURE_ALBEDO ) != NULL
		&& R_MaterialResourceTable_TextureBindingForSemantic( *record, MATERIAL_RESOURCE_TEXTURE_NORMAL ) != NULL
		&& R_MaterialResourceTable_TextureBindingForSemantic( *record, MATERIAL_RESOURCE_TEXTURE_ORM ) != NULL
		&& separateRecord != NULL && separateRecord->pbrResourceReady
		&& !separateRecord->pbrPackedMaterialData && separateRecord->pbrSeparateMaterialData
		&& separateRecord->pbrModernReady == pbrEnabled
		&& R_MaterialResourceTable_PBRModernPathEligible( *separateRecord ) == pbrEnabled
		&& separateRecord->pbrFallbackReason == expectedSeparateReason
		&& scalarRecord != NULL && scalarRecord->pbrResourceReady
		&& !scalarRecord->pbrPackedMaterialData && !scalarRecord->pbrSeparateMaterialData
		&& !scalarRecord->hasPBRNormal && !scalarRecord->hasPBRORM
		&& explicitRecord != NULL && explicitRecord->pbrHasExplicitLegacyFallback
		&& explicitRecord->pbrUsesGeneratedLegacyFallback
		&& !explicitRecord->pbrUsesApproximateLegacyFallback
		&& unsupportedRecord != NULL
		&& unsupportedRecord->pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_WORKFLOW
		&& missingFallbackRecord != NULL && missingFallbackRecord->pbrLegacyFallbackMissing
		&& missingAlbedoRecord != NULL && !missingAlbedoRecord->hasPBRAlbedo
		&& missingAlbedoRecord->pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_ALBEDO
		&& mutableImageRecord != NULL && mutableImageRecord->hasPBRAlbedo
		&& !mutableImageRecord->pbrResourceReady
		&& mutableImageRecord->pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_IMAGE
		&& redundantExplicitRecord != NULL && redundantExplicitRecord->pbrHasExplicitLegacyFallback
		&& !redundantExplicitRecord->pbrUsesGeneratedLegacyFallback
		&& transparentRecord != NULL
		&& transparentRecord->materialClass == RENDER_MATERIAL_TRANSLUCENT
		&& transparentRecord->blendMode == MATERIAL_RESOURCE_BLEND_BLEND
		&& !transparentRecord->pbrResourceReady
		&& !transparentRecord->pbrModernReady
		&& !R_MaterialResourceTable_PBRModernPathEligible( *transparentRecord )
		&& transparentRecord->pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_CLASSIC_FEATURE
		&& sourceAlphaContractAccepted
		&& sourceAlphaComplexBlendRejected
		&& sourceAlphaMismatchedAlbedoRejected
		&& sourceAlphaWrongPassRejected
		&& sourceAlphaTintedRejected
		&& sourceAlphaTransformedUVRejected
		&& stats.pbrRecords == 10
		&& stats.classicRecords == 0
		&& stats.pbrResourceReadyRecords == 6
		&& stats.pbrModernReadyRecords == ( pbrEnabled ? 5 : 0 )
		&& stats.pbrPackedMapRecords == 8
		&& stats.pbrSeparateMapRecords == 1
		&& stats.pbrAuthoredClassicFallbackRecords == 8
		&& stats.pbrExplicitGeneratedFallbackRecords == 1
		&& stats.pbrGeneratedFallbackRecords == 1
		&& stats.pbrApproximateFallbackRecords == 0
		&& stats.pbrMissingLegacyFallbackRecords == 1
		&& stats.pbrMissingAlbedoMapRecords == 1
		&& stats.pbrMissingNormalMapRecords == 1
		&& stats.pbrMissingORMMapRecords == 2
		&& stats.pbrFallbackMissingImage == 1
		&& stats.pbrFallbackRecords == ( pbrEnabled ? 4 : 10 )
		&& stats.pbrFallbackUnsupportedTextureLayout == 0
		&& pbrBindingsExcludedFromClassicTable
		&& stats.textureArrayTableDescriptors == 0;
	if ( !ok ) {
		common->Printf(
			"RendererMaterialResourceTable self-test failed: PBR contract added=%d/%d/%d/%d/%d/%d/%d/%d/%d/%d sourceAlpha=%d/%d/%d/%d/%d/%d record=%d resource=%d modern=%d fallback=%s records=%d packed=%d separate=%d missingFallback=%d mapsMissing=%d/%d/%d\n",
			added ? 1 : 0,
			separateAdded ? 1 : 0,
			scalarAdded ? 1 : 0,
			explicitAdded ? 1 : 0,
			unsupportedAdded ? 1 : 0,
			missingFallbackAdded ? 1 : 0,
			missingAlbedoAdded ? 1 : 0,
			mutableImageAdded ? 1 : 0,
			redundantExplicitAdded ? 1 : 0,
			transparentAdded ? 1 : 0,
			sourceAlphaContractAccepted ? 1 : 0,
			sourceAlphaComplexBlendRejected ? 1 : 0,
			sourceAlphaMismatchedAlbedoRejected ? 1 : 0,
			sourceAlphaWrongPassRejected ? 1 : 0,
			sourceAlphaTintedRejected ? 1 : 0,
			sourceAlphaTransformedUVRejected ? 1 : 0,
			record != NULL ? 1 : 0,
			record != NULL && record->pbrResourceReady ? 1 : 0,
			record != NULL && record->pbrModernReady ? 1 : 0,
			record != NULL ? MaterialResourcePBRFallbackReason_Name( record->pbrFallbackReason ) : "missing",
			stats.pbrRecords,
			stats.pbrPackedMapRecords,
			stats.pbrSeparateMapRecords,
			stats.pbrMissingLegacyFallbackRecords,
			stats.pbrMissingAlbedoMapRecords,
			stats.pbrMissingNormalMapRecords,
			stats.pbrMissingORMMapRecords );
	}
	// The table owns no declaration lifetime. Remove every retained synthetic
	// pointer and invalidate the hash before releasing the temporary material,
	// including failure exits that callers may inspect afterward.
	for ( int i = 0; i < stats.records; ++i ) {
		rg_materialResourceTable.records[i].material = NULL;
	}
	DeclManager_FreeAllocatedDecl( materialDecl );
	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.maxClassicTextureUnits = savedMaxClassicTextureUnits;
	if ( ok ) {
		common->Printf( "RendererMaterialResourceTable PBR contract self-test passed\n" );
	}
	return ok;
}

static bool R_MaterialResourceTable_RunSyntheticRecordSelfTest( void ) {
	R_MaterialResourceTable_ResetFrameStats();
	rg_materialResourceTable.stats.prepared = true;

	const rendererMaterialClass_t classes[] = {
		RENDER_MATERIAL_OPAQUE,
		RENDER_MATERIAL_PERFORATED,
		RENDER_MATERIAL_TRANSLUCENT,
		RENDER_MATERIAL_GUI,
		RENDER_MATERIAL_POST_PROCESS
	};
	for ( int i = 0; i < static_cast<int>( sizeof( classes ) / sizeof( classes[0] ) ); ++i ) {
		materialResourceRecord_t source;
		memset( &source, 0, sizeof( source ) );
		source.material = tr.defaultMaterial;
		source.diffuseImage = globalImages != NULL ? globalImages->defaultImage : NULL;
		source.normalImage = globalImages != NULL ? globalImages->flatNormalMap : NULL;
		source.specularImage = globalImages != NULL ? globalImages->whiteImage : NULL;
		source.resourceTableIndex = i;
		source.permutation.materialClass = classes[i];
		source.permutation.alphaMode = classes[i] == RENDER_MATERIAL_PERFORATED ? MC_PERFORATED : MC_OPAQUE;
		if ( !R_MaterialResourceTable_AddRecordFromSource( source, i, false ) ) {
			common->Printf( "RendererMaterialResourceTable self-test failed: synthetic class add failed\n" );
			return false;
		}
	}

	materialResourceRecord_t missingTextureSource;
	memset( &missingTextureSource, 0, sizeof( missingTextureSource ) );
	missingTextureSource.material = tr.defaultMaterial;
	missingTextureSource.resourceTableIndex = 100;
	missingTextureSource.permutation.materialClass = RENDER_MATERIAL_OPAQUE;
	missingTextureSource.permutation.alphaMode = MC_OPAQUE;
	if ( !R_MaterialResourceTable_AddRecordFromSource( missingTextureSource, 100, false ) ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: missing-texture record add failed\n" );
		return false;
	}
	R_MaterialResourceTable_BuildTextureArrayTable();

	const materialResourceTableStats_t &stats = R_MaterialResourceTable_Stats();
	if ( stats.records != 6
		|| stats.classicRecords != 6
		|| stats.opaqueRecords != 2
		|| stats.perforatedRecords != 1
		|| stats.translucentRecords != 1
		|| stats.guiRecords != 1
		|| stats.postProcessRecords != 1
		|| stats.alphaTestRecords != 1
		|| stats.fallbackMissingImage <= 0 ) {
		common->Printf(
			"RendererMaterialResourceTable self-test failed: synthetic counts records=%d classic=%d opaque=%d perforated=%d translucent=%d gui=%d post=%d alpha=%d missingFallback=%d\n",
			stats.records,
			stats.classicRecords,
			stats.opaqueRecords,
			stats.perforatedRecords,
			stats.translucentRecords,
			stats.guiRecords,
			stats.postProcessRecords,
			stats.alphaTestRecords,
			stats.fallbackMissingImage );
		return false;
	}
	const materialResourceTableRecord_t *perforated = R_MaterialResourceTable_RecordForIndex( 1 );
	const materialResourceTableRecord_t *post = R_MaterialResourceTable_RecordForIndex( 4 );
	const materialResourceTableRecord_t *missing = R_MaterialResourceTable_RecordForIndex( 5 );
	if ( perforated == NULL || perforated->blendMode != MATERIAL_RESOURCE_BLEND_ALPHA_TEST || !perforated->alphaTest ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: perforated classification mismatch\n" );
		return false;
	}
	if ( post == NULL || post->blendMode != MATERIAL_RESOURCE_BLEND_POST_PROCESS || !post->hasPostProcess ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: post classification mismatch\n" );
		return false;
	}
	if ( missing == NULL || missing->fallbackReason != MATERIAL_RESOURCE_FALLBACK_MISSING_IMAGE ) {
		common->Printf( "RendererMaterialResourceTable self-test failed: missing-texture fallback mismatch\n" );
		return false;
	}
	if ( stats.textureArraysSupported && ( !stats.textureArrayTableReady || stats.textureArrayTableTextures <= 0 || stats.textureArrayTableDescriptors <= 0 ) ) {
		common->Printf(
			"RendererMaterialResourceTable self-test failed: texture-array table mismatch ready=%d textures=%d desc=%d overflow=%d\n",
			stats.textureArrayTableReady ? 1 : 0,
			stats.textureArrayTableTextures,
			stats.textureArrayTableDescriptors,
			stats.textureArrayTableOverflows );
		return false;
	}
	if ( !R_MaterialResourceTable_RunAmbientOverlayContractSelfTest() ) {
		return false;
	}
	return true;
}

bool RendererMaterialResourceTable_RunSelfTest( void ) {
	if ( !R_MaterialResourceTable_RunPBRContractSelfTest() ) {
		return false;
	}
	if ( !rg_materialResourceTable.stats.initialized ) {
		common->Printf( "RendererMaterialResourceTable full self-test skipped: material resource table uninitialized\n" );
		return true;
	}
	if ( !rg_materialResourceTable.stats.available ) {
		common->Printf( "RendererMaterialResourceTable full self-test skipped: scene packets unavailable\n" );
		return true;
	}

	if ( !R_MaterialResourceTable_RunGuiPassContractSelfTest() ) {
		return false;
	}
	if ( !R_MaterialResourceTable_RunSyntheticRecordSelfTest() ) {
		return false;
	}

	srfTriangles_t geo;
	memset( &geo, 0, sizeof( geo ) );
	geo.numVerts = 3;
	geo.numIndexes = 6;
	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	for ( int i = 0; i < 2; ++i ) {
		drawSurfs[i].geo = &geo;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort() + static_cast<float>( i ) * 0.000001f;
		}
	}
	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	drawSurfs[0].space = &viewEntity;
	drawSurfs[1].space = &viewEntity;
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	R_MaterialResourceTable_PrepareFrame( packetFrame );
	const materialResourceTableStats_t &stats = R_MaterialResourceTable_Stats();
	const int materialRecordCount = packetFrame.NumMaterialRecords();
	const int drawPacketCount = packetFrame.NumDrawPackets();
	const int expectedRecords = tr.defaultMaterial != NULL ? 1 : 0;
	if ( stats.sourceMaterialRecords != materialRecordCount
		|| stats.records != expectedRecords
		|| stats.classicRecords != expectedRecords
		|| stats.drawPacketReferences != ( tr.defaultMaterial != NULL ? drawPacketCount : 0 )
		|| stats.overflow ) {
		common->Printf(
			"RendererMaterialResourceTable self-test failed: packet build mismatch source=%d/%d records=%d classic=%d expected=%d drawRefs=%d overflow=%d\n",
			stats.sourceMaterialRecords,
			materialRecordCount,
			stats.records,
			stats.classicRecords,
			expectedRecords,
			stats.drawPacketReferences,
			stats.overflow ? 1 : 0 );
		return false;
	}
	if ( expectedRecords > 0 ) {
		const materialResourceTableRecord_t *record = R_MaterialResourceTable_RecordForIndex( 0 );
		if ( record == NULL || record->tableIndex != 0 || record->sourceMaterialRecordIndex != 0 || record->material != tr.defaultMaterial || record->materialClass == RENDER_MATERIAL_NONE ) {
			common->Printf( "RendererMaterialResourceTable self-test failed: packet record identity mismatch\n" );
			return false;
		}
	}

	common->Printf(
		"RendererMaterialResourceTable self-test passed (records=%d textures=%d fallback=%d missing=%d arrays=%d table=%d/%d views=%d bindless=%d/%d)\n",
		stats.records,
		stats.textureBindings,
		stats.fallbackRecords,
		stats.missingImages,
		stats.textureArrayDescriptors,
		stats.textureArrayTableTextures,
		stats.textureArrayTableCapacity,
		stats.textureViewDescriptors,
		stats.bindlessEnabled ? 1 : 0,
		stats.bindlessSupported ? 1 : 0 );
	return true;
}
