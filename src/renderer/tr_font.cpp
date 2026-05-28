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

namespace {

enum q4FontSlotIndex_t {
	Q4_FONT_SLOT_SMALL = 0,
	Q4_FONT_SLOT_MEDIUM,
	Q4_FONT_SLOT_LARGE,
	Q4_FONT_SLOT_COUNT
};

enum q4FontSlotMask_t {
	Q4_FONT_SLOT_MASK_NONE = 0,
	Q4_FONT_SLOT_MASK_SMALL = 1 << Q4_FONT_SLOT_SMALL,
	Q4_FONT_SLOT_MASK_MEDIUM = 1 << Q4_FONT_SLOT_MEDIUM,
	Q4_FONT_SLOT_MASK_LARGE = 1 << Q4_FONT_SLOT_LARGE,
	Q4_FONT_SLOT_MASK_SMALL_MEDIUM = Q4_FONT_SLOT_MASK_SMALL | Q4_FONT_SLOT_MASK_MEDIUM,
	Q4_FONT_SLOT_MASK_SMALL_LARGE = Q4_FONT_SLOT_MASK_SMALL | Q4_FONT_SLOT_MASK_LARGE,
	Q4_FONT_SLOT_MASK_MEDIUM_LARGE = Q4_FONT_SLOT_MASK_MEDIUM | Q4_FONT_SLOT_MASK_LARGE
};

struct q4FontSlotSpec_t {
	q4FontSlotIndex_t	slot;
	int					pointSize;
	q4FontSlotMask_t	mask;
};

struct q4FontSlotExtents_t {
	float				maxWidth;
	float				maxHeight;
};

static const q4FontSlotSpec_t Q4_FONT_SLOT_SPECS[Q4_FONT_SLOT_COUNT] = {
	{ Q4_FONT_SLOT_SMALL, 12, Q4_FONT_SLOT_MASK_SMALL },
	{ Q4_FONT_SLOT_MEDIUM, 24, Q4_FONT_SLOT_MASK_MEDIUM },
	{ Q4_FONT_SLOT_LARGE, 48, Q4_FONT_SLOT_MASK_LARGE }
};

static const int Q4_FONTDAT_GLYPH_METRIC_FLOAT_COUNT = 9;
static const int Q4_FONTDAT_SCALAR_FLOAT_COUNT = 4;
static const int Q4_FONTDAT_SERIALIZED_MATERIAL_PTR_BYTES = 4;
static const int QUAKE4_FONTDAT_SIZE =
	GLYPHS_PER_FONT * Q4_FONTDAT_GLYPH_METRIC_FLOAT_COUNT * sizeof( float ) +
	Q4_FONTDAT_SCALAR_FLOAT_COUNT * sizeof( float ) +
	Q4_FONTDAT_SERIALIZED_MATERIAL_PTR_BYTES;

static fontInfo_t *R_FontSlotForIndex( fontInfoEx_t &font, q4FontSlotIndex_t slot ) {
	if ( slot == Q4_FONT_SLOT_SMALL ) {
		return &font.fontInfoSmall;
	}
	if ( slot == Q4_FONT_SLOT_MEDIUM ) {
		return &font.fontInfoMedium;
	}
	return &font.fontInfoLarge;
}

static const fontInfo_t *R_FontSlotForIndex( const fontInfoEx_t &font, q4FontSlotIndex_t slot ) {
	if ( slot == Q4_FONT_SLOT_SMALL ) {
		return &font.fontInfoSmall;
	}
	if ( slot == Q4_FONT_SLOT_MEDIUM ) {
		return &font.fontInfoMedium;
	}
	return &font.fontInfoLarge;
}

static void R_ClearFontSlotExtents( q4FontSlotExtents_t &extents ) {
	extents.maxWidth = 0.0f;
	extents.maxHeight = 0.0f;
}

static void R_AccumulateFontSlotExtents( const glyphInfo_t &glyph, q4FontSlotExtents_t &extents ) {
	if ( glyph.width > extents.maxWidth ) {
		extents.maxWidth = glyph.width;
	}
	if ( glyph.height > extents.maxHeight ) {
		extents.maxHeight = glyph.height;
	}
}

static bool R_ReadFontGlyph( idFile *fontFile, glyphInfo_t &glyph ) {
	return fontFile->ReadFloat( glyph.width ) == sizeof( float ) &&
		fontFile->ReadFloat( glyph.height ) == sizeof( float ) &&
		fontFile->ReadFloat( glyph.horiAdvance ) == sizeof( float ) &&
		fontFile->ReadFloat( glyph.horiBearingX ) == sizeof( float ) &&
		fontFile->ReadFloat( glyph.horiBearingY ) == sizeof( float ) &&
		fontFile->ReadFloat( glyph.s ) == sizeof( float ) &&
		fontFile->ReadFloat( glyph.t ) == sizeof( float ) &&
		fontFile->ReadFloat( glyph.s2 ) == sizeof( float ) &&
		fontFile->ReadFloat( glyph.t2 ) == sizeof( float );
}

static bool R_ReadFontSlotMetrics( idFile *fontFile, fontInfo_t &fontSlot, q4FontSlotExtents_t &extents ) {
	R_ClearFontSlotExtents( extents );

	for ( int glyphIndex = 0; glyphIndex < GLYPHS_PER_FONT; ++glyphIndex ) {
		glyphInfo_t &glyph = fontSlot.glyphs[glyphIndex];
		if ( !R_ReadFontGlyph( fontFile, glyph ) ) {
			return false;
		}
		R_AccumulateFontSlotExtents( glyph, extents );
	}

	return fontFile->ReadFloat( fontSlot.pointSize ) == sizeof( float ) &&
		fontFile->ReadFloat( fontSlot.fontHeight ) == sizeof( float ) &&
		fontFile->ReadFloat( fontSlot.ascender ) == sizeof( float ) &&
		fontFile->ReadFloat( fontSlot.descender ) == sizeof( float );
}

static void R_SetFontSlotMaxExtents( fontInfoEx_t &font, q4FontSlotIndex_t slot, const q4FontSlotExtents_t &extents ) {
	if ( slot == Q4_FONT_SLOT_SMALL ) {
		font.maxWidthSmall = extents.maxWidth;
		font.maxHeightSmall = extents.maxHeight;
	} else if ( slot == Q4_FONT_SLOT_MEDIUM ) {
		font.maxWidthMedium = extents.maxWidth;
		font.maxHeightMedium = extents.maxHeight;
	} else {
		font.maxWidthLarge = extents.maxWidth;
		font.maxHeightLarge = extents.maxHeight;
	}
}

static q4FontSlotExtents_t R_FontSlotExtentsForIndex( const fontInfoEx_t &font, q4FontSlotIndex_t slot ) {
	q4FontSlotExtents_t extents;
	if ( slot == Q4_FONT_SLOT_SMALL ) {
		extents.maxWidth = font.maxWidthSmall;
		extents.maxHeight = font.maxHeightSmall;
	} else if ( slot == Q4_FONT_SLOT_MEDIUM ) {
		extents.maxWidth = font.maxWidthMedium;
		extents.maxHeight = font.maxHeightMedium;
	} else {
		extents.maxWidth = font.maxWidthLarge;
		extents.maxHeight = font.maxHeightLarge;
	}
	return extents;
}

static void R_CopyFontSlot( fontInfoEx_t &font, q4FontSlotIndex_t dstIndex, q4FontSlotIndex_t srcIndex ) {
	fontInfo_t *dst = R_FontSlotForIndex( font, dstIndex );
	const fontInfo_t *src = R_FontSlotForIndex( font, srcIndex );
	const q4FontSlotExtents_t extents = R_FontSlotExtentsForIndex( font, srcIndex );

	memcpy( dst, src, sizeof( *dst ) );
	R_SetFontSlotMaxExtents( font, dstIndex, extents );
}

static void R_PropagateMissingFontSizes( fontInfoEx_t &font, int foundMask ) {
	switch ( foundMask ) {
		case Q4_FONT_SLOT_MASK_SMALL:
			R_CopyFontSlot( font, Q4_FONT_SLOT_MEDIUM, Q4_FONT_SLOT_SMALL );
			R_CopyFontSlot( font, Q4_FONT_SLOT_LARGE, Q4_FONT_SLOT_SMALL );
			break;
		case Q4_FONT_SLOT_MASK_MEDIUM:
			R_CopyFontSlot( font, Q4_FONT_SLOT_SMALL, Q4_FONT_SLOT_MEDIUM );
			R_CopyFontSlot( font, Q4_FONT_SLOT_LARGE, Q4_FONT_SLOT_MEDIUM );
			break;
		case Q4_FONT_SLOT_MASK_SMALL_MEDIUM:
			R_CopyFontSlot( font, Q4_FONT_SLOT_LARGE, Q4_FONT_SLOT_MEDIUM );
			break;
		case Q4_FONT_SLOT_MASK_LARGE:
			R_CopyFontSlot( font, Q4_FONT_SLOT_SMALL, Q4_FONT_SLOT_LARGE );
			R_CopyFontSlot( font, Q4_FONT_SLOT_MEDIUM, Q4_FONT_SLOT_LARGE );
			break;
		case Q4_FONT_SLOT_MASK_SMALL_LARGE:
			R_CopyFontSlot( font, Q4_FONT_SLOT_MEDIUM, Q4_FONT_SLOT_LARGE );
			break;
		case Q4_FONT_SLOT_MASK_MEDIUM_LARGE:
			R_CopyFontSlot( font, Q4_FONT_SLOT_SMALL, Q4_FONT_SLOT_MEDIUM );
			break;
		default:
			break;
	}
}

static bool R_LoadFontSlot( fontInfoEx_t &font, const char *fontName, const q4FontSlotSpec_t &slotSpec ) {
	idStr fontDataName = va( "%s_%i.fontdat", fontName, slotSpec.pointSize );
	if ( fileSystem->ReadFile( fontDataName.c_str(), NULL, NULL ) != QUAKE4_FONTDAT_SIZE ) {
		return false;
	}

	idFile *fontFile = fileSystem->OpenFileRead( fontDataName.c_str(), true );
	if ( fontFile == NULL ) {
		return false;
	}

	fontInfo_t *fontSlot = R_FontSlotForIndex( font, slotSpec.slot );
	q4FontSlotExtents_t extents;
	const bool readMetrics = R_ReadFontSlotMetrics( fontFile, *fontSlot, extents );

	int serializedMaterialPointer = 0;
	const bool readPointer = fontFile->ReadInt( serializedMaterialPointer ) == sizeof( int );
	fileSystem->CloseFile( fontFile );

	if ( !readMetrics || !readPointer ) {
		return false;
	}

	idStr::Copynz( fontSlot->name, fontDataName.c_str(), sizeof( fontSlot->name ) );
	fontSlot->material = declManager->FindMaterial( fontDataName.c_str() );
	if ( fontSlot->material != NULL ) {
		fontSlot->material->SetSort( SS_GUI );
	}
	R_SetFontSlotMaxExtents( font, slotSpec.slot, extents );
	return true;
}

}

/*
============
RegisterFont

Loads 3 point sizes, 12, 24, and 48
============
*/
bool idRenderSystemLocal::RegisterFont( const char *fontName, fontInfoEx_t &font ) {
	memset( &font, 0, sizeof( font ) );

	int foundMask = Q4_FONT_SLOT_MASK_NONE;
	for ( int slotIndex = 0; slotIndex < Q4_FONT_SLOT_COUNT; ++slotIndex ) {
		const q4FontSlotSpec_t &slotSpec = Q4_FONT_SLOT_SPECS[slotIndex];
		if ( R_LoadFontSlot( font, fontName, slotSpec ) ) {
			foundMask |= slotSpec.mask;
		}
	}

	R_PropagateMissingFontSizes( font, foundMask );

	if ( foundMask == Q4_FONT_SLOT_MASK_NONE ) {
		common->Warning( "RegisterFont: couldn't find font: '%s'", fontName );
		return false;
	}

	return true;
}

/*
============
R_InitFreeType
============
*/
void R_InitFreeType( void ) {
//	registeredFontCount = 0;
}

/*
============
R_DoneFreeType
============
*/
void R_DoneFreeType( void ) {
//	registeredFontCount = 0;
}
