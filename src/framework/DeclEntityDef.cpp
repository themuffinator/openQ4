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

idDeclEntityDef::idDeclEntityDef() {

}

/*
================
DeclMapDef_ApplyAwakeningLevelshot

The Awakening's campaign mapDefs name the alpha's own levelshots: single
development captures (one shows a frame counter, another the gunner HUD), and
m05_bio and both m06 levels borrow another map's picture. openQ4 ships a full
five-tile set for each campaign level under gfx/guis/loadscreens/awakening/,
and while The Awakening is mounted each of these mapDefs names its set instead,
so the loading screen, the pause and save pictures and the chapter list all
show it. A mapDef whose loadimage a mod has changed keeps the mod's, and a
set that is not installed (the loadscreen pack is optional) leaves the alpha's.
================
*/
struct awakeningLevelshot_t {
	const char *	mapDef;
	const char *	alphaLoadImage;
	const char *	loadImage;
};

static const awakeningLevelshot_t awakeningLevelshots[] = {
	{ "game/m01_stranarus_trench1",	"gfx/guis/loadscreens/m01_stranarus_trench1",	"gfx/guis/loadscreens/awakening/m01_stranarus_trench1" },
	{ "game/m01_stranarus_trench2",	"gfx/guis/loadscreens/m01_stranarus_trench2",	"gfx/guis/loadscreens/awakening/m01_stranarus_trench2" },
	{ "game/m02_trianfac",			"gfx/guis/loadscreens/m02_trianfac",			"gfx/guis/loadscreens/awakening/m02_trianfac" },
	{ "game/m03_airassault",		"gfx/guis/loadscreens/m03_airassault",			"gfx/guis/loadscreens/awakening/m03_airassault" },
	{ "game/m04_prison",			"gfx/guis/loadscreens/m04_prison",				"gfx/guis/loadscreens/awakening/m04_prison" },
	{ "game/m05_bio",				"gfx/guis/loadscreens/m06_prison2",				"gfx/guis/loadscreens/awakening/m05_bio" },
	{ "game/m06_mcc",				"gfx/guis/loadscreens/mcc",						"gfx/guis/loadscreens/awakening/m06_mcc" },
	{ "game/m06_mcc_invasion",		"gfx/guis/loadscreens/mcc",						"gfx/guis/loadscreens/awakening/m06_mcc_invasion" },
	{ "game/m07_race",				"gfx/guis/loadscreens/m07_race",				"gfx/guis/loadscreens/awakening/m07_race" },
	{ "game/m07_race1",				"gfx/guis/loadscreens/m07_race1",				"gfx/guis/loadscreens/awakening/m07_race1" },
	{ "game/m07_race2",				"gfx/guis/loadscreens/m07_race2",				"gfx/guis/loadscreens/awakening/m07_race2" },
	{ "game/m08_cryofac",			"gfx/guis/loadscreens/m08_cryofac",				"gfx/guis/loadscreens/awakening/m08_cryofac" },
	{ "game/m09_valkaryne",			"gfx/guis/loadscreens/m10_valkaryne",			"gfx/guis/loadscreens/awakening/m09_valkaryne" },
};

static bool DeclMapDef_LevelshotInstalled( const char *loadImage ) {
	return fileSystem->ReadFile( va( "%s.tga", loadImage ), NULL, NULL ) > 0 ||
		fileSystem->ReadFile( va( "%s.dds", loadImage ), NULL, NULL ) > 0;
}

static void DeclMapDef_ApplyAwakeningLevelshot( const char *mapDefName, idDict &dict ) {
	if ( !FS_AwakeningActive() || fileSystem == NULL ) {
		return;
	}
	for ( int i = 0; i < (int)( sizeof( awakeningLevelshots ) / sizeof( awakeningLevelshots[0] ) ); i++ ) {
		const awakeningLevelshot_t &shot = awakeningLevelshots[i];
		if ( idStr::Icmp( mapDefName, shot.mapDef ) != 0 ) {
			continue;
		}
		idStr current = dict.GetString( "loadimage", "" );
		current.BackSlashesToSlashes();
		current.StripFileExtension();
		if ( current.Icmp( shot.alphaLoadImage ) == 0 && DeclMapDef_LevelshotInstalled( shot.loadImage ) ) {
			dict.Set( "loadimage", shot.loadImage );
		}
		return;
	}
}

/*
=================
idDeclEntityDef::Size
=================
*/
size_t idDeclEntityDef::Size( void ) const {
	return sizeof( idDeclEntityDef ) + dict.Allocated();
}

/*
================
idDeclEntityDef::FreeData
================
*/
void idDeclEntityDef::FreeData( void ) {
	dict.Clear();
}

/*
================
idDeclEntityDef::Parse
================
*/
bool idDeclEntityDef::Parse( const char *text, const int textLength ) {
	return Parse( text, textLength, false );
}

/*
================
idDeclEntityDef::Parse
================
*/
bool idDeclEntityDef::Parse( const char *text, const int textLength, bool noCaching ) {
	idLexer src;
	idToken	token, token2;

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_LEXER_FLAGS );
	src.SkipUntilString( "{" );

	while (1) {
		if ( !src.ReadToken( &token ) ) {
			break;
		}

		if ( !token.Icmp( "}" ) ) {
			break;
		}
		if ( token.type != TT_STRING ) {
			src.Warning( "Expected quoted string, but found '%s'", token.c_str() );
			MakeDefault();
			return false;
		}

		if ( !src.ReadToken( &token2 ) ) {
			src.Warning( "Unexpected end of file" );
			MakeDefault();
			return false;
		}

		if ( dict.FindKey( token ) ) {
			src.Warning( "'%s' already defined", token.c_str() );
		}
		dict.Set( token, token2 );
	}

	// we always automatically set a "classname" key to our name
	dict.Set( "classname", GetName() );

	// "inherit" keys will cause all values from another entityDef to be copied into this one
	// if they don't conflict.  We can't have circular recursions, because each entityDef will
	// never be parsed mroe than once

	// find all of the dicts first, because copying inherited values will modify the dict
	idList<const idDeclEntityDef *> defList;

	while ( 1 ) {
		const idKeyValue *kv;
		kv = dict.MatchPrefix( "inherit", NULL );
		if ( !kv ) {
			break;
		}

		const idDeclEntityDef *copy = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, kv->GetValue(), true, noCaching ) );
		if ( !copy ) {
			src.Warning( "Unknown entityDef '%s' inherited by '%s'", kv->GetValue().c_str(), GetName() );
		} else {
			defList.Append( copy );
		}

		// delete this key/value pair
		dict.Delete( kv->GetKey() );
	}

	// now copy over the inherited key / value pairs
	for ( int i = 0 ; i < defList.Num() ; i++ ) {
		dict.SetDefaults( &defList[ i ]->dict );
	}

	if ( GetType() == DECL_MAPDEF ) {
		DeclMapDef_ApplyAwakeningLevelshot( GetName(), dict );
	}

	// Keep the retail-style tool-state decision centralized so validation/tool
	// modes do not drift away from entityDef media-cache behavior.
	if ( openQ4_ShouldCacheEntityDefMedia( noCaching ) ) {
		game->CacheDictionaryMedia( &dict );
	}

	return true;
}

/*
================
idDeclEntityDef::DefaultDefinition
================
*/
const char *idDeclEntityDef::DefaultDefinition( void ) const {
	return
		"{\n"
	"\t"	"\"DEFAULTED\"\t\"1\"\n"
		"}";
}

/*
================
idDeclEntityDef::Print

Dumps all key/value pairs, including inherited ones
================
*/
void idDeclEntityDef::Print( void ) const {
	dict.Print();
}

/*
================
idDeclEntityDef::Validate
================
*/
bool idDeclEntityDef::Validate( const char *psText, int iTextLength, idStr &strReportTo ) const {
	(void)strReportTo;

	idDecl *decl = declManager->AllocateDecl( DECL_ENTITYDEF );
	const bool valid = DeclManager_ValidateParsedDecl( decl, DECL_ENTITYDEF, decl != NULL && decl->Parse( psText, iTextLength, false ) );
	if ( decl != NULL ) {
		decl->FreeData();
	}
	DeclManager_FreeAllocatedDecl( decl );
	return valid;
}
