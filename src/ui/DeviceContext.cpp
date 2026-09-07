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
#include "../framework/KeyInput.h"

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

// Accessibility: Quake 4 draws most of its text straight over the world and over
// busy panel artwork, which leaves low-vision players with very little contrast
// to work with. A solid backing behind each line fixes the contrast ratio no
// matter what is underneath.
idCVar gui_textBackground( "gui_textBackground", "0", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT,
						   "accessibility: opacity of a solid black backing drawn behind menu and HUD text; 0 is off, 1 is fully opaque", 0.0f, 1.0f );
idCVar gui_textBackgroundPadding( "gui_textBackgroundPadding", "2", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT,
								  "accessibility: how far the text backing extends past the text, in 640x480 virtual units", 0.0f, 16.0f );


idList<fontInfoEx_t> idDeviceContext::fonts;
int idDeviceContext::fontsVideoRestartCount = -1;
int idDeviceContext::fontsCodePageGeneration = -1;

namespace {

static const float Q4_GUI_FONT_BASE_POINT_SIZE = 48.0f;
static const float Q4_TEXT_BRIGHTNESS_STEP = 0.1f;
static const float Q4_TEXT_RGB_ESCAPE_SCALE = 1.0f / 9.0f;
static const float Q4_TEXT_OUTLINE_DARK_THRESHOLD = 0.2f;
static const float Q4_TEXT_STYLE_OFFSET = 1.0f;
static const float Q4_TEXT_LINE_SPACING_SCALE = 1.25f;
static const float Q4_GLYPH_HORIZONTAL_GUARD_TEXELS = 0.5f;
static const float Q4_GLYPH_SMALL_ATLAS_HORIZONTAL_GUARD_TEXELS = 1.0f;
static const float Q4_GLYPH_SMALL_MARINE_CLIP_RIGHT_PAD_TEXELS = 2.0f;
static const float Q4_GLYPH_SMALL_FONT_MAX_POINT_SIZE = 12.0f;
// Retail guis/cinematic.gui used a 640x480 desktop with 60px top/bottom bars,
// leaving a 640x360 cinematic view.
static const float Q4_CINEMATIC_RETAIL_VISIBLE_WIDTH = 640.0f;
static const float Q4_CINEMATIC_RETAIL_VISIBLE_HEIGHT = 360.0f;
static const float Q4_CINEMATIC_RETAIL_ASPECT = Q4_CINEMATIC_RETAIL_VISIBLE_WIDTH / Q4_CINEMATIC_RETAIL_VISIBLE_HEIGHT;
static const int Q4_TEXT_STYLE_SHADOW = 1;
static const int Q4_TEXT_STYLE_OUTLINE = 2;
static const int Q4_TEXT_ALIGN_VERTICAL_CENTER = 3;
static const int Q4_TEXT_CURSOR_NONE = -1;
static const int Q4_TEXT_LINE_BUFFER_SIZE = 1024;
static const int Q4_TEXT_REPEAT_ESCAPE_MAX = 9;
static const int Q4_EMBEDDED_ICON_FULL_IMAGE = -1;
static const unsigned char Q4_INSERT_CURSOR_GLYPH = '|';
static const unsigned char Q4_OVERSTRIKE_CURSOR_GLYPH = '_';
static const unsigned char Q4_EMBEDDED_ICON_REFERENCE_GLYPH = 'W';
static const float Q4_KEY_BINDING_BASELINE_ASCENT = 0.84f;
static const float Q4_KEY_BINDING_BASELINE_DESCENT = 1.0f - Q4_KEY_BINDING_BASELINE_ASCENT;
static const float Q4_KEY_BINDING_INLINE_HEIGHT_RATIO = 0.99f;
static const float Q4_KEY_BINDING_PROMPT_HEIGHT_RATIO = 1.50f;
static const int Q4_KEY_BINDING_MAX_CURVE_BANDS = 16;

enum q4EmbeddedIconMeasure_t {
	Q4_EMBEDDED_ICON_DRAW_WIDTH,
	Q4_EMBEDDED_ICON_REGISTERED_WIDTH
};

static int openQ4_TextEscapeLength( const char *text, int *type = NULL ) {
	return idStr::IsEscape( text, type );
}

static bool openQ4_IsRepeatTextEscape( const char *escape, int escapeLength ) {
	return escapeLength > 2 && escape != NULL && ( escape[1] == 'N' || escape[1] == 'n' );
}

static int openQ4_TextEscapeRepeatCount( const char *escape ) {
	int repeats = static_cast<unsigned char>( escape[2] ) - '0';
	if ( repeats < 0 ) {
		repeats = 0;
	} else if ( repeats >= Q4_TEXT_REPEAT_ESCAPE_MAX ) {
		repeats = Q4_TEXT_REPEAT_ESCAPE_MAX;
	}
	return repeats;
}

static void openQ4_ResolveTextEscape( const char *source, int escapeLength, int escapeType,
		const char *&payload, int &payloadLength, int &payloadType, int &sourceLength, int &repeats ) {
	payload = source;
	payloadLength = escapeLength;
	payloadType = escapeType;
	sourceLength = escapeLength;
	repeats = 1;

	if ( !openQ4_IsRepeatTextEscape( source, escapeLength ) ) {
		return;
	}

	const char *repeatedPayload = source + escapeLength;
	int repeatedType = 0;
	const int repeatedLength = openQ4_TextEscapeLength( repeatedPayload, &repeatedType );
	if ( repeatedLength <= 0 ) {
		return;
	}

	payload = repeatedPayload;
	payloadLength = repeatedLength;
	payloadType = repeatedType;
	sourceLength += repeatedLength;
	repeats = openQ4_TextEscapeRepeatCount( source );
}

struct q4ScaledFont_t {
	const fontInfo_t *font;
	float renderScale;
	float maxWidth;
	float maxHeight;
};

struct q4VirtualScreenTransform_t {
	float xScale;
	float yScale;
	float xOffset;
	float yOffset;
};

static bool openQ4_ExtractIconCode( const char *escape, char code[4] ) {
	int escapeType = 0;
	if ( openQ4_TextEscapeLength( escape, &escapeType ) != 5 || escapeType != S_ESCAPE_ICON ) {
		return false;
	}

	code[0] = escape[2];
	code[1] = escape[3];
	code[2] = escape[4];
	code[3] = '\0';
	return true;
}

enum q4KeyBindingIconKind_t {
	Q4_KEY_BINDING_KEYBOARD,
	Q4_KEY_BINDING_MOUSE_BUTTON,
	Q4_KEY_BINDING_MOUSE_WHEEL,
	Q4_KEY_BINDING_PAD_FACE,
	Q4_KEY_BINDING_PAD_SHOULDER,
	Q4_KEY_BINDING_PAD_DPAD,
	Q4_KEY_BINDING_PAD_STICK,
	Q4_KEY_BINDING_PAD_MENU,
	Q4_KEY_BINDING_PAD_GUIDE,
	Q4_KEY_BINDING_PAD_TOUCHPAD,
	Q4_KEY_BINDING_PAD_PADDLE,
	Q4_KEY_BINDING_PAD_GENERIC
};

struct q4KeyBindingIconInfo_t {
	q4KeyBindingIconKind_t kind;
	idStr label;
	int detail;
};

static bool openQ4_IsKeyboardArrowKey( int keyNum ) {
	return keyNum == K_UPARROW || keyNum == K_DOWNARROW ||
		keyNum == K_LEFTARROW || keyNum == K_RIGHTARROW;
}

static int openQ4_HexDigitValue( char c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return c - 'a' + 10;
	}
	if ( c >= 'A' && c <= 'F' ) {
		return c - 'A' + 10;
	}
	return -1;
}

static bool openQ4_ExtractKeyBindingIcon( const char *escape, int &keyNum, float *heightRatio = NULL ) {
	char code[4];
	if ( !openQ4_ExtractIconCode( escape, code ) || ( code[0] != 'k' && code[0] != 'K' ) ) {
		return false;
	}

	const int high = openQ4_HexDigitValue( code[1] );
	const int low = openQ4_HexDigitValue( code[2] );
	if ( high < 0 || low < 0 ) {
		return false;
	}
	keyNum = ( high << 4 ) | low;
	if ( heightRatio != NULL ) {
		*heightRatio = code[0] == 'K' ? Q4_KEY_BINDING_PROMPT_HEIGHT_RATIO : Q4_KEY_BINDING_INLINE_HEIGHT_RATIO;
	}
	return keyNum >= 0 && keyNum <= 0xff;
}

static void openQ4_GetKeyBindingIconInfo( int keyNum, q4KeyBindingIconInfo_t &info ) {
	info.kind = Q4_KEY_BINDING_KEYBOARD;
	info.detail = keyNum;
	info.label = idKeyInput::KeyNumToString( keyNum, true );
	if ( info.label.Length() == 1 ) {
		info.label.ToUpper();
	}
	if ( openQ4_IsKeyboardArrowKey( keyNum ) ) {
		// Drawn procedurally rather than as U+2190..U+2193, so these read the
		// same whichever font path is active: the retail bitmap atlases have no
		// arrow art at all, and no byte of any codepage reaches one.
		info.label.Clear();
		return;
	}

	if ( keyNum >= K_MOUSE1 && keyNum <= K_MOUSE8 ) {
		info.kind = Q4_KEY_BINDING_MOUSE_BUTTON;
		info.detail = keyNum - K_MOUSE1 + 1;
		// The selected physical region identifies the button.  A bare number
		// makes the mouse look like a keyboard key, especially in short prompts.
		info.label.Clear();
		return;
	}
	if ( keyNum == K_MWHEELUP || keyNum == K_MWHEELDOWN ) {
		info.kind = Q4_KEY_BINDING_MOUSE_WHEEL;
		info.detail = keyNum == K_MWHEELUP ? 1 : -1;
		info.label.Clear();
		return;
	}

	const char *keyCode = idKeyInput::KeyNumToString( keyNum, false );
	if ( idStr::Icmpn( keyCode, "JOY", 3 ) != 0 && idStr::Icmpn( keyCode, "AUX", 3 ) != 0 ) {
		return;
	}

	const bool joystick = idStr::Icmpn( keyCode, "JOY", 3 ) == 0;
	const int number = atoi( keyCode + 3 );
	info.detail = number;
	if ( !joystick ) {
		info.kind = Q4_KEY_BINDING_PAD_GENERIC;
		return;
	}

	switch ( number ) {
		case 1: case 2:
			info.kind = Q4_KEY_BINDING_PAD_SHOULDER; info.label.Clear(); break;
		case 3: case 4: case 5: case 6:
			info.kind = Q4_KEY_BINDING_PAD_FACE; info.label.Clear(); break;
		case 7:	info.kind = Q4_KEY_BINDING_PAD_MENU; info.label.Clear(); break;
		case 8:	info.kind = Q4_KEY_BINDING_PAD_MENU; info.label = common->GetLanguageDict()->GetString( "#str_200018" ); break;
		case 9: case 10: case 11: case 12:
			info.kind = Q4_KEY_BINDING_PAD_DPAD; info.label.Clear(); break;
		case 13: case 14:
			info.kind = Q4_KEY_BINDING_PAD_STICK; info.label.Clear(); break;
		case 15: case 16:
			info.kind = Q4_KEY_BINDING_PAD_SHOULDER; info.label.Clear(); break;
		case 17:	info.kind = Q4_KEY_BINDING_PAD_GUIDE; info.label.Clear(); break;
		case 18:	info.kind = Q4_KEY_BINDING_PAD_TOUCHPAD; info.label.Clear(); break;
		case 19: case 20: case 21: case 22:
			info.kind = Q4_KEY_BINDING_PAD_PADDLE; info.label.Clear(); break;
		default:	info.kind = Q4_KEY_BINDING_PAD_GENERIC; break;
	}
}

static idVec4 openQ4_KeyBindingColor( float r, float g, float b, float a, float sourceAlpha ) {
	return idVec4( r, g, b, a * sourceAlpha );
}

static idVec4 openQ4_KeyBindingAccent( const idVec4 &source, float alpha = 1.0f ) {
	return idVec4(
		idMath::ClampFloat( 0.32f, 1.0f, source.x * 0.72f + 0.28f ),
		idMath::ClampFloat( 0.32f, 1.0f, source.y * 0.72f + 0.28f ),
		idMath::ClampFloat( 0.32f, 1.0f, source.z * 0.72f + 0.28f ),
		source.w * alpha );
}

static void openQ4_DrawRoundedFillCore( idDeviceContext *dc, float x, float y, float w, float h, float radius, const idVec4 &color ) {
	if ( dc == NULL || w <= 0.0f || h <= 0.0f || color.w <= 0.0f ) {
		return;
	}

	radius = idMath::ClampFloat( 0.0f, Min( w, h ) * 0.5f, radius );
	if ( radius < 0.25f ) {
		dc->DrawFilledRect( x, y, w, h, color );
		return;
	}

	const int bands = idMath::ClampInt( 6, Q4_KEY_BINDING_MAX_CURVE_BANDS,
		static_cast<int>( idMath::Ceil( radius * 6.0f ) ) );
	const float bandHeight = radius / static_cast<float>( bands );
	const float radiusSquared = radius * radius;

	if ( h > radius * 2.0f ) {
		dc->DrawFilledRect( x, y + radius, w, h - radius * 2.0f, color );
	}
	for ( int band = 0; band < bands; ++band ) {
		const float bandY = static_cast<float>( band ) * bandHeight;
		const float sampleY = radius - ( bandY + bandHeight * 0.5f );
		const float inset = radius - idMath::Sqrt( Max( 0.0f, radiusSquared - sampleY * sampleY ) );
		const float bandWidth = Max( 0.0f, w - inset * 2.0f );
		const float overlap = 0.02f;
		dc->DrawFilledRect( x + inset, y + bandY, bandWidth, bandHeight + overlap, color );
		dc->DrawFilledRect( x + inset, y + h - bandY - bandHeight - overlap, bandWidth, bandHeight + overlap, color );
	}
}

static void openQ4_DrawSmoothRoundedFill( idDeviceContext *dc, float x, float y, float w, float h, float radius, const idVec4 &color ) {
	if ( dc == NULL || w <= 0.0f || h <= 0.0f || color.w <= 0.0f ) {
		return;
	}

	const float fringe = Min( 0.42f, Min( w, h ) * 0.08f );
	idVec4 edgeColor = color;
	edgeColor.w *= 0.38f;
	openQ4_DrawRoundedFillCore( dc, x, y, w, h, radius, edgeColor );
	if ( w > fringe * 2.0f && h > fringe * 2.0f ) {
		openQ4_DrawRoundedFillCore( dc, x + fringe, y + fringe, w - fringe * 2.0f, h - fringe * 2.0f,
			Max( 0.0f, radius - fringe ), color );
	}
}

static void openQ4_DrawKeycapBase( idDeviceContext *dc, float x, float y, float w, float h, const idVec4 &sourceColor ) {
	const float radius = Max( 1.0f, h * 0.15f );
	openQ4_DrawSmoothRoundedFill( dc, x + 1.0f, y + 1.0f, w - 1.0f, h - 1.0f, radius,
		openQ4_KeyBindingColor( 0.0f, 0.0f, 0.0f, 0.48f, sourceColor.w ) );
	openQ4_DrawSmoothRoundedFill( dc, x, y, w, h, radius, openQ4_KeyBindingAccent( sourceColor, 0.92f ) );
	openQ4_DrawSmoothRoundedFill( dc, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, Max( 0.5f, radius - 0.75f ),
		openQ4_KeyBindingColor( 0.045f, 0.060f, 0.075f, 0.98f, sourceColor.w ) );
	dc->DrawFilledRect( x + radius + 1.0f, y + 1.0f, Max( 0.0f, w - radius * 2.0f - 2.0f ), 1.0f,
		openQ4_KeyBindingColor( 0.42f, 0.50f, 0.56f, 0.58f, sourceColor.w ) );
	dc->DrawFilledRect( x + radius, y + h - 2.0f, Max( 0.0f, w - radius * 2.0f ), 1.0f,
		openQ4_KeyBindingColor( 0.0f, 0.0f, 0.0f, 0.72f, sourceColor.w ) );
}

static void openQ4_DrawOctagonalBadge( idDeviceContext *dc, float x, float y, float size, const idVec4 &sourceColor, const idVec4 &faceColor ) {
	const float radius = Max( 1.0f, size * 0.30f );
	openQ4_DrawSmoothRoundedFill( dc, x + 1.0f, y + 1.0f, size - 1.0f, size - 1.0f, radius,
		openQ4_KeyBindingColor( 0.0f, 0.0f, 0.0f, 0.50f, sourceColor.w ) );
	openQ4_DrawSmoothRoundedFill( dc, x, y, size, size, radius, openQ4_KeyBindingAccent( sourceColor, 0.95f ) );
	openQ4_DrawSmoothRoundedFill( dc, x + 1.0f, y + 1.0f, size - 2.0f, size - 2.0f, Max( 0.5f, radius - 0.75f ), faceColor );
}

static void openQ4_DrawKeyboardArrowGlyph( idDeviceContext *dc, int keyNum, float x, float y, float size, const idVec4 &color ) {
	if ( dc == NULL || !openQ4_IsKeyboardArrowKey( keyNum ) ) {
		return;
	}

	const int bands = 6;
	const float headLength = size * 0.28f;
	const float headWidth = size * 0.48f;
	const float shaftThickness = Max( 1.0f, size * 0.11f );
	const float glyphMin = size * 0.23f;
	const float glyphMax = size * 0.77f;
	const float centerX = x + size * 0.5f;
	const float centerY = y + size * 0.5f;

	if ( keyNum == K_UPARROW || keyNum == K_DOWNARROW ) {
		const float headTop = keyNum == K_UPARROW ? y + glyphMin : y + glyphMax - headLength;
		const float bandHeight = headLength / static_cast<float>( bands );
		for ( int band = 0; band < bands; ++band ) {
			const int widthBand = keyNum == K_UPARROW ? band + 1 : bands - band;
			const float bandWidth = headWidth * static_cast<float>( widthBand ) / static_cast<float>( bands );
			dc->DrawFilledRect( centerX - bandWidth * 0.5f, headTop + band * bandHeight,
				bandWidth, bandHeight + 0.04f, color );
		}
		const float shaftTop = keyNum == K_UPARROW ? headTop + headLength * 0.72f : y + glyphMin;
		const float shaftBottom = keyNum == K_UPARROW ? y + glyphMax : headTop + headLength * 0.28f;
		dc->DrawFilledRect( centerX - shaftThickness * 0.5f, shaftTop,
			shaftThickness, Max( 0.0f, shaftBottom - shaftTop ), color );
	} else {
		const float headLeft = keyNum == K_LEFTARROW ? x + glyphMin : x + glyphMax - headLength;
		const float bandWidth = headLength / static_cast<float>( bands );
		for ( int band = 0; band < bands; ++band ) {
			const int heightBand = keyNum == K_LEFTARROW ? band + 1 : bands - band;
			const float bandHeight = headWidth * static_cast<float>( heightBand ) / static_cast<float>( bands );
			dc->DrawFilledRect( headLeft + band * bandWidth, centerY - bandHeight * 0.5f,
				bandWidth + 0.04f, bandHeight, color );
		}
		const float shaftLeft = keyNum == K_LEFTARROW ? headLeft + headLength * 0.72f : x + glyphMin;
		const float shaftRight = keyNum == K_LEFTARROW ? x + glyphMax : headLeft + headLength * 0.28f;
		dc->DrawFilledRect( shaftLeft, centerY - shaftThickness * 0.5f,
			Max( 0.0f, shaftRight - shaftLeft ), shaftThickness, color );
	}
}

static float openQ4_FontRenderScale( const fontInfo_t *font, float scale ) {
	if ( font == NULL || font->pointSize == 0.0f ) {
		return 0.0f;
	}
	return scale / font->pointSize * Q4_GUI_FONT_BASE_POINT_SIZE;
}

/*
===============================================================================

	Walking UTF-8 text.

	Engine text is UTF-8, so a character is one to four bytes and the glyph that
	draws it is found by code point rather than by array index. Every pass over
	text - drawing, measuring, wrapping - steps through openQ4_NextTextGlyph, so
	they cannot disagree about where one character ends and the next begins. A
	measure pass and a draw pass that split a two-byte character differently
	would put the ink, the wrap point and the edit cursor in three places.

	Byte offsets stay byte offsets throughout. The cursor positions and limits
	the GUI passes in are indices into the string it handed over, and quietly
	reinterpreting them as character counts would move every caller's cursor.

===============================================================================
*/
struct q4TextGlyph_t {
	unsigned int		codePoint;
	const glyphInfo_t *	glyph;			// NULL when nothing drawable was found
	const idMaterial *	material;		// the atlas 'glyph' lives in
	int					bytes;			// always the real width of the sequence
};

// Drawn in place of a code point the font has no art for. A question mark is
// deliberate: leaving the glyph empty would give it a zero advance, so the
// character would vanish rather than show up as unavailable.
static const unsigned int Q4_TEXT_MISSING_GLYPH = '?';

/*
================
openQ4_NextTextGlyph

Reads one character. Returns false when there is nothing drawable, but 'bytes'
is filled in either way, so a caller can always make progress.
================
*/
static bool openQ4_NextTextGlyph( const fontInfo_t *font, const char *s, int available, q4TextGlyph_t &out ) {
	out.codePoint = 0;
	out.glyph = NULL;
	out.material = ( font != NULL ) ? font->material : NULL;
	out.bytes = LangDict_NextCodePoint( s, available, out.codePoint );
	if ( out.bytes <= 0 ) {
		return false;
	}

	out.glyph = R_GlyphForCodePoint( font, out.codePoint, &out.material );
	if ( out.glyph != NULL ) {
		return true;
	}

	// Nothing for that code point. Punctuation the byte-indexed retail atlases
	// never carried folds to its ASCII stand-in first, so an em dash drawn
	// through a retail font comes out a hyphen rather than a hole. Only the
	// first character of a fold is used - the multi-character folds are applied
	// whole when a legacy string table is read, and this is the last resort for
	// text that never went through one.
	const char *fold = LangDict_AsciiFoldForCodePoint( out.codePoint );
	const unsigned int substitute = ( fold != NULL && fold[0] != '\0' )
		? (unsigned int)(unsigned char)fold[0] : Q4_TEXT_MISSING_GLYPH;
	out.glyph = R_GlyphForCodePoint( font, substitute, &out.material );
	return out.glyph != NULL;
}

/*
================
openQ4_CodePointIsPrintable

idStr::CharIsPrintable answers for a byte of an 8-bit codepage, which is the
wrong question once text is UTF-8 - it would be asked about continuation bytes
rather than about characters. The ranges that are not printable are the same
either way: the C0 controls, DEL, and the C1 block.
================
*/
static bool openQ4_CodePointIsPrintable( unsigned int codePoint ) {
	if ( codePoint < 0x20 ) {
		return false;
	}
	return codePoint < 0x7F || codePoint > 0x9F;
}

/*
================
openQ4_TextGlyphAt

The same step, for callers that have a NUL-terminated pointer rather than a
known remaining length.
================
*/
static bool openQ4_TextGlyphAt( const fontInfo_t *font, const char *s, q4TextGlyph_t &out ) {
	// Four is the longest UTF-8 sequence, and a terminator is never a valid
	// continuation byte, so the decoder always stops on it - reading up to four
	// bytes here cannot run past the end of the string.
	return openQ4_NextTextGlyph( font, s, UTF8_MAX_BYTES, out );
}

static int openQ4_ScaledFontUnits( float fontScale, float units ) {
	return static_cast<int>( fontScale * units );
}

static int openQ4_RoundedGlyphAdvance( const glyphInfo_t *glyph ) {
	return static_cast<int>( idMath::Ceil( glyph->horiAdvance ) );
}

static int openQ4_GlyphAdvanceUnits( const glyphInfo_t *glyph, int adjust ) {
	return adjust + openQ4_RoundedGlyphAdvance( glyph );
}

static int openQ4_GlyphHeightUnits( const glyphInfo_t *glyph ) {
	return static_cast<int>( glyph->height );
}

static int openQ4_EmbeddedIconDimensionOrImageSize( int registeredDimension, float imageDimension ) {
	return registeredDimension == Q4_EMBEDDED_ICON_FULL_IMAGE ? static_cast<int>( imageDimension ) : registeredDimension;
}

static void openQ4_SetEmbeddedIconAxisUV( float &uv1, float &uv2, int registeredOffset, int registeredLength, float imageLength ) {
	if ( imageLength <= 0.0f ) {
		uv1 = 0.0f;
		uv2 = 0.0f;
		return;
	}

	if ( registeredOffset == Q4_EMBEDDED_ICON_FULL_IMAGE ) {
		uv1 = 0.0f;
		uv2 = 1.0f;
		return;
	}

	uv1 = static_cast<float>( registeredOffset ) / imageLength;
	uv2 = static_cast<float>( registeredOffset + registeredLength ) / imageLength;
}

static int openQ4_EmbeddedIconWidthUnits( float iconWidth, float iconHeight, float referenceHeight, q4EmbeddedIconMeasure_t measureMode = Q4_EMBEDDED_ICON_DRAW_WIDTH ) {
	if ( measureMode == Q4_EMBEDDED_ICON_REGISTERED_WIDTH ) {
		return static_cast<int>( iconWidth );
	}

	if ( referenceHeight <= 0.0f || iconWidth <= 0.0f || iconHeight <= 0.0f ) {
		return 0;
	}
	return static_cast<int>( iconWidth * ( referenceHeight / iconHeight ) );
}

static float openQ4_ScaledGlyphAdvance( float fontScale, const glyphInfo_t *glyph, float adjust ) {
	return idMath::Ceil( ( glyph->horiAdvance + adjust ) * fontScale );
}

static float openQ4_GlyphDrawX( float x, float fontScale, const glyphInfo_t *glyph ) {
	return x + fontScale * glyph->horiBearingX;
}

static float openQ4_GlyphDrawY( float y, float fontScale, const glyphInfo_t *glyph ) {
	return y - fontScale * glyph->horiBearingY;
}

static float openQ4_GlyphHorizontalGuardTexels( const fontInfo_t *font ) {
	return ( font != NULL && font->pointSize <= Q4_GLYPH_SMALL_FONT_MAX_POINT_SIZE ) ? Q4_GLYPH_SMALL_ATLAS_HORIZONTAL_GUARD_TEXELS : Q4_GLYPH_HORIZONTAL_GUARD_TEXELS;
}

static bool openQ4_IsSmallMarineFont( const fontInfo_t *font ) {
	return font != NULL && font->pointSize <= Q4_GLYPH_SMALL_FONT_MAX_POINT_SIZE && idStr::FindText( font->name, "marine_12.fontdat", false ) >= 0;
}

static bool openQ4_ApplyGlyphHorizontalGuard( const fontInfo_t *font, const glyphInfo_t *glyph, float fontScale, float &x, float &width, float &s1, float &s2 ) {
	if ( glyph == NULL || fontScale == 0.0f || width <= 0.0f || s2 <= s1 ) {
		return false;
	}

	const float atlasWidth = width / ( s2 - s1 );
	if ( atlasWidth <= 0.0f ) {
		return false;
	}

	const float guardTexels = openQ4_GlyphHorizontalGuardTexels( font );
	const float leftGuard = Min( guardTexels, Max( 0.0f, s1 * atlasWidth ) );
	const float rightGuard = Min( guardTexels, Max( 0.0f, ( 1.0f - s2 ) * atlasWidth ) );
	if ( leftGuard == 0.0f && rightGuard == 0.0f ) {
		return false;
	}

	x -= leftGuard * fontScale;
	width += leftGuard + rightGuard;
	s1 -= leftGuard / atlasWidth;
	s2 += rightGuard / atlasWidth;
	return true;
}

static float openQ4_GlyphVisibleRightEdge( float x, const fontInfo_t *font, float fontScale, const glyphInfo_t *glyph ) {
	if ( glyph == NULL || fontScale == 0.0f ) {
		return x;
	}

	float drawX = openQ4_GlyphDrawX( x, fontScale, glyph );
	float width = glyph->width;
	float s1 = glyph->s;
	float s2 = glyph->s2;
	openQ4_ApplyGlyphHorizontalGuard( font, glyph, fontScale, drawX, width, s1, s2 );
	return drawX + width * fontScale;
}

static float openQ4_GlyphClipRightPad( const fontInfo_t *font, const glyphInfo_t *glyph, float fontScale ) {
	if ( !openQ4_IsSmallMarineFont( font ) || glyph == NULL || fontScale <= 0.0f ) {
		return 0.0f;
	}
	return idMath::Ceil( Q4_GLYPH_SMALL_MARINE_CLIP_RIGHT_PAD_TEXELS * fontScale );
}

static bool openQ4_HasRenderableFont( const q4ScaledFont_t &scaledFont ) {
	return scaledFont.font != NULL && scaledFont.renderScale != 0.0f;
}

// How far a font's glyphs actually reach either side of the baseline, in the
// font's own units.
struct q4TextInkExtents_t {
	float	ascent;
	float	descent;
};

/*
================
openQ4_FontInkExtents

Measured from the glyphs rather than read out of fontInfo_t.  The retail
ascender and descender fields do not describe where the ink is: the marine font
declares an ascender of 29.9 while its capitals reach 40.4, and declares no
descender at all despite having glyphs 11 units below the baseline.  A backing
box built from those numbers would clip the text it exists to sit behind.
================
*/
static void openQ4_FontInkExtents( const fontInfo_t *font, q4TextInkExtents_t &extents ) {
	extents.ascent = 0.0f;
	extents.descent = 0.0f;
	if ( font == NULL ) {
		return;
	}
	for ( int i = 0; i < GLYPHS_PER_FONT; i++ ) {
		const glyphInfo_t *glyph = &font->glyphs[i];
		if ( glyph->height <= 0.0f ) {
			continue;
		}
		extents.ascent = Max( extents.ascent, glyph->horiBearingY );
		extents.descent = Max( extents.descent, glyph->height - glyph->horiBearingY );
	}
}

/*
================
openQ4_TextBackgroundRect

Places the accessibility backing for one line of text.  The box is anchored on
the baseline and sized from the font's ink extents, so every line of a given
font gets the same height whatever characters it happens to contain.
================
*/
static bool openQ4_TextBackgroundRect( const q4ScaledFont_t &scaledFont, float x, float baselineY,
									   float textWidth, float padding,
									   float &outX, float &outY, float &outWidth, float &outHeight ) {
	if ( textWidth <= 0.0f || !openQ4_HasRenderableFont( scaledFont ) ) {
		return false;
	}

	q4TextInkExtents_t ink;
	openQ4_FontInkExtents( scaledFont.font, ink );

	const float ascent = ink.ascent * scaledFont.renderScale;
	const float descent = ink.descent * scaledFont.renderScale;
	if ( ascent + descent <= 0.0f ) {
		return false;
	}

	outX = x - padding;
	outY = baselineY - ascent - padding;
	outWidth = textWidth + padding * 2.0f;
	outHeight = ascent + descent + padding * 2.0f;
	return true;
}

static bool openQ4_TextCursorReached( int cursor, int count ) {
	return cursor != Q4_TEXT_CURSOR_NONE && cursor <= count;
}

static void openQ4_ApplyRgbTextEscapeColor( idVec4 &drawTextColor, idVec4 &currentColor, const unsigned char *payload ) {
	drawTextColor[0] = ( payload[2] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	drawTextColor[1] = ( payload[3] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	drawTextColor[2] = ( payload[4] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	currentColor = drawTextColor;
}

static bool openQ4_ShouldDrawEmptyTextCursor( bool calcOnly, int cursor ) {
	return !calcOnly && cursor == 0;
}

static bool openQ4_ShouldDrawFinalTextCursor( int cursor ) {
	return cursor == 0;
}

static bool openQ4_IsLineBreakChar( char c ) {
	return c == '\n' || c == '\r' || c == '\0';
}

static const char *openQ4_SkipPairedLineBreak( const char *text ) {
	if ( ( *text == '\n' && text[1] == '\r' ) || ( *text == '\r' && text[1] == '\n' ) ) {
		return text + 1;
	}
	return text;
}

static bool openQ4_ShouldCaptureBreak( bool lineBreak, bool wrap, char c ) {
	return lineBreak || ( wrap && ( c == ' ' || c == '\t' ) );
}

static float openQ4_InitialTextBaseline( idRectangle &rect, int &textAlign, float lineHeight ) {
	if ( textAlign == Q4_TEXT_ALIGN_VERTICAL_CENTER ) {
		textAlign = idDeviceContext::ALIGN_LEFT;
		return rect.y + rect.h * 0.5f + lineHeight * 0.5f;
	}
	return rect.y + lineHeight;
}

static float openQ4_AlignedTextX( const idRectangle &rect, int textAlign, int textWidth ) {
	if ( textAlign == idDeviceContext::ALIGN_RIGHT ) {
		return rect.x + rect.w - textWidth;
	}
	if ( textAlign == idDeviceContext::ALIGN_CENTER ) {
		return rect.x + ( rect.w - textWidth ) * 0.5f;
	}
	return rect.x;
}

static void openQ4_ClearVirtualScreenTransform( q4VirtualScreenTransform_t &transform ) {
	transform.xScale = 0.0f;
	transform.yScale = 0.0f;
	transform.xOffset = 0.0f;
	transform.yOffset = 0.0f;
}

static void openQ4_SetRetailVirtualTransform( float width, float height, q4VirtualScreenTransform_t &transform ) {
	openQ4_ClearVirtualScreenTransform( transform );

	if ( width <= 0.0f || height <= 0.0f ) {
		return;
	}

	transform.xScale = static_cast<float>( VIRTUAL_WIDTH ) * ( 1.0f / width );
	transform.yScale = static_cast<float>( VIRTUAL_HEIGHT ) * ( 1.0f / height );
}

static bool openQ4_GetCurrentViewportSize( float &windowWidth, float &windowHeight ) {
	windowWidth = static_cast<float>( engineWindowState.uiViewportWidth );
	windowHeight = static_cast<float>( engineWindowState.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( engineWindowState.vidWidth );
		windowHeight = static_cast<float>( engineWindowState.vidHeight );
	}
	return windowWidth > 0.0f && windowHeight > 0.0f;
}

static float openQ4_FontSelectionScaleForViewport( float authoredScale, float canvasWidth, float canvasHeight,
		float viewportWidth, float viewportHeight, bool aspectCorrect ) {
	if ( authoredScale <= 0.0f || canvasWidth <= 0.0f || canvasHeight <= 0.0f ||
			viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
		return authoredScale;
	}

	const float physicalScaleX = viewportWidth / canvasWidth;
	const float physicalScaleY = viewportHeight / canvasHeight;
	const float physicalScale = aspectCorrect
		? Min( physicalScaleX, physicalScaleY )
		: Max( physicalScaleX, physicalScaleY );
	return authoredScale * Max( 1.0f, physicalScale );
}

// A scalable font rasterises its small/medium/large slots from one face at
// 12/24/48 point, so their point-size-normalised metrics agree and picking a
// larger slot only buys resolution.  The retail .fontdat atlases were authored
// by hand per size and do not agree to the pixel, so picking a different one
// there moves text.  Only the scalable slots carry the "ttf/" name prefix.
static bool openQ4_FontAtlasesAreProportional( const fontInfoEx_t *font ) {
	if ( font == NULL ) {
		return false;
	}
	const fontInfo_t *slots[3] = { &font->fontInfoSmall, &font->fontInfoMedium, &font->fontInfoLarge };
	for ( int i = 0; i < 3; i++ ) {
		if ( idStr::Cmpn( slots[i]->name, "ttf/", 4 ) != 0 ) {
			return false;
		}
	}
	return true;
}

static float openQ4_FontSelectionScale( float authoredScale, float canvasWidth, float canvasHeight, bool aspectCorrect ) {
	float viewportWidth = 0.0f;
	float viewportHeight = 0.0f;
	if ( !openQ4_GetCurrentViewportSize( viewportWidth, viewportHeight ) ) {
		return authoredScale;
	}
	return openQ4_FontSelectionScaleForViewport( authoredScale, canvasWidth, canvasHeight,
		viewportWidth, viewportHeight, aspectCorrect );
}

static void openQ4_CalcVirtualScreenTransform( float width, float height, float windowWidth, float windowHeight, bool aspectCorrect, q4VirtualScreenTransform_t &transform ) {
	openQ4_ClearVirtualScreenTransform( transform );

	if ( width <= 0.0f || height <= 0.0f ) {
		return;
	}

	if ( !aspectCorrect || windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		openQ4_SetRetailVirtualTransform( width, height, transform );
		return;
	}

	const float targetAspect = width / height;
	const float windowAspect = windowWidth / windowHeight;
	const float uniformPhysicalScale = ( windowAspect >= targetAspect ) ? ( windowHeight / height ) : ( windowWidth / width );
	const float drawWidth = width * uniformPhysicalScale;
	const float drawHeight = height * uniformPhysicalScale;

	const float virtualPerPhysicalX = static_cast<float>( VIRTUAL_WIDTH ) / windowWidth;
	const float virtualPerPhysicalY = static_cast<float>( VIRTUAL_HEIGHT ) / windowHeight;

	transform.xScale = uniformPhysicalScale * virtualPerPhysicalX;
	transform.yScale = uniformPhysicalScale * virtualPerPhysicalY;
	transform.xOffset = ( windowWidth - drawWidth ) * 0.5f * virtualPerPhysicalX;
	transform.yOffset = ( windowHeight - drawHeight ) * 0.5f * virtualPerPhysicalY;
}

static void openQ4_CalcVirtualScreenExpansion( float width, float height, float windowWidth, float windowHeight, bool aspectCorrect, float &xExpand, float &yExpand ) {
	xExpand = 0.0f;
	yExpand = 0.0f;

	if ( !aspectCorrect || width <= 0.0f || height <= 0.0f || windowWidth <= 0.0f || windowHeight <= 0.0f ) {
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

static void openQ4_CalcCinematic16x9Bars( float width, float height, float windowWidth, float windowHeight, bool aspectCorrect, idRectangle &topBar, idRectangle &bottomBar, idRectangle &leftBar, idRectangle &rightBar, idRectangle &visibleArea ) {
	topBar.Empty();
	bottomBar.Empty();
	leftBar.Empty();
	rightBar.Empty();
	visibleArea.Empty();

	q4VirtualScreenTransform_t transform;
	openQ4_CalcVirtualScreenTransform( width, height, windowWidth, windowHeight, aspectCorrect, transform );

	if ( transform.xScale <= 0.0f || transform.yScale <= 0.0f || windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return;
	}

	const idRectangle fullArea(
		-transform.xOffset / transform.xScale,
		-transform.yOffset / transform.yScale,
		static_cast<float>( VIRTUAL_WIDTH ) / transform.xScale,
		static_cast<float>( VIRTUAL_HEIGHT ) / transform.yScale );

	if ( fullArea.w <= 0.0f || fullArea.h <= 0.0f ) {
		return;
	}

	const float physicalScaleX = transform.xScale * ( windowWidth / static_cast<float>( VIRTUAL_WIDTH ) );
	const float physicalScaleY = transform.yScale * ( windowHeight / static_cast<float>( VIRTUAL_HEIGHT ) );
	if ( physicalScaleX <= 0.0f || physicalScaleY <= 0.0f ) {
		return;
	}

	const float targetPhysicalAspect = Q4_CINEMATIC_RETAIL_ASPECT;
	const float targetLogicalAspect = targetPhysicalAspect * ( physicalScaleY / physicalScaleX );
	const float fullLogicalAspect = fullArea.w / fullArea.h;
	const float aspectEpsilon = 0.0001f;

	visibleArea = fullArea;
	if ( fullLogicalAspect > targetLogicalAspect + aspectEpsilon ) {
		visibleArea.w = fullArea.h * targetLogicalAspect;
		visibleArea.x = fullArea.x + ( fullArea.w - visibleArea.w ) * 0.5f;
	} else if ( fullLogicalAspect + aspectEpsilon < targetLogicalAspect ) {
		visibleArea.h = fullArea.w / targetLogicalAspect;
		visibleArea.y = fullArea.y + ( fullArea.h - visibleArea.h ) * 0.5f;
	}

	const float topHeight = Max( 0.0f, visibleArea.y - fullArea.y );
	const float bottomY = visibleArea.y + visibleArea.h;
	const float bottomHeight = Max( 0.0f, fullArea.Bottom() - bottomY );
	const float leftWidth = Max( 0.0f, visibleArea.x - fullArea.x );
	const float rightX = visibleArea.x + visibleArea.w;
	const float rightWidth = Max( 0.0f, fullArea.Right() - rightX );

	topBar = idRectangle( fullArea.x, fullArea.y, fullArea.w, topHeight );
	bottomBar = idRectangle( fullArea.x, bottomY, fullArea.w, bottomHeight );
	leftBar = idRectangle( fullArea.x, fullArea.y, leftWidth, fullArea.h );
	rightBar = idRectangle( rightX, fullArea.y, rightWidth, fullArea.h );
}

static float openQ4_ApplyVirtualX( const q4VirtualScreenTransform_t &transform, float x ) {
	return x * transform.xScale + transform.xOffset;
}

static float openQ4_ApplyVirtualY( const q4VirtualScreenTransform_t &transform, float y ) {
	return y * transform.yScale + transform.yOffset;
}

static bool openQ4_NearlyEqual( float actual, float expected, float epsilon = 0.001f ) {
	return idMath::Fabs( actual - expected ) <= epsilon;
}

static bool openQ4_CheckNear( const char *label, float actual, float expected, float epsilon = 0.001f ) {
	if ( openQ4_NearlyEqual( actual, expected, epsilon ) ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %.6f, expected %.6f", label, actual, expected );
	return false;
}

static bool openQ4_CheckBool( const char *label, bool actual, bool expected ) {
	if ( actual == expected ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %d, expected %d", label, actual ? 1 : 0, expected ? 1 : 0 );
	return false;
}

static bool openQ4_CheckInt( const char *label, int actual, int expected ) {
	if ( actual == expected ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %d, expected %d", label, actual, expected );
	return false;
}

struct q4GlyphClipCase_t {
	const char *label;
	float x;
	float y;
	float w;
	float h;
	float s1;
	float t1;
	float s2;
	float t2;
	bool clipped;
	float expectedX;
	float expectedY;
	float expectedW;
	float expectedH;
	float expectedS1;
	float expectedT1;
	float expectedS2;
	float expectedT2;
};

static bool openQ4_CheckGlyphClipCase( idDeviceContext &dc, const q4GlyphClipCase_t &clipCase ) {
	float x = clipCase.x;
	float y = clipCase.y;
	float w = clipCase.w;
	float h = clipCase.h;
	float s1 = clipCase.s1;
	float t1 = clipCase.t1;
	float s2 = clipCase.s2;
	float t2 = clipCase.t2;
	bool ok = true;

	const bool clipped = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= openQ4_CheckBool( va( "%s result", clipCase.label ), clipped, clipCase.clipped );
	ok &= openQ4_CheckNear( va( "%s x", clipCase.label ), x, clipCase.expectedX );
	ok &= openQ4_CheckNear( va( "%s y", clipCase.label ), y, clipCase.expectedY );
	ok &= openQ4_CheckNear( va( "%s w", clipCase.label ), w, clipCase.expectedW );
	ok &= openQ4_CheckNear( va( "%s h", clipCase.label ), h, clipCase.expectedH );
	ok &= openQ4_CheckNear( va( "%s s1", clipCase.label ), s1, clipCase.expectedS1 );
	ok &= openQ4_CheckNear( va( "%s t1", clipCase.label ), t1, clipCase.expectedT1 );
	ok &= openQ4_CheckNear( va( "%s s2", clipCase.label ), s2, clipCase.expectedS2 );
	ok &= openQ4_CheckNear( va( "%s t2", clipCase.label ), t2, clipCase.expectedT2 );
	return ok;
}

static void openQ4_SetGuiSortForFont( fontInfoEx_t &font ) {
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

static void openQ4_NormalizeFontLanguage( idStr &language ) {
	// Western European language packs share the English font artwork.
	if ( language == "french" || language == "german" || language == "spanish" || language == "italian" ) {
		language = "english";
	}
}

static void openQ4_ResolveFontFileName( const char *name, const idStr &language, idStr &fileName ) {
	fileName = name;
	if ( idStr::Icmp( fileName.c_str(), "fonts" ) == 0 ) {
		fileName = "fonts/chain";
	}
	fileName.Replace( "fonts", va( "fonts/%s", language.c_str() ) );
}

/*
================
openQ4_RegisterFontForLanguage

Languages outside the Western European set are not folded onto the English
artwork, because a localised release can ship its own atlases with the right
alphabet in them - the retail Polish build did. But openQ4's own content
directory only carries the shared faces, so a language whose folder is absent
would otherwise fail to register a single font and leave every menu blank.

Try the language folder first so a localised install still wins, then fall back
to English. The scalable path does the equivalent internally (a .ttf is looked
up under the language folder and then at the shared location), so this only
really bites the bitmap atlases, which is exactly where it used to break.
================
*/
static bool openQ4_RegisterFontForLanguage( const char *logicalName, const idStr &language, fontInfoEx_t &font, idStr &resolvedFileName ) {
	openQ4_ResolveFontFileName( logicalName, language, resolvedFileName );
	if ( renderSystem->RegisterFont( resolvedFileName, font ) ) {
		return true;
	}
	if ( idStr::Icmp( language.c_str(), "english" ) == 0 ) {
		return false;
	}

	idStr englishFileName;
	openQ4_ResolveFontFileName( logicalName, idStr( "english" ), englishFileName );
	if ( !renderSystem->RegisterFont( englishFileName, font ) ) {
		return false;
	}
	common->DPrintf( "No '%s' font artwork for %s; using %s\n",
					 language.c_str(), resolvedFileName.c_str(), englishFileName.c_str() );
	resolvedFileName = englishFileName;
	return true;
}

}

int idDeviceContext::FindFont( const char *name ) {
	EnsureFontsCurrent();

	int c = fonts.Num();

	for (int i = 0; i < c; i++) {
		if (idStr::Icmp(name, fonts[i].name) == 0) {
			openQ4_SetGuiSortForFont( fonts[i] );
			return i;
		}
	}

	// activeFont and useFont are pointers into the fonts list, and the list is
	// granularity 1, so the append below reallocates it every single time. A map
	// whose GUIs name a face nothing has asked for yet - the Strogg terminal font
	// is one, and it first appears partway through the campaign - would otherwise
	// leave both pointing at freed memory. activeFont is re-pointed by the next
	// idWindow::SetFont, but useFont is not: DrawText reads it through
	// MaxCharWidth and MaxCharHeight before SetFontByScale reselects it, so the
	// character cell and line skip the whole layout is built from would come out
	// of a freed allocation. SetFont and SetFontByScale move independently, so
	// the two can be on different entries; record each one on its own rather
	// than assuming they agree.
	int selectedFont = -1;
	int selectedSizeFont = -1;
	int selectedSize = -1;
	for ( int i = 0; i < c; i++ ) {
		if ( activeFont == &fonts[i] ) {
			selectedFont = i;
		}
		if ( useFont == &fonts[i].fontInfoSmall ) {
			selectedSizeFont = i;
			selectedSize = 0;
		} else if ( useFont == &fonts[i].fontInfoMedium ) {
			selectedSizeFont = i;
			selectedSize = 1;
		} else if ( useFont == &fonts[i].fontInfoLarge ) {
			selectedSizeFont = i;
			selectedSize = 2;
		}
	}

	// If the font was not found, try to register it
	idStr fileName;
	fontInfoEx_t fontInfo;
	int index = fonts.Append( fontInfo );

	activeFont = ( selectedFont >= 0 ) ? &fonts[selectedFont] : NULL;
	if ( selectedSizeFont >= 0 ) {
		fontInfoEx_t &sized = fonts[selectedSizeFont];
		useFont = ( selectedSize == 0 ) ? &sized.fontInfoSmall
				: ( selectedSize == 1 ) ? &sized.fontInfoMedium
				: &sized.fontInfoLarge;
	} else {
		useFont = NULL;
	}

	if ( openQ4_RegisterFontForLanguage( name, fontLang, fonts[index], fileName ) ) {
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
	openQ4_NormalizeFontLanguage( fontLang );

	// Default font has to be added first.
	FindFont( "fonts/chain" );
}

/*
================
idDeviceContext::ReloadFonts

A full vid_restart preserves GUI objects and their integer font indices while
destroying every GPU image; a partial mode change preserves the images but can
still change their required rasterisation scale. Re-register each cached
logical font in place so the TrueType scratch atlases, resolution-dependent
glyph UVs and metrics match the current viewport in either case. Keeping the
list allocation and order stable is important: parsed windows store font
indices, while this device context keeps pointers into the selected list entry.
================
*/
bool idDeviceContext::ReloadFonts() {
	if ( fonts.Num() == 0 ) {
		SetupFonts();
		SetFont( 0 );
		useFont = NULL;
		return fonts.Num() > 0 && fonts[0].name[0] != '\0';
	}

	int activeFontIndex = 0;
	int useFontSlot = -1;
	for ( int i = 0; i < fonts.Num(); ++i ) {
		if ( activeFont != &fonts[i] ) {
			continue;
		}
		activeFontIndex = i;
		if ( useFont == &fonts[i].fontInfoSmall ) {
			useFontSlot = 0;
		} else if ( useFont == &fonts[i].fontInfoMedium ) {
			useFontSlot = 1;
		} else if ( useFont == &fonts[i].fontInfoLarge ) {
			useFontSlot = 2;
		}
		break;
	}

	fontLang = cvarSystem->GetCVarString( "sys_lang" );
	openQ4_NormalizeFontLanguage( fontLang );

	bool allFontsReloaded = true;
	for ( int i = 0; i < fonts.Num(); ++i ) {
		char logicalName[sizeof( fonts[i].name )];
		idStr::Copynz( logicalName, fonts[i].name, sizeof( logicalName ) );
		if ( logicalName[0] == '\0' ) {
			allFontsReloaded = false;
			continue;
		}

		idStr fileName;
		fontInfoEx_t replacement;
		if ( !openQ4_RegisterFontForLanguage( logicalName, fontLang, replacement, fileName ) ) {
			common->Warning( "Could not reload font %s [%s] after renderer restart", logicalName, fileName.c_str() );
			allFontsReloaded = false;
			continue;
		}

		idStr::Copynz( replacement.name, logicalName, sizeof( replacement.name ) );
		fonts[i] = replacement;
	}

	SetFont( activeFontIndex );
	if ( activeFont == NULL ) {
		useFont = NULL;
	} else if ( useFontSlot == 0 ) {
		useFont = &activeFont->fontInfoSmall;
	} else if ( useFontSlot == 1 ) {
		useFont = &activeFont->fontInfoMedium;
	} else if ( useFontSlot == 2 ) {
		useFont = &activeFont->fontInfoLarge;
	} else {
		useFont = NULL;
	}

	return allFontsReloaded;
}

void idDeviceContext::EnsureFontsCurrent() {
	if ( !initialized || renderSystem == NULL || !renderSystem->IsOpenGLRunning() ) {
		return;
	}

	const int currentRestartCount = renderSystem->GetVideoRestartCount();
	// The glyph a byte maps to is fixed at rasterisation time, so switching
	// sys_lang between a Western and a Central European language invalidates
	// every atlas even though the renderer never restarted.
	const int currentCodePageGeneration = LangDict_GetCodePageGeneration();
	if ( fontsVideoRestartCount == currentRestartCount &&
		 fontsCodePageGeneration == currentCodePageGeneration ) {
		return;
	}

	// Record the generation before rebuilding so any registration path that
	// consults this context cannot recursively begin the same refresh.
	fontsVideoRestartCount = currentRestartCount;
	fontsCodePageGeneration = currentCodePageGeneration;
	if ( !ReloadFonts() ) {
		common->Warning( "could not rebuild every GUI font" );
	}
}

void idDeviceContext::SetFont( int num ) {
	EnsureFontsCurrent();

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

	// Always resolve from the authored rect rather than from the previously
	// resolved UVs, so sizing an icon more than once is idempotent.
	const float imageWidth = static_cast<float>( icon.material->GetImageWidth() );
	const float imageHeight = static_cast<float>( icon.material->GetImageHeight() );
	if ( imageWidth <= 0.0f || imageHeight <= 0.0f ) {
		// The image is not resident yet; leave the icon unsized so SizeIcons()
		// or the next lookup can pick it up once level media has loaded.
		icon.width = 0.0f;
		icon.height = 0.0f;
		icon.sized = false;
		return;
	}

	openQ4_SetEmbeddedIconAxisUV( icon.s1, icon.s2, icon.registeredX, icon.registeredWidth, imageWidth );
	openQ4_SetEmbeddedIconAxisUV( icon.t1, icon.t2, icon.registeredY, icon.registeredHeight, imageHeight );
	icon.width = static_cast<float>( openQ4_EmbeddedIconDimensionOrImageSize( icon.registeredWidth, imageWidth ) );
	icon.height = static_cast<float>( openQ4_EmbeddedIconDimensionOrImageSize( icon.registeredHeight, imageHeight ) );
	icon.sized = true;
}

void idDeviceContext::SizeIcons() {
	for ( int i = 0; i < icons.Num(); ++i ) {
		embeddedIcon_t *icon = icons.GetIndex( i );
		if ( icon != NULL && !icon->sized ) {
			SizeIcon( *icon );
		}
	}
}

bool idDeviceContext::FindIcon( const char *code, const embeddedIcon_t **icon ) {
	embeddedIcon_t *foundIcon = NULL;
	const bool found = icons.Get( code, &foundIcon );
	if ( found && foundIcon != NULL && !foundIcon->sized ) {
		// registered before its image was resident
		SizeIcon( *foundIcon );
	}
	if ( icon != NULL ) {
		*icon = foundIcon;
	}
	return found && foundIcon != NULL;
}

float idDeviceContext::GetIconDisplayWidth( const embeddedIcon_t &icon, float referenceHeight ) const {
	return static_cast<float>( openQ4_EmbeddedIconWidthUnits( icon.width, icon.height, referenceHeight, Q4_EMBEDDED_ICON_DRAW_WIDTH ) );
}

float idDeviceContext::GetKeyBindingIconHeight( float textScale, float heightRatio ) {
	const float lineHeight = static_cast<float>( MaxCharHeight( textScale ) );
	heightRatio = idMath::ClampFloat( 0.50f, 2.00f, heightRatio );
	const float minimumHeight = 6.0f * Max( 1.0f, heightRatio / Q4_KEY_BINDING_INLINE_HEIGHT_RATIO );
	return Max( minimumHeight, lineHeight * heightRatio );
}

float idDeviceContext::GetKeyBindingIconWidth( int keyNum, float textScale, float heightRatio ) {
	q4KeyBindingIconInfo_t info;
	openQ4_GetKeyBindingIconInfo( keyNum, info );
	const float height = GetKeyBindingIconHeight( textScale, heightRatio );
	float width = height;

	switch ( info.kind ) {
		case Q4_KEY_BINDING_KEYBOARD: {
			float widthClass = info.label.Length() > 1 ? 1.34f : 1.0f;
			switch ( keyNum ) {
				case K_SPACE:
					widthClass = 2.20f;
					break;
				case K_ENTER:
				case K_KP_ENTER:
				case K_BACKSPACE:
				case K_SHIFT:
				case K_CAPSLOCK:
					widthClass = 1.72f;
					break;
				case K_TAB:
				case K_CTRL:
				case K_ALT:
				case K_RIGHT_ALT:
				case K_COMMAND:
				case K_LWIN:
				case K_RWIN:
				case K_MENU:
				case K_PRINT_SCR:
					widthClass = 1.48f;
					break;
			}
			width = height * widthClass;
			break;
		}
		case Q4_KEY_BINDING_MOUSE_BUTTON:
			// Match the tall silhouette used by the rerelease device glyphs.  The
			// old 1.30:1 shape read as a horizontal keycap rather than a mouse.
			width = height * 0.74f;
			break;
		case Q4_KEY_BINDING_MOUSE_WHEEL:
			// Leave room beside the mouse for the wheel-direction arrow.
			width = height * 0.98f;
			break;
		case Q4_KEY_BINDING_PAD_MENU:
			width = info.label.Length() > 0 ? height * 1.60f : height;
			break;
		case Q4_KEY_BINDING_PAD_SHOULDER:
			width = height * 1.52f;
			break;
		case Q4_KEY_BINDING_PAD_PADDLE:
			width = height * 1.42f;
			break;
		case Q4_KEY_BINDING_PAD_GENERIC:
			width = height * 1.42f;
			break;
		case Q4_KEY_BINDING_PAD_FACE:
			width = height * 1.30f;
			break;
		case Q4_KEY_BINDING_PAD_STICK:
			width = height * 1.38f;
			break;
		case Q4_KEY_BINDING_PAD_TOUCHPAD:
			width = height * 1.52f;
			break;
		default:
			width = height;
			break;
	}

	// Give localized keyboard legends enough room to remain readable without
	// allowing an unusually long language-table value to turn a compact inline
	// hint into a banner.  The draw path performs the final exact fit inside this
	// bounded cap.
	if ( info.label.Length() > 0 &&
		( info.kind == Q4_KEY_BINDING_KEYBOARD || info.kind == Q4_KEY_BINDING_PAD_MENU || info.kind == Q4_KEY_BINDING_PAD_GENERIC ) ) {
		const float presentationScale = Min( 1.25f, heightRatio / Q4_KEY_BINDING_INLINE_HEIGHT_RATIO );
		const float labelScale = textScale * 0.54f * presentationScale;
		const float sidePadding = Max( 3.0f, height * 0.24f );
		const float labelWidth = static_cast<float>( TextWidth( info.label, labelScale, -1 ) );
		const float maximumWidth = height * ( info.kind == Q4_KEY_BINDING_PAD_GENERIC ? 1.90f : 2.20f );
		width = Min( maximumWidth, Max( width, labelWidth + sidePadding * 2.0f ) );
		SetFontByScale( textScale );
	}

	return idMath::Ceil( width );
}

void idDeviceContext::DrawKeyBindingIcon( int keyNum, float x, float baselineY, float textScale, const idVec4 &color, float heightRatio ) {
	q4KeyBindingIconInfo_t info;
	openQ4_GetKeyBindingIconInfo( keyNum, info );
	const float height = GetKeyBindingIconHeight( textScale, heightRatio );
	const float width = GetKeyBindingIconWidth( keyNum, textScale, heightRatio );
	const float y = baselineY - height * Q4_KEY_BINDING_BASELINE_ASCENT;
	const idVec4 darkFace = openQ4_KeyBindingColor( 0.045f, 0.060f, 0.075f, 0.98f, color.w );
	const idVec4 midFace = openQ4_KeyBindingColor( 0.075f, 0.095f, 0.115f, 0.98f, color.w );
	const idVec4 inactiveFace = openQ4_KeyBindingColor( 0.30f, 0.36f, 0.42f, 0.90f, color.w );
	const idVec4 accent = openQ4_KeyBindingAccent( color, 0.96f );

	switch ( info.kind ) {
		case Q4_KEY_BINDING_KEYBOARD:
			openQ4_DrawKeycapBase( this, x, y, width, height, color );
			if ( openQ4_IsKeyboardArrowKey( keyNum ) ) {
				openQ4_DrawKeyboardArrowGlyph( this, keyNum, x, y, height,
					openQ4_KeyBindingColor( 0.94f, 0.97f, 1.0f, 1.0f, color.w ) );
			}
			break;

		case Q4_KEY_BINDING_PAD_GENERIC:
			openQ4_DrawKeycapBase( this, x, y, width, height, color );
			break;

		case Q4_KEY_BINDING_MOUSE_BUTTON:
		case Q4_KEY_BINDING_MOUSE_WHEEL: {
			const float mouseWidth = height * 0.68f;
			float mouseX = x + ( width - mouseWidth ) * 0.5f;
			if ( info.kind == Q4_KEY_BINDING_MOUSE_WHEEL ) {
				mouseX = info.detail > 0 ? x + width - mouseWidth : x;
			}
			const float radius = Max( 1.0f, mouseWidth * 0.47f );
			const idVec4 mouseFrame = openQ4_KeyBindingColor( 0.42f, 0.45f, 0.48f, 0.98f, color.w );
			const idVec4 selectedButton = openQ4_KeyBindingAccent( color, 1.0f );
			openQ4_DrawSmoothRoundedFill( this, mouseX + 1.0f, y + 1.0f, mouseWidth - 1.0f, height - 1.0f, radius,
				openQ4_KeyBindingColor( 0.0f, 0.0f, 0.0f, 0.48f, color.w ) );
			openQ4_DrawSmoothRoundedFill( this, mouseX, y, mouseWidth, height, radius, mouseFrame );
			openQ4_DrawSmoothRoundedFill( this, mouseX + 1.0f, y + 1.0f, mouseWidth - 2.0f, height - 2.0f,
				Max( 0.5f, radius - 0.75f ), darkFace );

			const float splitY = y + height * 0.46f;
			const float middleX = mouseX + mouseWidth * 0.5f;
			const float buttonInset = Max( 0.75f, height * 0.055f );
			const float divider = Max( 0.75f, height * 0.025f );
			const float buttonY = y + buttonInset;
			const float buttonHeight = Max( 1.0f, splitY - buttonY - divider );
			const float buttonWidth = Max( 1.0f, mouseWidth * 0.5f - buttonInset - divider * 0.5f );
			const float rightButtonX = middleX + divider * 0.5f;
			const float buttonRadius = Max( 0.75f, mouseWidth * 0.15f );
			openQ4_DrawSmoothRoundedFill( this, mouseX + buttonInset, buttonY, buttonWidth, buttonHeight,
				buttonRadius, info.kind == Q4_KEY_BINDING_MOUSE_BUTTON && info.detail == 1 ? selectedButton : midFace );
			openQ4_DrawSmoothRoundedFill( this, rightButtonX, buttonY, buttonWidth, buttonHeight,
				buttonRadius, info.kind == Q4_KEY_BINDING_MOUSE_BUTTON && info.detail == 2 ? selectedButton : midFace );

			DrawFilledRect( mouseX + 1.0f, splitY, mouseWidth - 2.0f, 1.0f,
				openQ4_KeyBindingColor( 0.34f, 0.41f, 0.47f, 0.70f, color.w ) );
			DrawFilledRect( middleX - divider * 0.5f, y + buttonInset, divider, height * 0.38f,
				openQ4_KeyBindingColor( 0.34f, 0.41f, 0.47f, 0.70f, color.w ) );

			const float wheelW = Max( 2.0f, idMath::Floor( mouseWidth * 0.17f ) );
			const float wheelX = middleX - wheelW * 0.5f;
			const idVec4 wheelColor = ( info.kind == Q4_KEY_BINDING_MOUSE_WHEEL || info.detail == 3 ) ? selectedButton : inactiveFace;
			openQ4_DrawSmoothRoundedFill( this, wheelX, y + height * 0.13f, wheelW, height * 0.22f, wheelW * 0.5f, wheelColor );

			if ( info.kind == Q4_KEY_BINDING_MOUSE_WHEEL ) {
				const float arrowCenterX = info.detail > 0
					? x + ( width - mouseWidth ) * 0.42f
					: x + mouseWidth + ( width - mouseWidth ) * 0.58f;
				const float shaftWidth = Max( 1.0f, height * 0.045f );
				const float shaftY = y + height * 0.34f;
				const float shaftHeight = height * 0.34f;
				DrawFilledRect( arrowCenterX - shaftWidth * 0.5f, shaftY, shaftWidth, shaftHeight, selectedButton );
				const int arrowBands = 4;
				for ( int band = 0; band < arrowBands; ++band ) {
					const float bandWidth = height * ( 0.08f + 0.045f * band );
					const float bandY = info.detail > 0
						? y + height * ( 0.26f + 0.025f * band )
						: y + height * ( 0.715f - 0.025f * band );
					DrawFilledRect( arrowCenterX - bandWidth * 0.5f, bandY, bandWidth, Max( 1.0f, height * 0.028f ), selectedButton );
				}
			}

			if ( info.kind == Q4_KEY_BINDING_MOUSE_BUTTON && info.detail >= 4 ) {
				const int selectedSideButton = idMath::ClampInt( 0, 4, info.detail - 4 );
				const float sideButtonWidth = Max( 1.25f, mouseWidth * 0.12f );
				const float sideButtonHeight = Max( 1.0f, height * 0.055f );
				for ( int sideButton = 0; sideButton < 5; ++sideButton ) {
					const float sideButtonY = y + height * ( 0.54f + 0.065f * sideButton );
					openQ4_DrawSmoothRoundedFill( this, mouseX + mouseWidth * 0.06f, sideButtonY,
						sideButtonWidth, sideButtonHeight, sideButtonHeight * 0.5f,
						sideButton == selectedSideButton ? selectedButton : midFace );
				}
			}
			break;
		}

		case Q4_KEY_BINDING_PAD_FACE: {
			// A physical-position cluster remains correct for Xbox, PlayStation,
			// Nintendo and generic SDL pads without guessing a connected pad's
			// letter or colour convention.
			const float buttonSize = Max( 2.5f, height * 0.24f );
			const float centerX = x + width * 0.5f;
			const float centerY = y + height * 0.5f;
			const float radiusX = height * 0.27f;
			const float radiusY = height * 0.25f;
			const float buttonX[4] = {
				centerX - buttonSize * 0.5f,
				centerX + radiusX - buttonSize * 0.5f,
				centerX - buttonSize * 0.5f,
				centerX - radiusX - buttonSize * 0.5f
			};
			const float buttonY[4] = {
				centerY + radiusY - buttonSize * 0.5f,
				centerY - buttonSize * 0.5f,
				centerY - radiusY - buttonSize * 0.5f,
				centerY - buttonSize * 0.5f
			};
			const int selectedButton = idMath::ClampInt( 0, 3, info.detail - 3 );
			for ( int i = 0; i < 4; i++ ) {
				const idVec4 buttonFace = i == selectedButton ? accent : inactiveFace;
				openQ4_DrawSmoothRoundedFill( this, buttonX[i], buttonY[i], buttonSize, buttonSize,
					buttonSize * 0.5f, buttonFace );
			}
			break;
		}

		case Q4_KEY_BINDING_PAD_SHOULDER: {
			openQ4_DrawKeycapBase( this, x, y, width, height, color );
			const bool right = info.detail == 2 || info.detail == 15;
			const bool trigger = info.detail == 15 || info.detail == 16;
			const float half = width * 0.5f;
			const float selectedX = right ? x + half : x + 1.0f;
			const float shoulderY = trigger ? y + height * 0.48f : y + height * 0.18f;
			const float shoulderH = trigger ? height * 0.30f : height * 0.24f;
			openQ4_DrawSmoothRoundedFill( this, x + 2.0f, y + height * 0.18f, half - 3.0f, height * 0.18f, height * 0.08f, midFace );
			openQ4_DrawSmoothRoundedFill( this, x + half + 1.0f, y + height * 0.18f, half - 3.0f, height * 0.18f, height * 0.08f, midFace );
			openQ4_DrawSmoothRoundedFill( this, selectedX + 1.0f, shoulderY, half - 3.0f, shoulderH, shoulderH * 0.35f, accent );
			break;
		}

		case Q4_KEY_BINDING_PAD_DPAD: {
			const float arm = Max( 3.0f, idMath::Floor( height * 0.34f ) );
			const float centerX = x + height * 0.5f;
			const float centerY = y + height * 0.5f;
			const idVec4 shadow = openQ4_KeyBindingColor( 0.0f, 0.0f, 0.0f, 0.48f, color.w );
			openQ4_DrawSmoothRoundedFill( this, centerX - arm * 0.5f + 1.0f, y + 1.0f, arm, height - 1.0f, arm * 0.30f, shadow );
			openQ4_DrawSmoothRoundedFill( this, x + 1.0f, centerY - arm * 0.5f + 1.0f, height - 1.0f, arm, arm * 0.30f, shadow );
			openQ4_DrawSmoothRoundedFill( this, centerX - arm * 0.5f, y, arm, height, arm * 0.30f, accent );
			openQ4_DrawSmoothRoundedFill( this, x, centerY - arm * 0.5f, height, arm, arm * 0.30f, accent );
			openQ4_DrawSmoothRoundedFill( this, centerX - arm * 0.5f + 1.0f, y + 1.0f, arm - 2.0f, height - 2.0f, Max( 0.4f, arm * 0.22f ), darkFace );
			openQ4_DrawSmoothRoundedFill( this, x + 1.0f, centerY - arm * 0.5f + 1.0f, height - 2.0f, arm - 2.0f, Max( 0.4f, arm * 0.22f ), darkFace );
			if ( info.detail == 9 ) {
				openQ4_DrawSmoothRoundedFill( this, centerX - arm * 0.5f + 1.0f, y + 1.0f, arm - 2.0f, height * 0.38f, arm * 0.20f, accent );
			} else if ( info.detail == 10 ) {
				openQ4_DrawSmoothRoundedFill( this, centerX - arm * 0.5f + 1.0f, centerY + arm * 0.5f, arm - 2.0f, height * 0.38f - 1.0f, arm * 0.20f, accent );
			} else if ( info.detail == 11 ) {
				openQ4_DrawSmoothRoundedFill( this, centerX + arm * 0.5f, centerY - arm * 0.5f + 1.0f, height * 0.38f - 1.0f, arm - 2.0f, arm * 0.20f, accent );
			} else {
				openQ4_DrawSmoothRoundedFill( this, x + 1.0f, centerY - arm * 0.5f + 1.0f, height * 0.38f, arm - 2.0f, arm * 0.20f, accent );
			}
			break;
		}

		case Q4_KEY_BINDING_PAD_STICK: {
			openQ4_DrawKeycapBase( this, x, y, width, height, color );
			const float stickSize = height * 0.36f;
			const float leftX = x + width * 0.28f - stickSize * 0.5f;
			const float rightX = x + width * 0.72f - stickSize * 0.5f;
			const float stickY = y + height * 0.50f - stickSize * 0.5f;
			openQ4_DrawSmoothRoundedFill( this, leftX, stickY, stickSize, stickSize,
				stickSize * 0.5f, info.detail == 13 ? accent : midFace );
			openQ4_DrawSmoothRoundedFill( this, rightX, stickY, stickSize, stickSize,
				stickSize * 0.5f, info.detail == 14 ? accent : midFace );
			break;
		}

		case Q4_KEY_BINDING_PAD_MENU:
			if ( info.label.Length() > 0 ) {
				openQ4_DrawKeycapBase( this, x, y, width, height, color );
				break;
			}
			openQ4_DrawOctagonalBadge( this, x, y, height, color, darkFace );
			if ( info.detail == 7 ) {
				for ( int i = 0; i < 3; i++ ) {
					DrawFilledRect( x + height * 0.27f, y + height * ( 0.32f + i * 0.17f ), height * 0.46f, 1.0f, accent );
				}
			} else {
				DrawFilledRect( x + height * 0.25f, y + height * 0.34f, height * 0.22f, height * 0.24f, accent );
				DrawFilledRect( x + height * 0.52f, y + height * 0.42f, height * 0.22f, height * 0.24f, accent );
			}
			break;

		case Q4_KEY_BINDING_PAD_GUIDE:
			openQ4_DrawOctagonalBadge( this, x, y, height, color, darkFace );
			openQ4_DrawSmoothRoundedFill( this, x + height * 0.31f, y + height * 0.31f, height * 0.38f, height * 0.38f,
				height * 0.19f, accent );
			openQ4_DrawSmoothRoundedFill( this, x + height * 0.42f, y + height * 0.42f, height * 0.16f, height * 0.16f,
				height * 0.08f, darkFace );
			break;

		case Q4_KEY_BINDING_PAD_TOUCHPAD:
			openQ4_DrawKeycapBase( this, x, y, width, height, color );
			openQ4_DrawSmoothRoundedFill( this, x + width * 0.18f, y + height * 0.22f, width * 0.64f, height * 0.48f, height * 0.12f, midFace );
			DrawFilledRect( x + width * 0.24f, y + height * 0.28f, width * 0.52f, 1.0f, accent );
			DrawFilledRect( x + width * 0.24f, y + height * 0.48f, width * 0.52f, 1.0f, accent );
			break;

		case Q4_KEY_BINDING_PAD_PADDLE: {
			openQ4_DrawKeycapBase( this, x, y, width, height, color );
			const float paddleW = Max( 1.5f, width * 0.13f );
			const float paddleH = height * 0.48f;
			const float paddleY = y + height * 0.27f;
			const float paddleX[4] = {
				x + width * 0.68f,
				x + width * 0.19f,
				x + width * 0.82f,
				x + width * 0.05f
			};
			const int selectedPaddle = idMath::ClampInt( 0, 3, info.detail - 19 );
			for ( int i = 0; i < 4; i++ ) {
				openQ4_DrawSmoothRoundedFill( this, paddleX[i], paddleY, paddleW, paddleH, paddleW * 0.5f,
					i == selectedPaddle ? accent : midFace );
			}
			break;
		}
	}

	if ( info.label.Length() > 0 ) {
		const float presentationScale = Min( 1.25f, heightRatio / Q4_KEY_BINDING_INLINE_HEIGHT_RATIO );
		float labelRegionX = x;
		float labelRegionY = y;
		float labelRegionWidth = width;
		float labelRegionHeight = height;
		float labelScale = textScale * 0.68f * presentationScale;
		const float sidePadding = Max( 3.0f, height * 0.24f );
		const float availableWidth = Max( 1.0f, labelRegionWidth - sidePadding * 2.0f );
		float labelWidth = static_cast<float>( TextWidth( info.label, labelScale, -1 ) );
		if ( labelWidth > availableWidth && labelWidth > 0.0f ) {
			// Fit exactly: custom language tables may use labels much longer than
			// the stock translations, and no label may escape its keycap.
			labelScale *= availableWidth / labelWidth;
			labelWidth = static_cast<float>( TextWidth( info.label, labelScale, -1 ) );
		}
		const float labelX = labelRegionX + ( labelRegionWidth - labelWidth ) * 0.5f;
		SetFontByScale( labelScale );
		q4TextInkExtents_t labelInk;
		openQ4_FontInkExtents( useFont, labelInk );
		const float labelRenderScale = openQ4_FontRenderScale( useFont, labelScale );
		const float labelAscent = labelInk.ascent * labelRenderScale;
		const float labelDescent = labelInk.descent * labelRenderScale;
		const float centeredBaseline = labelRegionY + labelRegionHeight * 0.5f + ( labelAscent - labelDescent ) * 0.5f;
		const float labelBaseline = idMath::ClampFloat(
			labelRegionY + labelAscent,
			Max( labelRegionY + labelAscent, labelRegionY + labelRegionHeight - labelDescent - 0.5f ),
			centeredBaseline );
		const idVec4 savedDrawTextColor = drawTextColor;
		const float savedDrawTextColorAdjust = drawTextColorAdjust;
		const idVec4 labelColor = openQ4_KeyBindingColor( 0.94f, 0.97f, 1.0f, 1.0f, color.w );
		DrawText( labelX, labelBaseline, labelScale, labelColor, info.label, 0.0f, 0, Q4_TEXT_STYLE_SHADOW, -1, true, true );
		drawTextColor = savedDrawTextColor;
		drawTextColorAdjust = savedDrawTextColorAdjust;
	}

	SetFontByScale( textScale );
}

void idDeviceContext::RegisterIcon( const char *code, const char *shader, int x, int y, int w, int h ) {
	if ( code == NULL || shader == NULL || code[0] == '\0' || shader[0] == '\0' ) {
		return;
	}
	if ( ( code[0] == 'k' || code[0] == 'K' ) && code[1] != '\0' && code[2] != '\0' &&
		openQ4_HexDigitValue( code[1] ) >= 0 && openQ4_HexDigitValue( code[2] ) >= 0 ) {
		common->Warning( "RegisterIcon: '%s' is reserved for graphical key bindings", code );
		return;
	}

	embeddedIcon_t icon;
	idStr::Copynz( icon.code, code, sizeof( icon.code ) );
	icon.material = declManager->FindMaterial( shader );
	if ( icon.material == NULL ) {
		return;
	}

	const_cast<idMaterial *>( icon.material )->EnsureNotPurged();
	icon.material->SetSort( SS_GUI );
	icon.registeredX = x;
	icon.registeredY = y;
	icon.registeredWidth = w;
	icon.registeredHeight = h;
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
	if ( fonts.Num() == 0 && renderSystem != NULL ) {
		fontsVideoRestartCount = renderSystem->GetVideoRestartCount();
		fontsCodePageGeneration = LangDict_GetCodePageGeneration();
	}
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
	fontLang.Clear();
	clipRects.Clear();
	fonts.Clear();
	fontsVideoRestartCount = -1;
	fontsCodePageGeneration = -1;
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
			return true;
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

		// A quad wholly above or left of the viewport can now have a negative
		// extent. Cull it before extrapolating its atlas coordinates or passing
		// an inverted quad to the renderer (for example, scrolled chat glyphs).
		if ( *w < 0.0f || *h < 0.0f ) {
			return true;
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

	return ( *w <= 0.0f || *h <= 0.0f );
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

static ID_INLINE void openQ4_SetGuiDrawVert( idDrawVert &vert, float x, float y, float z, float s, float t ) {
	vert.Clear();
	vert.xyz.Set( x, y, z );
	vert.st.Set( s, t );
	vert.normal.Set( 0, 0, 1 );
	vert.tangents[0].Set( 1, 0, 0 );
	vert.tangents[1].Set( 0, 1, 0 );
	vert.color[0] = vert.color[1] = vert.color[2] = vert.color[3] = 255;
	vert.color2[0] = vert.color2[1] = vert.color2[2] = vert.color2[3] = 255;
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
	openQ4_SetGuiDrawVert( verts[0], x, y, 0.0f, s1, t1 );
	openQ4_SetGuiDrawVert( verts[1], x + w, y, 0.0f, s2, t1 );
	openQ4_SetGuiDrawVert( verts[2], x + w, y + h, 0.0f, s2, t2 );
	openQ4_SetGuiDrawVert( verts[3], x, y + h, 0.0f, s1, t2 );
	
	const bool hasTransform = !mat.IsIdentity();
	if ( hasTransform ) {
		for ( int i = 0; i < 4; i++ ) {
			TransformVertInVirtualSpace( verts[i], origin, mat, xScale, yScale, xOffset, yOffset );
		}
	}

	renderSystem->DrawStretchPic( &verts[0], &indexes[0], 4, 6, shader, hasTransform, 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) );
	
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
	openQ4_SetGuiDrawVert( verts[0], x, y, 0.0f, s1, t1 );
	openQ4_SetGuiDrawVert( verts[1], x + w, y, 0.0f, s2, t1 );
	openQ4_SetGuiDrawVert( verts[2], x + w, y + h, 0.0f, s2, t2 );
	openQ4_SetGuiDrawVert( verts[3], x, y + h, 0.0f, s1, t2 );

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


	renderSystem->DrawStretchPic( &verts[0], &indexes[0], 4, 6, shader, ident || angle != 0.0f, 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) );
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

void idDeviceContext::PaintGlyph( float x, float y, float scale, const fontInfo_t *font, const glyphInfo_t *glyph, const idMaterial *hShader ) {
	if ( glyph == NULL ) {
		return;
	}

	float width = glyph->width;
	float s = glyph->s;
	float s2 = glyph->s2;
	openQ4_ApplyGlyphHorizontalGuard( font, glyph, scale, x, width, s, s2 );

	// Tight stock HUD windows can otherwise clip the guarded right edge of the small marine radio font.
	const float clipRightPad = openQ4_GlyphClipRightPad( font, glyph, scale );
	const int clipIndex = clipRightPad > 0.0f && enableClipping && clipRects.Num() > 1 ? clipRects.Num() - 1 : -1;
	if ( clipIndex >= 0 ) {
		clipRects[clipIndex].w += clipRightPad;
	}
	PaintChar( x, y, width, glyph->height, scale, s, glyph->t, s2, glyph->t2, hShader );
	if ( clipIndex >= 0 ) {
		clipRects[clipIndex].w -= clipRightPad;
	}
}


void idDeviceContext::SetFontByScale(float scale) {
	EnsureFontsCurrent();

	if ( activeFont == NULL ) {
		useFont = NULL;
		return;
	}
	// The GUI scale is authored for a 640x480-era canvas, while the selected
	// atlas is sampled at the final viewport resolution. Account for that
	// physical enlargement so high-resolution displays use the 24/48-point
	// source instead of magnifying a small atlas.
	//
	// useFont is also what TextWidth, MaxCharHeight and the DrawText advances
	// read, so this may only move between atlases whose normalised metrics
	// agree. Scalable slots qualify because one face produced all three; the
	// retail .fontdat atlases do not, and moving between those would shift
	// text width, line height and right alignment on a large window.
	const float selectionScale = openQ4_FontAtlasesAreProportional( activeFont )
		? openQ4_FontSelectionScale( scale, vidWidth, vidHeight, aspectCorrect )
		: scale;
	if (selectionScale <= gui_smallFontLimit.GetFloat()) {
		useFont = &activeFont->fontInfoSmall;
		activeFont->maxHeight = activeFont->maxHeightSmall;
		activeFont->maxWidth = activeFont->maxWidthSmall;
	} else if (selectionScale <= gui_mediumFontLimit.GetFloat()) {
		useFont = &activeFont->fontInfoMedium;
		activeFont->maxHeight = activeFont->maxHeightMedium;
		activeFont->maxWidth = activeFont->maxWidthMedium;
	} else {
		useFont = &activeFont->fontInfoLarge;
		activeFont->maxHeight = activeFont->maxHeightLarge;
		activeFont->maxWidth = activeFont->maxWidthLarge;
	}
}

int idDeviceContext::DrawText(float x, float y, float scale, idVec4 color, const char *text, float adjust, int limit, int style, int cursor, bool resetEscapes, bool suppressBackground) {
	SetFontByScale( scale );
	q4ScaledFont_t scaledFont;
	scaledFont.font = useFont;
	scaledFont.renderScale = openQ4_FontRenderScale( useFont, scale );
	scaledFont.maxWidth = activeFont != NULL ? activeFont->maxWidth : 0.0f;
	scaledFont.maxHeight = activeFont != NULL ? activeFont->maxHeight : 0.0f;

	if ( !openQ4_HasRenderableFont( scaledFont ) || text == NULL || color.w == 0.0f ) {
		return 0;
	}

	if ( resetEscapes ) {
		drawTextColor = color;
		drawTextColorAdjust = 0.0f;
	}

	idVec4 currentColor = drawTextColor;

	// Accessibility backing, drawn once for the whole line before any glyph so
	// it never shows through the gaps between characters. Colour escapes inside
	// the line change the text colour but not the backing.
	const float backgroundOpacity = gui_textBackground.GetFloat();
	if ( backgroundOpacity > 0.0f && !suppressBackground ) {
		float backgroundX = 0.0f;
		float backgroundY = 0.0f;
		float backgroundWidth = 0.0f;
		float backgroundHeight = 0.0f;
		const float lineWidth = static_cast<float>( TextWidth( text, scale, limit, static_cast<int>( adjust ) ) );
		if ( openQ4_TextBackgroundRect( scaledFont, x, y, lineWidth, gui_textBackgroundPadding.GetFloat(),
										backgroundX, backgroundY, backgroundWidth, backgroundHeight ) ) {
			// Keycaps are baseline-aligned tokens rather than font glyphs.  Union
			// their full box into the accessibility backing so the lower lip and
			// label shadow never sit outside the contrast field.
			float maxBindingHeight = 0.0f;
			const char *backgroundScan = text;
			int backgroundIndex = 0;
			while ( *backgroundScan != '\0' && ( limit <= 0 || backgroundIndex < limit ) ) {
				int scanType = 0;
				const int scanLength = openQ4_TextEscapeLength( backgroundScan, &scanType );
				if ( scanLength > 0 ) {
					const char *scanPayload = backgroundScan;
					int scanPayloadLength = scanLength;
					int scanPayloadType = scanType;
					int scanSourceLength = scanLength;
					int scanRepeats = 1;
					openQ4_ResolveTextEscape( backgroundScan, scanLength, scanType, scanPayload,
						scanPayloadLength, scanPayloadType, scanSourceLength, scanRepeats );
					int scanKey = -1;
					float scanHeightRatio = Q4_KEY_BINDING_INLINE_HEIGHT_RATIO;
					if ( scanRepeats > 0 && scanPayloadType == S_ESCAPE_ICON &&
						openQ4_ExtractKeyBindingIcon( scanPayload, scanKey, &scanHeightRatio ) ) {
						maxBindingHeight = Max( maxBindingHeight, GetKeyBindingIconHeight( scale, scanHeightRatio ) );
					}
					backgroundScan += scanSourceLength;
					backgroundIndex += scanSourceLength;
				} else {
					backgroundScan++;
					backgroundIndex++;
				}
			}
			if ( maxBindingHeight > 0.0f ) {
				const float padding = gui_textBackgroundPadding.GetFloat();
				const float bindingTop = y - maxBindingHeight * Q4_KEY_BINDING_BASELINE_ASCENT - padding;
				const float bindingBottom = y + maxBindingHeight * Q4_KEY_BINDING_BASELINE_DESCENT + padding;
				const float backgroundBottom = Max( backgroundY + backgroundHeight, bindingBottom );
				backgroundY = Min( backgroundY, bindingTop );
				backgroundHeight = backgroundBottom - backgroundY;
				SetFontByScale( scale );
			}
			DrawFilledRect( backgroundX, backgroundY, backgroundWidth, backgroundHeight,
							idVec4( 0.0f, 0.0f, 0.0f, backgroundOpacity * color.w ) );
		}
	}

	renderSystem->SetColor( currentColor );

	const unsigned char *s = reinterpret_cast<const unsigned char *>( text );
	int len = idLib::SizeToInt( strlen( text ), "idDeviceContext::DrawText" );
	if ( limit > 0 && len > limit ) {
		len = limit;
	}

	int count = 0;
	while ( *s != '\0' && count < len ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( reinterpret_cast<const char *>( s ), &escapeType );
		if ( escapeLength > 0 ) {
			const unsigned char *payload = s;
			int payloadType = escapeType;
			int payloadLength = escapeLength;
			int sourceAdvance = escapeLength;
			int countAdvance = escapeLength;
			int repeats = 1;

			if ( openQ4_IsRepeatTextEscape( reinterpret_cast<const char *>( s ), escapeLength ) ) {
				payload = s + escapeLength;
				payloadLength = openQ4_TextEscapeLength( reinterpret_cast<const char *>( payload ), &payloadType );
				if ( payloadLength <= 0 ) {
					s += escapeLength;
					continue;
				}
				sourceAdvance = escapeLength + payloadLength;
				countAdvance = payloadLength;
				repeats = openQ4_TextEscapeRepeatCount( reinterpret_cast<const char *>( s ) );
			}

			for ( int repeatIndex = 0; repeatIndex < repeats; ++repeatIndex ) {
				if ( payloadType == S_ESCAPE_ICON ) {
					int keyNum = -1;
					float bindingHeightRatio = Q4_KEY_BINDING_INLINE_HEIGHT_RATIO;
					if ( openQ4_ExtractKeyBindingIcon( reinterpret_cast<const char *>( payload ), keyNum, &bindingHeightRatio ) ) {
						const float bindingWidth = GetKeyBindingIconWidth( keyNum, scale, bindingHeightRatio );
						DrawKeyBindingIcon( keyNum, x, y, scale, currentColor, bindingHeightRatio );
						x += bindingWidth;
						SetFontByScale( scale );
						renderSystem->SetColor( currentColor );
					} else {
						char iconCode[4];
						if ( !openQ4_ExtractIconCode( reinterpret_cast<const char *>( payload ), iconCode ) ) {
							continue;
						}
						const embeddedIcon_t *icon = NULL;
						if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
							const glyphInfo_t *referenceGlyph = &scaledFont.font->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
							const float referenceHeight = referenceGlyph->height;
							const float iconWidth = GetIconDisplayWidth( *icon, referenceHeight );
							if ( iconWidth > 0.0f ) {
								const float iconY = openQ4_GlyphDrawY( y, scaledFont.renderScale, referenceGlyph );
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
								openQ4_ApplyRgbTextEscapeColor( drawTextColor, currentColor, payload );
								drawTextColorAdjust = 0.0f;
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

		q4TextGlyph_t next;
		const bool drawable = openQ4_NextTextGlyph( scaledFont.font, reinterpret_cast<const char *>( s ),
													len - count, next );
		if ( !drawable ) {
			// No art anywhere for this character, not even a substitute. Skip it
			// rather than dereference nothing, but still consume its bytes so the
			// loop cannot stall on it.
			s += Max( 1, next.bytes );
			count += Max( 1, next.bytes );
			continue;
		}

		const glyphInfo_t *glyph = next.glyph;
		const idMaterial *glyphMaterial = next.material;
		const float drawX = openQ4_GlyphDrawX( x, scaledFont.renderScale, glyph );
		const float drawY = openQ4_GlyphDrawY( y, scaledFont.renderScale, glyph );

		if ( style == Q4_TEXT_STYLE_SHADOW ) {
			idVec4 shadowColor( 0.0f, 0.0f, 0.0f, currentColor[3] );
			renderSystem->SetColor( shadowColor );
			PaintGlyph( drawX + Q4_TEXT_STYLE_OFFSET, drawY + Q4_TEXT_STYLE_OFFSET, scaledFont.renderScale, scaledFont.font, glyph, glyphMaterial );
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
				PaintGlyph( drawX + offsets[i][0], drawY + offsets[i][1], scaledFont.renderScale, scaledFont.font, glyph, glyphMaterial );
			}
			renderSystem->SetColor( currentColor );
		}

		PaintGlyph( drawX, drawY, scaledFont.renderScale, scaledFont.font, glyph, glyphMaterial );

		if ( openQ4_TextCursorReached( cursor, count ) ) {
			DrawEditCursor( x, y, scale );
			cursor = Q4_TEXT_CURSOR_NONE;
		}
		x += openQ4_ScaledGlyphAdvance( scaledFont.renderScale, glyph, adjust );
		s += next.bytes;
		count += next.bytes;
	}
	if ( openQ4_TextCursorReached( cursor, count ) ) {
		DrawEditCursor( x, y, scale );
	}
	return count;
}

void idDeviceContext::CalcVirtualScaleOffset( float width, float height, float &outXScale, float &outYScale, float &outXOffset, float &outYOffset ) const {
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	openQ4_GetCurrentViewportSize( windowWidth, windowHeight );

	q4VirtualScreenTransform_t transform;
	openQ4_CalcVirtualScreenTransform( width, height, windowWidth, windowHeight, aspectCorrect, transform );
	outXScale = transform.xScale;
	outYScale = transform.yScale;
	outXOffset = transform.xOffset;
	outYOffset = transform.yOffset;
}

void idDeviceContext::GetVirtualScreenExpansion( float width, float height, float &xExpand, float &yExpand ) const {
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	openQ4_GetCurrentViewportSize( windowWidth, windowHeight );
	openQ4_CalcVirtualScreenExpansion( width, height, windowWidth, windowHeight, aspectCorrect, xExpand, yExpand );
}

void idDeviceContext::GetCinematic16x9Bars( float width, float height, idRectangle &topBar, idRectangle &bottomBar, idRectangle &leftBar, idRectangle &rightBar, idRectangle &visibleArea ) const {
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	openQ4_GetCurrentViewportSize( windowWidth, windowHeight );
	openQ4_CalcCinematic16x9Bars( width, height, windowWidth, windowHeight, aspectCorrect, topBar, bottomBar, leftBar, rightBar, visibleArea );
}

float idDeviceContext::GetCanvasAspect() const {
	float windowWidth = static_cast<float>( engineWindowState.uiViewportWidth );
	float windowHeight = static_cast<float>( engineWindowState.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( engineWindowState.vidWidth );
		windowHeight = static_cast<float>( engineWindowState.vidHeight );
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

int idDeviceContext::CharWidthForCodePoint( unsigned int codePoint, float scale, int adjust ) {
	SetFontByScale( scale );
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	const idMaterial *material = NULL;
	const glyphInfo_t *glyph = R_GlyphForCodePoint( useFont, codePoint, &material );
	if ( glyph == NULL ) {
		glyph = R_GlyphForCodePoint( useFont, Q4_TEXT_MISSING_GLYPH, &material );
	}
	if ( glyph == NULL ) {
		return 0;
	}
	return static_cast<int>( openQ4_ScaledGlyphAdvance( useScale, glyph, static_cast<float>( adjust ) ) );
}

// A single byte, read as Latin-1. Callers that step through text one byte at a
// time are measuring bytes rather than characters, so this cannot be given
// UTF-8 semantics without changing what those callers mean; the text layer
// itself uses CharWidthForCodePoint.
int idDeviceContext::CharWidth( const char c, float scale, int adjust ) {
	return CharWidthForCodePoint( (unsigned int)(unsigned char)c, scale, adjust );
}

int idDeviceContext::TextWidth( const char *text, float scale, int limit, int adjust ) {
	SetFontByScale( scale );
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( text == NULL || useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	float advanceX = 0.0f;
	float visibleRight = 0.0f;
	int index = 0;
	const unsigned char *s = reinterpret_cast<const unsigned char *>( text );
	while ( *s != '\0' && ( limit <= 0 || index < limit ) ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( reinterpret_cast<const char *>( s ), &escapeType );
		if ( escapeLength > 0 ) {
			const char *payload = reinterpret_cast<const char *>( s );
			int payloadLength = escapeLength;
			int payloadType = escapeType;
			int sourceLength = escapeLength;
			int repeats = 1;
			openQ4_ResolveTextEscape( reinterpret_cast<const char *>( s ), escapeLength, escapeType,
				payload, payloadLength, payloadType, sourceLength, repeats );
			if ( payloadType == S_ESCAPE_ICON && repeats > 0 ) {
				int keyNum = -1;
				float bindingHeightRatio = Q4_KEY_BINDING_INLINE_HEIGHT_RATIO;
				if ( openQ4_ExtractKeyBindingIcon( payload, keyNum, &bindingHeightRatio ) ) {
					const float bindingWidth = GetKeyBindingIconWidth( keyNum, scale, bindingHeightRatio ) * repeats;
					visibleRight = Max( visibleRight, advanceX + bindingWidth );
					advanceX += bindingWidth;
					SetFontByScale( scale );
				} else {
					char iconCode[4];
					if ( !openQ4_ExtractIconCode( payload, iconCode ) ) {
						s += sourceLength;
						index += sourceLength;
						continue;
					}
					const embeddedIcon_t *icon = NULL;
					if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
						const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
						const float iconWidth = static_cast<float>( openQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_DRAW_WIDTH ) );
						for ( int repeatIndex = 0; repeatIndex < repeats; ++repeatIndex ) {
							visibleRight = Max( visibleRight, advanceX + iconWidth * useScale );
							advanceX += iconWidth;
						}
					}
				}
			}
			s += sourceLength;
			index += sourceLength;
			continue;
		}

		q4TextGlyph_t next;
		if ( openQ4_NextTextGlyph( useFont, reinterpret_cast<const char *>( s ), UTF8_MAX_BYTES, next ) ) {
			visibleRight = Max( visibleRight, openQ4_GlyphVisibleRightEdge( advanceX, useFont, useScale, next.glyph ) );
			advanceX += openQ4_ScaledGlyphAdvance( useScale, next.glyph, static_cast<float>( adjust ) );
		}
		s += Max( 1, next.bytes );
		index += Max( 1, next.bytes );
	}
	return static_cast<int>( idMath::Ceil( Max( advanceX, visibleRight ) ) );
}

int idDeviceContext::TextHeight(const char *text, float scale, int limit, int adjust) {
	(void)adjust;

	SetFontByScale( scale );
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( text == NULL || useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	int maxHeight = 0;
	float maxKeyBindingHeight = 0.0f;
	int index = 0;
	const char *s = text;
	while ( *s != '\0' && ( limit <= 0 || index < limit ) ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( s, &escapeType );
		if ( escapeLength > 0 ) {
			const char *payload = s;
			int payloadLength = escapeLength;
			int payloadType = escapeType;
			int sourceLength = escapeLength;
			int repeats = 1;
			openQ4_ResolveTextEscape( s, escapeLength, escapeType, payload, payloadLength,
				payloadType, sourceLength, repeats );
			if ( payloadType == S_ESCAPE_ICON && repeats > 0 ) {
				int keyNum = -1;
				float bindingHeightRatio = Q4_KEY_BINDING_INLINE_HEIGHT_RATIO;
				if ( openQ4_ExtractKeyBindingIcon( payload, keyNum, &bindingHeightRatio ) ) {
					maxKeyBindingHeight = Max( maxKeyBindingHeight, GetKeyBindingIconHeight( scale, bindingHeightRatio ) );
					SetFontByScale( scale );
				} else {
					const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
					const int referenceHeight = openQ4_GlyphHeightUnits( referenceGlyph );
					if ( maxHeight < referenceHeight ) {
						maxHeight = referenceHeight;
					}
				}
			}
			s += sourceLength;
			index += sourceLength;
			continue;
		}

		q4TextGlyph_t next;
		if ( openQ4_TextGlyphAt( useFont, s, next ) ) {
			const int glyphHeight = openQ4_GlyphHeightUnits( next.glyph );
			if ( maxHeight < glyphHeight ) {
				maxHeight = glyphHeight;
			}
		}
		s += Max( 1, next.bytes );
		index += Max( 1, next.bytes );
	}

	return static_cast<int>( idMath::Ceil( Max( static_cast<float>( openQ4_ScaledFontUnits( useScale, maxHeight ) ), maxKeyBindingHeight ) ) );
}

bool idDeviceContext::GetMaxTextIndex( const char *text, int limit, float textScale, wrapInfo_t &wrapInfo ) {
	SetFontByScale( textScale );
	const float useScale = openQ4_FontRenderScale( useFont, textScale );
	if ( text == NULL || text[0] == '\0' || useFont == NULL || useScale == 0.0f ) {
		return false;
	}

	int width = 0;
	int index = 0;
	while ( text[index] != '\0' ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( &text[index], &escapeType );
		const char *payload = &text[index];
		int payloadLength = escapeLength;
		int payloadType = escapeType;
		int sourceLength = escapeLength;
		int repeats = 1;
		if ( escapeLength > 0 ) {
			openQ4_ResolveTextEscape( &text[index], escapeLength, escapeType, payload, payloadLength,
				payloadType, sourceLength, repeats );
		}

		// A character is one to four bytes, and maxIndex below is consumed as a
		// string length, so the step has to be the real width of the sequence -
		// splitting one would hand the caller a truncated lead byte.
		q4TextGlyph_t next;
		next.bytes = 1;
		next.glyph = NULL;
		next.codePoint = 0;
		if ( escapeLength == 0 ) {
			openQ4_TextGlyphAt( useFont, &text[index], next );
		}
		const int tokenLength = escapeLength > 0 ? sourceLength : Max( 1, next.bytes );
		int tokenWidth = 0;

		if ( payloadType == S_ESCAPE_ICON && repeats > 0 ) {
			int keyNum = -1;
			float bindingHeightRatio = Q4_KEY_BINDING_INLINE_HEIGHT_RATIO;
			if ( openQ4_ExtractKeyBindingIcon( payload, keyNum, &bindingHeightRatio ) ) {
				const float bindingWidth = GetKeyBindingIconWidth( keyNum, textScale, bindingHeightRatio );
				tokenWidth = useScale > 0.0f ? static_cast<int>( idMath::Ceil( bindingWidth * repeats / useScale ) ) : 0;
				SetFontByScale( textScale );
			} else {
				char iconCode[4];
				if ( !openQ4_ExtractIconCode( payload, iconCode ) ) {
					index += tokenLength;
					continue;
				}
				const embeddedIcon_t *icon = NULL;
				if ( FindIcon( iconCode, &icon ) ) {
					const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
					tokenWidth = repeats * openQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_REGISTERED_WIDTH );
				}
			}
		} else if ( escapeLength == 0 && next.glyph != NULL ) {
			tokenWidth = openQ4_GlyphAdvanceUnits( next.glyph, 0 );
		}

		width += tokenWidth;
		if ( openQ4_ScaledFontUnits( useScale, width ) > limit ) {
			// maxIndex is consumed as a string length.  Split before an atomic
			// escape, never in its five-byte payload; if the first token alone is
			// wider than the line, keep it whole so the wrapping caller progresses.
			wrapInfo.maxIndex = index > 0 ? index : tokenLength;
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
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || activeFont == NULL ) {
		return 0;
	}
	return openQ4_ScaledFontUnits( useScale, activeFont->maxWidth );
}

int idDeviceContext::MaxCharHeight(float scale) {
	SetFontByScale(scale);
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || activeFont == NULL ) {
		return 0;
	}
	return openQ4_ScaledFontUnits( useScale, activeFont->maxHeight );
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
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || useFont->material == NULL ) {
		return;
	}
	const glyphInfo_t *glyph = &useFont->glyphs[overStrikeMode ? Q4_OVERSTRIKE_CURSOR_GLYPH : Q4_INSERT_CURSOR_GLYPH];
	PaintGlyph( x, openQ4_GlyphDrawY( y, useScale, glyph ), useScale, useFont, glyph, useFont->material );
}

int idDeviceContext::DrawText( const char *text, float textScale, int textAlign, idVec4 color, idRectangle rectDraw, bool wrap, int cursor, bool calcOnly, idList<int> *breaks, int limit, int adjust, int style, bool chatWindow ) {
	const float charSkip = MaxCharWidth( textScale ) + 1;
	const float fontLineSkip = MaxCharHeight( textScale );
	const float contentHeight = text != NULL ? static_cast<float>( TextHeight( text, textScale, limit, adjust ) ) : 0.0f;
	const float lineSkip = Max( fontLineSkip, contentHeight );
	const float cursorSkip = ( cursor >= 0 ? charSkip : 0 );
	const int visibleCellCount = charSkip > 0.0f ? idMath::FtoiFast( rectDraw.w / charSkip ) : 0;

	SetFontByScale( textScale );
	const float useScale = openQ4_FontRenderScale( useFont, textScale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return visibleCellCount;
	}

	drawTextColor = color;
	drawTextColorAdjust = 0.0f;

	if ( breaks ) {
		breaks->Append( 0 );
	}

	if ( !( text && *text ) ) {
		if ( openQ4_ShouldDrawEmptyTextCursor( calcOnly, cursor ) ) {
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
	float y = openQ4_InitialTextBaseline( rectDraw, textAlign, lineSkip );
	int count = 0;

	while ( p != NULL ) {
		if ( openQ4_IsLineBreakChar( *p ) ) {
			lineBreak = true;
			p = openQ4_SkipPairedLineBreak( p );
		}

		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( p, &escapeType );
		const char *escapePayload = p;
		int escapePayloadLength = escapeLength;
		int escapePayloadType = escapeType;
		int escapeSourceLength = escapeLength;
		int escapeRepeats = 1;
		if ( escapeLength > 0 ) {
			openQ4_ResolveTextEscape( p, escapeLength, escapeType, escapePayload, escapePayloadLength,
				escapePayloadType, escapeSourceLength, escapeRepeats );
		}
		const bool isIconEscape = escapeLength > 0 && escapePayloadType == S_ESCAPE_ICON;
		if ( escapeLength > 0 ) {
			if ( len + escapeSourceLength < static_cast<int>( sizeof( buff ) ) ) {
				idStr::Copynz( &buff[len], p, escapeSourceLength + 1 );
			}
			if ( !isIconEscape ) {
				len += escapeSourceLength;
				p += escapeSourceLength;
				continue;
			}
		}

		// The line buffer is copied into a character at a time below, so the
		// whole sequence has to be read here; wrapping in the middle of one
		// would put a lead byte at the end of a line and a continuation byte at
		// the start of the next.
		q4TextGlyph_t charGlyph;
		charGlyph.bytes = 1;
		charGlyph.codePoint = (unsigned int)(unsigned char)*p;
		charGlyph.glyph = NULL;
		if ( escapeLength == 0 ) {
			openQ4_TextGlyphAt( useFont, p, charGlyph );
		}
		const int charBytes = Max( 1, charGlyph.bytes );

		int nextCharWidth = 0;
		if ( chatWindow && !lineBreak ) {
			if ( isIconEscape ) {
				int keyNum = -1;
				float bindingHeightRatio = Q4_KEY_BINDING_INLINE_HEIGHT_RATIO;
				if ( openQ4_ExtractKeyBindingIcon( escapePayload, keyNum, &bindingHeightRatio ) ) {
					nextCharWidth = static_cast<int>( idMath::Ceil( GetKeyBindingIconWidth( keyNum, textScale, bindingHeightRatio ) * escapeRepeats ) );
					SetFontByScale( textScale );
				} else {
					char iconCode[4];
					if ( !openQ4_ExtractIconCode( escapePayload, iconCode ) ) {
						iconCode[0] = '\0';
					}
					const embeddedIcon_t *icon = NULL;
					if ( iconCode[0] != '\0' && FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
						const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
						nextCharWidth = escapeRepeats * openQ4_ScaledFontUnits( useScale, openQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_DRAW_WIDTH ) );
					}
				}
			} else if ( openQ4_CodePointIsPrintable( charGlyph.codePoint ) ) {
				nextCharWidth = CharWidthForCodePoint( charGlyph.codePoint, textScale, adjust );
			} else {
				nextCharWidth = static_cast<int>( cursorSkip );
			}
		}

		if ( !lineBreak && ( textWidth + nextCharWidth ) > rectDraw.w && !( isIconEscape && len == 0 ) ) {
			if ( len > 0 && newLine == 0 ) {
				newLine = len;
				newLinePtr = p;
				newLineWidth = static_cast<int>( textWidth );
			}
			wordBreak = true;
		} else if ( openQ4_ShouldCaptureBreak( lineBreak, wrap, *p ) ) {
			newLine = len;
			newLinePtr = p + 1;
			newLineWidth = static_cast<int>( textWidth );
		}

		if ( lineBreak || wordBreak ) {
			const float x = openQ4_AlignedTextX( rectDraw, textAlign, newLineWidth );

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
			len += escapeSourceLength;
			p += escapeSourceLength;
		} else {
			if ( len + charBytes < static_cast<int>( sizeof( buff ) ) ) {
				for ( int i = 0; i < charBytes; i++ ) {
					buff[len++] = p[i];
				}
				buff[len] = '\0';
			}
			p += charBytes;
		}

		textWidth = static_cast<float>( TextWidth( buff, textScale, -1, adjust ) );
	}

	if ( openQ4_ShouldDrawFinalTextCursor( cursor ) ) {
		renderSystem->SetColor( color );
		DrawEditCursor( rectDraw.x, rectDraw.y + lineSkip, textScale );
	}

	return visibleCellCount;
}

bool UI_FontParity_RunSelfTest( void ) {
	bool ok = true;

	// The later cases here assert parity with the retail bitmap atlases: exact
	// glyph advances, and that the radio font resolves to the marine atlas
	// material.  The TrueType path deliberately rasterises its own glyphs and
	// binds its own atlas, so measuring it against the retail atlas is a
	// category error rather than a regression.  Say so instead of failing.
	const bool retailAtlasActive = !cvarSystem->GetCVarBool( "r_useTrueTypeFonts" );
	if ( !retailAtlasActive ) {
		common->Printf( "uiFontParitySelfTest: skipping retail atlas parity cases, "
						"r_useTrueTypeFonts is enabled; set it to 0 to run them\n" );
	}

	fontInfo_t font = {};
	font.pointSize = 12.0f;
	ok &= openQ4_CheckNear( "12 point font scale", openQ4_FontRenderScale( &font, 0.25f ), 1.0f );
	font.pointSize = 24.0f;
	ok &= openQ4_CheckNear( "24 point font scale", openQ4_FontRenderScale( &font, 0.5f ), 1.0f );
	font.pointSize = Q4_GUI_FONT_BASE_POINT_SIZE;
	ok &= openQ4_CheckNear( "48 point font scale", openQ4_FontRenderScale( &font, 1.0f ), 1.0f );
	ok &= openQ4_CheckNear( "retail viewport font selection scale",
		openQ4_FontSelectionScaleForViewport( 0.25f, 640.0f, 480.0f, 640.0f, 480.0f, true ), 0.25f );
	ok &= openQ4_CheckNear( "1080p font selection scale",
		openQ4_FontSelectionScaleForViewport( 0.25f, 640.0f, 480.0f, 1920.0f, 1080.0f, true ), 0.5625f );
	ok &= openQ4_CheckNear( "1440p font selection scale",
		openQ4_FontSelectionScaleForViewport( 0.25f, 640.0f, 480.0f, 2560.0f, 1440.0f, true ), 0.75f );

	// The enlargement above may only choose between atlases whose normalised
	// metrics agree, because the chosen atlas is also what TextWidth,
	// MaxCharHeight and the DrawText advances read. Scalable slots qualify;
	// the hand-authored retail atlases do not.
	fontInfoEx_t scalableAtlasFont = {};
	idStr::Copynz( scalableAtlasFont.fontInfoSmall.name, "ttf/marine_12", sizeof( scalableAtlasFont.fontInfoSmall.name ) );
	idStr::Copynz( scalableAtlasFont.fontInfoMedium.name, "ttf/marine_24", sizeof( scalableAtlasFont.fontInfoMedium.name ) );
	idStr::Copynz( scalableAtlasFont.fontInfoLarge.name, "ttf/marine_48", sizeof( scalableAtlasFont.fontInfoLarge.name ) );
	ok &= openQ4_CheckBool( "scalable atlases are proportional",
		openQ4_FontAtlasesAreProportional( &scalableAtlasFont ), true );

	fontInfoEx_t retailAtlasFont = {};
	idStr::Copynz( retailAtlasFont.fontInfoSmall.name, "fonts/english/marine_12.fontdat", sizeof( retailAtlasFont.fontInfoSmall.name ) );
	idStr::Copynz( retailAtlasFont.fontInfoMedium.name, "fonts/english/marine_24.fontdat", sizeof( retailAtlasFont.fontInfoMedium.name ) );
	idStr::Copynz( retailAtlasFont.fontInfoLarge.name, "fonts/english/marine_48.fontdat", sizeof( retailAtlasFont.fontInfoLarge.name ) );
	ok &= openQ4_CheckBool( "retail atlases are not proportional",
		openQ4_FontAtlasesAreProportional( &retailAtlasFont ), false );

	// A font that produced only some scalable slots must not qualify either.
	fontInfoEx_t mixedAtlasFont = scalableAtlasFont;
	idStr::Copynz( mixedAtlasFont.fontInfoLarge.name, "fonts/english/marine_48.fontdat", sizeof( mixedAtlasFont.fontInfoLarge.name ) );
	ok &= openQ4_CheckBool( "mixed atlases are not proportional",
		openQ4_FontAtlasesAreProportional( &mixedAtlasFont ), false );

	glyphInfo_t glyph = {};
	glyph.horiAdvance = 7.2f;
	glyph.height = 11.9f;
	glyph.horiBearingX = -1.5f;
	glyph.horiBearingY = 10.0f;
	ok &= openQ4_CheckNear( "rounded glyph advance", static_cast<float>( openQ4_RoundedGlyphAdvance( &glyph ) ), 8.0f );
	ok &= openQ4_CheckNear( "adjusted glyph advance units", static_cast<float>( openQ4_GlyphAdvanceUnits( &glyph, -1 ) ), 7.0f );
	ok &= openQ4_CheckNear( "glyph height units", static_cast<float>( openQ4_GlyphHeightUnits( &glyph ) ), 11.0f );
	ok &= openQ4_CheckNear( "scaled glyph advance", openQ4_ScaledGlyphAdvance( 1.0f, &glyph, -1.0f ), 7.0f );
	ok &= openQ4_CheckNear( "glyph draw x bearing", openQ4_GlyphDrawX( 20.0f, 2.0f, &glyph ), 17.0f );
	ok &= openQ4_CheckNear( "glyph draw y bearing", openQ4_GlyphDrawY( 30.0f, 2.0f, &glyph ), 10.0f );

	glyphInfo_t guardedGlyph = {};
	guardedGlyph.width = 8.5625f;
	guardedGlyph.s = 145.0f / 256.0f;
	guardedGlyph.s2 = 153.5625f / 256.0f;
	fontInfo_t smallGuardFont = {};
	smallGuardFont.pointSize = 12.0f;
	float guardedX = 20.0f;
	float guardedW = guardedGlyph.width;
	float guardedS1 = guardedGlyph.s;
	float guardedS2 = guardedGlyph.s2;
	ok &= openQ4_CheckBool( "glyph horizontal guard applied", openQ4_ApplyGlyphHorizontalGuard( &smallGuardFont, &guardedGlyph, 0.8f, guardedX, guardedW, guardedS1, guardedS2 ), true );
	ok &= openQ4_CheckNear( "glyph horizontal guard x", guardedX, 19.2f );
	ok &= openQ4_CheckNear( "glyph horizontal guard width", guardedW, 10.5625f );
	ok &= openQ4_CheckNear( "glyph horizontal guard s1", guardedS1, 144.0f / 256.0f );
	ok &= openQ4_CheckNear( "glyph horizontal guard s2", guardedS2, 154.5625f / 256.0f );

	fontInfo_t mediumGuardFont = {};
	mediumGuardFont.pointSize = 24.0f;
	guardedX = 20.0f;
	guardedW = guardedGlyph.width;
	guardedS1 = guardedGlyph.s;
	guardedS2 = guardedGlyph.s2;
	ok &= openQ4_CheckBool( "medium glyph horizontal guard applied", openQ4_ApplyGlyphHorizontalGuard( &mediumGuardFont, &guardedGlyph, 0.8f, guardedX, guardedW, guardedS1, guardedS2 ), true );
	ok &= openQ4_CheckNear( "medium glyph horizontal guard x", guardedX, 19.6f );
	ok &= openQ4_CheckNear( "medium glyph horizontal guard width", guardedW, 9.5625f );
	ok &= openQ4_CheckNear( "medium glyph horizontal guard s1", guardedS1, 144.5f / 256.0f );
	ok &= openQ4_CheckNear( "medium glyph horizontal guard s2", guardedS2, 154.0625f / 256.0f );

	fontInfo_t marineClipFont = {};
	marineClipFont.pointSize = 12.0f;
	idStr::Copynz( marineClipFont.name, "fonts/english/marine_12.fontdat", sizeof( marineClipFont.name ) );
	glyphInfo_t &marineClipGlyph = marineClipFont.glyphs[static_cast<unsigned char>( 'g' )];
	marineClipGlyph.width = 8.296875f;
	marineClipGlyph.height = 7.078125f;
	marineClipGlyph.s = 231.0f / 256.0f;
	marineClipGlyph.s2 = 239.296875f / 256.0f;
	ok &= openQ4_CheckNear( "marine small glyph clip pad", openQ4_GlyphClipRightPad( &marineClipFont, &marineClipGlyph, 0.8f ), 2.0f );
	// Accessibility text backing. These are font-path independent, so they run
	// whichever glyph source is active.
	fontInfo_t inkFont = {};
	inkFont.glyphs['H'].horiBearingY = 10.0f;
	inkFont.glyphs['H'].height = 10.0f;
	inkFont.glyphs['g'].horiBearingY = 7.0f;
	inkFont.glyphs['g'].height = 9.0f;		// two units below the baseline
	inkFont.glyphs[' '].horiBearingY = 99.0f;	// blank glyphs must not count
	inkFont.glyphs[' '].height = 0.0f;
	q4TextInkExtents_t ink;
	openQ4_FontInkExtents( &inkFont, ink );
	ok &= openQ4_CheckNear( "font ink ascent", ink.ascent, 10.0f );
	ok &= openQ4_CheckNear( "font ink descent", ink.descent, 2.0f );

	q4ScaledFont_t inkScaledFont;
	inkScaledFont.font = &inkFont;
	inkScaledFont.renderScale = 2.0f;
	inkScaledFont.maxWidth = 0.0f;
	inkScaledFont.maxHeight = 0.0f;
	float backX = 0.0f;
	float backY = 0.0f;
	float backW = 0.0f;
	float backH = 0.0f;
	ok &= openQ4_CheckBool( "text background rect built", openQ4_TextBackgroundRect( inkScaledFont, 100.0f, 50.0f, 40.0f, 3.0f, backX, backY, backW, backH ), true );
	ok &= openQ4_CheckNear( "text background x", backX, 97.0f );
	ok &= openQ4_CheckNear( "text background y", backY, 27.0f );
	ok &= openQ4_CheckNear( "text background width", backW, 46.0f );
	ok &= openQ4_CheckNear( "text background height", backH, 30.0f );
	ok &= openQ4_CheckBool( "empty text draws no background", openQ4_TextBackgroundRect( inkScaledFont, 100.0f, 50.0f, 0.0f, 3.0f, backX, backY, backW, backH ), false );

	ok &= openQ4_CheckNear( "embedded icon draw width units", static_cast<float>( openQ4_EmbeddedIconWidthUnits( 32.0f, 16.0f, 12.0f, Q4_EMBEDDED_ICON_DRAW_WIDTH ) ), 24.0f );
	ok &= openQ4_CheckNear( "embedded icon registered width units", static_cast<float>( openQ4_EmbeddedIconWidthUnits( 32.0f, 16.0f, 12.0f, Q4_EMBEDDED_ICON_REGISTERED_WIDTH ) ), 32.0f );
	ok &= openQ4_CheckNear( "embedded icon full-image dimension", static_cast<float>( openQ4_EmbeddedIconDimensionOrImageSize( Q4_EMBEDDED_ICON_FULL_IMAGE, 64.0f ) ), 64.0f );
	ok &= openQ4_CheckNear( "embedded icon registered dimension", static_cast<float>( openQ4_EmbeddedIconDimensionOrImageSize( 24, 64.0f ) ), 24.0f );

	int keyBindingNumber = -1;
	float keyBindingHeightRatio = 0.0f;
	ok &= openQ4_CheckBool( "keyboard binding escape decoded", openQ4_ExtractKeyBindingIcon( "^ik20", keyBindingNumber, &keyBindingHeightRatio ), true );
	ok &= openQ4_CheckNear( "keyboard binding escape value", static_cast<float>( keyBindingNumber ), static_cast<float>( K_SPACE ) );
	ok &= openQ4_CheckNear( "inline binding height ratio", keyBindingHeightRatio, Q4_KEY_BINDING_INLINE_HEIGHT_RATIO );
	ok &= openQ4_CheckBool( "prompt binding escape decoded", openQ4_ExtractKeyBindingIcon( "^iK20", keyBindingNumber, &keyBindingHeightRatio ), true );
	ok &= openQ4_CheckNear( "prompt binding escape value", static_cast<float>( keyBindingNumber ), static_cast<float>( K_SPACE ) );
	ok &= openQ4_CheckNear( "prompt binding height ratio", keyBindingHeightRatio, Q4_KEY_BINDING_PROMPT_HEIGHT_RATIO );
	ok &= openQ4_CheckBool( "mouse binding escape decoded", openQ4_ExtractKeyBindingIcon( "^ikbb", keyBindingNumber ), true );
	ok &= openQ4_CheckNear( "mouse binding escape value", static_cast<float>( keyBindingNumber ), static_cast<float>( K_MOUSE1 ) );
	ok &= openQ4_CheckBool( "controller binding escape decoded", openQ4_ExtractKeyBindingIcon( "^ikc5", keyBindingNumber ), true );
	ok &= openQ4_CheckNear( "controller binding escape value", static_cast<float>( keyBindingNumber ), static_cast<float>( K_JOY1 ) );
	ok &= openQ4_CheckBool( "malformed binding escape rejected", openQ4_ExtractKeyBindingIcon( "^ikzz", keyBindingNumber ), false );
	const char *repeatedBinding = "^N3^ik20";
	int repeatedEscapeType = 0;
	const int repeatedEscapeLength = openQ4_TextEscapeLength( repeatedBinding, &repeatedEscapeType );
	const char *repeatedPayload = repeatedBinding;
	int repeatedPayloadLength = repeatedEscapeLength;
	int repeatedPayloadType = repeatedEscapeType;
	int repeatedSourceLength = repeatedEscapeLength;
	int repeatedCount = 1;
	openQ4_ResolveTextEscape( repeatedBinding, repeatedEscapeLength, repeatedEscapeType, repeatedPayload,
		repeatedPayloadLength, repeatedPayloadType, repeatedSourceLength, repeatedCount );
	ok &= openQ4_CheckNear( "binding repeat count", static_cast<float>( repeatedCount ), 3.0f );
	ok &= openQ4_CheckNear( "binding repeat source length", static_cast<float>( repeatedSourceLength ), static_cast<float>( strlen( repeatedBinding ) ) );
	ok &= openQ4_CheckNear( "binding repeat payload type", static_cast<float>( repeatedPayloadType ), static_cast<float>( S_ESCAPE_ICON ) );
	ok &= openQ4_CheckBool( "binding repeat payload decoded", openQ4_ExtractKeyBindingIcon( repeatedPayload, keyBindingNumber ), true );
	ok &= openQ4_CheckNear( "binding repeat payload value", static_cast<float>( keyBindingNumber ), static_cast<float>( K_SPACE ) );
	q4KeyBindingIconInfo_t keyBindingInfo;
	openQ4_GetKeyBindingIconInfo( K_SPACE, keyBindingInfo );
	ok &= openQ4_CheckNear( "keyboard binding presentation kind", static_cast<float>( keyBindingInfo.kind ), static_cast<float>( Q4_KEY_BINDING_KEYBOARD ) );
	openQ4_GetKeyBindingIconInfo( K_MWHEELUP, keyBindingInfo );
	ok &= openQ4_CheckNear( "mouse wheel presentation kind", static_cast<float>( keyBindingInfo.kind ), static_cast<float>( Q4_KEY_BINDING_MOUSE_WHEEL ) );
	openQ4_GetKeyBindingIconInfo( K_JOY9, keyBindingInfo );
	ok &= openQ4_CheckNear( "controller dpad presentation kind", static_cast<float>( keyBindingInfo.kind ), static_cast<float>( Q4_KEY_BINDING_PAD_DPAD ) );

	float iconUv1 = -1.0f;
	float iconUv2 = -1.0f;
	openQ4_SetEmbeddedIconAxisUV( iconUv1, iconUv2, Q4_EMBEDDED_ICON_FULL_IMAGE, 16, 64.0f );
	ok &= openQ4_CheckNear( "embedded icon full-image uv1", iconUv1, 0.0f );
	ok &= openQ4_CheckNear( "embedded icon full-image uv2", iconUv2, 1.0f );
	openQ4_SetEmbeddedIconAxisUV( iconUv1, iconUv2, 8, 16, 64.0f );
	ok &= openQ4_CheckNear( "embedded icon sprite uv1", iconUv1, 0.125f );
	ok &= openQ4_CheckNear( "embedded icon sprite uv2", iconUv2, 0.375f );

	idRectangle alignRect( 100.0f, 50.0f, 200.0f, 40.0f );
	ok &= openQ4_CheckNear( "text left align x", openQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_LEFT, 40 ), 100.0f );
	ok &= openQ4_CheckNear( "text center align x", openQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_CENTER, 40 ), 180.0f );
	ok &= openQ4_CheckNear( "text right align x", openQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_RIGHT, 40 ), 260.0f );

	int verticalAlign = Q4_TEXT_ALIGN_VERTICAL_CENTER;
	idRectangle baselineRect( 100.0f, 50.0f, 200.0f, 40.0f );
	ok &= openQ4_CheckNear( "vertical-center baseline", openQ4_InitialTextBaseline( baselineRect, verticalAlign, 12.0f ), 76.0f );
	ok &= openQ4_CheckNear( "vertical-center align reset", static_cast<float>( verticalAlign ), static_cast<float>( idDeviceContext::ALIGN_LEFT ) );
	ok &= openQ4_CheckBool( "empty calcOnly cursor draw", openQ4_ShouldDrawEmptyTextCursor( true, 0 ), false );
	ok &= openQ4_CheckBool( "final calcOnly cursor draw", openQ4_ShouldDrawFinalTextCursor( 0 ), true );
	ok &= openQ4_CheckBool( "final hidden cursor draw", openQ4_ShouldDrawFinalTextCursor( Q4_TEXT_CURSOR_NONE ), false );

	idVec4 rgbEscapeDrawColor( 0.25f, 0.5f, 0.75f, 0.42f );
	idVec4 rgbEscapeCurrentColor( 1.0f, 1.0f, 1.0f, 0.9f );
	const unsigned char rgbEscape[] = { '^', 'c', '9', '4', '1', '\0' };
	openQ4_ApplyRgbTextEscapeColor( rgbEscapeDrawColor, rgbEscapeCurrentColor, rgbEscape );
	ok &= openQ4_CheckNear( "rgb escape red", rgbEscapeCurrentColor[0], 1.0f );
	ok &= openQ4_CheckNear( "rgb escape green", rgbEscapeCurrentColor[1], 4.0f / 9.0f );
	ok &= openQ4_CheckNear( "rgb escape blue", rgbEscapeCurrentColor[2], 1.0f / 9.0f );
	ok &= openQ4_CheckNear( "rgb escape preserves alpha", rgbEscapeCurrentColor[3], 0.42f );
	ok &= openQ4_CheckNear( "rgb escape draw color alpha", rgbEscapeDrawColor[3], 0.42f );

	idDeviceContext radioFontDc;
	radioFontDc.Init();
	const int radioFont = radioFontDc.FindFont( "fonts/marine" );
	// With the TrueType path disabled and no retail .fontdat atlases installed
	// (an assetless validation startup), no glyph source exists at all, so
	// registration cannot succeed.  That is a missing environment rather than
	// a font regression - say so instead of failing, like the TrueType skip
	// above.  With the TrueType path enabled, or any .fontdat present,
	// registration is still required to succeed.
	const bool marineSourceMissing = retailAtlasActive && radioFont < 0 &&
		fileSystem->ReadFile( "fonts/english/marine_12.fontdat", NULL, NULL ) <= 0;
	if ( marineSourceMissing ) {
		common->Printf( "uiFontParitySelfTest: skipping marine font cases, "
						"no marine glyph source is installed\n" );
	} else {
		ok &= openQ4_CheckBool( "hud radio marine font registered", radioFont >= 0, true );
	}
	// The measurements below are keyed to the retail atlas's exact advances.
	if ( radioFont >= 0 && retailAtlasActive ) {
		radioFontDc.SetFont( radioFont );
		const float radioFontScale = 0.2f / 12.0f * Q4_GUI_FONT_BASE_POINT_SIZE;
		const float incomingGlyphBearingX = 1.140625f;
		const float incomingGlyphBearingY = 7.078125f;
		const float incomingGlyphWidth = 6.484375f;
		const float incomingGlyphHeight = 7.078125f;
		glyphInfo_t incomingGlyph = {};
		incomingGlyph.width = incomingGlyphWidth;
		incomingGlyph.height = incomingGlyphHeight;
		incomingGlyph.horiBearingY = incomingGlyphBearingY;
		incomingGlyph.s = 20.0f / 256.0f;
		incomingGlyph.t = 65.0f / 128.0f;
		incomingGlyph.s2 = 26.484375f / 256.0f;
		incomingGlyph.t2 = 72.078125f / 128.0f;
		idRectangle radioIncomingTextRect( 545.0f + 2.0f, 6.0f + 2.0f, 81.0f - 2.0f, 12.0f - 2.0f );
		idRectangle radioTransmissionTextRect( 545.0f + 2.0f, 13.0f + 2.0f, 81.0f - 2.0f, 12.0f - 2.0f );
		int radioIncomingTextAlign = idDeviceContext::ALIGN_LEFT;
		int radioTransmissionTextAlign = idDeviceContext::ALIGN_LEFT;
		const float radioLineHeight = static_cast<float>( radioFontDc.MaxCharHeight( 0.2f ) );
		const float radioIncomingBaseline = openQ4_InitialTextBaseline( radioIncomingTextRect, radioIncomingTextAlign, radioLineHeight );
		const float radioTransmissionBaseline = openQ4_InitialTextBaseline( radioTransmissionTextRect, radioTransmissionTextAlign, radioLineHeight );
		const float incomingGlyphX = radioIncomingTextRect.x + radioFontScale * incomingGlyphBearingX;
		const float incomingGlyphY = openQ4_GlyphDrawY( radioIncomingBaseline, radioFontScale, &incomingGlyph );
		const float transmissionGlyphX = radioTransmissionTextRect.x + radioFontScale * incomingGlyphBearingX;
		const float transmissionGlyphY = openQ4_GlyphDrawY( radioTransmissionBaseline, radioFontScale, &incomingGlyph );
		const float incomingGlyphH = incomingGlyphHeight * radioFontScale;
		float guardedIncomingGlyphX = incomingGlyphX;
		float guardedIncomingGlyphWidth = incomingGlyphWidth;
		float guardedIncomingGlyphS1 = incomingGlyph.s;
		float guardedIncomingGlyphS2 = incomingGlyph.s2;
		ok &= openQ4_CheckBool( "hud radio glyph horizontal guard applied", openQ4_ApplyGlyphHorizontalGuard( &smallGuardFont, &incomingGlyph, radioFontScale, guardedIncomingGlyphX, guardedIncomingGlyphWidth, guardedIncomingGlyphS1, guardedIncomingGlyphS2 ), true );
		const float guardedIncomingGlyphW = guardedIncomingGlyphWidth * radioFontScale;

		ok &= openQ4_CheckNear( "hud radio marine scale", radioFontScale, 0.8f );
		ok &= openQ4_CheckNear( "hud radio marine line height", radioLineHeight, 11.0f );
		ok &= openQ4_CheckNear( "hud radio incoming width", static_cast<float>( radioFontDc.TextWidth( "incoming", 0.2f, 0, 0 ) ), 57.0f );
		ok &= openQ4_CheckNear( "hud radio transmission width", static_cast<float>( radioFontDc.TextWidth( "transmission", 0.2f, 0, 0 ) ), 84.0f );
		ok &= openQ4_CheckNear( "hud radio communication width", static_cast<float>( radioFontDc.TextWidth( "communication", 0.2f, 0, 0 ) ), 92.0f );
		ok &= openQ4_CheckNear( "loading title visual width", static_cast<float>( radioFontDc.TextWidth( "Data Networking Tower", 0.36f, 0, -2 ) ), 223.0f );
		idRectangle loadingTitleRect( 290.0f, 11.0f, 336.0f, 20.0f );
		ok &= openQ4_CheckNear( "loading title right align x", openQ4_AlignedTextX( loadingTitleRect, idDeviceContext::ALIGN_RIGHT, radioFontDc.TextWidth( "Data Networking Tower", 0.36f, 0, -2 ) ), 403.0f );
		ok &= openQ4_CheckNear( "hud radio incoming text height", static_cast<float>( radioFontDc.TextHeight( "incoming", 0.2f, 0, 0 ) ), 5.0f );
		ok &= openQ4_CheckNear( "hud radio incoming baseline", radioIncomingBaseline, 19.0f );
		ok &= openQ4_CheckNear( "hud radio transmission baseline", radioTransmissionBaseline, 26.0f );
		ok &= openQ4_CheckNear( "hud radio incoming glyph y", incomingGlyphY, 13.3375f );
		ok &= openQ4_CheckNear( "hud radio incoming glyph bottom", incomingGlyphY + incomingGlyphH, 19.0f );
		ok &= openQ4_CheckNear( "hud radio incoming unclipped overhang", incomingGlyphY + incomingGlyphH - radioIncomingTextRect.Bottom(), 1.0f );
		ok &= openQ4_CheckNear( "hud radio transmission glyph y", transmissionGlyphY, 20.3375f );
		ok &= openQ4_CheckNear( "hud radio transmission glyph bottom", transmissionGlyphY + incomingGlyphH, 26.0f );
		ok &= openQ4_CheckNear( "hud radio transmission unclipped overhang", transmissionGlyphY + incomingGlyphH - radioTransmissionTextRect.Bottom(), 1.0f );
		ok &= openQ4_CheckNear( "hud radio guarded glyph x", guardedIncomingGlyphX, 547.1125f );
		ok &= openQ4_CheckNear( "hud radio guarded glyph width", guardedIncomingGlyphW, 6.7875f );
		ok &= openQ4_CheckNear( "hud radio guarded glyph s1", guardedIncomingGlyphS1, 19.0f / 256.0f );
		ok &= openQ4_CheckNear( "hud radio guarded glyph s2", guardedIncomingGlyphS2, 27.484375f / 256.0f );

		idDeviceContext radioClipDc;
		radioClipDc.EnableClipping( true );
		radioClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
		radioClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
		ok &= openQ4_CheckGlyphClipCase( radioClipDc, {
			"hud radio incoming parent clip",
			guardedIncomingGlyphX, incomingGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2,
			false,
			guardedIncomingGlyphX, incomingGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2
		} );
		ok &= openQ4_CheckGlyphClipCase( radioClipDc, {
			"hud radio transmission parent clip",
			transmissionGlyphX - ( incomingGlyphX - guardedIncomingGlyphX ), transmissionGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2,
			false,
			transmissionGlyphX - ( incomingGlyphX - guardedIncomingGlyphX ), transmissionGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2
		} );
	}

	idStr fontAtlasLang = cvarSystem->GetCVarString( "sys_lang" );
	openQ4_NormalizeFontLanguage( fontAtlasLang );
	// The TrueType path never touches the retail .fontdat atlas, so this only
	// means anything while the bitmap fonts are in charge - and only when a
	// retail atlas exists to measure against at all.
	if ( retailAtlasActive && !marineSourceMissing ) {
		const idMaterial *fontAtlasMaterial = declManager->FindMaterial( va( "fonts/%s/marine_12.fontdat", fontAtlasLang.c_str() ), false );
		if ( fontAtlasMaterial == NULL && idStr::Icmp( fontAtlasLang.c_str(), "english" ) != 0 ) {
			// A language with no atlases of its own falls back to the English
			// artwork, the same way openQ4_RegisterFontForLanguage does.
			fontAtlasLang = "english";
			fontAtlasMaterial = declManager->FindMaterial( va( "fonts/%s/marine_12.fontdat", fontAtlasLang.c_str() ), false );
		}
		ok &= openQ4_CheckBool( "hud radio marine atlas material", fontAtlasMaterial != NULL, true );
		if ( fontAtlasMaterial != NULL && fontAtlasMaterial->GetNumStages() > 0 ) {
			materialImageInfo_t fontAtlasInfo;
			const bool fontAtlasImagePresent = renderSystem->GetMaterialStageImageInfo( fontAtlasMaterial, 0, fontAtlasInfo );
			ok &= openQ4_CheckBool( "hud radio marine atlas image", fontAtlasImagePresent, true );
			if ( fontAtlasImagePresent ) {
				ok &= openQ4_CheckBool( "hud radio marine atlas format", fontAtlasInfo.isDXT1Compressed, true );
				ok &= openQ4_CheckBool( "hud radio marine atlas color format", fontAtlasInfo.usesGreenAlphaColorFormat, true );
				ok &= openQ4_CheckInt( "hud radio marine atlas mip levels", fontAtlasInfo.numLevels, 4 );
			}
		}
	}

	q4VirtualScreenTransform_t transform;
	openQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 640.0f, 480.0f, true, transform );
	ok &= openQ4_CheckNear( "4:3 x scale", transform.xScale, 1.0f );
	ok &= openQ4_CheckNear( "4:3 y scale", transform.yScale, 1.0f );
	ok &= openQ4_CheckNear( "4:3 x offset", transform.xOffset, 0.0f );
	ok &= openQ4_CheckNear( "4:3 y offset", transform.yOffset, 0.0f );

	openQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 1920.0f, 1080.0f, true, transform );
	ok &= openQ4_CheckNear( "wide x scale", transform.xScale, 0.75f );
	ok &= openQ4_CheckNear( "wide y scale", transform.yScale, 1.0f );
	ok &= openQ4_CheckNear( "wide x offset", transform.xOffset, 80.0f );
	ok &= openQ4_CheckNear( "wide y offset", transform.yOffset, 0.0f );

	float wideExpand = 0.0f;
	float tallExpand = 0.0f;
	openQ4_CalcVirtualScreenExpansion( 640.0f, 480.0f, 1920.0f, 1080.0f, true, wideExpand, tallExpand );
	ok &= openQ4_CheckNear( "wide expansion x", wideExpand, 106.666664f );
	ok &= openQ4_CheckNear( "wide expansion y", tallExpand, 0.0f );
	ok &= openQ4_CheckNear( "wide expanded left edge", openQ4_ApplyVirtualX( transform, -wideExpand ), 0.0f );
	ok &= openQ4_CheckNear( "wide authored left edge", openQ4_ApplyVirtualX( transform, 0.0f ), 80.0f );
	ok &= openQ4_CheckNear( "wide authored right edge", openQ4_ApplyVirtualX( transform, 640.0f ), 560.0f );
	ok &= openQ4_CheckNear( "wide expanded right edge", openQ4_ApplyVirtualX( transform, 640.0f + wideExpand ), 640.0f );

	openQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 1080.0f, 1920.0f, true, transform );
	ok &= openQ4_CheckNear( "tall x scale", transform.xScale, 1.0f );
	ok &= openQ4_CheckNear( "tall y scale", transform.yScale, 0.421875f );
	ok &= openQ4_CheckNear( "tall x offset", transform.xOffset, 0.0f );
	ok &= openQ4_CheckNear( "tall y offset", transform.yOffset, 138.75f );

	float xExpand = 0.0f;
	float yExpand = 0.0f;
	openQ4_CalcVirtualScreenExpansion( 640.0f, 480.0f, 1080.0f, 1920.0f, true, xExpand, yExpand );
	ok &= openQ4_CheckNear( "tall expansion x", xExpand, 0.0f );
	ok &= openQ4_CheckNear( "tall expansion y", yExpand, 328.888885f );
	ok &= openQ4_CheckNear( "tall expanded top edge", openQ4_ApplyVirtualY( transform, -yExpand ), 0.0f );
	ok &= openQ4_CheckNear( "tall expanded bottom edge", openQ4_ApplyVirtualY( transform, 480.0f + yExpand ), 480.0f );

	idRectangle cinematicTop;
	idRectangle cinematicBottom;
	idRectangle cinematicLeft;
	idRectangle cinematicRight;
	idRectangle cinematicVisible;
	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 640.0f, 480.0f, true, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "4:3 cinematic top bar height", cinematicTop.h, 60.0f );
	ok &= openQ4_CheckNear( "4:3 cinematic bottom bar y", cinematicBottom.y, 420.0f );
	ok &= openQ4_CheckNear( "4:3 cinematic visible width", cinematicVisible.w, Q4_CINEMATIC_RETAIL_VISIBLE_WIDTH );
	ok &= openQ4_CheckNear( "4:3 cinematic visible height", cinematicVisible.h, Q4_CINEMATIC_RETAIL_VISIBLE_HEIGHT );
	ok &= openQ4_CheckNear( "4:3 cinematic left pillar width", cinematicLeft.w, 0.0f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 1080.0f, 1920.0f, true, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "tall cinematic top bar height", cinematicTop.h, 388.888885f );
	ok &= openQ4_CheckNear( "tall cinematic bottom bar y", cinematicBottom.y, 420.0f );
	ok &= openQ4_CheckNear( "tall cinematic visible height", cinematicVisible.h, Q4_CINEMATIC_RETAIL_VISIBLE_HEIGHT );
	ok &= openQ4_CheckNear( "tall cinematic left pillar width", cinematicLeft.w, 0.0f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 1920.0f, 1080.0f, true, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "16:9 cinematic top bar height", cinematicTop.h, 0.0f );
	ok &= openQ4_CheckNear( "16:9 cinematic left pillar width", cinematicLeft.w, 0.0f );
	ok &= openQ4_CheckNear( "16:9 cinematic visible width", cinematicVisible.w, 853.333313f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 2560.0f, 1080.0f, true, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "ultrawide cinematic top bar height", cinematicTop.h, 0.0f );
	ok &= openQ4_CheckNear( "ultrawide cinematic left pillar width", cinematicLeft.w, 142.222229f );
	ok &= openQ4_CheckNear( "ultrawide cinematic right pillar width", cinematicRight.w, 142.222229f );
	ok &= openQ4_CheckNear( "ultrawide cinematic visible width", cinematicVisible.w, 853.333313f );

	openQ4_CalcVirtualScreenTransform( 320.0f, 240.0f, 1920.0f, 1080.0f, false, transform );
	ok &= openQ4_CheckNear( "retail x scale without aspect correction", transform.xScale, 2.0f );
	ok &= openQ4_CheckNear( "retail y scale without aspect correction", transform.yScale, 2.0f );
	ok &= openQ4_CheckNear( "retail x offset without aspect correction", transform.xOffset, 0.0f );
	ok &= openQ4_CheckNear( "retail y offset without aspect correction", transform.yOffset, 0.0f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 1920.0f, 1080.0f, false, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "stretched 16:9 cinematic top bar height", cinematicTop.h, 0.0f );
	ok &= openQ4_CheckNear( "stretched 16:9 cinematic left pillar width", cinematicLeft.w, 0.0f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 2560.0f, 1080.0f, false, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "stretched ultrawide cinematic left pillar width", cinematicLeft.w, 80.0f );
	ok &= openQ4_CheckNear( "stretched ultrawide cinematic visible width", cinematicVisible.w, 480.0f );

	idDeviceContext baseClipDc;
	baseClipDc.EnableClipping( true );
	baseClipDc.PushClipRect( idRectangle( 100.0f, 100.0f, 50.0f, 40.0f ) );
	ok &= openQ4_CheckGlyphClipCase( baseClipDc, {
		"retail base clip skip",
		90.0f, 110.0f, 20.0f, 10.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		false,
		90.0f, 110.0f, 20.0f, 10.0f,
		0.0f, 0.0f, 1.0f, 1.0f
	} );

	idDeviceContext glyphClipDc;
	glyphClipDc.EnableClipping( true );
	glyphClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
	glyphClipDc.PushClipRect( idRectangle( 100.0f, 100.0f, 50.0f, 40.0f ) );
	const q4GlyphClipCase_t glyphClipCases[] = {
		{
			"glyph partial left clip",
			90.0f, 110.0f, 20.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			100.0f, 110.0f, 10.0f, 10.0f,
			0.5f, 0.0f, 1.0f, 1.0f
		},
		{
			"glyph partial right clip",
			140.0f, 110.0f, 20.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			140.0f, 110.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 0.5f, 1.0f
		},
		{
			"glyph partial top clip",
			110.0f, 90.0f, 10.0f, 20.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			110.0f, 100.0f, 10.0f, 10.0f,
			0.0f, 0.5f, 1.0f, 1.0f
		},
		{
			"glyph partial bottom clip",
			110.0f, 130.0f, 10.0f, 20.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			110.0f, 130.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 0.5f
		},
		{
			"glyph exact right edge cull",
			150.0f, 110.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			true,
			150.0f, 110.0f, 0.0f, 10.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		},
		{
			"glyph exact bottom edge cull",
			110.0f, 140.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			true,
			110.0f, 140.0f, 10.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f
		}
	};
	for ( int glyphClipCaseIndex = 0; glyphClipCaseIndex < static_cast<int>( sizeof( glyphClipCases ) / sizeof( glyphClipCases[0] ) ); ++glyphClipCaseIndex ) {
		ok &= openQ4_CheckGlyphClipCase( glyphClipDc, glyphClipCases[glyphClipCaseIndex] );
	}

	idDeviceContext dc;
	dc.EnableClipping( true );
	dc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
	dc.PushClipRect( idRectangle( -wideExpand, 0.0f, static_cast<float>( VIRTUAL_WIDTH ) + 2.0f * wideExpand, static_cast<float>( VIRTUAL_HEIGHT ) ) );

	float x = -110.0f;
	float y = 10.0f;
	float w = 10.0f;
	float h = 20.0f;
	float s1 = 0.0f;
	float t1 = 0.0f;
	float s2 = 1.0f;
	float t2 = 1.0f;
	const bool partialClippedOut = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= openQ4_CheckBool( "expanded partial clip result", partialClippedOut, false );
	ok &= openQ4_CheckNear( "expanded partial clip x", x, -wideExpand );
	ok &= openQ4_CheckNear( "expanded partial clip width", w, wideExpand - 100.0f );
	ok &= openQ4_CheckNear( "expanded partial clip s1", s1, ( 110.0f - wideExpand ) * 0.1f );
	ok &= openQ4_CheckNear( "expanded partial clip s2", s2, 1.0f );
	ok &= openQ4_CheckNear( "expanded partial clip t1", t1, 0.0f );
	ok &= openQ4_CheckNear( "expanded partial clip t2", t2, 1.0f );

	x = -100.0f;
	y = 10.0f;
	w = 20.0f;
	h = 20.0f;
	s1 = 0.0f;
	t1 = 0.0f;
	s2 = 1.0f;
	t2 = 1.0f;
	const bool insideClippedOut = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= openQ4_CheckBool( "expanded inside clip result", insideClippedOut, false );
	ok &= openQ4_CheckNear( "expanded inside clip x", x, -100.0f );
	ok &= openQ4_CheckNear( "expanded inside clip width", w, 20.0f );
	ok &= openQ4_CheckNear( "expanded inside clip s1", s1, 0.0f );
	ok &= openQ4_CheckNear( "expanded inside clip s2", s2, 1.0f );

	if ( ok ) {
		common->Printf( "uiFontParitySelfTest passed: retail glyph metrics, atlas upload, icon and key-bind sizing, cursor handling, alignment, aspect expansion, and clipping are stable\n" );
	}
	return ok;
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

	idStr::snPrintf( s, sizeof( str[0] ), "%.2f %.2f %.2f %.2f", x, y, w, h );

	return s;
}
