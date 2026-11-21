/*
===========================================================================

openQ4 Source Code
Copyright (C) 2026 DarkMatter Productions

This file is part of the openQ4 Source Code.

openQ4 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

openQ4 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with openQ4 Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

/*
===============================================================================

	TrueType-backed replacement for the retail bitmap fonts.

	The retail fonts are fixed 12/24/48 point atlases.  On anything above the
	640x480 virtual canvas the GUI scales those atlases up, so text softens as
	the display resolution rises.  This path rasterises the shipped .ttf faces
	at the resolution the display actually needs and packs the result into a
	glyph atlas, which keeps text crisp at 1080p and 4K alike.

	It fills the same fontInfo_t the bitmap loader produces - identical metric
	units, identical UV conventions - so nothing downstream has to know which
	path produced the font.

===============================================================================
*/

#include "tr_local.h"
#include "TrueType.h"
#include "RetainedFontCache.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

// Glyph metrics live in "atlas pixels at the slot's point size", the same unit
// the retail .fontdat files use, so the GUI's scaling maths is unchanged.
const int Q4_TTF_FIRST_CODE = 32;
const int Q4_TTF_LAST_CODE = 255;

// The retail atlases record each glyph's rect as its ink plus a one texel
// border on every side, and one retail texel is one metric unit.  The GUI reads
// those rects back as glyph.width/height, so they set line spacing, the
// character cell DrawText fits text into, and TextHeight.  Matching that border
// exactly - one *metric unit*, which is 'upscale' texels here - is what keeps
// text laid out the same as the bitmap path at every display resolution.
// Kept fractional: rounding it to whole texels would leave up to half a texel
// of error, which is 0.5/upscale of a metric unit and shows up as several
// percent on the 12 point slots.  Nothing needs it to be integral - the blit
// still lands on whole texels, only the recorded rect and its UVs move.
static float R_TTFGlyphBorder( float upscale ) {
	return Max( 1.0f, upscale );
}

// Transparent gutter around every glyph.  It has to cover the metric border
// above plus the bleed guard DeviceContext applies: the guard is expressed in
// retail atlas texels, and because these atlases are rasterised at 'upscale'
// times that resolution it covers that many real texels here.  Anything less
// and the guard reads into the neighbouring glyph.
static int R_TTFGlyphPadding( int pointSize, float upscale ) {
	const float guardTexels = ( pointSize <= 12 ) ? 1.0f : 0.5f;
	return (int)idMath::Ceil( R_TTFGlyphBorder( upscale ) ) + (int)idMath::Ceil( guardTexels * upscale );
}

const int Q4_TTF_MIN_PAGE = 128;
const int Q4_TTF_MAX_PAGE = 4096;
// Largest atlas one point size of one face may occupy, in texels. 2048x2048 at
// RGBA8 is 16MB, which is what a 1440p display needs for the 48 point slot at
// its native resolution.
const int Q4_TTF_MAX_PAGE_AREA = 2048 * 2048;
// Shelf packing leaves gaps, so ask for more area than the glyphs sum to.
const double Q4_TTF_PACKING_HEADROOM = 1.25;

// The console sheet: a 16x16 grid of 16x16 pixel cells indexed by byte.
//
// Unlike the GUI atlases this one stays byte-indexed, because 256 cells is what
// the console and loading screen slice out of it and reworking those draw paths
// buys nothing - so its 256 cells are a codepage rather than Latin-1, and the
// console draw path maps a code point onto one. The sheet is rebuilt whenever
// the active codepage changes, which is what lets a Russian session get a
// Cyrillic console. The cost is that the console shows one script at a time;
// the GUI, which is where localised text actually lives, has no such limit.
const int Q4_CONSOLE_GRID = 16;
const int Q4_CONSOLE_CELL_SIZE = 16;
const char * const Q4_CONSOLE_ATLAS_IMAGE = "_ttfconsolefont";
const char * const Q4_CONSOLE_FONT_MATERIAL = "fonts/english/bigchars";

// The virtual GUI canvas is 640x480; rasterising at the display's upscale
// factor is what makes the text resolution-independent.
const float Q4_TTF_REFERENCE_HEIGHT = 480.0f;
const float Q4_TTF_MIN_UPSCALE = 1.0f;
const float Q4_TTF_MAX_UPSCALE = 6.0f;

// The base 256 slots are Latin-1: slot i holds U+00<i>, whatever sys_lang is.
// They used to be indexed by string-table byte through the active codepage,
// which meant one atlas could only ever express one 8-bit page and had to be
// rebuilt whenever the language moved between codepages. Engine text is UTF-8
// now, so a slot means the same character in every language and anything above
// U+00FF lives in the extended pages built below.
//
// The 0x80-0x9F band stays real typography rather than control codes: those
// code points have no art in any face, but the generator fills them from a
// donor so a legacy table folded into that band still draws.

// Code points above U+00FF that every language needs, whatever it is.
//
// The Latin blocks are not optional. Windows-1252 reaches outside Latin-1 in
// its 0x80-0x9F band - o-e ligature, s-caron, z-caron, florin, y-diaeresis -
// and the shipped French, Spanish and Italian tables use those characters. As
// single bytes they used to land in the base 256 slots; as UTF-8 they are code
// points above U+00FF and need real pages, or French would lose every "coeur"
// it spells properly. Latin Extended-A also covers the Central European
// alphabets, so Polish and Czech need nothing of their own.
//
// The rest is typography a UTF-8 authored table writes directly rather than
// folding to ASCII: General Punctuation and Currency Symbols for em dashes,
// curly quotes and the euro, and Letterlike Symbols for the numero sign
// Russian uses where English writes "No.".
//
// Only the code points a face actually covers are rasterised, so asking for a
// whole block costs nothing on a face that carries none of it.
static const unsigned int Q4_TTF_UNIVERSAL_RANGES[] = {
	0x0100, 0x017F,		// Latin Extended-A
	0x0180, 0x024F,		// Latin Extended-B
	0x02B0, 0x02FF,		// Spacing Modifier Letters (circumflex, small tilde)
	0x2000, 0x20FF,		// General Punctuation, Currency Symbols
	0x2100, 0x21FF,		// Letterlike Symbols, Arrows
	0
};

// The three point sizes the GUI selects between, matching the retail atlases.
struct q4TTFSlotSpec_t {
	int			pointSize;
};

static const q4TTFSlotSpec_t Q4_TTF_SLOTS[3] = { { 12 }, { 24 }, { 48 } };

/*
================
q4TTFShelfPacker

Glyphs from one face at one size are close enough in height that a shelf
packer wastes very little, and it keeps the layout deterministic.
================
*/
class q4TTFShelfPacker {
public:
	void Init( int pageWidth, int pageHeight ) {
		width = pageWidth;
		height = pageHeight;
		shelfY = 0;
		shelfHeight = 0;
		cursorX = 0;
	}

	bool Place( int glyphWidth, int glyphHeight, int &outX, int &outY ) {
		if ( glyphWidth > width ) {
			return false;
		}
		if ( cursorX + glyphWidth > width ) {
			shelfY += shelfHeight;
			shelfHeight = 0;
			cursorX = 0;
		}
		if ( shelfY + glyphHeight > height ) {
			return false;
		}
		outX = cursorX;
		outY = shelfY;
		cursorX += glyphWidth;
		if ( glyphHeight > shelfHeight ) {
			shelfHeight = glyphHeight;
			// Growing the shelf after the fact can push it past the page.
			if ( shelfY + shelfHeight > height ) {
				return false;
			}
		}
		return true;
	}

private:
	int width;
	int height;
	int shelfY;
	int shelfHeight;
	int cursorX;
};

struct q4TTFRaster_t {
	ttGlyphBitmap_t	bitmap;
	int				advance;
	bool			valid;
	bool			covered;
};

/*
================
R_TTFCollectExtendedCodePoints

Which code points above U+00FF the atlases have to carry, as a sorted list.

Three sources, unioned. The Unicode blocks the active language declares, so
text that never passed through a string table - a player name, a server name, a
line typed at the console - draws in the same alphabet as the menus around it.
The universal punctuation and symbol blocks above. And a sweep of the loaded
string tables, which catches whatever a language needs that nobody declared;
that sweep is what makes adding a language a matter of authoring its tables
rather than of editing this file.

Bounded by construction: the declared blocks are a few hundred code points, the
sweep can only surface characters that are actually in the shipped text, and
the face filters both down to what it has art for.
================
*/
static void R_TTFAppendRangeCodePoints( idList<unsigned int> &out, const unsigned int *ranges ) {
	if ( ranges == NULL ) {
		return;
	}
	for ( int i = 0; ranges[i] != 0; i += 2 ) {
		for ( unsigned int cp = ranges[i]; cp <= ranges[i + 1]; cp++ ) {
			out.Append( cp );
		}
	}
}

static int R_TTFCompareCodePoints( const void *a, const void *b ) {
	const unsigned int left = *(const unsigned int *)a;
	const unsigned int right = *(const unsigned int *)b;
	if ( left < right ) {
		return -1;
	}
	return ( left > right ) ? 1 : 0;
}

static void R_TTFCollectExtendedCodePoints( idList<unsigned int> &out ) {
	out.Clear();
	out.SetGranularity( 256 );

	R_TTFAppendRangeCodePoints( out, Q4_TTF_UNIVERSAL_RANGES );
	R_TTFAppendRangeCodePoints( out,
		LangDict_ExtendedRangesForLanguage( cvarSystem->GetCVarString( "sys_lang" ) ) );

	// The string tables are already normalised to UTF-8 by the loader, so this
	// reads code points straight out of them.
	const idLangDict *dict = ( common != NULL ) ? common->GetLanguageDict() : NULL;
	if ( dict != NULL ) {
		for ( int i = 0; i < dict->GetNumKeyVals(); i++ ) {
			const idLangKeyValue *kv = dict->GetKeyVal( i );
			if ( kv == NULL ) {
				continue;
			}
			const char *value = kv->value.c_str();
			const int length = kv->value.Length();
			for ( int at = 0; at < length; ) {
				unsigned int cp = 0;
				const int used = LangDict_NextCodePoint( value + at, length - at, cp );
				if ( used <= 0 ) {
					break;
				}
				at += used;
				if ( cp > 0xFF && cp <= GLYPH_MAX_CODEPOINT ) {
					out.Append( cp );
				}
			}
		}
	}

	if ( out.Num() < 2 ) {
		return;
	}
	qsort( out.Ptr(), out.Num(), sizeof( unsigned int ), R_TTFCompareCodePoints );

	int unique = 1;
	for ( int i = 1; i < out.Num(); i++ ) {
		if ( out[i] != out[unique - 1] ) {
			out[unique++] = out[i];
		}
	}
	out.SetNum( unique, false );
}

/*
================
q4TTFPageSet

The extended glyph pages for one face at one point size.

Allocated once per (face, point size) and refilled in place when that font is
re-registered, never freed before renderer shutdown. A fontInfo_t is copied by
value all over the GUI - into idDeviceContext's font list, into the GUI windows
that cache one - and freeing a set would strand every copy still pointing at it
with no way to tell which those are.
================
*/
class q4TTFPageSet {
public:
					q4TTFPageSet() { memset( pages, 0, sizeof( pages ) ); }
					~q4TTFPageSet() {
						for ( int i = 0; i < GLYPH_MAX_PAGES; i++ ) {
							if ( pages[i] != NULL ) {
								Mem_Free( pages[i] );
							}
						}
					}

	fontGlyphPage_t *Page( int index, bool create ) {
						if ( index < 0 || index >= GLYPH_MAX_PAGES ) {
							return NULL;
						}
						if ( pages[index] == NULL && create ) {
							pages[index] = (fontGlyphPage_t *)Mem_ClearedAlloc( sizeof( fontGlyphPage_t ) );
						}
						return pages[index];
					}

						// Drops the art but keeps the allocation, so a refill after a
						// language change cannot leave last language's glyphs behind
						// in a page the new one does not touch.
	void			ClearArt( void ) {
						for ( int i = 0; i < GLYPH_MAX_PAGES; i++ ) {
							if ( pages[i] != NULL ) {
								memset( pages[i], 0, sizeof( fontGlyphPage_t ) );
							}
						}
					}

	idStr			key;
	fontGlyphPage_t *pages[GLYPH_MAX_PAGES];
};

/*
================
R_TTFUpscale

How many device pixels the GUI puts on one virtual unit.  Text rasterised at
this factor lands on the display at its native resolution.
================
*/
static float R_TTFUpscale( void ) {
	// glConfig, not engineWindowState: the latter is the engine-owned window
	// mirror that the module polls at present time, so at font registration it
	// still holds the pre-mode logical window size and its UI viewport is not
	// populated at all. Rasterising from it produced atlases at half the
	// resolution the display actually needed.
	float displayHeight = (float)glConfig.uiViewportHeight;
	if ( displayHeight <= 0.0f ) {
		displayHeight = (float)glConfig.vidHeight;
	}
	if ( displayHeight <= 0.0f ) {
		displayHeight = (float)engineWindowState.vidHeight;
	}
	if ( displayHeight <= 0.0f ) {
		return Q4_TTF_MIN_UPSCALE;
	}

	float upscale = displayHeight / Q4_TTF_REFERENCE_HEIGHT;
	if ( r_ttfFontResolution.GetFloat() > 0.0f ) {
		upscale *= r_ttfFontResolution.GetFloat();
	}
	if ( r_ttfFontDebug.GetBool() ) {
		common->Printf( "TTF font: upscale %.2f from ui=%ix%i vid=%ix%i glConfigUi=%ix%i glConfigVid=%ix%i\n",
						upscale, engineWindowState.uiViewportWidth, engineWindowState.uiViewportHeight,
						engineWindowState.vidWidth, engineWindowState.vidHeight,
						glConfig.uiViewportWidth, glConfig.uiViewportHeight,
						glConfig.vidWidth, glConfig.vidHeight );
	}
	return Max( Q4_TTF_MIN_UPSCALE, Min( Q4_TTF_MAX_UPSCALE, upscale ) );
}

class q4TTFFace {
public:
	idStr			name;
	idTrueTypeFont	face;
};

class q4TTFFontManager {
public:
					q4TTFFontManager() {}

	idTrueTypeFont *FindFace( const char *fontName );
	void			Shutdown();

private:
	idList<q4TTFFace *> faces;
};

static q4TTFFontManager ttfFonts;

// The console material is authored by the shipped assets.  The scalable path
// only retargets its image pointer at runtime, so retain the authored image and
// put it back before a renderer shutdown/restart.  This also makes changing
// r_useTrueTypeFonts from on to off across vid_restart restore the retail sheet
// instead of leaving the material attached to a purged scratch image.
static idMaterial *ttfConsoleMaterial = NULL;
static idImage *ttfConsoleOriginalImage = NULL;

// Every atlas material this module generates, with the source text that
// produces it.  These are runtime declarations with no file behind them, so
// the declaration manager cannot rebuild one on its own; see
// R_TTFRestoreAtlasMaterials for what that costs and how it is repaired.
class q4TTFAtlasMaterial {
public:
	idStr	materialName;
	idStr	imageName;
	idStr	source;
};

static idList<q4TTFAtlasMaterial *> ttfAtlasMaterials;

// An atlas has no file to reload. Keep its one-byte coverage plane until the
// image manager shuts down; the generated image can then rebuild its RGBA
// storage after reloadImages or a video restart without rasterizing again.
class q4TTFAtlasImage {
public:
	idStr		name;
	int			width;
	int			height;
	idList<byte>	coverage;
};

static idList<q4TTFAtlasImage *> ttfAtlasImages;

static void R_TTFGenerateAtlasImage( idImage *image ) {
	for ( int i = 0; i < ttfAtlasImages.Num(); i++ ) {
		const q4TTFAtlasImage &atlas = *ttfAtlasImages[i];
		if ( atlas.name.Icmp( image->GetName() ) != 0 ) {
			continue;
		}

		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_RGBA8;
		opts.colorFormat = CFM_DEFAULT;
		opts.width = atlas.width;
		opts.height = atlas.height;
		opts.numLevels = 1;
		opts.isPersistant = true;
		image->AllocImage( opts, TF_LINEAR, TR_CLAMP );

		// GUI blending expects white RGB with glyph coverage in alpha.
		const int texels = atlas.coverage.Num();
		byte *rgba = (byte *)Mem_Alloc( texels * 4 );
		for ( int j = 0; j < texels; j++ ) {
			rgba[j * 4 + 0] = 255;
			rgba[j * 4 + 1] = 255;
			rgba[j * 4 + 2] = 255;
			rgba[j * 4 + 3] = atlas.coverage[j];
		}
		image->SubImageUpload( 0, 0, 0, 0, atlas.width, atlas.height, rgba );
		Mem_Free( rgba );
		return;
	}
	common->Error( "TTF font: no retained atlas for %s", image->GetName() );
}

static idImage *R_TTFUploadAtlasImage( const char *name, int width, int height, const byte *coverage ) {
	q4TTFAtlasImage *atlas = NULL;
	for ( int i = 0; i < ttfAtlasImages.Num(); i++ ) {
		if ( ttfAtlasImages[i]->name.Icmp( name ) == 0 ) {
			atlas = ttfAtlasImages[i];
			break;
		}
	}
	const bool replacingAtlas = ( atlas != NULL );
	if ( atlas == NULL ) {
		atlas = new q4TTFAtlasImage();
		atlas->name = name;
		ttfAtlasImages.Append( atlas );
	}
	atlas->width = width;
	atlas->height = height;
	atlas->coverage.SetNum( width * height );
	memcpy( atlas->coverage.Ptr(), coverage, width * height );

	idImage *image = globalImages->ImageFromFunction( name, R_TTFGenerateAtlasImage );
	// ImageFromFunction invokes the generator only for a new image. A font
	// registration at another scale or codepage replaces the existing pixels.
	if ( replacingAtlas ) {
		image->Reload( true );
	}
	return image;
}

static idList<q4TTFPageSet *> ttfPageSets;

/*
================
R_TTFForEachGlyphBlock

Runs body( first, last ) over [0, count) in blocks of glyphs, on worker threads
and the calling thread together. Rasterising a face's glyphs is most of a font
registration (the 2,900-glyph faces take tens of milliseconds a size, at
startup and in level loads), each glyph is independent and the face is only
read, so the blocks run at once. The caller merges results in index order.
================
*/
template< typename body_t >
static void R_TTFForEachGlyphBlock( int count, body_t &&body ) {
	static const int blockGlyphs = 32;
	const unsigned int hardware = std::thread::hardware_concurrency();
	const int workers = count >= blockGlyphs * 4
		? idMath::ClampInt( 0, 7, hardware > 1 ? static_cast<int>( hardware ) - 1 : 0 ) : 0;
	if ( workers == 0 ) {
		body( 0, count );
		return;
	}
	std::atomic<int> nextBlock( 0 );
	const auto run = [&]() {
		for ( ;; ) {
			const int first = nextBlock.fetch_add( blockGlyphs );
			if ( first >= count ) {
				return;
			}
			body( first, Min( count, first + blockGlyphs ) );
		}
	};
	std::vector<std::thread> threads;
	threads.reserve( static_cast<size_t>( workers ) );
	for ( int i = 0; i < workers; i++ ) {
		try {
			threads.emplace_back( run );
		} catch ( ... ) {
			break;
		}
	}
	run();
	for ( std::thread &thread : threads ) {
		thread.join();
	}
}

/*
================
R_TTFFindPageSet

Keyed by the atlas name, which already encodes face and point size.
================
*/
static q4TTFPageSet *R_TTFFindPageSet( const char *key ) {
	for ( int i = 0; i < ttfPageSets.Num(); i++ ) {
		if ( ttfPageSets[i]->key.Icmp( key ) == 0 ) {
			ttfPageSets[i]->ClearArt();
			return ttfPageSets[i];
		}
	}
	q4TTFPageSet *set = new q4TTFPageSet();
	set->key = key;
	ttfPageSets.Append( set );
	return set;
}

/*
================
R_TTFAtlasMaterialSource

The atlas lives in a generated image whose name starts with an underscore, so
the image manager hands the same object to every reference regardless of the
sampler parameters asked for.
================
*/
static void R_TTFAtlasMaterialSource( const char *imageName, const char *materialName, idStr &source ) {
	source = "";
	source += va( "%s\n", materialName );
	source += "{\n";
	source += "\t{\n";
	source += "\t\tblend blend\n";
	source += "\t\tcolored\n";
	source += "\t\tnopicmip\n";
	source += "\t\tlinear\n";
	source += "\t\tclamp\n";
	source += va( "\t\tmap %s\n", imageName );
	source += "\t}\n";
	source += "}\n";
}

/*
================
R_TTFMaterialSamplesAtlas

A material that still carries the generated stage needs no work.  Anything else
- no stages at all, or a stage pointing at the default image - means the
declaration was reset behind our back and has to be rebuilt.
================
*/
static bool R_TTFMaterialSamplesAtlas( const idMaterial *material, const char *imageName ) {
	if ( material == NULL || material->GetNumStages() < 1 ) {
		return false;
	}
	const shaderStage_t *stage = material->GetStage( 0 );
	const idImage *bound = ( stage != NULL ) ? stage->texture.image : NULL;
	return bound != NULL && idStr::Icmp( bound->GetName(), imageName ) == 0;
}

/*
================
R_TTFInstallAtlasStage

Installs the generated stage on a declaration the manager has already created.
SetText is what makes the declaration self-describing: without it the only copy
of the atlas stage is the parsed data this call produces, and anything that
resets that data - a level load purge, a reloadDecls - destroys the font for the
rest of the session.  Implicit declarations are excluded from the network decl
checksum, so giving one text stays process local.
================
*/
static void R_TTFInstallAtlasStage( idMaterial *material, const idStr &source ) {
	material->SetText( source.c_str() );
	material->FreeData();
	material->Parse( source.c_str(), source.Length() );
	material->SetSort( SS_GUI );
}

/*
================
q4TTFFontManager::FindFace

'fontName' arrives as the resolved bitmap path, e.g. "fonts/english/chain".
The .ttf files are language independent, so the language folder is tried first
and then the shared location.
================
*/
idTrueTypeFont *q4TTFFontManager::FindFace( const char *fontName ) {
	idStr key = fontName;
	key.BackSlashesToSlashes();
	key.StripFileExtension();

	for ( int i = 0; i < faces.Num(); i++ ) {
		if ( faces[i]->name.Icmp( key ) == 0 ) {
			return faces[i]->face.IsLoaded() ? &faces[i]->face : NULL;
		}
	}

	idStr candidates[2];
	candidates[0] = key + ".ttf";

	// "fonts/english/chain" -> "fonts/chain"
	idStr shared = key;
	int firstSlash = shared.Find( '/' );
	int lastSlash = shared.Last( '/' );
	if ( firstSlash >= 0 && lastSlash > firstSlash ) {
		shared = shared.Left( firstSlash + 1 ) + shared.Right( shared.Length() - lastSlash - 1 );
	}
	candidates[1] = shared + ".ttf";

	q4TTFFace *entry = new q4TTFFace();
	entry->name = key;
	faces.Append( entry );

	for ( int i = 0; i < 2; i++ ) {
		if ( i == 1 && candidates[1].Icmp( candidates[0] ) == 0 ) {
			break;
		}
		void *buffer = NULL;
		const int length = fileSystem->ReadFile( candidates[i].c_str(), &buffer );
		if ( buffer == NULL || length <= 0 ) {
			continue;
		}
		const bool loaded = entry->face.Load( (const byte *)buffer, length );
		fileSystem->FreeFile( buffer );
		if ( loaded ) {
			common->Printf( "TTF font: loaded %s (%i glyphs, %i upem)\n",
							candidates[i].c_str(), entry->face.NumGlyphs(), entry->face.UnitsPerEm() );
			return &entry->face;
		}
		common->Warning( "TTF font: '%s' is not a usable TrueType file", candidates[i].c_str() );
	}

	return NULL;
}

void q4TTFFontManager::Shutdown() {
	faces.DeleteContents( true );
}

/*
================
R_TTFCreateAtlasMaterial

The atlas lives in a generated image whose name starts with an underscore, so
the image manager hands the same object to every reference regardless of the
sampler parameters asked for.  The process-local material deliberately has the
same name as that image.  An implicit material initially maps an image named
after itself; sharing the intrinsic atlas identity means that first parse finds
the generated image which was uploaded immediately before this call instead of
probing a nonexistent file and leaving a redundant defaulted image behind.

The material is then installed from source text rather than shipped as a .mtr,
which keeps this feature inside the executable and pins the GUI sampler state.
================
*/
static const idMaterial *R_TTFCreateAtlasMaterial( const char *imageName, const char *materialName ) {
	idStr source;
	R_TTFAtlasMaterialSource( imageName, materialName, source );

	// Renderer-generated atlas materials are process-local implementation
	// details. Keep them implicit so a graphical client does not add decls to
	// the network checksum that a dedicated server can never create.
	idMaterial *material = const_cast<idMaterial *>( declManager->FindMaterial( materialName, true ) );
	if ( material == NULL ) {
		return NULL;
	}

	// Always replace the implicit stage.  It already resolves to the atlas (the
	// material and image identities match), which keeps creation warning-free,
	// but only this generated source pins blend, filtering, clamp and no-picmip
	// semantics and survives a declaration purge as a complete definition.
	R_TTFInstallAtlasStage( material, source );

	for ( int i = 0; i < ttfAtlasMaterials.Num(); i++ ) {
		if ( ttfAtlasMaterials[i]->materialName.Icmp( materialName ) == 0 ) {
			return material;
		}
	}

	q4TTFAtlasMaterial *entry = new q4TTFAtlasMaterial();
	entry->materialName = materialName;
	entry->imageName = imageName;
	entry->source = source;
	ttfAtlasMaterials.Append( entry );
	return material;
}

/*
================
R_TTFPackAtlas

Rasterises one list of code points at one point size, packs them into a single
page, uploads it, and reports each one's metrics.

Both the base Latin-1 slots and the extended pages come through here, so a
glyph above U+00FF gets exactly the same border, gutter and metric treatment as
one below it. The layout rules the retail atlases set are not something an
extended page may quietly diverge from.

'requestedUpscale' is a ceiling rather than a promise - see the area cap below -
and what it settled on comes back in result.usedUpscale. Metrics are recorded in
point units either way, so a reduced scale costs sharpness and never layout.

outCovered is optional: the base slots are always addressable, blank or not,
while an extended page has to be able to say "this face has no art here" so the
text layer can substitute rather than draw a zero-advance hole.
================
*/
struct q4TTFAtlasResult_t {
	const idMaterial *	material;
	float				maxWidth;
	float				maxHeight;
	int					rendered;
	float				usedUpscale;
};

static bool R_TTFPackAtlas( idTrueTypeFont &face, const char *fontName, int pointSize, float requestedUpscale,
							const unsigned int *codePoints, int count, bool measureLayout,
							const char *imageName, const char *materialName,
							glyphInfo_t *outGlyphs, bool *outCovered, q4TTFAtlasResult_t &result ) {
	result.material = NULL;
	result.maxWidth = 0.0f;
	result.maxHeight = 0.0f;
	result.rendered = 0;
	result.usedUpscale = requestedUpscale;

	if ( count <= 0 ) {
		return false;
	}

	const int padding = R_TTFGlyphPadding( pointSize, requestedUpscale );
	// Glyph area grows with the square of the rasterisation scale, so at 4K the
	// large slot would otherwise want a 4096x4096 page - 64MB for one size of
	// one face. Predict the area from the glyph metrics, which needs no
	// rasterising, and pull this slot's scale back until it fits the cap. The
	// clamp only ever engages above 1440p, where 3x is already well past the
	// point of visible return.
	float slotUpscale = requestedUpscale;
	{
		double requiredArea = 0.0;
		const float probeScale = face.ScaleForPixelHeight( (float)pointSize * requestedUpscale );
		for ( int i = 0; i < count; i++ ) {
			ttGlyphMetrics_t metrics;
			const int glyphIndex = face.GlyphForCodepoint( (int)codePoints[i] );
			if ( glyphIndex == 0 || !face.GetGlyphMetrics( glyphIndex, metrics ) ) {
				continue;
			}
			if ( metrics.xMax <= metrics.xMin || metrics.yMax <= metrics.yMin ) {
				continue;
			}
			const double width = ( metrics.xMax - metrics.xMin ) * probeScale + 2.0 + padding * 2.0;
			const double height = ( metrics.yMax - metrics.yMin ) * probeScale + 2.0 + padding * 2.0;
			requiredArea += width * height;
		}
		const double capArea = (double)Q4_TTF_MAX_PAGE_AREA;
		if ( requiredArea * Q4_TTF_PACKING_HEADROOM > capArea ) {
			slotUpscale = requestedUpscale * (float)sqrt( capArea / ( requiredArea * Q4_TTF_PACKING_HEADROOM ) );
			slotUpscale = Max( Q4_TTF_MIN_UPSCALE, slotUpscale );
		}
	}
	result.usedUpscale = slotUpscale;

	const float pixelEm = (float)pointSize * slotUpscale;
	const float scale = face.ScaleForPixelHeight( pixelEm );
	if ( scale <= 0.0f ) {
		return false;
	}
	// The rasteriser already leaves one texel of slack around the ink, so only
	// the remainder of the retail one-unit border has to be added here.  The
	// gutter is sized from the requested upscale, which is never below the
	// slot's own, so the widened rect always stays inside the packed cell.
	const float extraBorder = R_TTFGlyphBorder( slotUpscale ) - 1.0f;
	// Converts font design units straight to the slot's metric units.
	const float unitsToPoint = (float)pointSize / (float)face.UnitsPerEm();
	const float pixelsToPoint = 1.0f / slotUpscale;

	q4TTFRaster_t *rasters = (q4TTFRaster_t *)Mem_ClearedAlloc( count * sizeof( q4TTFRaster_t ) );

	int totalArea = 0;
	int widest = 0;
	int tallest = 0;
	int rendered = 0;

	// Each glyph writes only its own slot; the totals below are summed in order.
	idList<byte> rasterized;
	rasterized.SetNum( count );
	memset( rasterized.Ptr(), 0, count );
	R_TTFForEachGlyphBlock( count, [&]( int first, int last ) {
		for ( int i = first; i < last; i++ ) {
			const int codepoint = (int)codePoints[i];
			const int glyphIndex = face.GlyphForCodepoint( codepoint );

			ttGlyphMetrics_t metrics;
			if ( !face.GetGlyphMetrics( glyphIndex, metrics ) ) {
				continue;
			}
			rasters[i].advance = metrics.advance;
			rasters[i].valid = true;
			rasters[i].covered = ( glyphIndex != 0 );

			if ( glyphIndex == 0 && codepoint != 0 ) {
				// Not covered by this face; leave a blank slot but keep the advance.
				continue;
			}
			rasterized[i] = face.RasterizeGlyph( glyphIndex, scale, rasters[i].bitmap ) ? 1 : 0;
		}
	} );

	for ( int i = 0; i < count; i++ ) {
		if ( !rasterized[i] ) {
			continue;
		}
		if ( rasters[i].bitmap.pixels == NULL ) {
			continue;
		}

		const int paddedWidth = rasters[i].bitmap.width + padding * 2;
		const int paddedHeight = rasters[i].bitmap.height + padding * 2;
		totalArea += paddedWidth * paddedHeight;
		widest = Max( widest, paddedWidth );
		tallest = Max( tallest, paddedHeight );
		rendered++;
	}

	if ( rendered == 0 ) {
		for ( int i = 0; i < count; i++ ) {
			idTrueTypeFont::FreeGlyphBitmap( rasters[i].bitmap );
		}
		Mem_Free( rasters );
		return false;
	}

	// Choose the smallest power-of-two page the shelf packer can actually fill.
	int pageWidth = Q4_TTF_MIN_PAGE;
	int pageHeight = Q4_TTF_MIN_PAGE;
	while ( pageWidth < widest && pageWidth < Q4_TTF_MAX_PAGE ) {
		pageWidth <<= 1;
	}
	while ( pageHeight < tallest && pageHeight < Q4_TTF_MAX_PAGE ) {
		pageHeight <<= 1;
	}
	while ( (double)pageWidth * pageHeight < (double)totalArea * Q4_TTF_PACKING_HEADROOM ) {
		if ( pageWidth <= pageHeight && pageWidth < Q4_TTF_MAX_PAGE ) {
			pageWidth <<= 1;
		} else if ( pageHeight < Q4_TTF_MAX_PAGE ) {
			pageHeight <<= 1;
		} else {
			break;
		}
	}

	byte *page = NULL;
	q4TTFShelfPacker packer;
	bool packed = false;

	for ( int attempt = 0; attempt < 6 && !packed; attempt++ ) {
		packer.Init( pageWidth, pageHeight );
		packed = true;
		for ( int i = 0; i < count && packed; i++ ) {
			if ( rasters[i].bitmap.pixels == NULL ) {
				continue;
			}
			int x = 0;
			int y = 0;
			if ( !packer.Place( rasters[i].bitmap.width + padding * 2,
								rasters[i].bitmap.height + padding * 2, x, y ) ) {
				packed = false;
			}
		}
		if ( packed ) {
			break;
		}
		if ( pageWidth <= pageHeight && pageWidth < Q4_TTF_MAX_PAGE ) {
			pageWidth <<= 1;
		} else if ( pageHeight < Q4_TTF_MAX_PAGE ) {
			pageHeight <<= 1;
		} else {
			break;
		}
	}

	if ( !packed ) {
		common->Warning( "TTF font: '%s' %ipt does not fit a %ix%i atlas", fontName, pointSize, pageWidth, pageHeight );
		for ( int i = 0; i < count; i++ ) {
			idTrueTypeFont::FreeGlyphBitmap( rasters[i].bitmap );
		}
		Mem_Free( rasters );
		return false;
	}

	page = (byte *)Mem_ClearedAlloc( pageWidth * pageHeight );

	// Re-run the packer, this time actually blitting and recording the layout.
	packer.Init( pageWidth, pageHeight );
	for ( int i = 0; i < count; i++ ) {
		const unsigned int code = codePoints[i];
		glyphInfo_t &glyph = outGlyphs[i];

		if ( !rasters[i].valid ) {
			continue;
		}
		glyph.horiAdvance = rasters[i].advance * unitsToPoint;
		if ( outCovered != NULL ) {
			outCovered[i] = rasters[i].covered;
		}

		if ( rasters[i].bitmap.pixels == NULL ) {
			continue;
		}

		const ttGlyphBitmap_t &bitmap = rasters[i].bitmap;
		int x = 0;
		int y = 0;
		packer.Place( bitmap.width + padding * 2, bitmap.height + padding * 2, x, y );

		const int destX = x + padding;
		const int destY = y + padding;
		for ( int row = 0; row < bitmap.height; row++ ) {
			memcpy( page + ( destY + row ) * pageWidth + destX, bitmap.pixels + row * bitmap.width, bitmap.width );
		}

		// Report the rect with the retail one-unit border.  The extra texels are
		// transparent gutter and the UV rect grows with them, so the ink still
		// lands at exactly the same size and place; what changes is that the
		// metrics the GUI lays out with now mean the same thing they do in the
		// bitmap path, at any rasterisation scale.
		const float rectX = (float)destX - extraBorder;
		const float rectY = (float)destY - extraBorder;
		const float rectWidth = (float)bitmap.width + extraBorder * 2.0f;
		const float rectHeight = (float)bitmap.height + extraBorder * 2.0f;

		glyph.width = rectWidth * pixelsToPoint;
		glyph.height = rectHeight * pixelsToPoint;
		glyph.horiBearingX = ( (float)bitmap.left - extraBorder ) * pixelsToPoint;
		glyph.horiBearingY = -( (float)bitmap.top - extraBorder ) * pixelsToPoint;
		glyph.s = rectX / (float)pageWidth;
		glyph.t = rectY / (float)pageHeight;
		glyph.s2 = ( rectX + rectWidth ) / (float)pageWidth;
		glyph.t2 = ( rectY + rectHeight ) / (float)pageHeight;

		// The retail atlases leave most of the Windows-1252 punctuation band
		// blank - chain and profont carry an empty 2x2 rect for the florin,
		// per mille, em dash and ellipsis - so the generator fills those from a
		// donor face.  Those donor glyphs are wider and taller than anything the
		// retail font could draw, and letting one set the layout cell inflates
		// line spacing and shrinks the character count DrawText fits into a
		// rect.  Measure the repertoire the GUIs were actually authored against.
		//
		// Extended pages are excluded for the same reason and pass measureLayout
		// false: line spacing and the character cell are properties of the font
		// as the GUIs were laid out against it, and must not move because a
		// language happened to pull in a tall Cyrillic capital or a wide arrow.
		if ( measureLayout && ( code < 0x80 || code > 0x9F ) ) {
			result.maxWidth = Max( result.maxWidth, glyph.width );
			result.maxHeight = Max( result.maxHeight, glyph.height );
		}
	}

	for ( int i = 0; i < count; i++ ) {
		idTrueTypeFont::FreeGlyphBitmap( rasters[i].bitmap );
	}
	Mem_Free( rasters );

	idImage *image = R_TTFUploadAtlasImage( imageName, pageWidth, pageHeight, page );
	if ( image == NULL ) {
		Mem_Free( page );
		return false;
	}

	if ( r_ttfFontDebug.GetBool() ) {
		// Dump what was handed to the GPU, so a glyph problem can be told apart
		// from an upload or sampling one without guessing.
		byte *rgba = (byte *)Mem_Alloc( pageWidth * pageHeight * 4 );
		for ( int i = 0; i < pageWidth * pageHeight; i++ ) {
			rgba[i * 4 + 0] = page[i];
			rgba[i * 4 + 1] = page[i];
			rgba[i * 4 + 2] = page[i];
			rgba[i * 4 + 3] = 255;
		}
		R_WriteTGA( va( "ttfatlas/%s.tga", imageName ), rgba, pageWidth, pageHeight, false, "fs_savepath" );
		Mem_Free( rgba );
		common->Printf( "TTF font: %s atlas %ix%i, %i glyphs, format=%i levels=%i\n",
						imageName, pageWidth, pageHeight, rendered, (int)FMT_RGBA8, 1 );
	}

	Mem_Free( page );

	const idMaterial *material = R_TTFCreateAtlasMaterial( imageName, materialName );
	if ( material == NULL ) {
		return false;
	}

	if ( r_ttfFontDebug.GetBool() ) {
		const int stages = material->GetNumStages();
		const shaderStage_t *stage = ( stages > 0 ) ? material->GetStage( 0 ) : NULL;
		const idImage *bound = ( stage != NULL ) ? stage->texture.image : NULL;
		common->Printf( "TTF font: material '%s' stages=%i boundImage=%s %ix%i (atlas image %ix%i)\n",
						materialName, stages,
						bound != NULL ? bound->GetName() : "<none>",
						bound != NULL ? bound->GetUploadWidth() : -1,
						bound != NULL ? bound->GetUploadHeight() : -1,
						image->GetUploadWidth(), image->GetUploadHeight() );
	}

	result.material = material;
	result.rendered = rendered;
	return true;
}

/*
================
R_TTFBuildExtendedPages

Everything the face has art for above U+00FF, packed into one atlas per point
size and scattered into the sparse page directory the text layer reads.

Asking the face last is what keeps this cheap: the requested list is whole
Unicode blocks, but a face that covers none of a block contributes nothing to
the atlas, and 'strogg' - which is Latin-only by design - ends up with no
extended pages at all rather than a page full of blanks.
================
*/
static void R_TTFBuildExtendedPages( idTrueTypeFont &face, const char *fontName, const idStr &safeName,
									 int pointSize, float upscale, const idList<unsigned int> &requested,
									 fontInfo_t &out ) {
	if ( requested.Num() == 0 ) {
		return;
	}

	idList<unsigned int> covered;
	covered.SetGranularity( 256 );
	for ( int i = 0; i < requested.Num(); i++ ) {
		if ( face.GlyphForCodepoint( (int)requested[i] ) != 0 ) {
			covered.Append( requested[i] );
		}
	}
	if ( covered.Num() == 0 ) {
		return;
	}

	const idStr imageName = va( "_ttfatlasx_%s_%i", safeName.c_str(), pointSize );
	const idStr materialName = imageName;

	glyphInfo_t *glyphs = (glyphInfo_t *)Mem_ClearedAlloc( covered.Num() * sizeof( glyphInfo_t ) );
	bool *isCovered = (bool *)Mem_ClearedAlloc( covered.Num() * sizeof( bool ) );

	q4TTFAtlasResult_t result;
	const bool packed = R_TTFPackAtlas( face, fontName, pointSize, upscale, covered.Ptr(), covered.Num(),
										false, imageName.c_str(), materialName.c_str(),
										glyphs, isCovered, result );
	if ( !packed || result.material == NULL ) {
		Mem_Free( glyphs );
		Mem_Free( isCovered );
		return;
	}

	q4TTFPageSet *set = R_TTFFindPageSet( imageName.c_str() );
	int installed = 0;
	for ( int i = 0; i < covered.Num(); i++ ) {
		if ( !isCovered[i] ) {
			continue;
		}
		const unsigned int codePoint = covered[i];
		fontGlyphPage_t *page = set->Page( (int)( codePoint >> GLYPH_PAGE_SHIFT ), true );
		if ( page == NULL ) {
			continue;
		}
		const int index = (int)( codePoint & ( GLYPH_PAGE_SIZE - 1 ) );
		page->material = result.material;
		page->glyphs[index] = glyphs[i];
		page->covered[index] = true;
		installed++;
	}

	Mem_Free( glyphs );
	Mem_Free( isCovered );

	out.extendedPages = set->pages;

	if ( r_ttfFontDebug.GetBool() ) {
		common->Printf( "TTF font: '%s' %ipt extended atlas, %i of %i requested code points\n",
						fontName, pointSize, installed, requested.Num() );
	}
}

/*
================
R_TTFBuildSlot

Builds one point size of one face: the Latin-1 base slots, then whatever the
active language and the loaded string tables need above them.
================
*/
static bool R_TTFBuildSlot( idTrueTypeFont &face, const char *fontName, const q4TTFSlotSpec_t &slot,
							float upscale, const idList<unsigned int> &extendedCodePoints,
							fontInfo_t &out, float &outMaxWidth, float &outMaxHeight ) {
	memset( &out, 0, sizeof( out ) );
	outMaxWidth = 0.0f;
	outMaxHeight = 0.0f;

	// Slot i is U+00<i>. The list is contiguous, so the base glyph array can be
	// written straight through rather than scattered.
	const int count = Q4_TTF_LAST_CODE - Q4_TTF_FIRST_CODE + 1;
	unsigned int *codePoints = (unsigned int *)Mem_Alloc( count * sizeof( unsigned int ) );
	for ( int i = 0; i < count; i++ ) {
		codePoints[i] = (unsigned int)( Q4_TTF_FIRST_CODE + i );
	}

	idStr safeName = fontName;
	safeName.Replace( "/", "_" );
	const idStr imageName = va( "_ttfatlas_%s_%i", safeName.c_str(), slot.pointSize );
	const idStr materialName = imageName;

	q4TTFAtlasResult_t result;
	const bool packed = R_TTFPackAtlas( face, fontName, slot.pointSize, upscale, codePoints, count, true,
										imageName.c_str(), materialName.c_str(),
										&out.glyphs[Q4_TTF_FIRST_CODE], NULL, result );
	Mem_Free( codePoints );

	if ( !packed || result.material == NULL ) {
		return false;
	}

	const float unitsToPoint = (float)slot.pointSize / (float)face.UnitsPerEm();

	outMaxWidth = result.maxWidth;
	outMaxHeight = result.maxHeight;
	out.material = result.material;
	out.glyphIndexing = GLYPH_INDEX_UNICODE;
	out.pointSize = (float)slot.pointSize;
	out.ascender = face.Ascender() * unitsToPoint;
	out.descender = -face.Descender() * unitsToPoint;
	out.fontHeight = out.ascender + out.descender;
	idStr::Copynz( out.name, va( "ttf/%s_%i", safeName.c_str(), slot.pointSize ), sizeof( out.name ) );

	// Built at the scale the base atlas settled on, so both halves of one point
	// size are rasterised alike even when the area cap pulled the base back.
	R_TTFBuildExtendedPages( face, fontName, safeName, slot.pointSize, result.usedUpscale,
							 extendedCodePoints, out );

	return true;
}

}

/*
============
R_TTFRestoreAtlasMaterials

idDeclManagerLocal::BeginLevelLoad purges every declaration that is not marked
as parsed outside a level load, and idDeclManagerLocal::FindType clears that
mark on anything it resolves while a level is loading.  A face first registered
during a load - or any face re-registered during one, which is what a lazy font
refresh after a vid_restart does - therefore ends up classified as level media.

For a shipped material that is harmless: the next reference reparses it from its
.mtr text.  These atlas materials have no file, and the implicit text the
manager generates for a nameless declaration maps an image called after the
material itself, which does not exist.  That resolves to the default image,
which is fully transparent outside developer builds, so a purged atlas material
draws every glyph as nothing at all - silently, with no warning and no way back
inside the session.

The stage is now carried in the declaration's own text, so a purge is
recoverable; this reasserts it immediately after the purge, before the loading
screen or anything else draws with a font.
============
*/
void R_TTFRestoreAtlasMaterials( void ) {
	for ( int i = 0; i < ttfAtlasMaterials.Num(); i++ ) {
		const q4TTFAtlasMaterial *entry = ttfAtlasMaterials[i];

		// Look without parsing first. A healthy declaration then costs nothing,
		// and in particular is not resolved through FindType, which would clear
		// its parsed-outside-level-load mark and make it purgeable next time.
		const idMaterial *current = static_cast<const idMaterial *>(
			declManager->FindDeclWithoutParsing( DECL_MATERIAL, entry->materialName.c_str(), false ) );
		if ( R_TTFMaterialSamplesAtlas( current, entry->imageName.c_str() ) ) {
			continue;
		}
		if ( r_ttfFontDebug.GetBool() ) {
			common->Printf( "TTF font: restoring purged atlas material '%s'\n", entry->materialName.c_str() );
		}

		// FindMaterial reparses the declaration from the text installed at
		// registration, which is normally all it takes; reinstall directly if
		// something still left it pointing somewhere else.
		idMaterial *material = const_cast<idMaterial *>( declManager->FindMaterial( entry->materialName.c_str(), false ) );
		if ( material == NULL ) {
			continue;
		}
		if ( !R_TTFMaterialSamplesAtlas( material, entry->imageName.c_str() ) ) {
			R_TTFInstallAtlasStage( material, entry->source );
		}
		material->SetSort( SS_GUI );
	}
}

/*
============
R_UseScalableFonts

Whether to draw from the .ttf faces, which is r_useTrueTypeFonts except when the
active language leaves no choice.

The retail .fontdat atlases are 256 byte-indexed glyphs of Latin art, so the
most a byte-indexed font can express is one 8-bit codepage - and for Cyrillic
not even that, because no shipped atlas has the glyphs whatever byte you index
it with. Honouring r_useTrueTypeFonts 0 under Russian would produce a menu of
question marks with nothing to explain it, so the language wins and says so
once. Czech, Hungarian and Turkish have no retail release and so only find the
English atlases, where their codepage bytes land on Windows-1252 art: c-caron
would draw as e-grave. They take the scalable path for the same reason.

The cvar is deliberately not written back. It is archived, and quietly
rewriting a user's setting because they tried a language would leave the bitmap
path off after they switched away again.
============
*/
static bool R_UseScalableFonts( void ) {
	if ( r_useTrueTypeFonts.GetBool() ) {
		return true;
	}

	const char *language = cvarSystem->GetCVarString( "sys_lang" );
	if ( !LangDict_LanguageNeedsScalableFonts( language ) ) {
		return false;
	}

	static idStr reportedLanguage;
	if ( reportedLanguage.Icmp( language ) != 0 ) {
		reportedLanguage = language;
		common->Printf( "TTF font: '%s' needs letters the bitmap atlases have no art for; "
						"ignoring r_useTrueTypeFonts 0 for this language\n", language );
	}
	return true;
}

/*
============
R_RegisterTrueTypeFont

Fills 'font' from a shipped .ttf face, matching the layout the bitmap loader
produces.  Returns false when no usable face exists, so the caller can fall
back to the retail atlases.
============
*/
bool R_RegisterTrueTypeFont( const char *fontName, fontInfoEx_t &font ) {
	if ( !R_UseScalableFonts() ) {
		return false;
	}

	idTrueTypeFont *face = ttfFonts.FindFace( fontName );
	if ( face == NULL || !face->IsLoaded() ) {
		return false;
	}

	const float upscale = R_TTFUpscale();

	// The same for every face and size, and the string table sweep inside it is
	// not free, so collect it once here rather than three times per face.
	idList<unsigned int> extendedCodePoints;
	R_TTFCollectExtendedCodePoints( extendedCodePoints );

	fontInfo_t *slots[3] = { &font.fontInfoSmall, &font.fontInfoMedium, &font.fontInfoLarge };
	float *maxWidths[3] = { &font.maxWidthSmall, &font.maxWidthMedium, &font.maxWidthLarge };
	float *maxHeights[3] = { &font.maxHeightSmall, &font.maxHeightMedium, &font.maxHeightLarge };

	int built = 0;
	for ( int i = 0; i < 3; i++ ) {
		if ( R_TTFBuildSlot( *face, fontName, Q4_TTF_SLOTS[i], upscale, extendedCodePoints,
							 *slots[i], *maxWidths[i], *maxHeights[i] ) ) {
			built++;
		}
	}

	if ( built != 3 ) {
		common->Warning( "TTF font: '%s' only produced %i of 3 sizes; using the bitmap font", fontName, built );
		return false;
	}

	return true;
}

/*
============
R_BuildConsoleFontAtlas

The console and the loading screen do not go through the font system at all:
they slice characters straight out of the 'bigchars' sheet, a 16x16 grid of
16x16 pixel cells indexed by byte.  At any modern resolution that sheet is
being magnified several times over, which is why console text is the blurriest
text in the game.

Rather than reworking those draw paths, this rebuilds the sheet itself at the
resolution the display needs - same 16x16 grid, same cell indexing, just larger
cells - and points the existing material at it.  Every caller keeps working
unchanged because the UV maths is purely fractional.
============
*/
bool R_BuildConsoleFontAtlas( void ) {
	if ( !R_UseScalableFonts() ) {
		return false;
	}

	idTrueTypeFont *face = ttfFonts.FindFace( "fonts/bigchars" );
	if ( face == NULL || !face->IsLoaded() ) {
		return false;
	}

	// Keep the whole sheet inside the maximum texture the atlas code allows.
	const float upscale = R_TTFUpscale();
	int cellPixels = (int)idMath::Ceil( (float)Q4_CONSOLE_CELL_SIZE * upscale );
	cellPixels = Max( Q4_CONSOLE_CELL_SIZE, Min( Q4_TTF_MAX_PAGE / Q4_CONSOLE_GRID, cellPixels ) );

	const int pageSize = cellPixels * Q4_CONSOLE_GRID;
	const float scale = (float)cellPixels / (float)face->UnitsPerEm();
	// The face is built with one em to a cell, so its ascender is exactly how
	// far the baseline sits below the top of a cell.
	const int baselineOffset = (int)idMath::Rint( face->Ascender() * scale );

	byte *page = (byte *)Mem_ClearedAlloc( pageSize * pageSize );
	int rendered = 0;

	for ( int code = 32; code <= 255; code++ ) {
		// The cells are codepage bytes, so the character a cell stands for
		// depends on the active language. Getting this wrong is not a missing
		// glyph but a confidently wrong one: 0xB9 is a-ogonek in Windows-1250,
		// superscript one in Windows-1252 and a soft hyphen in Windows-1251.
		//
		// A slot the codepage leaves unassigned keeps its raw byte value, which
		// lands in the C1 control block that no face covers - so it yields glyph
		// 0 and an empty cell, rather than a .notdef box.
		const unsigned int unicode = LangDict_UnicodeForByte( code );
		const int codepoint = ( unicode != 0 ) ? (int)unicode : code;
		const int glyphIndex = face->GlyphForCodepoint( codepoint );
		if ( glyphIndex == 0 ) {
			continue;
		}

		ttGlyphBitmap_t bitmap;
		if ( !face->RasterizeGlyph( glyphIndex, scale, bitmap ) || bitmap.pixels == NULL ) {
			continue;
		}

		const int cellX = ( code & 15 ) * cellPixels;
		const int cellY = ( code >> 4 ) * cellPixels;
		const int originX = cellX + bitmap.left;
		const int originY = cellY + baselineOffset + bitmap.top;

		// Clip to the cell. The console samples exactly one cell, so anything
		// spilling over would surface as a fragment of the neighbouring glyph.
		for ( int row = 0; row < bitmap.height; row++ ) {
			const int destY = originY + row;
			if ( destY < cellY || destY >= cellY + cellPixels ) {
				continue;
			}
			for ( int column = 0; column < bitmap.width; column++ ) {
				const int destX = originX + column;
				if ( destX < cellX || destX >= cellX + cellPixels ) {
					continue;
				}
				byte &target = page[destY * pageSize + destX];
				const byte source = bitmap.pixels[row * bitmap.width + column];
				if ( source > target ) {
					target = source;
				}
			}
		}

		idTrueTypeFont::FreeGlyphBitmap( bitmap );
		rendered++;
	}

	if ( rendered == 0 ) {
		Mem_Free( page );
		return false;
	}

	idImage *image = R_TTFUploadAtlasImage( Q4_CONSOLE_ATLAS_IMAGE, pageSize, pageSize, page );
	if ( image == NULL ) {
		Mem_Free( page );
		return false;
	}

	const int texels = pageSize * pageSize;

	if ( r_ttfFontDebug.GetBool() ) {
		byte *dump = (byte *)Mem_Alloc( texels * 4 );
		for ( int i = 0; i < texels; i++ ) {
			dump[i * 4 + 0] = page[i];
			dump[i * 4 + 1] = page[i];
			dump[i * 4 + 2] = page[i];
			dump[i * 4 + 3] = 255;
		}
		R_WriteTGA( "ttfatlas/console.tga", dump, pageSize, pageSize, false, "fs_savepath" );
		Mem_Free( dump );
	}
	Mem_Free( page );

	// Retarget the existing material rather than introducing a new name, so
	// the console and the loading screen pick it up without either of them
	// having to know this path exists. The replacement keeps the retail stage
	// keywords so blending, depth and filtering behave exactly as before.
	idMaterial *material = const_cast<idMaterial *>( declManager->FindMaterial( Q4_CONSOLE_FONT_MATERIAL, false ) );
	if ( material == NULL ) {
		return false;
	}

	const shaderStage_t *originalStage = ( material->GetNumStages() > 0 ) ? material->GetStage( 0 ) : NULL;
	idImage *originalImage = ( originalStage != NULL ) ? originalStage->texture.image : NULL;
	if ( !material->OverrideStageImageForRuntime( 0, image ) ) {
		common->Warning( "TTF font: console material has no image stage to retarget" );
		return false;
	}
	if ( ttfConsoleMaterial == NULL ) {
		ttfConsoleMaterial = material;
		ttfConsoleOriginalImage = originalImage;
	}
	material->SetSort( SS_GUI );

	const shaderStage_t *stage = ( material->GetNumStages() > 0 ) ? material->GetStage( 0 ) : NULL;
	const idImage *bound = ( stage != NULL ) ? stage->texture.image : NULL;
	if ( bound != image ) {
		common->Warning( "TTF font: console material did not take the rebuilt sheet" );
		return false;
	}

	common->Printf( "TTF font: console sheet rebuilt at %ix%i (%i px cells, %i glyphs), '%s' now samples %s\n",
					pageSize, pageSize, cellPixels, rendered, Q4_CONSOLE_FONT_MATERIAL, bound->GetName() );
	return true;
}

/*
============
R_ShutdownTrueTypeFonts
============
*/
void R_ShutdownTrueTypeFonts( bool preserveAtlasImages ) {
	tr.ResetRetainedFontCache();
	if ( ttfConsoleMaterial != NULL && ttfConsoleOriginalImage != NULL ) {
		if ( !ttfConsoleMaterial->OverrideStageImageForRuntime( 0, ttfConsoleOriginalImage ) ) {
			common->Warning( "TTF font: could not restore the authored console material image" );
		}
	}
	ttfConsoleMaterial = NULL;
	ttfConsoleOriginalImage = NULL;
	ttfAtlasMaterials.DeleteContents( true );
	// A full vid_restart keeps the image objects and invokes their generators
	// before the UI registers its fonts again. Their pixels must survive that
	// boundary; final renderer shutdown destroys both the cache and the images.
	if ( !preserveAtlasImages ) {
		ttfAtlasImages.DeleteContents( true );
	}
	// Nothing may still be drawing by this point: every fontInfo_t that shares a
	// page set is dead with the renderer, and idDeviceContext re-registers its
	// fonts from scratch after a restart.
	ttfPageSets.DeleteContents( true );
	ttfFonts.Shutdown();
}

namespace {
class RetainedFontSource final : public openq4::fonts::Source {
public:
	bool Metrics(const std::string& name, int pixels, renderFontMetrics_t& out) override {
		idTrueTypeFont* face = ttfFonts.FindFace(name.c_str());
		if (!face) return false;
		const float scale = face->ScaleForPixelHeight(static_cast<float>(pixels));
		ttGlyphMetrics_t x = {};
		face->GetGlyphMetrics(face->GlyphForCodepoint('x'),x);
		out = {face->Ascender()*scale, -face->Descender()*scale,
			(face->Ascender()-face->Descender())*scale, (x.yMax-x.yMin)*scale};
		if (r_ttfFontDebug.GetBool()) common->Printf("Retained font: %s at %i output pixels\n",name.c_str(),pixels);
		return true;
	}
	bool Resolve(const std::string& name, std::uint32_t scalar, int& glyph) override {
		idTrueTypeFont* face = ttfFonts.FindFace(name.c_str());
		if (!face) return false;
		glyph = face->GlyphForCodepoint(static_cast<int>(scalar));
		if (!glyph) glyph = face->GlyphForCodepoint('?');
		return true; // .notdef is valid when even '?' is absent.
	}
	bool Rasterize(const std::string& name, int pixels, int glyph, openq4::fonts::Raster& out) override {
		idTrueTypeFont* face = ttfFonts.FindFace(name.c_str());
		ttGlyphMetrics_t metrics;
		if (!face || !face->GetGlyphMetrics(glyph,metrics)) return false;
		struct Bitmap {
			ttGlyphBitmap_t value = {};
			~Bitmap() { idTrueTypeFont::FreeGlyphBitmap(value); }
		} bitmap;
		const float scale = face->ScaleForPixelHeight(static_cast<float>(pixels));
		if (!face->RasterizeGlyph(glyph,scale,bitmap.value)) return false;
		out.advance = metrics.advance*scale;
		out.left = bitmap.value.left; out.top = bitmap.value.top;
		out.width = bitmap.value.width; out.height = bitmap.value.height;
		if (bitmap.value.pixels) out.coverage.assign(bitmap.value.pixels,
			bitmap.value.pixels+static_cast<size_t>(out.width)*out.height);
		return true;
	}
};
void RetainedFontPageImage(idImage* image);
class RetainedFontDevice final : public openq4::fonts::Device {
public:
	bool CreatePage(unsigned page, int dimension, const char* name) override {
		if (dimension != openq4::fonts::Cache::PageSize) return false;
		// A scratch page would come back from reloadImages reallocated and
		// empty while every glyph on it stays published. A generated page is
		// rebuilt instead: the cache rasterizes its glyphs into the same places.
		idImage* image = globalImages->ImageFromFunction(name,RetainedFontPageImage);
		if (!image) return false;
		images[page] = image;
		// A new image ran its generator before this page owned it, and one kept
		// from before a Reset has no storage. Allocate it now. An image of the
		// same name made elsewhere has no page generator and is refused.
		image->Reload(true);
		return image->IsLoaded() && image->GetOpts().width == dimension && image->GetOpts().height == dimension;
	}
	bool Upload(unsigned page, int x, int y, int width, int height, const unsigned char* rgba) override {
		if (!images[page] || !images[page]->IsLoaded()) return false;
		images[page]->SubImageUpload(0,x,y,0,width,height,rgba);
		return true;
	}
	void Reset() override {
		for (auto& image : images) { if (image) image->PurgeImage(); image = NULL; }
	}
	bool Owns(const idImage* image, unsigned& page) const {
		for (unsigned i = 0; i < openq4::fonts::Cache::MaxPages; ++i) if (images[i] == image) { page = i; return true; }
		return false;
	}
private:
	idImage* images[openq4::fonts::Cache::MaxPages] = {};
};
RetainedFontSource retainedFontSource;
RetainedFontDevice retainedFontDevice;
openq4::fonts::Cache retainedFontCache(retainedFontSource,retainedFontDevice);
void RetainedFontPageImage(idImage* image) {
	// A page released by Reset stays empty until the cache opens it again.
	unsigned page = 0;
	if (!retainedFontDevice.Owns(image,page)) return;
	const int dimension = openq4::fonts::Cache::PageSize;
	idImageOpts opts;
	opts.textureType = TT_2D; opts.format = FMT_RGBA8; opts.colorFormat = CFM_DEFAULT;
	opts.width = opts.height = dimension; opts.numLevels = 1; opts.isPersistant = true;
	image->AllocImage(opts,TF_LINEAR,TR_CLAMP);
	if (!image->IsLoaded()) return;
	std::vector<byte> clear(static_cast<size_t>(dimension)*dimension*4,255);
	for (size_t i = 3; i < clear.size(); i += 4) clear[i] = 0;
	image->SubImageUpload(0,0,0,0,dimension,dimension,clear.data());
	// A page still being opened has no glyphs yet.
	if (page < retainedFontCache.PageCount() && !retainedFontCache.RestorePage(page))
		common->Warning("Retained font page %s could not restore every glyph",image->GetName());
}
}
bool idRenderSystemLocal::GetRetainedFontMetrics(const char* face, int pixels, renderFontMetrics_t& out) {
	return face && retainedFontCache.Metrics(face,pixels,out);
}
bool idRenderSystemLocal::GetRetainedFontGlyph(const char* face, int pixels, unsigned int scalar, renderFontGlyph_t& out) {
	return face && retainedFontCache.Glyph(face,pixels,scalar,out);
}
void idRenderSystemLocal::ResetRetainedFontCache() {
	if (r_ttfFontDebug.GetBool() && retainedFontCache.GlyphCount()) common->Printf(
		"Retained font cache reset: %u glyphs, %u pages\n",retainedFontCache.GlyphCount(),retainedFontCache.PageCount());
	retainedFontCache.Reset();
}
