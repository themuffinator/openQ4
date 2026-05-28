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




#include "DeviceContext.h"
#include "UserInterface.h"
#include "../renderer/tr_local.h"

idVec4 idDeviceContext::colorPurple;
idVec4 idDeviceContext::colorOrange;
idVec4 idDeviceContext::colorYellow;
idVec4 idDeviceContext::colorGreen;
idVec4 idDeviceContext::colorBlue;
idVec4 idDeviceContext::colorRed;
idVec4 idDeviceContext::colorBlack;
idVec4 idDeviceContext::colorWhite;
idVec4 idDeviceContext::colorNone;


idCVar gui_smallFontLimit( "gui_smallFontLimit", "0.30", CVAR_GUI | CVAR_ARCHIVE, "" );
idCVar gui_mediumFontLimit( "gui_mediumFontLimit", "0.60", CVAR_GUI | CVAR_ARCHIVE, "" );


idList<fontInfoEx_t> idDeviceContext::fonts;

namespace {

static const float Q4_GUI_FONT_BASE_POINT_SIZE = 48.0f;
static const float Q4_TEXT_BRIGHTNESS_STEP = 0.1f;
static const float Q4_TEXT_RGB_ESCAPE_SCALE = 1.0f / 9.0f;
static const float Q4_TEXT_OUTLINE_DARK_THRESHOLD = 0.2f;
static const float Q4_TEXT_STYLE_OFFSET = 1.0f;
static const float Q4_TEXT_LINE_SPACING_SCALE = 1.25f;
static const int Q4_TEXT_STYLE_SHADOW = 1;
static const int Q4_TEXT_STYLE_OUTLINE = 2;
static const int Q4_TEXT_ALIGN_VERTICAL_CENTER = 3;
static const int Q4_TEXT_CURSOR_NONE = -1;
static const int Q4_TEXT_LINE_BUFFER_SIZE = 1024;
static const int Q4_TEXT_REPEAT_ESCAPE_MAX = 9;
static const unsigned char Q4_INSERT_CURSOR_GLYPH = '|';
static const unsigned char Q4_OVERSTRIKE_CURSOR_GLYPH = '_';
static const unsigned char Q4_EMBEDDED_ICON_REFERENCE_GLYPH = 'W';

static int OpenQ4_TextEscapeLength( const char *text, int *type = NULL ) {
	return idStr::IsEscape( text, type );
}

static bool OpenQ4_IsRepeatTextEscape( const char *escape, int escapeLength ) {
	return escapeLength > 2 && escape != NULL && ( escape[1] == 'N' || escape[1] == 'n' );
}

static int OpenQ4_TextEscapeRepeatCount( const char *escape ) {
	int repeats = static_cast<unsigned char>( escape[2] ) - '0';
	if ( repeats < 0 ) {
		repeats = 0;
	} else if ( repeats >= Q4_TEXT_REPEAT_ESCAPE_MAX ) {
		repeats = Q4_TEXT_REPEAT_ESCAPE_MAX;
	}
	return repeats;
}

struct q4ScaledFont_t {
	const fontInfo_t *font;
	float renderScale;
	float maxWidth;
	float maxHeight;
};

static bool OpenQ4_ExtractIconCode( const char *escape, char code[4] ) {
	int escapeType = 0;
	if ( OpenQ4_TextEscapeLength( escape, &escapeType ) != 5 || escapeType != S_ESCAPE_ICON ) {
		return false;
	}

	code[0] = escape[2];
	code[1] = escape[3];
	code[2] = escape[4];
	code[3] = '\0';
	return true;
}

static float OpenQ4_FontRenderScale( const fontInfo_t *font, float scale ) {
	if ( font == NULL || font->pointSize == 0.0f ) {
		return 0.0f;
	}
	return scale / font->pointSize * Q4_GUI_FONT_BASE_POINT_SIZE;
}

static int OpenQ4_ScaledFontUnits( float fontScale, float units ) {
	return static_cast<int>( fontScale * units );
}

static int OpenQ4_RoundedGlyphAdvance( const glyphInfo_t *glyph ) {
	return static_cast<int>( idMath::Ceil( glyph->horiAdvance ) );
}

static int OpenQ4_GlyphAdvanceUnits( const glyphInfo_t *glyph, int adjust ) {
	return adjust + OpenQ4_RoundedGlyphAdvance( glyph );
}

static int OpenQ4_GlyphHeightUnits( const glyphInfo_t *glyph ) {
	return static_cast<int>( glyph->height );
}

static int OpenQ4_EmbeddedIconWidthUnits( float iconWidth, float iconHeight, float referenceHeight ) {
	if ( referenceHeight <= 0.0f || iconWidth <= 0.0f || iconHeight <= 0.0f ) {
		return 0;
	}
	return static_cast<int>( iconWidth * ( referenceHeight / iconHeight ) );
}

static float OpenQ4_ScaledGlyphAdvance( float fontScale, const glyphInfo_t *glyph, float adjust ) {
	return idMath::Ceil( ( glyph->horiAdvance + adjust ) * fontScale );
}

static float OpenQ4_GlyphDrawX( float x, float fontScale, const glyphInfo_t *glyph ) {
	return x + fontScale * glyph->horiBearingX;
}

static float OpenQ4_GlyphDrawY( float y, float fontScale, const glyphInfo_t *glyph ) {
	return y - ( fontScale * glyph->horiBearingY - 1.0f );
}

static bool OpenQ4_HasRenderableFont( const q4ScaledFont_t &scaledFont ) {
	return scaledFont.font != NULL && scaledFont.renderScale != 0.0f && scaledFont.font->material != NULL;
}

static bool OpenQ4_TextCursorReached( int cursor, int count ) {
	return cursor != Q4_TEXT_CURSOR_NONE && cursor <= count;
}

static bool OpenQ4_IsLineBreakChar( char c ) {
	return c == '\n' || c == '\r' || c == '\0';
}

static const char *OpenQ4_SkipPairedLineBreak( const char *text ) {
	if ( ( *text == '\n' && text[1] == '\r' ) || ( *text == '\r' && text[1] == '\n' ) ) {
		return text + 1;
	}
	return text;
}

static bool OpenQ4_ShouldCaptureBreak( bool lineBreak, bool wrap, char c ) {
	return lineBreak || ( wrap && ( c == ' ' || c == '\t' ) );
}

static float OpenQ4_InitialTextBaseline( idRectangle &rect, int &textAlign, float lineHeight ) {
	if ( textAlign == Q4_TEXT_ALIGN_VERTICAL_CENTER ) {
		textAlign = idDeviceContext::ALIGN_LEFT;
		return rect.y + rect.h * 0.5f + lineHeight * 0.5f;
	}
	return rect.y + lineHeight;
}

static float OpenQ4_AlignedTextX( const idRectangle &rect, int textAlign, int textWidth ) {
	if ( textAlign == idDeviceContext::ALIGN_RIGHT ) {
		return rect.x + rect.w - textWidth;
	}
	if ( textAlign == idDeviceContext::ALIGN_CENTER ) {
		return rect.x + ( rect.w - textWidth ) * 0.5f;
	}
	return rect.x;
}

static void OpenQ4_SetGuiSortForFont( fontInfoEx_t &font ) {
	if ( font.fontInfoSmall.material != NULL ) {
		font.fontInfoSmall.material->SetSort( SS_GUI );
	}
	if ( font.fontInfoMedium.material != NULL ) {
		font.fontInfoMedium.material->SetSort( SS_GUI );
	}
	if ( font.fontInfoLarge.material != NULL ) {
		font.fontInfoLarge.material->SetSort( SS_GUI );
	}
}

}

int idDeviceContext::FindFont( const char *name ) {
	int c = fonts.Num();

	for (int i = 0; i < c; i++) {
		if (idStr::Icmp(name, fonts[i].name) == 0) {
			OpenQ4_SetGuiSortForFont( fonts[i] );
			return i;
		}
	}

	// If the font was not found, try to register it
	idStr fileName = name;
	if ( idStr::Icmp( fileName.c_str(), "fonts" ) == 0 ) {
		fileName = "fonts/chain";
	}
	fileName.Replace("fonts", va("fonts/%s", fontLang.c_str()) );

	fontInfoEx_t fontInfo;
	int index = fonts.Append( fontInfo );
	if ( renderSystem->RegisterFont( fileName, fonts[index] ) ) {
		idStr::Copynz( fonts[index].name, name, sizeof( fonts[index].name ) );
		return index;
	} else {
		common->Printf( "Could not register font %s [%s]\n", name, fileName.c_str() );
		return -1;
	}
}

void idDeviceContext::SetupFonts() {
	fonts.SetGranularity( 1 );

	fontLang = cvarSystem->GetCVarString( "sys_lang" );
	
	// western european languages can use the english font
	if ( fontLang == "french" || fontLang == "german" || fontLang == "spanish" || fontLang == "italian" ) {
		fontLang = "english";
	}

	// Default font has to be added first.
	FindFont( "fonts/chain" );
}

void idDeviceContext::SetFont( int num ) {
	if ( fonts.Num() == 0 ) {
		activeFont = NULL;
		return;
	}
	if ( num >= 0 && num < fonts.Num() ) {
		activeFont = &fonts[num];
	} else {
		activeFont = &fonts[0];
	}
}

void idDeviceContext::SizeIcon( embeddedIcon_t &icon ) {
	if ( icon.material == NULL ) {
		return;
	}

	const float imageWidth = static_cast<float>( icon.material->GetImageWidth() );
	const float imageHeight = static_cast<float>( icon.material->GetImageHeight() );
	if ( imageWidth <= 0.0f || imageHeight <= 0.0f ) {
		icon.width = 0.0f;
		icon.height = 0.0f;
		return;
	}

	const float x = icon.s1;
	const float y = icon.t1;
	float width = icon.width;
	float height = icon.height;

	if ( width < 0.0f ) {
		width = imageWidth - Max( x, 0.0f );
	}
	if ( height < 0.0f ) {
		height = imageHeight - Max( y, 0.0f );
	}

	icon.s1 = ( x < 0.0f ) ? 0.0f : ( x / imageWidth );
	icon.t1 = ( y < 0.0f ) ? 0.0f : ( y / imageHeight );
	icon.s2 = ( x < 0.0f ) ? 1.0f : ( ( x + width ) / imageWidth );
	icon.t2 = ( y < 0.0f ) ? 1.0f : ( ( y + height ) / imageHeight );
	icon.width = width;
	icon.height = height;
}

bool idDeviceContext::FindIcon( const char *code, const embeddedIcon_t **icon ) const {
	embeddedIcon_t *foundIcon = NULL;
	const bool found = icons.Get( code, &foundIcon );
	if ( icon != NULL ) {
		*icon = foundIcon;
	}
	return found && foundIcon != NULL;
}

float idDeviceContext::GetIconDisplayWidth( const embeddedIcon_t &icon, float referenceHeight ) const {
	if ( referenceHeight <= 0.0f || icon.width <= 0.0f || icon.height <= 0.0f ) {
		return 0.0f;
	}
	return icon.width * ( referenceHeight / icon.height );
}

void idDeviceContext::RegisterIcon( const char *code, const char *shader, int x, int y, int w, int h ) {
	if ( code == NULL || shader == NULL || code[0] == '\0' || shader[0] == '\0' ) {
		return;
	}

	embeddedIcon_t icon;
	idStr::Copynz( icon.code, code, sizeof( icon.code ) );
	icon.material = declManager->FindMaterial( shader );
	if ( icon.material == NULL ) {
		return;
	}

	icon.material->SetSort( SS_GUI );
	icon.s1 = static_cast<float>( x );
	icon.t1 = static_cast<float>( y );
	icon.width = static_cast<float>( w );
	icon.height = static_cast<float>( h );
	SizeIcon( icon );
	icons.Set( icon.code, icon );
	idStr::RegisterIconEscapeCode( icon.code );
}

void idDeviceContext::RegisterBuiltinIcons() {
	static const struct {
		const char *code;
		const char *shader;
	} builtinIcons[] = {
		{ "vce", "gfx/guis/hud/icons/icon_speaker" },
		{ "vcd", "gfx/guis/hud/icons/icon_speaker_disabled" },
		{ "fde", "gfx/guis/hud/icons/icon_friend" },
		{ "fdd", "gfx/guis/hud/icons/icon_friend_disabled" },
		{ "flm", "gfx/guis/hud/icons/sb_flag_marine" },
		{ "fls", "gfx/guis/hud/icons/sb_flag_strogg" },
		{ "yrd", "gfx/guis/hud/icons/icon_ready" },
		{ "nrd", "gfx/guis/hud/icons/icon_notready" },
		{ "qad", "gfx/guis/hud/icons/item_quadkill_colored" },
		{ "ds0", "gfx/guis/mainmenu/icon_dedserver" },
		{ "dsp", "gfx/guis/mainmenu/icon_pb" },
		{ "sl0", "gfx/guis/mainmenu/icon_locked" },
		{ "sf0", "gfx/guis/mainmenu/icon_favorite" }
	};

	for ( int i = 0; i < static_cast<int>( sizeof( builtinIcons ) / sizeof( builtinIcons[0] ) ); ++i ) {
		RegisterIcon( builtinIcons[i].code, builtinIcons[i].shader );
	}
}


void idDeviceContext::Init() {
	xScale = 0.0;
	aspectCorrect = true;
	SetSize(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
	whiteImage = declManager->FindMaterial("gfx/guis/white");
	whiteImage->SetSort( SS_GUI );
	mbcs = false;
	SetupFonts();
	activeFont = fonts.Num() > 0 ? &fonts[0] : NULL;
	icons.Clear();
	idStr::ClearIconEscapeCodes();
	RegisterBuiltinIcons();
	colorPurple = idVec4(1, 0, 1, 1);
	colorOrange = idVec4(1, 1, 0, 1);
	colorYellow = idVec4(0, 1, 1, 1);
	colorGreen = idVec4(0, 1, 0, 1);
	colorBlue = idVec4(0, 0, 1, 1);
	colorRed = idVec4(1, 0, 0, 1);
	colorWhite = idVec4(1, 1, 1, 1);
	colorBlack = idVec4(0, 0, 0, 1);
	colorNone = idVec4(0, 0, 0, 0);
	cursorImages[CURSOR_ARROW] = declManager->FindMaterial("gfx/guis/guicursor_arrow");
	cursorImages[CURSOR_HAND] = declManager->FindMaterial("gfx/guis/guicursor_hand");
	scrollBarImages[SCROLLBAR_HBACK] = declManager->FindMaterial("gfx/guis/scrollbarh");
	scrollBarImages[SCROLLBAR_VBACK] = declManager->FindMaterial("gfx/guis/scrollbarv");
	scrollBarImages[SCROLLBAR_THUMB] = declManager->FindMaterial("gfx/guis/scrollbar_thumb");
	scrollBarImages[SCROLLBAR_RIGHT] = declManager->FindMaterial("gfx/guis/scrollbar_right");
	scrollBarImages[SCROLLBAR_LEFT] = declManager->FindMaterial("gfx/guis/scrollbar_left");
	scrollBarImages[SCROLLBAR_UP] = declManager->FindMaterial("gfx/guis/scrollbar_up");
	scrollBarImages[SCROLLBAR_DOWN] = declManager->FindMaterial("gfx/guis/scrollbar_down");
	cursorImages[CURSOR_ARROW]->SetSort( SS_GUI );
	cursorImages[CURSOR_HAND]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_HBACK]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_VBACK]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_THUMB]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_RIGHT]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_LEFT]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_UP]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_DOWN]->SetSort( SS_GUI );
	cursor = CURSOR_ARROW;
	enableClipping = true;
	overStrikeMode = true;
	drawTextColor = colorWhite;
	drawTextColorAdjust = 0.0f;
	mat.Identity();
	origin.Zero();
	initialized = true;
}

void idDeviceContext::Shutdown() {
	fontName.Clear();
	clipRects.Clear();
	fonts.Clear();
	Clear();
}

void idDeviceContext::Clear() {
	initialized = false;
	useFont = NULL;
	activeFont = NULL;
	mbcs = false;
	aspectCorrect = true;
	drawTextColor.Zero();
	drawTextColorAdjust = 0.0f;
	icons.Clear();
}

idDeviceContext::idDeviceContext() {
	Clear();
}

void idDeviceContext::SetTransformInfo(const idVec3 &org, const idMat3 &m) {
	origin = org;
	mat = m;
}

void idDeviceContext::SetAspectCorrection( bool enabled ) {
	aspectCorrect = enabled;
}

// 
//  added method
void idDeviceContext::GetTransformInfo(idVec3& org, idMat3& m )
{
	m = mat;
	org = origin;
}
// 

void idDeviceContext::PopClipRect() {
	if (clipRects.Num()) {
		clipRects.RemoveIndex(clipRects.Num()-1);
	}
}

void idDeviceContext::PushClipRect(idRectangle r) {
	clipRects.Append(r);
}

void idDeviceContext::PushClipRect(float x, float y, float w, float h) {
	clipRects.Append(idRectangle(x, y, w, h));
}

bool idDeviceContext::ClippedCoords(float *x, float *y, float *w, float *h, float *s1, float *t1, float *s2, float *t2) {

	if ( enableClipping == false || clipRects.Num() == 0 ) {
		return false;
	}

	int c = clipRects.Num();
	while( --c > 0 ) {
		idRectangle *clipRect = &clipRects[c];
 
		float ox = *x;
		float oy = *y;
		float ow = *w;
		float oh = *h;

		if ( ow <= 0.0f || oh <= 0.0f ) {
			break;
		}

		if (*x < clipRect->x) {
			*w -= clipRect->x - *x;
			*x = clipRect->x;
		} else if (*x > clipRect->x + clipRect->w) {
			*x = *w = *y = *h = 0;
		}
		if (*y < clipRect->y) {
			*h -= clipRect->y - *y;
			*y = clipRect->y;
		} else if (*y > clipRect->y + clipRect->h) {
			*x = *w = *y = *h = 0;
		}
		if (*w > clipRect->w) {
			*w = clipRect->w - *x + clipRect->x;
		} else if (*x + *w > clipRect->x + clipRect->w) {
			*w = clipRect->Right() - *x;
		}
		if (*h > clipRect->h) {
			*h = clipRect->h - *y + clipRect->y;
		} else if (*y + *h > clipRect->y + clipRect->h) {
			*h = clipRect->Bottom() - *y;
		}

		if ( s1 && s2 && t1 && t2 && ow > 0.0f ) {
			float ns1, ns2, nt1, nt2;
			// upper left
			float u = ( *x - ox ) / ow;
			ns1 = *s1 * ( 1.0f - u ) + *s2 * ( u );

			// upper right
			u = ( *x + *w - ox ) / ow;
			ns2 = *s1 * ( 1.0f - u ) + *s2 * ( u );

			// lower left
			u = ( *y - oy ) / oh;
			nt1 = *t1 * ( 1.0f - u ) + *t2 * ( u );

			// lower right
			u = ( *y + *h - oy ) / oh;
			nt2 = *t1 * ( 1.0f - u ) + *t2 * ( u );

			// set values
			*s1 = ns1;
			*s2 = ns2;
			*t1 = nt1;
			*t2 = nt2;
		}
	}

	return (*w == 0 || *h == 0) ? true : false;
}


void idDeviceContext::AdjustCoords(float *x, float *y, float *w, float *h) {
	if (x) {
		*x = (*x * xScale) + xOffset;
	}
	if (y) {
		*y = (*y * yScale) + yOffset;
	}
	if (w) {
		*w *= xScale;
	}
	if (h) {
		*h *= yScale;
	}
}

static ID_INLINE void TransformVertInVirtualSpace( idDrawVert &vert, const idVec3 &origin, const idMat3 &mat, float xScale, float yScale, float xOffset, float yOffset ) {
	if ( xScale == 0.0f || yScale == 0.0f ) {
		vert.xyz -= origin;
		vert.xyz *= mat;
		vert.xyz += origin;
		return;
	}

	// UI transforms are authored in virtual GUI space, so map to virtual space,
	// apply transform, then map back to the current draw-space viewport.
	idVec3 virtualPos = vert.xyz;
	virtualPos[0] = ( virtualPos[0] - xOffset ) / xScale;
	virtualPos[1] = ( virtualPos[1] - yOffset ) / yScale;
	virtualPos -= origin;
	virtualPos *= mat;
	virtualPos += origin;
	vert.xyz[0] = ( virtualPos[0] * xScale ) + xOffset;
	vert.xyz[1] = ( virtualPos[1] * yScale ) + yOffset;
	vert.xyz[2] = virtualPos[2];
}

void idDeviceContext::DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *shader) {
	idDrawVert verts[4];
	glIndex_t indexes[6];
	indexes[0] = 3;
	indexes[1] = 0;
	indexes[2] = 2;
	indexes[3] = 2;
	indexes[4] = 0;
	indexes[5] = 1;
	verts[0].xyz[0] = x;
	verts[0].xyz[1] = y;
	verts[0].xyz[2] = 0;
	verts[0].st[0] = s1;
	verts[0].st[1] = t1;
	verts[0].normal[0] = 0;
	verts[0].normal[1] = 0;
	verts[0].normal[2] = 1;
	verts[0].tangents[0][0] = 1;
	verts[0].tangents[0][1] = 0;
	verts[0].tangents[0][2] = 0;
	verts[0].tangents[1][0] = 0;
	verts[0].tangents[1][1] = 1;
	verts[0].tangents[1][2] = 0;
	verts[1].xyz[0] = x + w;
	verts[1].xyz[1] = y;
	verts[1].xyz[2] = 0;
	verts[1].st[0] = s2;
	verts[1].st[1] = t1;
	verts[1].normal[0] = 0;
	verts[1].normal[1] = 0;
	verts[1].normal[2] = 1;
	verts[1].tangents[0][0] = 1;
	verts[1].tangents[0][1] = 0;
	verts[1].tangents[0][2] = 0;
	verts[1].tangents[1][0] = 0;
	verts[1].tangents[1][1] = 1;
	verts[1].tangents[1][2] = 0;
	verts[2].xyz[0] = x + w;
	verts[2].xyz[1] = y + h;
	verts[2].xyz[2] = 0;
	verts[2].st[0] = s2;
	verts[2].st[1] = t2;
	verts[2].normal[0] = 0;
	verts[2].normal[1] = 0;
	verts[2].normal[2] = 1;
	verts[2].tangents[0][0] = 1;
	verts[2].tangents[0][1] = 0;
	verts[2].tangents[0][2] = 0;
	verts[2].tangents[1][0] = 0;
	verts[2].tangents[1][1] = 1;
	verts[2].tangents[1][2] = 0;
	verts[3].xyz[0] = x;
	verts[3].xyz[1] = y + h;
	verts[3].xyz[2] = 0;
	verts[3].st[0] = s1;
	verts[3].st[1] = t2;
	verts[3].normal[0] = 0;
	verts[3].normal[1] = 0;
	verts[3].normal[2] = 1;
	verts[3].tangents[0][0] = 1;
	verts[3].tangents[0][1] = 0;
	verts[3].tangents[0][2] = 0;
	verts[3].tangents[1][0] = 0;
	verts[3].tangents[1][1] = 1;
	verts[3].tangents[1][2] = 0;
	
	const bool ident = !mat.IsIdentity();
	if ( ident ) {
		for ( int i = 0; i < 4; i++ ) {
			TransformVertInVirtualSpace( verts[i], origin, mat, xScale, yScale, xOffset, yOffset );
		}
	}

	tr.DrawStretchPic( &verts[0], &indexes[0], 4, 6, shader, ident );
	
}


void idDeviceContext::DrawMaterial(float x, float y, float w, float h, const idMaterial *mat, const idVec4 &color, float scalex, float scaley) {

	renderSystem->SetColor(color);

	float	s0, s1, t0, t1;
// 
//  handle negative scales as well	
	if ( scalex < 0 )
	{
		w *= -1;
		scalex *= -1;
	}
	if ( scaley < 0 )
	{
		h *= -1;
		scaley *= -1;
	}
// 
	if( w < 0 ) {	// flip about vertical
		w  = -w;
		s0 = 1 * scalex;
		s1 = 0;
	}
	else {
		s0 = 0;
		s1 = 1 * scalex;
	}

	if( h < 0 ) {	// flip about horizontal
		h  = -h;
		t0 = 1 * scaley;
		t1 = 0;
	}
	else {
		t0 = 0;
		t1 = 1 * scaley;
	}

	if ( ClippedCoords( &x, &y, &w, &h, &s0, &t0, &s1, &t1 ) ) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);

	DrawStretchPic( x, y, w, h, s0, t0, s1, t1, mat);
}

void idDeviceContext::DrawMaterialRotated(float x, float y, float w, float h, const idMaterial *mat, const idVec4 &color, float scalex, float scaley, float angle) {
	
	renderSystem->SetColor(color);

	float	s0, s1, t0, t1;
	// 
	//  handle negative scales as well	
	if ( scalex < 0 )
	{
		w *= -1;
		scalex *= -1;
	}
	if ( scaley < 0 )
	{
		h *= -1;
		scaley *= -1;
	}
	// 
	if( w < 0 ) {	// flip about vertical
		w  = -w;
		s0 = 1 * scalex;
		s1 = 0;
	}
	else {
		s0 = 0;
		s1 = 1 * scalex;
	}

	if( h < 0 ) {	// flip about horizontal
		h  = -h;
		t0 = 1 * scaley;
		t1 = 0;
	}
	else {
		t0 = 0;
		t1 = 1 * scaley;
	}

	if ( angle == 0.0f && ClippedCoords( &x, &y, &w, &h, &s0, &t0, &s1, &t1 ) ) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);

	DrawStretchPicRotated( x, y, w, h, s0, t0, s1, t1, mat, angle);
}

void idDeviceContext::DrawStretchPicRotated(float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *shader, float angle) {
	
	idDrawVert verts[4];
	glIndex_t indexes[6];
	indexes[0] = 3;
	indexes[1] = 0;
	indexes[2] = 2;
	indexes[3] = 2;
	indexes[4] = 0;
	indexes[5] = 1;
	verts[0].xyz[0] = x;
	verts[0].xyz[1] = y;
	verts[0].xyz[2] = 0;
	verts[0].st[0] = s1;
	verts[0].st[1] = t1;
	verts[0].normal[0] = 0;
	verts[0].normal[1] = 0;
	verts[0].normal[2] = 1;
	verts[0].tangents[0][0] = 1;
	verts[0].tangents[0][1] = 0;
	verts[0].tangents[0][2] = 0;
	verts[0].tangents[1][0] = 0;
	verts[0].tangents[1][1] = 1;
	verts[0].tangents[1][2] = 0;
	verts[1].xyz[0] = x + w;
	verts[1].xyz[1] = y;
	verts[1].xyz[2] = 0;
	verts[1].st[0] = s2;
	verts[1].st[1] = t1;
	verts[1].normal[0] = 0;
	verts[1].normal[1] = 0;
	verts[1].normal[2] = 1;
	verts[1].tangents[0][0] = 1;
	verts[1].tangents[0][1] = 0;
	verts[1].tangents[0][2] = 0;
	verts[1].tangents[1][0] = 0;
	verts[1].tangents[1][1] = 1;
	verts[1].tangents[1][2] = 0;
	verts[2].xyz[0] = x + w;
	verts[2].xyz[1] = y + h;
	verts[2].xyz[2] = 0;
	verts[2].st[0] = s2;
	verts[2].st[1] = t2;
	verts[2].normal[0] = 0;
	verts[2].normal[1] = 0;
	verts[2].normal[2] = 1;
	verts[2].tangents[0][0] = 1;
	verts[2].tangents[0][1] = 0;
	verts[2].tangents[0][2] = 0;
	verts[2].tangents[1][0] = 0;
	verts[2].tangents[1][1] = 1;
	verts[2].tangents[1][2] = 0;
	verts[3].xyz[0] = x;
	verts[3].xyz[1] = y + h;
	verts[3].xyz[2] = 0;
	verts[3].st[0] = s1;
	verts[3].st[1] = t2;
	verts[3].normal[0] = 0;
	verts[3].normal[1] = 0;
	verts[3].normal[2] = 1;
	verts[3].tangents[0][0] = 1;
	verts[3].tangents[0][1] = 0;
	verts[3].tangents[0][2] = 0;
	verts[3].tangents[1][0] = 0;
	verts[3].tangents[1][1] = 1;
	verts[3].tangents[1][2] = 0;

	const bool ident = !mat.IsIdentity();
	if ( ident ) {
		for ( int i = 0; i < 4; i++ ) {
			TransformVertInVirtualSpace( verts[i], origin, mat, xScale, yScale, xOffset, yOffset );
		}
	}

	//Generate a translation so we can translate to the center of the image rotate and draw
	idVec3 origTrans;
	origTrans.x = x+(w/2);
	origTrans.y = y+(h/2);
	origTrans.z = 0;


	//Rotate the verts about the z axis before drawing them
	idMat4 rotz;
	rotz.Identity();
	float sinAng = idMath::Sin(angle);
	float cosAng = idMath::Cos(angle);
	rotz[0][0] = cosAng;
	rotz[0][1] = sinAng;
	rotz[1][0] = -sinAng;
	rotz[1][1] = cosAng;
	for(int i = 0; i < 4; i++) {
		//Translate to origin
		verts[i].xyz -= origTrans;

		//Rotate
		verts[i].xyz = rotz * verts[i].xyz;

		//Translate back
		verts[i].xyz += origTrans;
	}


	tr.DrawStretchPic( &verts[0], &indexes[0], 4, 6, shader, (angle == 0.0) ? false : true );
}

void idDeviceContext::DrawFilledRect( float x, float y, float w, float h, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	
	if (ClippedCoords(&x, &y, &w, &h, NULL, NULL, NULL, NULL)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic( x, y, w, h, 0, 0, 0, 0, whiteImage);
}


void idDeviceContext::DrawRect( float x, float y, float w, float h, float size, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	
	if (ClippedCoords(&x, &y, &w, &h, NULL, NULL, NULL, NULL)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic( x, y, size, h, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x + w - size, y, size, h, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x, y, w, size, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x, y + h - size, w, size, 0, 0, 0, 0, whiteImage );
}

void idDeviceContext::DrawMaterialRect( float x, float y, float w, float h, float size, const idMaterial *mat, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	DrawMaterial( x, y, size, h, mat, color );
	DrawMaterial( x + w - size, y, size, h, mat, color );
	DrawMaterial( x, y, w, size, mat, color );
	DrawMaterial( x, y + h - size, w, size, mat, color );
}


void idDeviceContext::SetCursor(int n) {
	cursor = (n < CURSOR_ARROW || n >= CURSOR_COUNT) ? CURSOR_ARROW : n;
}

void idDeviceContext::DrawCursor(float *x, float *y, float size) {
	float minX = 0.0f;
	float maxX = vidWidth;
	float minY = 0.0f;
	float maxY = vidHeight;
	GetCursorBounds( minX, maxX, minY, maxY );

	*x = idMath::ClampFloat( minX, maxX, *x );
	*y = idMath::ClampFloat( minY, maxY, *y );

	renderSystem->SetColor(colorWhite);
	// Keep GUI cursor state in virtual coordinates; only transform local draw coords.
	float drawX = *x;
	float drawY = *y;
	float drawWidth = size;
	float drawHeight = size;
	// Scale dimensions independently for aspect correction while keeping the hotspot at drawX/drawY.
	AdjustCoords(&drawX, &drawY, &drawWidth, &drawHeight);
	DrawStretchPic(drawX, drawY, drawWidth, drawHeight, 0, 0, 1, 1, cursorImages[cursor]);
}
/*
 =======================================================================================================================
 =======================================================================================================================
 */

void idDeviceContext::PaintChar(float x,float y,float width,float height,float scale,float	s,float	t,float	s2,float t2,const idMaterial *hShader) {
	float	w, h;
	w = width * scale;
	h = height * scale;

	if (ClippedCoords(&x, &y, &w, &h, &s, &t, &s2, &t2)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic(x, y, w, h, s, t, s2, t2, hShader);
}


void idDeviceContext::SetFontByScale(float scale) {
	if ( activeFont == NULL ) {
		useFont = NULL;
		return;
	}
	if (scale <= gui_smallFontLimit.GetFloat()) {
		useFont = &activeFont->fontInfoSmall;
		activeFont->maxHeight = activeFont->maxHeightSmall;
		activeFont->maxWidth = activeFont->maxWidthSmall;
	} else if (scale <= gui_mediumFontLimit.GetFloat()) {
		useFont = &activeFont->fontInfoMedium;
		activeFont->maxHeight = activeFont->maxHeightMedium;
		activeFont->maxWidth = activeFont->maxWidthMedium;
	} else {
		useFont = &activeFont->fontInfoLarge;
		activeFont->maxHeight = activeFont->maxHeightLarge;
		activeFont->maxWidth = activeFont->maxWidthLarge;
	}
}

int idDeviceContext::DrawText(float x, float y, float scale, idVec4 color, const char *text, float adjust, int limit, int style, int cursor, bool resetEscapes) {
	SetFontByScale( scale );
	q4ScaledFont_t scaledFont;
	scaledFont.font = useFont;
	scaledFont.renderScale = OpenQ4_FontRenderScale( useFont, scale );
	scaledFont.maxWidth = activeFont != NULL ? activeFont->maxWidth : 0.0f;
	scaledFont.maxHeight = activeFont != NULL ? activeFont->maxHeight : 0.0f;

	if ( !OpenQ4_HasRenderableFont( scaledFont ) || text == NULL || color.w == 0.0f ) {
		return 0;
	}

	if ( resetEscapes ) {
		drawTextColor = color;
		drawTextColorAdjust = 0.0f;
	}

	idVec4 currentColor = drawTextColor;
	currentColor[3] = color[3];
	renderSystem->SetColor( currentColor );

	const unsigned char *s = reinterpret_cast<const unsigned char *>( text );
	int len = strlen( text );
	if ( limit > 0 && len > limit ) {
		len = limit;
	}

	int count = 0;
	while ( *s != '\0' && count < len ) {
		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( reinterpret_cast<const char *>( s ), &escapeType );
		if ( escapeLength > 0 ) {
			const unsigned char *payload = s;
			int payloadType = escapeType;
			int payloadLength = escapeLength;
			int sourceAdvance = escapeLength;
			int countAdvance = escapeLength;
			int repeats = 1;

			if ( OpenQ4_IsRepeatTextEscape( reinterpret_cast<const char *>( s ), escapeLength ) ) {
				payload = s + escapeLength;
				payloadLength = OpenQ4_TextEscapeLength( reinterpret_cast<const char *>( payload ), &payloadType );
				if ( payloadLength <= 0 ) {
					s += escapeLength;
					continue;
				}
				sourceAdvance = escapeLength + payloadLength;
				countAdvance = payloadLength;
				repeats = OpenQ4_TextEscapeRepeatCount( reinterpret_cast<const char *>( s ) );
			}

			for ( int repeatIndex = 0; repeatIndex < repeats; ++repeatIndex ) {
				if ( payloadType == S_ESCAPE_ICON ) {
					char iconCode[4];
					if ( OpenQ4_ExtractIconCode( reinterpret_cast<const char *>( payload ), iconCode ) ) {
						const embeddedIcon_t *icon = NULL;
						if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
							const glyphInfo_t *referenceGlyph = &scaledFont.font->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
							const float referenceHeight = referenceGlyph->height;
							const float iconWidth = GetIconDisplayWidth( *icon, referenceHeight );
							if ( iconWidth > 0.0f ) {
								const float iconY = OpenQ4_GlyphDrawY( y, scaledFont.renderScale, referenceGlyph );
								PaintChar( x, iconY, iconWidth, referenceHeight, scaledFont.renderScale, icon->s1, icon->t1, icon->s2, icon->t2, icon->material );
								x += iconWidth;
							}
						}
					}
				} else {
					switch ( payload[1] ) {
						case '+':
							drawTextColorAdjust += Q4_TEXT_BRIGHTNESS_STEP;
							currentColor = idVec4( drawTextColor.x + drawTextColorAdjust, drawTextColor.y + drawTextColorAdjust, drawTextColor.z + drawTextColorAdjust, color.w );
							renderSystem->SetColor( currentColor );
							break;
						case '-':
							drawTextColorAdjust -= Q4_TEXT_BRIGHTNESS_STEP;
							currentColor = idVec4( drawTextColor.x + drawTextColorAdjust, drawTextColor.y + drawTextColorAdjust, drawTextColor.z + drawTextColorAdjust, color.w );
							renderSystem->SetColor( currentColor );
							break;
						case '0':
						case 'R':
						case 'r':
							drawTextColor = color;
							drawTextColorAdjust = 0.0f;
							currentColor = color;
							renderSystem->SetColor( currentColor );
							break;
						case '1': case '2': case '3': case '4': case '5':
						case '6': case '7': case '8': case '9': case ':':
							drawTextColor = idStr::ColorForIndex( payload[1] );
							drawTextColor[3] = color[3];
							drawTextColorAdjust = 0.0f;
							currentColor = drawTextColor;
							renderSystem->SetColor( currentColor );
							break;
						case 'C':
						case 'c':
							if ( payloadLength >= 5 ) {
								drawTextColor = idVec4( ( payload[2] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE, ( payload[3] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE, ( payload[4] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE, color[3] );
								drawTextColorAdjust = 0.0f;
								currentColor = drawTextColor;
								renderSystem->SetColor( currentColor );
							}
							break;
						default:
							break;
					}
				}
			}
			s += sourceAdvance;
			count += countAdvance;
			continue;
		}

		const glyphInfo_t *glyph = &scaledFont.font->glyphs[*s];
		const float drawX = OpenQ4_GlyphDrawX( x, scaledFont.renderScale, glyph );
		const float drawY = OpenQ4_GlyphDrawY( y, scaledFont.renderScale, glyph );

		if ( style == Q4_TEXT_STYLE_SHADOW ) {
			idVec4 shadowColor( 0.0f, 0.0f, 0.0f, currentColor[3] );
			renderSystem->SetColor( shadowColor );
			PaintChar( drawX + Q4_TEXT_STYLE_OFFSET, drawY + Q4_TEXT_STYLE_OFFSET, glyph->width, glyph->height, scaledFont.renderScale, glyph->s, glyph->t, glyph->s2, glyph->t2, scaledFont.font->material );
			renderSystem->SetColor( currentColor );
		} else if ( style == Q4_TEXT_STYLE_OUTLINE ) {
			const bool darkOutline = currentColor[0] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD || currentColor[1] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD || currentColor[2] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD;
			idVec4 outlineColor = darkOutline ? idVec4( 0.0f, 0.0f, 0.0f, currentColor[3] ) : idVec4( 1.0f, 1.0f, 1.0f, currentColor[3] );
			static const float offsets[4][2] = {
				{ Q4_TEXT_STYLE_OFFSET, Q4_TEXT_STYLE_OFFSET },
				{ -Q4_TEXT_STYLE_OFFSET, Q4_TEXT_STYLE_OFFSET },
				{ -Q4_TEXT_STYLE_OFFSET, -Q4_TEXT_STYLE_OFFSET },
				{ Q4_TEXT_STYLE_OFFSET, -Q4_TEXT_STYLE_OFFSET }
			};
			renderSystem->SetColor( outlineColor );
			for ( int i = 0; i < 4; ++i ) {
				PaintChar( drawX + offsets[i][0], drawY + offsets[i][1], glyph->width, glyph->height, scaledFont.renderScale, glyph->s, glyph->t, glyph->s2, glyph->t2, scaledFont.font->material );
			}
			renderSystem->SetColor( currentColor );
		}

		PaintChar( drawX, drawY, glyph->width, glyph->height, scaledFont.renderScale, glyph->s, glyph->t, glyph->s2, glyph->t2, scaledFont.font->material );

		if ( OpenQ4_TextCursorReached( cursor, count ) ) {
			DrawEditCursor( x, y, scale );
			cursor = Q4_TEXT_CURSOR_NONE;
		}
		x += OpenQ4_ScaledGlyphAdvance( scaledFont.renderScale, glyph, adjust );
		s++;
		count++;
	}
	if ( OpenQ4_TextCursorReached( cursor, count ) ) {
		DrawEditCursor( x, y, scale );
	}
	return count;
}

void idDeviceContext::CalcVirtualScaleOffset( float width, float height, float &outXScale, float &outYScale, float &outXOffset, float &outYOffset ) const {
	outXScale = 0.0f;
	outYScale = 0.0f;
	outXOffset = 0.0f;
	outYOffset = 0.0f;

	if ( width <= 0.0f || height <= 0.0f ) {
		return;
	}

	float windowWidth = static_cast<float>( glConfig.uiViewportWidth );
	float windowHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( glConfig.vidWidth );
		windowHeight = static_cast<float>( glConfig.vidHeight );
	}

	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		outXScale = static_cast<float>( VIRTUAL_WIDTH ) * ( 1.0f / width );
		outYScale = static_cast<float>( VIRTUAL_HEIGHT ) * ( 1.0f / height );
		return;
	}

	if ( aspectCorrect ) {
		// Preserve GUI aspect ratio by fitting to height on wide screens and to width on narrow screens.
		const float targetAspect = width / height;
		const float windowAspect = windowWidth / windowHeight;
		const float uniformPhysicalScale = ( windowAspect >= targetAspect ) ? ( windowHeight / height ) : ( windowWidth / width );
		const float drawWidth = width * uniformPhysicalScale;
		const float drawHeight = height * uniformPhysicalScale;

		const float virtualPerPhysicalX = static_cast<float>( VIRTUAL_WIDTH ) / windowWidth;
		const float virtualPerPhysicalY = static_cast<float>( VIRTUAL_HEIGHT ) / windowHeight;

		outXScale = uniformPhysicalScale * virtualPerPhysicalX;
		outYScale = uniformPhysicalScale * virtualPerPhysicalY;
		outXOffset = ( windowWidth - drawWidth ) * 0.5f * virtualPerPhysicalX;
		outYOffset = ( windowHeight - drawHeight ) * 0.5f * virtualPerPhysicalY;
	} else {
		outXScale = static_cast<float>( VIRTUAL_WIDTH ) * ( 1.0f / width );
		outYScale = static_cast<float>( VIRTUAL_HEIGHT ) * ( 1.0f / height );
	}
}

void idDeviceContext::GetVirtualScreenExpansion( float width, float height, float &xExpand, float &yExpand ) const {
	xExpand = 0.0f;
	yExpand = 0.0f;

	if ( !aspectCorrect || width <= 0.0f || height <= 0.0f ) {
		return;
	}

	float windowWidth = static_cast<float>( glConfig.uiViewportWidth );
	float windowHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( glConfig.vidWidth );
		windowHeight = static_cast<float>( glConfig.vidHeight );
	}

	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return;
	}

	const float targetAspect = width / height;
	const float windowAspect = windowWidth / windowHeight;
	const float aspectEpsilon = 0.0001f;

	if ( windowAspect > targetAspect + aspectEpsilon ) {
		xExpand = ( width * ( windowAspect / targetAspect - 1.0f ) ) * 0.5f;
	} else if ( windowAspect + aspectEpsilon < targetAspect ) {
		yExpand = ( height * ( targetAspect / windowAspect - 1.0f ) ) * 0.5f;
	}
}

float idDeviceContext::GetCanvasAspect() const {
	float windowWidth = static_cast<float>( glConfig.uiViewportWidth );
	float windowHeight = static_cast<float>( glConfig.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( glConfig.vidWidth );
		windowHeight = static_cast<float>( glConfig.vidHeight );
	}

	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return static_cast<float>( VIRTUAL_WIDTH ) / static_cast<float>( VIRTUAL_HEIGHT );
	}

	return windowWidth / windowHeight;
}

void idDeviceContext::SetSize(float width, float height) {
	vidWidth = ( width > 0.0f ) ? width : static_cast<float>( VIRTUAL_WIDTH );
	vidHeight = ( height > 0.0f ) ? height : static_cast<float>( VIRTUAL_HEIGHT );

	CalcVirtualScaleOffset( width, height, xScale, yScale, xOffset, yOffset );
}

void idDeviceContext::GetCursorBounds( float &minX, float &maxX, float &minY, float &maxY ) const {
	minX = 0.0f;
	maxX = vidWidth;
	minY = 0.0f;
	maxY = vidHeight;

	if ( xScale != 0.0f ) {
		minX = ( 0.0f - xOffset ) / xScale;
		maxX = ( vidWidth - xOffset ) / xScale;
		if ( minX > maxX ) {
			const float tmp = minX;
			minX = maxX;
			maxX = tmp;
		}
	}

	if ( yScale != 0.0f ) {
		minY = ( 0.0f - yOffset ) / yScale;
		maxY = ( vidHeight - yOffset ) / yScale;
		if ( minY > maxY ) {
			const float tmp = minY;
			minY = maxY;
			maxY = tmp;
		}
	}
}

int idDeviceContext::CharWidth( const char c, float scale, int adjust ) {
	SetFontByScale( scale );
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return 0;
	}
	const glyphInfo_t *glyph = &useFont->glyphs[(const unsigned char)c];
	return OpenQ4_ScaledFontUnits( useScale, OpenQ4_GlyphAdvanceUnits( glyph, adjust ) );
}

int idDeviceContext::TextWidth( const char *text, float scale, int limit, int adjust ) {
	SetFontByScale( scale );
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( text == NULL || useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	int width = 0;
	int index = 0;
	const unsigned char *s = reinterpret_cast<const unsigned char *>( text );
	while ( *s != '\0' && ( limit <= 0 || index < limit ) ) {
		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( reinterpret_cast<const char *>( s ), &escapeType );
		if ( escapeLength > 0 ) {
			if ( escapeType == S_ESCAPE_ICON ) {
				char iconCode[4];
				if ( OpenQ4_ExtractIconCode( reinterpret_cast<const char *>( s ), iconCode ) ) {
					const embeddedIcon_t *icon = NULL;
					if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
						const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
						width += OpenQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height );
					}
				}
			}
			s += escapeLength;
			index += escapeLength;
			continue;
		}

		width += OpenQ4_GlyphAdvanceUnits( &useFont->glyphs[*s], adjust );
		s++;
		index++;
	}
	return OpenQ4_ScaledFontUnits( useScale, width );
}

int idDeviceContext::TextHeight(const char *text, float scale, int limit, int adjust) {
	(void)adjust;

	SetFontByScale( scale );
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( text == NULL || useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	int maxHeight = 0;
	int index = 0;
	const char *s = text;
	while ( *s != '\0' && ( limit <= 0 || index < limit ) ) {
		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( s, &escapeType );
		if ( escapeLength > 0 ) {
			if ( escapeType == S_ESCAPE_ICON ) {
				const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
				const int referenceHeight = OpenQ4_GlyphHeightUnits( referenceGlyph );
				if ( maxHeight < referenceHeight ) {
					maxHeight = referenceHeight;
				}
			}
			s += escapeLength;
			index += escapeLength;
			continue;
		}

		const glyphInfo_t *glyph = &useFont->glyphs[*(const unsigned char *)s];
		const int glyphHeight = OpenQ4_GlyphHeightUnits( glyph );
		if ( maxHeight < glyphHeight ) {
			maxHeight = glyphHeight;
		}
		s++;
		index++;
	}

	return OpenQ4_ScaledFontUnits( useScale, maxHeight );
}

bool idDeviceContext::GetMaxTextIndex( const char *text, int limit, float textScale, wrapInfo_t &wrapInfo ) {
	SetFontByScale( textScale );
	const float useScale = OpenQ4_FontRenderScale( useFont, textScale );
	if ( text == NULL || text[0] == '\0' || useFont == NULL || useScale == 0.0f ) {
		return false;
	}

	int width = 0;
	int index = 0;
	while ( text[index] != '\0' ) {
		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( &text[index], &escapeType );
		const int tokenLength = escapeLength > 0 ? escapeLength : 1;
		int tokenWidth = 0;

		if ( escapeType == S_ESCAPE_ICON ) {
			char iconCode[4];
			if ( OpenQ4_ExtractIconCode( &text[index], iconCode ) ) {
				const embeddedIcon_t *icon = NULL;
				if ( FindIcon( iconCode, &icon ) ) {
					tokenWidth = icon->width;
				}
			}
		} else if ( escapeLength == 0 ) {
			tokenWidth = OpenQ4_GlyphAdvanceUnits( &useFont->glyphs[static_cast<unsigned char>( text[index] )], 0 );
		}

		width += tokenWidth;
		if ( OpenQ4_ScaledFontUnits( useScale, width ) > limit ) {
			const int lastTokenIndex = index + ( escapeLength > 0 ? escapeLength - 1 : 0 );
			wrapInfo.maxIndex = lastTokenIndex - 1;
			return true;
		}

		const int lastTokenIndex = index + tokenLength - 1;
		if ( text[lastTokenIndex] == ' ' ) {
			wrapInfo.lastWhitespace = lastTokenIndex;
		}

		index += tokenLength;
	}

	return false;
}

int idDeviceContext::MaxCharWidth(float scale) {
	SetFontByScale(scale);
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || activeFont == NULL ) {
		return 0;
	}
	return OpenQ4_ScaledFontUnits( useScale, activeFont->maxWidth );
}

int idDeviceContext::MaxCharHeight(float scale) {
	SetFontByScale(scale);
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || activeFont == NULL ) {
		return 0;
	}
	return OpenQ4_ScaledFontUnits( useScale, activeFont->maxHeight );
}

const idMaterial *idDeviceContext::GetScrollBarImage(int index) {
	if (index >= SCROLLBAR_HBACK && index < SCROLLBAR_COUNT) {
		return scrollBarImages[index];
	}
	return scrollBarImages[SCROLLBAR_HBACK];
}

// this only supports left aligned text
idRegion *idDeviceContext::GetTextRegion(const char *text, float textScale, idRectangle rectDraw, float xStart, float yStart) {
#if 0
	const char	*p, *textPtr, *newLinePtr;
	char		buff[1024];
	int			len, textWidth, newLine, newLineWidth;
	float		y;

	float charSkip = MaxCharWidth(textScale) + 1;
	float lineSkip = MaxCharHeight(textScale);

	textWidth = 0;
	newLinePtr = NULL;
#endif
	return NULL;
/*
	if (text == NULL) {
		return;
	}

	textPtr = text;
	if (*textPtr == '\0') {
		return;
	}

	y = lineSkip + rectDraw.y + yStart; 
	len = 0;
	buff[0] = '\0';
	newLine = 0;
	newLineWidth = 0;
	p = textPtr;

	textWidth = 0;
	while (p) {
		if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\0') {
			newLine = len;
			newLinePtr = p + 1;
			newLineWidth = textWidth;
		}

		if ((newLine && textWidth > rectDraw.w) || *p == '\n' || *p == '\0') {
			if (len) {

				float x = rectDraw.x ;
				
				buff[newLine] = '\0';
				DrawText(x, y, textScale, color, buff, 0, 0, 0);
				if (!wrap) {
					return;
				}
			}

			if (*p == '\0') {
				break;
			}

			y += lineSkip + 5;
			p = newLinePtr;
			len = 0;
			newLine = 0;
			newLineWidth = 0;
			continue;
		}

		buff[len++] = *p++;
		buff[len] = '\0';
		textWidth = TextWidth( buff, textScale, -1 );
	}
*/
}

void idDeviceContext::DrawEditCursor( float x, float y, float scale ) {
	if ( (int)( com_ticNumber >> 4 ) & 1 ) {
		return;
	}
	SetFontByScale(scale);
	const float useScale = OpenQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || useFont->material == NULL ) {
		return;
	}
	const glyphInfo_t *glyph = &useFont->glyphs[overStrikeMode ? Q4_OVERSTRIKE_CURSOR_GLYPH : Q4_INSERT_CURSOR_GLYPH];
	PaintChar( x, OpenQ4_GlyphDrawY( y, useScale, glyph ), glyph->width, glyph->height, useScale, glyph->s, glyph->t, glyph->s2, glyph->t2, useFont->material );
}

int idDeviceContext::DrawText( const char *text, float textScale, int textAlign, idVec4 color, idRectangle rectDraw, bool wrap, int cursor, bool calcOnly, idList<int> *breaks, int limit, int adjust, int style, bool chatWindow ) {
	const float charSkip = MaxCharWidth( textScale ) + 1;
	const float lineSkip = MaxCharHeight( textScale );
	const float cursorSkip = ( cursor >= 0 ? charSkip : 0 );
	const int visibleCellCount = charSkip > 0.0f ? idMath::FtoiFast( rectDraw.w / charSkip ) : 0;

	SetFontByScale( textScale );
	const float useScale = OpenQ4_FontRenderScale( useFont, textScale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return visibleCellCount;
	}

	drawTextColor = color;
	drawTextColorAdjust = 0.0f;

	if ( breaks ) {
		breaks->Append( 0 );
	}

	if ( !( text && *text ) ) {
		if ( !calcOnly && cursor == 0 ) {
			renderSystem->SetColor( color );
			DrawEditCursor( rectDraw.x, rectDraw.y + lineSkip, textScale );
		}
		return visibleCellCount;
	}

	char buff[Q4_TEXT_LINE_BUFFER_SIZE];
	buff[0] = '\0';
	int len = 0;
	int newLine = 0;
	int newLineWidth = 0;
	float textWidth = 0.0f;
	bool lineBreak = false;
	bool wordBreak = false;
	const char *p = text;
	const char *newLinePtr = NULL;
	float y = OpenQ4_InitialTextBaseline( rectDraw, textAlign, lineSkip );
	int count = 0;

	while ( p != NULL ) {
		if ( OpenQ4_IsLineBreakChar( *p ) ) {
			lineBreak = true;
			p = OpenQ4_SkipPairedLineBreak( p );
		}

		int escapeType = 0;
		const int escapeLength = OpenQ4_TextEscapeLength( p, &escapeType );
		const bool isIconEscape = escapeLength > 0 && escapeType == S_ESCAPE_ICON;
		if ( escapeLength > 0 ) {
			if ( len + escapeLength < static_cast<int>( sizeof( buff ) ) ) {
				idStr::Copynz( &buff[len], p, escapeLength + 1 );
			}
			if ( !isIconEscape ) {
				len += escapeLength;
				p += escapeLength;
				continue;
			}
		}

		int nextCharWidth = 0;
		if ( chatWindow && !lineBreak ) {
			if ( isIconEscape ) {
				char iconCode[4];
				if ( OpenQ4_ExtractIconCode( p, iconCode ) ) {
					const embeddedIcon_t *icon = NULL;
					if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
						const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
						nextCharWidth = OpenQ4_ScaledFontUnits( useScale, OpenQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height ) );
					}
				}
			} else if ( idStr::CharIsPrintable( *p ) ) {
				nextCharWidth = CharWidth( *p, textScale, adjust );
			} else {
				nextCharWidth = static_cast<int>( cursorSkip );
			}
		}

		if ( !lineBreak && ( textWidth + nextCharWidth ) > rectDraw.w ) {
			if ( len > 0 && newLine == 0 ) {
				newLine = len;
				newLinePtr = p;
				newLineWidth = static_cast<int>( textWidth );
			}
			wordBreak = true;
		} else if ( OpenQ4_ShouldCaptureBreak( lineBreak, wrap, *p ) ) {
			newLine = len;
			newLinePtr = p + 1;
			newLineWidth = static_cast<int>( textWidth );
		}

		if ( lineBreak || wordBreak ) {
			const float x = OpenQ4_AlignedTextX( rectDraw, textAlign, newLineWidth );

			if ( wrap || newLine > 0 ) {
				buff[newLine] = '\0';
				if ( wordBreak && cursor >= newLine && newLine == len ) {
					cursor++;
				}
			}

			if ( !calcOnly ) {
				count += DrawText( x, y, textScale, color, buff, static_cast<float>( adjust ), 0, style, cursor );
			}

			if ( cursor < newLine ) {
				cursor = Q4_TEXT_CURSOR_NONE;
			} else if ( cursor >= 0 ) {
				cursor -= ( newLine + 1 );
			}

			if ( !wrap ) {
				return newLine;
			}

			if ( ( limit && count > limit ) || *p == '\0' ) {
				return visibleCellCount;
			}

			y += lineSkip * Q4_TEXT_LINE_SPACING_SCALE;
			if ( !calcOnly && y > rectDraw.Bottom() ) {
				return visibleCellCount;
			}

			p = newLinePtr;
			if ( breaks ) {
				breaks->Append( p - text );
			}

			buff[0] = '\0';
			len = 0;
			newLine = 0;
			newLineWidth = 0;
			textWidth = 0.0f;
			lineBreak = false;
			wordBreak = false;
			continue;
		}

		if ( escapeLength > 0 ) {
			len += escapeLength;
			p += escapeLength;
		} else {
			if ( len + 1 < static_cast<int>( sizeof( buff ) ) ) {
				buff[len++] = *p;
				buff[len] = '\0';
			}
			p++;
		}

		textWidth = static_cast<float>( TextWidth( buff, textScale, -1, adjust ) );
	}

	if ( cursor == 0 && !calcOnly ) {
		renderSystem->SetColor( color );
		DrawEditCursor( rectDraw.x, rectDraw.y + lineSkip, textScale );
	}

	return visibleCellCount;
}

/*
=============
idRectangle::String
=============
*/
char *idRectangle::String( void ) const {
	static	int		index = 0;
	static	char	str[ 8 ][ 48 ];
	char	*s;

	// use an array so that multiple toString's won't collide
	s = str[ index ];
	index = (index + 1)&7;

	sprintf( s, "%.2f %.2f %.2f %.2f", x, y, w, h );

	return s;
}
