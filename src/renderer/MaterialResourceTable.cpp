// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "MaterialResourceTable.h"
#include "GLDebugScope.h"

typedef struct materialResourceTableState_s {
	materialResourceTableRecord_t	records[MATERIAL_RESOURCE_TABLE_MAX_RECORDS];
	materialResourceTableStats_t		stats;
	renderBackendCaps_t				caps;
	renderFeatureSet_t				features;
	int								maxClassicTextureUnits;
} materialResourceTableState_t;

static materialResourceTableState_t rg_materialResourceTable;

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
	case MATERIAL_RESOURCE_TEXTURE_NONE:
	default:
		return "none";
	}
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
	case MATERIAL_RESOURCE_FALLBACK_DYNAMIC_IMAGE:
		return "dynamicImage";
	case MATERIAL_RESOURCE_FALLBACK_UNSUPPORTED_TEXGEN:
		return "unsupportedTexgen";
	case MATERIAL_RESOURCE_FALLBACK_NEEDS_CURRENT_RENDER:
		return "needsCurrentRender";
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
	if ( sort >= SS_DECAL && sort < SS_MEDIUM ) {
		return MATERIAL_RESOURCE_SORT_DECAL;
	}
	if ( sort >= SS_MEDIUM ) {
		return MATERIAL_RESOURCE_SORT_TRANSLUCENT;
	}
	if ( sort >= SS_OPAQUE ) {
		return MATERIAL_RESOURCE_SORT_OPAQUE;
	}
	return MATERIAL_RESOURCE_SORT_UNKNOWN;
}

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
	if ( material == NULL || material->GetNumStages() <= 0 ) {
		return materialClass == RENDER_MATERIAL_TRANSLUCENT ? MATERIAL_RESOURCE_BLEND_BLEND : MATERIAL_RESOURCE_BLEND_OPAQUE;
	}

	bool sawAlphaTest = false;
	for ( int i = 0; i < material->GetNumStages(); ++i ) {
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
		if ( blendBits == ( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO )
			|| blendBits == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR ) ) {
			return MATERIAL_RESOURCE_BLEND_FILTER;
		}
		return MATERIAL_RESOURCE_BLEND_BLEND;
	}
	if ( blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) ) {
		return MATERIAL_RESOURCE_BLEND_ADD;
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
	default:
		return -1;
	}
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
		|| blendBits == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR );
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
	default:
		break;
	}
}

static int R_MaterialResourceTable_FindTextureBindingIndex( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic ) {
	for ( int i = 0; i < record.textureBindingCount; ++i ) {
		if ( record.textures[i].semantic == semantic ) {
			return i;
		}
	}
	return -1;
}

static bool R_MaterialResourceTable_HasSemanticBinding( const materialResourceTableRecord_t &record, materialResourceTextureSemantic_t semantic ) {
	for ( int i = 0; i < record.textureBindingCount; ++i ) {
		if ( record.textures[i].semantic == semantic ) {
			return true;
		}
	}
	return false;
}

static void R_MaterialResourceTable_AddTextureBinding(
	materialResourceTableRecord_t &record,
	materialResourceTextureSemantic_t semantic,
	const idImage *image,
	const shaderStage_t *stage,
	int stageIndex ) {
	if ( semantic == MATERIAL_RESOURCE_TEXTURE_NONE || image == NULL ) {
		record.hasMissingImage = true;
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_MISSING_IMAGE, MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_IMAGE );
		rg_materialResourceTable.stats.missingImages++;
		return;
	}
	if ( R_MaterialResourceTable_HasSemanticBinding( record, semantic ) ) {
		R_MaterialResourceTable_UpdateRecordSemanticFlags( record, semantic, image );
		return;
	}
	if ( record.textureBindingCount >= MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS ) {
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_TOO_MANY_TEXTURES, MATERIAL_RESOURCE_FALLBACK_FLAG_TOO_MANY_TEXTURES );
		rg_materialResourceTable.stats.unsupportedFeatures++;
		return;
	}

	materialResourceTextureBinding_t &binding = record.textures[record.textureBindingCount++];
	memset( &binding, 0, sizeof( binding ) );
	binding.semantic = semantic;
	binding.image = image;
	binding.textureHandle = image->IsLoaded() ? const_cast<idImage *>( image )->GetDeviceHandle() : 0;
	binding.filter = image->GetFilter();
	binding.repeat = image->GetRepeat();
	binding.classicUnit = R_MaterialResourceTable_SemanticSlot( semantic );
	if ( binding.classicUnit >= rg_materialResourceTable.maxClassicTextureUnits ) {
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_TOO_MANY_TEXTURES, MATERIAL_RESOURCE_FALLBACK_FLAG_TOO_MANY_TEXTURES );
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
	binding.depthWrite = stage != NULL && ( stage->drawStateBits & GLS_DEPTHMASK ) != 0;
	binding.colorMasked = stage != NULL && ( stage->drawStateBits & GLS_COLORMASK ) != 0;
	if ( stage != NULL ) {
		memcpy( binding.colorRegisters, stage->color.registers, sizeof( binding.colorRegisters ) );
		memcpy( binding.matrixRegisters, stage->texture.matrix, sizeof( binding.matrixRegisters ) );
		binding.hasTextureMatrix = stage->texture.hasMatrix;
	}
	binding.loaded = image->IsLoaded();
	binding.defaulted = image->IsDefaulted();
	binding.textureArrayCandidate = rg_materialResourceTable.stats.textureArraysSupported;
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

	R_MaterialResourceTable_UpdateRecordSemanticFlags( record, semantic, image );
	if ( binding.defaulted ) {
		record.hasDefaultedImage = true;
		rg_materialResourceTable.stats.defaultedImages++;
	}
	rg_materialResourceTable.stats.textureBindings++;
	if ( binding.classicUnit >= 0 ) {
		rg_materialResourceTable.stats.classicTextureBindings++;
	}
	if ( binding.textureArrayCandidate ) {
		rg_materialResourceTable.stats.textureArrayDescriptors++;
	}
	if ( binding.textureViewCandidate ) {
		rg_materialResourceTable.stats.textureViewDescriptors++;
	}
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
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_DYNAMIC_IMAGE ) != 0 ) {
		rg_materialResourceTable.stats.fallbackDynamicImage++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_TEXGEN ) != 0 ) {
		rg_materialResourceTable.stats.fallbackUnsupportedTexgen++;
	}
	if ( ( record.fallbackFlags & MATERIAL_RESOURCE_FALLBACK_FLAG_NEEDS_CURRENT_RENDER ) != 0 ) {
		rg_materialResourceTable.stats.fallbackNeedsCurrentRender++;
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

	for ( int i = 0; i < material->GetNumStages(); ++i ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage == NULL ) {
			continue;
		}
		record.evaluatedStageCount++;
		record.hasConditionRegisters |= stage->mNumStageOps > 0;
		record.hasTextureMatrix |= stage->texture.hasMatrix;
		record.hasVertexColor |= stage->vertexColor != SVC_IGNORE;
		record.hasPrivatePolygonOffset |= stage->privatePolygonOffset != 0.0f;
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
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_CUSTOM_PROGRAM, MATERIAL_RESOURCE_FALLBACK_FLAG_CUSTOM_PROGRAM );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( stage->texture.dynamic != DI_STATIC ) {
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_DYNAMIC_IMAGE, MATERIAL_RESOURCE_FALLBACK_FLAG_DYNAMIC_IMAGE );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}
		if ( stage->texture.texgen != TG_EXPLICIT ) {
			R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_UNSUPPORTED_TEXGEN, MATERIAL_RESOURCE_FALLBACK_FLAG_UNSUPPORTED_TEXGEN );
			rg_materialResourceTable.stats.unsupportedFeatures++;
		}

		const materialResourceTextureSemantic_t semantic = R_MaterialResourceTable_StageSemantic( *stage, record.materialClass, record.needsCurrentRender );
		if ( !R_MaterialResourceTable_HasSemanticBinding( record, semantic ) ) {
			R_MaterialResourceTable_AddTextureBinding( record, semantic, stage->texture.image, stage, i );
		}
		if ( stage->hasAlphaTest && record.shadowAlphaBindingIndex < 0 ) {
			record.shadowAlphaBindingIndex = R_MaterialResourceTable_FindTextureBindingIndex( record, semantic );
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

static bool R_MaterialResourceTable_AddRecordFromSource( const materialResourceRecord_t &sourceRecord, int sourceIndex, bool scanMaterialStages ) {
	if ( rg_materialResourceTable.stats.records >= MATERIAL_RESOURCE_TABLE_MAX_RECORDS ) {
		rg_materialResourceTable.stats.overflow = true;
		R_MaterialResourceTable_SetStatus( "material table overflow" );
		return false;
	}

	materialResourceTableRecord_t &record = rg_materialResourceTable.records[rg_materialResourceTable.stats.records];
	memset( &record, 0, sizeof( record ) );
	record.tableIndex = rg_materialResourceTable.stats.records;
	record.sourceMaterialRecordIndex = sourceIndex;
	record.materialId = sourceRecord.material != NULL ? sourceRecord.material->Index() : -1;
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
	record.castsShadow = sourceRecord.material != NULL ? sourceRecord.material->SurfaceCastsShadow() : false;
	record.twoSided = sourceRecord.material != NULL ? ( sourceRecord.material->GetCullType() == CT_TWO_SIDED || sourceRecord.material->ShouldCreateBackSides() ) : false;
	record.alphaTest = sourceRecord.permutation.alphaMode == MC_PERFORATED || record.blendMode == MATERIAL_RESOURCE_BLEND_ALPHA_TEST;
	record.alphaTestMode = 0;
	record.alphaTestRegister = 0;
	record.needsCurrentRender = sourceRecord.material != NULL && sourceRecord.material->TestMaterialFlag( MF_NEED_CURRENT_RENDER );
	record.hasGui = sourceRecord.material != NULL && sourceRecord.material->HasGui();
	record.hasSubview = sourceRecord.material != NULL && sourceRecord.material->HasSubview();
	record.hasMaterialPolygonOffset = sourceRecord.material != NULL && sourceRecord.material->TestMaterialFlag( MF_POLYGONOFFSET );
	record.polygonOffset = sourceRecord.material != NULL ? sourceRecord.material->GetPolygonOffset() : 0.0f;
	record.shadowAlphaBindingIndex = -1;
	record.registerStart = 0;
	record.registerCount = sourceRecord.material != NULL ? sourceRecord.material->GetNumRegisters() : 0;
	record.fallbackReason = MATERIAL_RESOURCE_FALLBACK_NONE;

	if ( scanMaterialStages ) {
		R_MaterialResourceTable_ScanMaterialStages( record, sourceRecord );
		R_MaterialResourceTable_AddSourceImages( record, sourceRecord );
	} else if ( sourceRecord.material == NULL ) {
		R_MaterialResourceTable_AddSourceImages( record, sourceRecord );
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_MISSING_MATERIAL, MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_MATERIAL );
	} else {
		R_MaterialResourceTable_AddSourceImages( record, sourceRecord );
	}
	if ( R_MaterialResourceTable_RecordNeedsSurfaceImage( record )
		&& !record.hasDiffuse
		&& !record.hasEmissive
		&& !record.hasGui
		&& !record.hasPostProcess ) {
		record.hasMissingImage = true;
		R_MaterialResourceTable_AddFallback( record, MATERIAL_RESOURCE_FALLBACK_MISSING_IMAGE, MATERIAL_RESOURCE_FALLBACK_FLAG_MISSING_IMAGE );
		rg_materialResourceTable.stats.missingImages++;
	}
	if ( !scanMaterialStages ) {
		record.shadowCasterSupported = record.castsShadow && record.fallbackReason == MATERIAL_RESOURCE_FALLBACK_NONE;
		record.shadowFallbackFlags = record.fallbackFlags;
	}
	R_MaterialResourceTable_FinalizeShadowContract( record );
	R_MaterialResourceTable_FinalizeRegisterRange( record );
	R_MaterialResourceTable_CountClass( record );
	R_MaterialResourceTable_CountFallbacks( record );

	rg_materialResourceTable.stats.records++;
	return true;
}

static void R_MaterialResourceTable_ResetFrameStats( void ) {
	const bool initialized = rg_materialResourceTable.stats.initialized;
	const bool available = rg_materialResourceTable.stats.available;
	const bool bindlessSupported = rg_materialResourceTable.stats.bindlessSupported;
	const bool textureArraysSupported = rg_materialResourceTable.stats.textureArraysSupported;
	const bool textureViewsSupported = rg_materialResourceTable.stats.textureViewsSupported;
	memset( rg_materialResourceTable.records, 0, sizeof( rg_materialResourceTable.records ) );
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
	rg_materialResourceTable.caps = caps;
	rg_materialResourceTable.features = features;
	rg_materialResourceTable.maxClassicTextureUnits = caps.maxTextureImageUnits > 0 ? Min( caps.maxTextureImageUnits, MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS ) : MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS;
	rg_materialResourceTable.stats.initialized = true;
	rg_materialResourceTable.stats.available = features.scenePackets;
	rg_materialResourceTable.stats.bindlessSupported = features.bindlessTextures && caps.hasBindlessTexture;
	rg_materialResourceTable.stats.bindlessEnabled = false;
	rg_materialResourceTable.stats.textureArraysSupported = caps.hasTextureArrays;
	rg_materialResourceTable.stats.textureViewsSupported = features.gpuDriven && caps.hasTextureViews;
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
	rg_materialResourceTable.stats.sourceMaterialRecords = packetFrame.NumMaterialRecords();
	for ( int i = 0; i < packetFrame.NumDrawPackets(); ++i ) {
		if ( packetFrame.DrawPacket( i ).materialRecordIndex >= 0 ) {
			rg_materialResourceTable.stats.drawPacketReferences++;
		}
	}
	if ( !rg_materialResourceTable.stats.available ) {
		return;
	}
	for ( int i = 0; i < packetFrame.NumMaterialRecords(); ++i ) {
		R_MaterialResourceTable_AddRecordFromSource( packetFrame.MaterialRecord( i ), i, true );
	}
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

const materialResourceTableRecord_t *R_MaterialResourceTable_FindRecordForMaterial( const idMaterial *material ) {
	if ( material == NULL ) {
		return NULL;
	}
	for ( int i = 0; i < rg_materialResourceTable.stats.records; ++i ) {
		if ( rg_materialResourceTable.records[i].material == material ) {
			return &rg_materialResourceTable.records[i];
		}
	}
	return NULL;
}

void R_MaterialResourceTable_PrintGfxInfo( void ) {
	const materialResourceTableStats_t &stats = R_MaterialResourceTable_Stats();
	common->Printf(
		"Material resource table: initialized=%d available=%d prepared=%d records=%d source=%d draws=%d textures=%d classic=%d arrays=%d views=%d bindless=%d/%d fallback=%d missing=%d unsupported=%d debugTrunc=%d source='%s' status='%s'\n",
		stats.initialized ? 1 : 0,
		stats.available ? 1 : 0,
		stats.prepared ? 1 : 0,
		stats.records,
		stats.sourceMaterialRecords,
		stats.drawPacketReferences,
		stats.textureBindings,
		stats.classicTextureBindings,
		stats.textureArrayDescriptors,
		stats.textureViewDescriptors,
		stats.bindlessEnabled ? 1 : 0,
		stats.bindlessSupported ? 1 : 0,
		stats.fallbackRecords,
		stats.missingImages,
		stats.unsupportedFeatures,
		stats.debugStringTruncations,
		stats.debugStringTruncationSource,
		stats.lastFailure );
}

void R_MaterialResourceTable_DumpLatest( void ) {
	const materialResourceTableStats_t &stats = R_MaterialResourceTable_Stats();
	common->Printf(
		"MaterialResourceTable dump: prepared=%d available=%d records=%d source=%d draws=%d textures=%d fallback=%d missing=%d defaulted=%d unsupported=%d debugTrunc=%d source='%s' status='%s'\n",
		stats.prepared ? 1 : 0,
		stats.available ? 1 : 0,
		stats.records,
		stats.sourceMaterialRecords,
		stats.drawPacketReferences,
		stats.textureBindings,
		stats.fallbackRecords,
		stats.missingImages,
		stats.defaultedImages,
		stats.unsupportedFeatures,
		stats.debugStringTruncations,
		stats.debugStringTruncationSource,
		stats.lastFailure );
	for ( int i = 0; i < stats.records; ++i ) {
		const materialResourceTableRecord_t &record = rg_materialResourceTable.records[i];
		common->Printf(
			"  material[%d] source=%d id=%d name='%s' class=%s blend=%s sort=%s/%.2f regs=%d+%d stageRegs=%d+%d stages=%d/%d alpha=%d textures=%d fallback=%s flags=0x%x current=%d gui=%d subview=%d light=%d shadow=%d/%d matrix=%d vertexColor=%d offset=%.2f twoSided=%d shadowFlags=0x%x\n",
			record.tableIndex,
			record.sourceMaterialRecordIndex,
			record.materialId,
			record.materialName,
			RendererMaterialClass_Name( record.materialClass ),
			MaterialResourceBlendMode_Name( record.blendMode ),
			R_MaterialResourceTable_SortGroupName( record.sortGroup ),
			record.sortValue,
			record.registerStart,
			record.registerCount,
			record.stageRegisterStart,
			record.stageRegisterCount,
			record.evaluatedStageCount,
			record.stageCount,
			record.alphaTest ? 1 : 0,
			record.textureBindingCount,
			MaterialResourceFallbackReason_Name( record.fallbackReason ),
			record.fallbackFlags,
			record.needsCurrentRender ? 1 : 0,
			record.hasGui ? 1 : 0,
			record.hasSubview ? 1 : 0,
			record.receivesLighting ? 1 : 0,
			record.castsShadow ? 1 : 0,
			record.shadowCasterSupported ? 1 : 0,
			record.hasTextureMatrix ? 1 : 0,
			record.hasVertexColor ? 1 : 0,
			record.polygonOffset,
			record.twoSided ? 1 : 0,
			record.shadowFallbackFlags );
		for ( int bindingIndex = 0; bindingIndex < record.textureBindingCount; ++bindingIndex ) {
			const materialResourceTextureBinding_t &binding = record.textures[bindingIndex];
			common->Printf(
				"    tex[%d] semantic=%s unit=%d image='%s' handle=%u loaded=%d defaulted=%d filter=%d repeat=%d stage=%d regs=%d+%d cond=%d alpha=%d texgen=%d matrix=%d vcolor=%d blend=%d depthWrite=%d offset=%.2f array=%d view=%d bindless=%d\n",
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
				binding.textureViewCandidate ? 1 : 0,
				binding.bindlessEnabled ? 1 : 0 );
		}
	}
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

	const materialResourceTableStats_t &stats = R_MaterialResourceTable_Stats();
	if ( stats.records != 6
		|| stats.opaqueRecords != 2
		|| stats.perforatedRecords != 1
		|| stats.translucentRecords != 1
		|| stats.guiRecords != 1
		|| stats.postProcessRecords != 1
		|| stats.alphaTestRecords != 1
		|| stats.fallbackMissingImage <= 0 ) {
		common->Printf(
			"RendererMaterialResourceTable self-test failed: synthetic counts records=%d opaque=%d perforated=%d translucent=%d gui=%d post=%d alpha=%d missingFallback=%d\n",
			stats.records,
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
	return true;
}

bool RendererMaterialResourceTable_RunSelfTest( void ) {
	if ( !rg_materialResourceTable.stats.initialized || !rg_materialResourceTable.stats.available ) {
		common->Printf( "RendererMaterialResourceTable self-test skipped: material resource table unavailable\n" );
		return true;
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
	const int expectedRecords = tr.defaultMaterial != NULL ? 1 : 0;
	if ( stats.sourceMaterialRecords != packetFrame.NumMaterialRecords()
		|| stats.records != expectedRecords
		|| stats.drawPacketReferences != ( tr.defaultMaterial != NULL ? packetFrame.NumDrawPackets() : 0 )
		|| stats.overflow ) {
		common->Printf(
			"RendererMaterialResourceTable self-test failed: packet build mismatch source=%d/%d records=%d expected=%d drawRefs=%d overflow=%d\n",
			stats.sourceMaterialRecords,
			packetFrame.NumMaterialRecords(),
			stats.records,
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
		"RendererMaterialResourceTable self-test passed (records=%d textures=%d fallback=%d missing=%d arrays=%d views=%d bindless=%d/%d)\n",
		stats.records,
		stats.textureBindings,
		stats.fallbackRecords,
		stats.missingImages,
		stats.textureArrayDescriptors,
		stats.textureViewDescriptors,
		stats.bindlessEnabled ? 1 : 0,
		stats.bindlessSupported ? 1 : 0 );
	return true;
}
