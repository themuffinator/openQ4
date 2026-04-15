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




static autoComplete_t	globalAutoComplete;
static bool				completionQueryMode;
static bool				completionQueryCollectAll;
static editFieldCompletionQueryCallback_t completionQueryCallback;
static void *			completionQueryContext;
static int				completionQueryTotalCount;

static const char *FindCompletionSegmentStart( const char *cmd ) {
	const char *segmentStart = cmd;
	for ( const char *scan = cmd; *scan != '\0'; ++scan ) {
		if ( *scan == ';' ) {
			segmentStart = scan + 1;
		}
	}
	return segmentStart;
}

static void NormalizeCompletionCommandString( const char *cmd, char *normalized, size_t normalizedSize ) {
	const char *segmentStart = FindCompletionSegmentStart( cmd );

	while ( *segmentStart != '\0' && *segmentStart <= ' ' ) {
		++segmentStart;
	}

	idStr::Copynz( normalized, segmentStart, normalizedSize );
	if ( normalized[0] == '/' || normalized[0] == '\\' ) {
		memmove( normalized, normalized + 1, strlen( normalized ) );
	}
}

static int QueryCompletionInternal( const char *cmd, bool *appendSpace, editFieldCompletionQueryCallback_t callback, void *context, bool collectAll );

static void DrawScaledSmallChar( float x, float y, float charWidth, int ch, const idMaterial *shader ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;
	if ( ch == ' ' ) {
		return;
	}

	if ( y < -SMALLCHAR_HEIGHT ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;
	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;

	renderSystem->DrawStretchPic( x, y, charWidth, SMALLCHAR_HEIGHT,
		fcol, frow, fcol + size, frow + size, shader );
}

static void DrawScaledSmallStringExt( float x, float y, float charWidth, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *shader ) {
	idVec4 color;
	const unsigned char *s;
	float xx;

	s = reinterpret_cast<const unsigned char *>( string );
	xx = x;
	renderSystem->SetColor( setColor );

	while ( *s ) {
		idVec4 parsedColor;
		bool resetToDefault = false;
		const int colorEscapeLength = idStr::ColorEscapeLength( reinterpret_cast<const char *>( s ), &parsedColor, &resetToDefault );
		if ( colorEscapeLength > 0 ) {
			if ( !forceColor ) {
				if ( resetToDefault ) {
					renderSystem->SetColor( setColor );
				} else {
					color = parsedColor;
					color[3] = setColor[3];
					renderSystem->SetColor( color );
				}
			}
			s += colorEscapeLength;
			continue;
		}

		DrawScaledSmallChar( xx, y, charWidth, *s, shader );
		xx += charWidth;
		s++;
	}

	renderSystem->SetColor( colorWhite );
}

/*
===============
FindMatches
===============
*/
static void FindMatches( const char *s ) {
	int		i;
	const bool prefixMatch = ( idStr::Icmpn( s, globalAutoComplete.completionString, strlen( globalAutoComplete.completionString ) ) == 0 );

	if ( completionQueryMode && completionQueryCallback != NULL && ( prefixMatch || completionQueryCollectAll ) ) {
		++completionQueryTotalCount;
		if ( !completionQueryCallback( s, completionQueryContext ) ) {
			completionQueryCallback = NULL;
		}
	}

	if ( !prefixMatch ) {
		return;
	}
	globalAutoComplete.matchCount++;
	if ( globalAutoComplete.matchCount == 1 ) {
		idStr::Copynz( globalAutoComplete.currentMatch, s, sizeof( globalAutoComplete.currentMatch ) );
		return;
	}

	// cut currentMatch to the amount common with s
	for ( i = 0; s[i]; i++ ) {
		if ( tolower( globalAutoComplete.currentMatch[i] ) != tolower( s[i] ) ) {
			globalAutoComplete.currentMatch[i] = 0;
			break;
		}
	}
	globalAutoComplete.currentMatch[i] = 0;
}

/*
===============
FindIndexMatch
===============
*/
static void FindIndexMatch( const char *s ) {

	if ( idStr::Icmpn( s, globalAutoComplete.completionString, strlen( globalAutoComplete.completionString ) ) != 0 ) {
		return;
	}

	if( globalAutoComplete.findMatchIndex == globalAutoComplete.matchIndex ) {
		idStr::Copynz( globalAutoComplete.currentMatch, s, sizeof( globalAutoComplete.currentMatch ) );
	}

	globalAutoComplete.findMatchIndex++;
}

/*
===============
PrintMatches
===============
*/
static void PrintMatches( const char *s ) {
	if ( idStr::Icmpn( s, globalAutoComplete.currentMatch, strlen( globalAutoComplete.currentMatch ) ) == 0 ) {
		common->Printf( "    %s\n", s );
	}
}

/*
===============
PrintCvarMatches
===============
*/
static void PrintCvarMatches( const char *s ) {
	if ( idStr::Icmpn( s, globalAutoComplete.currentMatch, strlen( globalAutoComplete.currentMatch ) ) == 0 ) {
		common->Printf( "    %s" S_COLOR_WHITE " = \"%s\"\n", s, cvarSystem->GetCVarString( s ) );
	}
}

/*
===============
idEditField::idEditField
===============
*/
idEditField::idEditField() {
	widthInChars = 0;
	Clear();
}

/*
===============
idEditField::~idEditField
===============
*/
idEditField::~idEditField() {
}

/*
===============
idEditField::Clear
===============
*/
void idEditField::Clear( void ) {
	buffer[0] = 0;
	cursor = 0;
	scroll = 0;
	autoComplete.length = 0;
	autoComplete.valid = false;
}

/*
===============
idEditField::SetWidthInChars
===============
*/
void idEditField::SetWidthInChars( int w ) {
	assert( w <= MAX_EDIT_LINE );
	widthInChars = w;
	ClampCursorAndScroll();
}

/*
===============
idEditField::SetCursor
===============
*/
void idEditField::SetCursor( int c ) {
	assert( c <= MAX_EDIT_LINE );
	cursor = c;
	ClampCursorAndScroll();
}

/*
===============
idEditField::GetCursor
===============
*/
int idEditField::GetCursor( void ) const {
	return cursor;
}

/*
===============
idEditField::GetScroll
===============
*/
int idEditField::GetScroll( void ) const {
	return scroll;
}

/*
===============
idEditField::SetScroll
===============
*/
void idEditField::SetScroll( int s ) {
	scroll = s;
	ClampCursorAndScroll();
}

/*
===============
idEditField::GetWidthInChars
===============
*/
int idEditField::GetWidthInChars( void ) const {
	return widthInChars;
}

/*
===============
idEditField::GetLength
===============
*/
int idEditField::GetLength( void ) const {
	return strlen( buffer );
}

/*
===============
idEditField::ClampCursorAndScroll
===============
*/
void idEditField::ClampCursorAndScroll( void ) {
	const int len = strlen( buffer );
	const int drawLen = Max( 1, widthInChars );

	if ( cursor < 0 ) {
		cursor = 0;
	} else if ( cursor > len ) {
		cursor = len;
	}

	if ( scroll < 0 ) {
		scroll = 0;
	}

	if ( cursor < scroll ) {
		scroll = cursor;
	} else if ( cursor >= scroll + drawLen ) {
		scroll = cursor - drawLen + 1;
	}

	if ( len > drawLen && scroll > len - drawLen ) {
		scroll = len - drawLen;
	}

	if ( scroll < 0 ) {
		scroll = 0;
	}
}

/*
===============
idEditField::ClearAutoComplete
===============
*/
void idEditField::ClearAutoComplete( void ) {
	if ( autoComplete.length > 0 && autoComplete.length <= (int) strlen( buffer ) ) {
		buffer[autoComplete.length] = '\0';
		if ( cursor > autoComplete.length ) {
			cursor = autoComplete.length;
		}
	}
	autoComplete.length = 0;
	autoComplete.valid = false;
}

/*
===============
idEditField::GetAutoCompleteLength
===============
*/
int idEditField::GetAutoCompleteLength( void ) const {
	return autoComplete.length;
}

/*
===============
idEditField::AutoComplete
===============
*/
void idEditField::AutoComplete( void ) {
	char completionArgString[MAX_EDIT_LINE];
	idCmdArgs args;

	if ( !autoComplete.valid ) {
		const bool explicitCommandPrefix = ( buffer[0] == '/' || buffer[0] == '\\' );
		const char commandPrefix = explicitCommandPrefix ? buffer[0] : '\0';

		args.TokenizeString( buffer, false );
		const char *completionBase = args.Argv( 0 );
		if ( ( completionBase[0] == '/' || completionBase[0] == '\\' ) && completionBase[1] != '\0' ) {
			++completionBase;
		}
		idStr::Copynz( autoComplete.completionString, completionBase, sizeof( autoComplete.completionString ) );
		idStr::Copynz( completionArgString, args.Args(), sizeof( completionArgString ) );
		autoComplete.matchCount = 0;
		autoComplete.matchIndex = 0;
		autoComplete.currentMatch[0] = 0;

		if ( strlen( autoComplete.completionString ) == 0 ) {
			return;
		}

		globalAutoComplete = autoComplete;

		cmdSystem->CommandCompletion( FindMatches );
		cvarSystem->CommandCompletion( FindMatches );

		autoComplete = globalAutoComplete;

		if ( autoComplete.matchCount == 0 ) {
			return;	// no matches
		}

		// when there's only one match or there's an argument
		if ( autoComplete.matchCount == 1 || completionArgString[0] != '\0' ) {

			/// try completing arguments
			idStr::Append( autoComplete.completionString, sizeof( autoComplete.completionString ), " " );
			idStr::Append( autoComplete.completionString, sizeof( autoComplete.completionString ), completionArgString );
			autoComplete.matchCount = 0;

			globalAutoComplete = autoComplete;

			cmdSystem->ArgCompletion( autoComplete.completionString, FindMatches );
			cvarSystem->ArgCompletion( autoComplete.completionString, FindMatches );

			autoComplete = globalAutoComplete;

			idStr::snPrintf( buffer, sizeof( buffer ), "%s", autoComplete.currentMatch );
			if ( explicitCommandPrefix && buffer[0] != '/' && buffer[0] != '\\' && strlen( buffer ) + 1 < sizeof( buffer ) ) {
				memmove( buffer + 1, buffer, strlen( buffer ) + 1 );
				buffer[0] = commandPrefix;
			}

			if ( autoComplete.matchCount == 0 ) {
				// no argument matches
				idStr::Append( buffer, sizeof( buffer ), " " );
				idStr::Append( buffer, sizeof( buffer ), completionArgString );
				SetCursor( strlen( buffer ) );
				return;
			}
		} else {

			// multiple matches, complete to shortest
			idStr::snPrintf( buffer, sizeof( buffer ), "%s", autoComplete.currentMatch );
			if ( explicitCommandPrefix && buffer[0] != '/' && buffer[0] != '\\' && strlen( buffer ) + 1 < sizeof( buffer ) ) {
				memmove( buffer + 1, buffer, strlen( buffer ) + 1 );
				buffer[0] = commandPrefix;
			}
			if ( strlen( completionArgString ) ) {
				idStr::Append( buffer, sizeof( buffer ), " " );
				idStr::Append( buffer, sizeof( buffer ), completionArgString );
			}
		}

		autoComplete.length = strlen( buffer );
		autoComplete.valid = ( autoComplete.matchCount != 1 );
		SetCursor( autoComplete.length );

		common->Printf( "]%s\n", buffer );

		// run through again, printing matches
		globalAutoComplete = autoComplete;

		cmdSystem->CommandCompletion( PrintMatches );
		cmdSystem->ArgCompletion( autoComplete.completionString, PrintMatches );
		cvarSystem->CommandCompletion( PrintCvarMatches );
		cvarSystem->ArgCompletion( autoComplete.completionString, PrintMatches );

	} else if ( autoComplete.matchCount != 1 ) {
		const bool explicitCommandPrefix = ( buffer[0] == '/' || buffer[0] == '\\' );
		const char commandPrefix = explicitCommandPrefix ? buffer[0] : '\0';

		// get the next match and show instead
		autoComplete.matchIndex++;
		if ( autoComplete.matchIndex == autoComplete.matchCount ) {
			autoComplete.matchIndex = 0;
		}
		autoComplete.findMatchIndex = 0;

		globalAutoComplete = autoComplete;

		cmdSystem->CommandCompletion( FindIndexMatch );
		cmdSystem->ArgCompletion( autoComplete.completionString, FindIndexMatch );
		cvarSystem->CommandCompletion( FindIndexMatch );
		cvarSystem->ArgCompletion( autoComplete.completionString, FindIndexMatch );

		autoComplete = globalAutoComplete;

		// and print it
		idStr::snPrintf( buffer, sizeof( buffer ), autoComplete.currentMatch );
		if ( explicitCommandPrefix && buffer[0] != '/' && buffer[0] != '\\' && strlen( buffer ) + 1 < sizeof( buffer ) ) {
			memmove( buffer + 1, buffer, strlen( buffer ) + 1 );
			buffer[0] = commandPrefix;
		}
		if ( autoComplete.length > (int)strlen( buffer ) ) {
			autoComplete.length = strlen( buffer );
		}
		SetCursor( autoComplete.length );
	}
}

static int QueryCompletionInternal( const char *cmd, bool *appendSpace, editFieldCompletionQueryCallback_t callback, void *context, bool collectAll ) {
	char normalizedCmd[MAX_EDIT_LINE];
	idCmdArgs args;
	bool savedQueryMode;
	bool savedCollectAll;
	editFieldCompletionQueryCallback_t savedCallback;
	void *savedContext;

	if ( appendSpace != NULL ) {
		*appendSpace = false;
	}

	if ( cmd == NULL || cmd[0] == '\0' ) {
		return 0;
	}

	NormalizeCompletionCommandString( cmd, normalizedCmd, sizeof( normalizedCmd ) );
	if ( normalizedCmd[0] == '\0' ) {
		return 0;
	}

	args.TokenizeString( normalizedCmd, false );
	if ( args.Argc() == 0 ) {
		return 0;
	}

	globalAutoComplete.valid = false;
	globalAutoComplete.length = 0;
	globalAutoComplete.matchCount = 0;
	globalAutoComplete.matchIndex = 0;
	globalAutoComplete.findMatchIndex = 0;
	globalAutoComplete.currentMatch[0] = '\0';

	const int normalizedLength = strlen( normalizedCmd );
	const bool trailingWhitespace = normalizedLength > 0 && normalizedCmd[normalizedLength - 1] <= ' ';
	const bool completingArguments = ( args.Argc() > 1 ) || trailingWhitespace;
	if ( completingArguments ) {
		globalAutoComplete.completionString[0] = '\0';
	} else {
		idStr::Copynz( globalAutoComplete.completionString, args.Argv( args.Argc() - 1 ), sizeof( globalAutoComplete.completionString ) );
	}

	savedQueryMode = completionQueryMode;
	savedCollectAll = completionQueryCollectAll;
	savedCallback = completionQueryCallback;
	savedContext = completionQueryContext;

	completionQueryMode = true;
	completionQueryCollectAll = collectAll;
	completionQueryCallback = callback;
	completionQueryContext = context;
	completionQueryTotalCount = 0;

	if ( completingArguments ) {
		cmdSystem->ArgCompletion( normalizedCmd, FindMatches );
		cvarSystem->ArgCompletion( normalizedCmd, FindMatches );
	} else {
		cmdSystem->CommandCompletion( FindMatches );
		cvarSystem->CommandCompletion( FindMatches );
	}

	if ( appendSpace != NULL ) {
		*appendSpace = ( globalAutoComplete.matchCount == 1 );
	}

	completionQueryMode = savedQueryMode;
	completionQueryCollectAll = savedCollectAll;
	completionQueryCallback = savedCallback;
	completionQueryContext = savedContext;

	return collectAll ? completionQueryTotalCount : globalAutoComplete.matchCount;
}

/*
===============
idEditField::QueryCompletionMatches
===============
*/
int idEditField::QueryCompletionMatches( const char *cmd, bool *appendSpace, editFieldCompletionQueryCallback_t callback, void *context ) {
	return QueryCompletionInternal( cmd, appendSpace, callback, context, false );
}

/*
===============
idEditField::QueryCompletionCandidates
===============
*/
int idEditField::QueryCompletionCandidates( const char *cmd, editFieldCompletionQueryCallback_t callback, void *context ) {
	return QueryCompletionInternal( cmd, NULL, callback, context, true );
}

/*
===============
idEditField::CharEvent
===============
*/
void idEditField::CharEvent( int ch ) {
	int		len;

	if ( ch == 'v' - 'a' + 1 ) {	// ctrl-v is paste
		Paste();
		return;
	}

	if ( ch == 'c' - 'a' + 1 ) {	// ctrl-c clears the field
		Clear();
		return;
	}

	len = strlen( buffer );

	if ( ch == 'h' - 'a' + 1 || ch == K_BACKSPACE ) {	// ctrl-h is backspace
		if ( cursor > 0 ) {
			memmove( buffer + cursor - 1, buffer + cursor, len + 1 - cursor );
			cursor--;
			if ( cursor < scroll ) {
				scroll--;
			}
		}
		return;
	}

	if ( ch == 'a' - 'a' + 1 ) {	// ctrl-a is home
		cursor = 0;
		scroll = 0;
		return;
	}

	if ( ch == 'e' - 'a' + 1 ) {	// ctrl-e is end
		cursor = len;
		scroll = cursor - widthInChars;
		return;
	}

	//
	// ignore any other non printable chars
	//
	if ( ch < 32 ) {
		return;
	}

	if ( idKeyInput::GetOverstrikeMode() ) {	
		if ( cursor == MAX_EDIT_LINE - 1 ) {
			return;
		}
		buffer[cursor] = ch;
		cursor++;
	} else {	// insert mode
		if ( len == MAX_EDIT_LINE - 1 ) {
			return; // all full
		}
		memmove( buffer + cursor + 1, buffer + cursor, len + 1 - cursor );
		buffer[cursor] = ch;
		cursor++;
	}


	if ( cursor >= widthInChars ) {
		scroll++;
	}

	if ( cursor == len + 1 ) {
		buffer[cursor] = 0;
	}
}

/*
===============
idEditField::KeyDownEvent
===============
*/
void idEditField::KeyDownEvent( int key ) {
	int		len;

	// shift-insert is paste
	if ( ( ( key == K_INS ) || ( key == K_KP_INS ) ) && idKeyInput::IsDown( K_SHIFT ) ) {
		ClearAutoComplete();
		Paste();
		return;
	}

	len = strlen( buffer );

	if ( key == K_DEL ) {
		if ( autoComplete.length ) {
			ClearAutoComplete();
		} else if ( cursor < len ) {
			memmove( buffer + cursor, buffer + cursor + 1, len - cursor );
		}
		return;
	}

	if ( key == K_RIGHTARROW ) {
		if ( idKeyInput::IsDown( K_CTRL ) ) {
			// skip to next word
			while( ( cursor < len ) && ( buffer[ cursor ] != ' ' ) ) {
				cursor++;
			}

			while( ( cursor < len ) && ( buffer[ cursor ] == ' ' ) ) {
				cursor++;
			}
		} else {
			cursor++;
		}

		if ( cursor > len ) {
			cursor = len;
		}

		if ( cursor >= scroll + widthInChars ) {
			scroll = cursor - widthInChars + 1;
		}

		if ( autoComplete.length > 0 ) {
			autoComplete.length = cursor;
		}
		return;
	}

	if ( key == K_LEFTARROW ) {
		if ( idKeyInput::IsDown( K_CTRL ) ) {
			// skip to previous word
			while( ( cursor > 0 ) && ( buffer[ cursor - 1 ] == ' ' ) ) {
				cursor--;
			}

			while( ( cursor > 0 ) && ( buffer[ cursor - 1 ] != ' ' ) ) {
				cursor--;
			}
		} else {
			cursor--;
		}

		if ( cursor < 0 ) {
			cursor = 0;
		}
		if ( cursor < scroll ) {
			scroll = cursor;
		}

		if ( autoComplete.length ) {
			autoComplete.length = cursor;
		}
		return;
	}

	if ( key == K_HOME || ( tolower( key ) == 'a' && idKeyInput::IsDown( K_CTRL ) ) ) {
		cursor = 0;
		scroll = 0;
		if ( autoComplete.length ) {
			autoComplete.length = cursor;
			autoComplete.valid = false;
		}
		return;
	}

	if ( key == K_END || ( tolower( key ) == 'e' && idKeyInput::IsDown( K_CTRL ) ) ) {
		cursor = len;
		if ( cursor >= scroll + widthInChars ) {
			scroll = cursor - widthInChars + 1;
		}
		if ( autoComplete.length ) {
			autoComplete.length = cursor;
			autoComplete.valid = false;
		}
		return;
	}

	if ( key == K_INS ) {
		idKeyInput::SetOverstrikeMode( !idKeyInput::GetOverstrikeMode() );
		return;
	}

	// clear autocompletion buffer on normal key input
	if ( key != K_CAPSLOCK && key != K_ALT && key != K_CTRL && key != K_SHIFT ) {
		ClearAutoComplete();
	}
}

/*
===============
idEditField::Paste
===============
*/
void idEditField::Paste( void ) {
	char	*cbd;
	int		pasteLen, i;

	cbd = Sys_GetClipboardData();

	if ( !cbd ) {
		return;
	}

	// send as if typed, so insert / overstrike works properly
	pasteLen = strlen( cbd );
	for ( i = 0; i < pasteLen; i++ ) {
		CharEvent( cbd[i] );
	}

	Mem_Free( cbd );
}

/*
===============
idEditField::GetBuffer
===============
*/
char *idEditField::GetBuffer( void ) {
	return buffer;
}

/*
===============
idEditField::GetBuffer
===============
*/
const char *idEditField::GetBuffer( void ) const {
	return buffer;
}

/*
===============
idEditField::SetBuffer
===============
*/
void idEditField::SetBuffer( const char *buf ) {
	Clear();
	idStr::Copynz( buffer, buf, sizeof( buffer ) );
	SetCursor( strlen( buffer ) );
	ClampCursorAndScroll();
}

/*
===============
idEditField::Draw
===============
*/
void idEditField::Draw( int x, int y, int width, bool showCursor, const idMaterial *shader ) {
	int		len;
	int		drawLen;
	int		prestep;
	int		cursorChar;
	char	str[MAX_EDIT_LINE];
	float	size;

	if ( widthInChars > 0 && width > 0 ) {
		size = static_cast<float>( width ) / static_cast<float>( widthInChars );
	} else {
		size = SMALLCHAR_WIDTH;
	}
	size = idMath::ClampFloat( 1.0f, 64.0f, size );

	drawLen = widthInChars;
	len = strlen( buffer ) + 1;

	// guarantee that cursor will be visible
	if ( len <= drawLen ) {
		prestep = 0;
	} else {
		if ( scroll + drawLen > len ) {
			scroll = len - drawLen;
			if ( scroll < 0 ) {
				scroll = 0;
			}
		}
		prestep = scroll;

		// Skip color code
		//if ( idStr::IsColor( buffer + prestep ) ) { 
		//	prestep += 2;
		//}
		//if ( prestep > 0 && idStr::IsColor( buffer + prestep - 1 ) ) {
		//	prestep++;
		//}
	}

	if ( prestep + drawLen > len ) {
		drawLen = len - prestep;
	}

	// extract <drawLen> characters from the field at <prestep>
	if ( drawLen >= MAX_EDIT_LINE ) {
		common->Error( "drawLen >= MAX_EDIT_LINE" );
	}

	memcpy( str, buffer + prestep, drawLen );
	str[ drawLen ] = 0;

	// draw it
	if ( idMath::Fabs( size - SMALLCHAR_WIDTH ) < 0.001f ) {
		renderSystem->DrawSmallStringExt( x, y, str, colorWhite, false, shader );
	} else {
		DrawScaledSmallStringExt( static_cast<float>( x ), static_cast<float>( y ), size, str, colorWhite, false, shader );
	}

	// draw the cursor
	if ( !showCursor ) {
		return;
	}

	if ( (int)( com_ticNumber >> 4 ) & 1 ) {
		return;		// off blink
	}

	if ( idKeyInput::GetOverstrikeMode() ) {
		cursorChar = 11;
	} else {
		cursorChar = 10;
	}

	// Move the cursor back to account for color codes
	//for ( int i = 0; i<cursor; i++ ) {
	//	if ( idStr::IsColor( &str[i] ) ) {
	//		i++;
	//		prestep += 2;
	//	}
	//}

	if ( idMath::Fabs( size - SMALLCHAR_WIDTH ) < 0.001f ) {
		renderSystem->DrawSmallChar( x + idMath::FtoiFast( ( cursor - prestep ) * size ), y, cursorChar, shader );
	} else {
		DrawScaledSmallChar( x + ( cursor - prestep ) * size, static_cast<float>( y ), size, cursorChar, shader );
	}
}
