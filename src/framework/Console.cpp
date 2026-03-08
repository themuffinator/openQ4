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




void SCR_DrawTextLeftAlign( float &y, const char *text, ... ) id_attribute((format(printf,2,3)));
void SCR_DrawTextRightAlign( float &y, const char *text, ... ) id_attribute((format(printf,2,3)));

#define	NUM_CON_TIMES			4
#define	CON_TEXTSIZE			0x30000
#define CON_DEFAULT_LINE_WIDTH	78
#define CON_MIN_LINE_WIDTH		8
#define CON_LINE_STRIDE			256
#define	CON_MAX_TOTAL_LINES		(CON_TEXTSIZE / CON_LINE_STRIDE)
#define CON_HISTORY_MAX_CHARS	(CON_TEXTSIZE * 8)
#define CONSOLE_FIRSTREPEAT		200
#define CONSOLE_REPEAT			100

#define	COMMAND_HISTORY			64
static const char *kConsoleHistoryFileName = "consolehistory.dat";

static const idVec4 kConsoleBorderColor( 0.9411765f, 0.6196079f, 0.0509804f, 1.0f ); // #f09e0d
static const idVec4 kConsoleVersionColor( 0.4509804f, 0.4509804f, 0.4509804f, 1.0f ); // #737373
static const idVec4 kConsoleBackgroundColor( 0.0f, 0.0f, 0.0f, 1.0f );

// the console will query the cvar and command systems for
// command completion information

class idConsoleLocal : public idConsole {
public:
	virtual	void		Init( void );
	virtual void		Shutdown( void );
	virtual	void		LoadGraphics( void );
	virtual	bool		ProcessEvent( const sysEvent_t *event, bool forceAccept );
	virtual	bool		Active( void );
	virtual	void		ClearNotifyLines( void );
	virtual	void		Close( void );
	virtual	void		Print( const char *text );
	virtual	void		Draw( bool forceFullScreen );

	void				Dump( const char *toFile );
	void				Clear();
	void				UpdateLayoutMetrics( bool force = false );

	int					GetLineWidth( void ) const { return lineWidth; }
	float				GetSmallCharWidth( void ) const { return smallCharWidth; }
	float				GetBigCharWidth( void ) const { return bigCharWidth; }

	//============================

	const idMaterial *	charSetShader;

private:
	void				KeyDownEvent( int key );

	void				Linefeed();

	void				PageUp();
	void				PageDown();
	void				Top();
	void				Bottom();

	void				DrawInput();
	void				DrawNotify();
	void				DrawSolidConsole( float frac );

	void				Scroll();
	void				SetDisplayFraction( float frac );
	void				UpdateDisplayFraction( void );
	short *				LinePtr( int line );
	const short *		LinePtr( int line ) const;
	void				PrintToBuffer( const char *txt, bool markNotifyTime );
	void				RebuildBufferFromHistory( void );
	void				LoadCommandHistory( void );
	void				SaveCommandHistory( void );

	//============================

	bool				keyCatching;

	short				text[CON_TEXTSIZE];
	int					current;		// line where next message will be printed
	int					x;				// offset in current line for next print
	int					display;		// bottom of console displays this line
	int					lastKeyEvent;	// time of last key event for scroll delay
	int					nextKeyEvent;	// keyboard repeat rate

	float				displayFrac;	// approaches finalFrac at scr_conspeed
	float				finalFrac;		// 0.0 to 1.0 lines of console to display
	int					fracTime;		// time of last displayFrac update

	int					vislines;		// in scanlines

	int					times[NUM_CON_TIMES];	// cls.realtime time the line was generated
									// for transparent notify lines
	idVec4				color;

	idEditField			historyEditLines[COMMAND_HISTORY];

	int					nextHistoryLine;// the last line in the history buffer, not masked
	int					historyLine;	// the line being displayed from history buffer
									// will be <= nextHistoryLine
	bool				commandHistoryLoaded;

	idEditField			consoleField;

	int					lineWidth;
	float				smallCharWidth;
	float				bigCharWidth;
	idStr				printHistory;

	static idCVar		con_speed;
	static idCVar		con_notifyTime;
	static idCVar		con_noPrint;

	const idMaterial *	whiteShader;
	const idMaterial *	consoleShader;
};

static idConsoleLocal localConsole;
idConsole	*console = &localConsole;

idCVar idConsoleLocal::con_speed( "con_speed", "3", CVAR_SYSTEM, "speed at which the console moves up and down" );
idCVar idConsoleLocal::con_notifyTime( "con_notifyTime", "3", CVAR_SYSTEM, "time messages are displayed onscreen when console is pulled up" );
#ifdef DEBUG
idCVar idConsoleLocal::con_noPrint( "con_noPrint", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "print on the console but not onscreen when console is pulled up" );
#else
idCVar idConsoleLocal::con_noPrint( "con_noPrint", "1", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "print on the console but not onscreen when console is pulled up" );
#endif

static float Con_GetConsoleXScale( void ) {
	const float baseAspect = 4.0f / 3.0f;
	float xScale = 1.0f;
	const int screenWidth = renderSystem->GetScreenWidth();
	const int screenHeight = renderSystem->GetScreenHeight();

	if ( screenWidth > 0 && screenHeight > 0 ) {
		const float aspect = static_cast<float>( screenWidth ) / static_cast<float>( screenHeight );
		if ( aspect > 0.0f ) {
			// Keep glyph proportions stable for any aspect ratio, including narrow/tall displays.
			xScale = baseAspect / aspect;
		}
	}

	return idMath::ClampFloat( 0.25f, 8.0f, xScale );
}

short *idConsoleLocal::LinePtr( int line ) {
	return text + ( line % CON_MAX_TOTAL_LINES ) * CON_LINE_STRIDE;
}

const short *idConsoleLocal::LinePtr( int line ) const {
	return text + ( line % CON_MAX_TOTAL_LINES ) * CON_LINE_STRIDE;
}

void idConsoleLocal::UpdateLayoutMetrics( bool force ) {
	const int oldLineWidth = lineWidth;
	const float xScale = Con_GetConsoleXScale();
	const float maxSmallCharWidth = SCREEN_WIDTH / static_cast<float>( CON_MIN_LINE_WIDTH + 2 );
	const float maxBigCharWidth = maxSmallCharWidth * ( static_cast<float>( BIGCHAR_WIDTH ) / static_cast<float>( SMALLCHAR_WIDTH ) );
	const float newSmallCharWidth = idMath::ClampFloat( 1.0f, maxSmallCharWidth, SMALLCHAR_WIDTH * xScale );
	const float newBigCharWidth = idMath::ClampFloat( 2.0f, maxBigCharWidth, BIGCHAR_WIDTH * xScale );
	const int computedWidth = idMath::FtoiFast( SCREEN_WIDTH / newSmallCharWidth ) - 2;
	const int newLineWidth = idMath::ClampInt( CON_MIN_LINE_WIDTH, CON_LINE_STRIDE - 1, computedWidth );

	if ( !force &&
		lineWidth == newLineWidth &&
		idMath::Fabs( smallCharWidth - newSmallCharWidth ) < 0.001f &&
		idMath::Fabs( bigCharWidth - newBigCharWidth ) < 0.001f ) {
		return;
	}

	lineWidth = newLineWidth;
	smallCharWidth = newSmallCharWidth;
	bigCharWidth = newBigCharWidth;

	consoleField.SetWidthInChars( lineWidth );
	for ( int i = 0; i < COMMAND_HISTORY; ++i ) {
		historyEditLines[i].SetWidthInChars( lineWidth );
	}

	if ( x >= lineWidth ) {
		x = lineWidth - 1;
	}

	if ( current - display >= CON_MAX_TOTAL_LINES ) {
		display = current - CON_MAX_TOTAL_LINES + 1;
	}

	if ( oldLineWidth > 0 && oldLineWidth != lineWidth && printHistory.Length() > 0 ) {
		RebuildBufferFromHistory();
	}
}

void idConsoleLocal::PrintToBuffer( const char *txt, bool markNotifyTime ) {
	int c, l;
	int color = idStr::ColorIndex( C_COLOR_CONSOLE );
	const int width = lineWidth;

	while ( ( c = *(const unsigned char *)txt ) != 0 ) {
		short *line = LinePtr( current );

		if ( idStr::IsColor( txt ) ) {
			if ( *( txt + 1 ) == C_COLOR_DEFAULT ) {
				color = idStr::ColorIndex( C_COLOR_CONSOLE );
			} else {
				color = idStr::ColorIndex( *( txt + 1 ) );
			}
			txt += 2;
			continue;
		}

		// if we are about to print a new word, check to see if we should wrap
		// to the new line
		if ( c > ' ' && ( x == 0 || ( line[x - 1] & 0xff ) <= ' ' ) ) {
			// count word length
			for ( l = 0; l < width; l++ ) {
				if ( txt[l] <= ' ' ) {
					break;
				}
			}

			// word wrap
			if ( l != width && ( x + l >= width ) ) {
				Linefeed();
				line = LinePtr( current );
			}
		}

		txt++;

		switch ( c ) {
			case '\n':
				Linefeed();
				break;
			case '\t':
				do {
					line[x] = ( color << 8 ) | ' ';
					x++;
					if ( x >= width ) {
						Linefeed();
						line = LinePtr( current );
					}
				} while ( x & 3 );
				break;
			case '\r':
				x = 0;
				break;
			default:	// display character and advance
				line[x] = ( color << 8 ) | c;
				x++;
				if ( x >= width ) {
					Linefeed();
				}
				break;
		}
	}

	if ( markNotifyTime && current >= 0 ) {
		times[current % NUM_CON_TIMES] = com_frameTime;
	}
}

void idConsoleLocal::RebuildBufferFromHistory( void ) {
	const int scrollback = idMath::ClampInt( 0, CON_MAX_TOTAL_LINES - 1, current - display );
	const short blankCell = ( idStr::ColorIndex( C_COLOR_CONSOLE ) << 8 ) | ' ';

	for ( int i = 0; i < CON_TEXTSIZE; ++i ) {
		text[i] = blankCell;
	}

	current = 0;
	x = 0;
	display = 0;

	if ( printHistory.Length() > 0 ) {
		PrintToBuffer( printHistory.c_str(), false );
	}

	if ( scrollback > 0 ) {
		display = current - scrollback;
		if ( display < 0 ) {
			display = 0;
		}
		if ( current - display >= CON_MAX_TOTAL_LINES ) {
			display = current - CON_MAX_TOTAL_LINES + 1;
		}
	} else {
		display = current;
	}

	ClearNotifyLines();
	if ( current >= 0 ) {
		times[current % NUM_CON_TIMES] = com_frameTime;
	}
}

static void Con_DrawSizedChar( float x, float y, float charWidth, float charHeight, int ch, const idMaterial *material ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;
	if ( ch == ' ' ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;
	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;

	renderSystem->DrawStretchPic( x, y, charWidth, charHeight, fcol, frow, fcol + size, frow + size, material );
}

static void Con_DrawSmallChar( float x, float y, int ch ) {
	Con_DrawSizedChar( x, y, localConsole.GetSmallCharWidth(), SMALLCHAR_HEIGHT, ch, localConsole.charSetShader );
}

static void Con_DrawBigChar( float x, float y, int ch ) {
	Con_DrawSizedChar( x, y, localConsole.GetBigCharWidth(), BIGCHAR_HEIGHT, ch, localConsole.charSetShader );
}

static void Con_DrawSmallStringExt( float x, float y, const char *string, const idVec4 &setColor, bool forceColor ) {
	idVec4 color;
	const unsigned char *s;
	float xx;

	s = reinterpret_cast<const unsigned char *>( string );
	xx = x;
	renderSystem->SetColor( setColor );

	while ( *s ) {
		if ( idStr::IsColor( reinterpret_cast<const char *>( s ) ) ) {
			if ( !forceColor ) {
				if ( *( s + 1 ) == C_COLOR_DEFAULT ) {
					renderSystem->SetColor( setColor );
				} else {
					color = idStr::ColorForIndex( *( s + 1 ) );
					color[3] = setColor[3];
					renderSystem->SetColor( color );
				}
			}
			s += 2;
			continue;
		}

		Con_DrawSmallChar( xx, y, *s );
		xx += localConsole.GetSmallCharWidth();
		s++;
	}

	renderSystem->SetColor( colorWhite );
}

static void Con_DrawBigStringExt( float x, float y, const char *string, const idVec4 &setColor, bool forceColor ) {
	idVec4 color;
	const unsigned char *s;
	float xx;

	s = reinterpret_cast<const unsigned char *>( string );
	xx = x;
	renderSystem->SetColor( setColor );

	while ( *s ) {
		if ( idStr::IsColor( reinterpret_cast<const char *>( s ) ) ) {
			if ( !forceColor ) {
				if ( *( s + 1 ) == C_COLOR_DEFAULT ) {
					renderSystem->SetColor( setColor );
				} else {
					color = idStr::ColorForIndex( *( s + 1 ) );
					color[3] = setColor[3];
					renderSystem->SetColor( color );
				}
			}
			s += 2;
			continue;
		}

		Con_DrawBigChar( xx, y, *s );
		xx += localConsole.GetBigCharWidth();
		s++;
	}

	renderSystem->SetColor( colorWhite );
}



/*
=============================================================================

	Misc stats

=============================================================================
*/

/*
==================
SCR_DrawTextLeftAlign
==================
*/
void SCR_DrawTextLeftAlign( float &y, const char *text, ... ) {
	char string[MAX_STRING_CHARS];
	va_list argptr;
	va_start( argptr, text );
	idStr::vsnPrintf( string, sizeof( string ), text, argptr );
	va_end( argptr );
	Con_DrawSmallStringExt( 0.0f, y + 2.0f, string, colorWhite, true );
	y += SMALLCHAR_HEIGHT + 4;
}

/*
==================
SCR_DrawTextRightAlign
==================
*/
void SCR_DrawTextRightAlign( float &y, const char *text, ... ) {
	char string[MAX_STRING_CHARS];
	va_list argptr;
	va_start( argptr, text );
	int i = idStr::vsnPrintf( string, sizeof( string ), text, argptr );
	va_end( argptr );
	const float rightEdge = SCREEN_WIDTH - localConsole.GetSmallCharWidth() * 0.5f;
	Con_DrawSmallStringExt( rightEdge - i * localConsole.GetSmallCharWidth(), y + 2.0f, string, colorWhite, true );
	y += SMALLCHAR_HEIGHT + 4;
}




/*
==================
SCR_DrawFPS
==================
*/
#define	FPS_FRAMES	4
float SCR_DrawFPS( float y ) {
	char		*s;
	float		w;
	static int	previousTimes[FPS_FRAMES];
	static int	index;
	int		i, total;
	int		fps;
	static	int	previous;
	int		t, frameTime;

	// don't use serverTime, because that will be drifting to
	// correct for internet lag changes, timescales, timedemos, etc
	t = Sys_Milliseconds();
	frameTime = t - previous;
	previous = t;

	previousTimes[index % FPS_FRAMES] = frameTime;
	index++;
	if ( index > FPS_FRAMES ) {
		// average multiple frames together to smooth changes out a bit
		total = 0;
		for ( i = 0 ; i < FPS_FRAMES ; i++ ) {
			total += previousTimes[i];
		}
		if ( !total ) {
			total = 1;
		}
		fps = 10000 * FPS_FRAMES / total;
		fps = (fps + 5)/10;

		s = va( "%ifps", fps );
		w = strlen( s ) * localConsole.GetBigCharWidth();

		Con_DrawBigStringExt( SCREEN_WIDTH - localConsole.GetBigCharWidth() * 0.5f - w, y + 2.0f, s, colorWhite, true );
	}

	return y + BIGCHAR_HEIGHT + 4;
}

/*
==================
SCR_DrawMemoryUsage
==================
*/
float SCR_DrawMemoryUsage( float y ) {
	memoryStats_t allocs, frees;
	
	Mem_GetStats( allocs );
	SCR_DrawTextRightAlign( y, "total allocated memory: %4d, %4dkB", allocs.num, allocs.totalSize>>10 );

	Mem_GetFrameStats( allocs, frees );
	SCR_DrawTextRightAlign( y, "frame alloc: %4d, %4dkB  frame free: %4d, %4dkB", allocs.num, allocs.totalSize>>10, frees.num, frees.totalSize>>10 );

	Mem_ClearFrameStats();

	return y;
}

/*
==================
SCR_DrawAsyncStats
==================
*/
float SCR_DrawAsyncStats( float y ) {
	int i, outgoingRate, incomingRate;
	float outgoingCompression, incomingCompression;

	if ( idAsyncNetwork::server.IsActive() ) {

		SCR_DrawTextRightAlign( y, "server delay = %d msec", idAsyncNetwork::server.GetDelay() );
		SCR_DrawTextRightAlign( y, "total outgoing rate = %d KB/s", idAsyncNetwork::server.GetOutgoingRate() >> 10 );
		SCR_DrawTextRightAlign( y, "total incoming rate = %d KB/s", idAsyncNetwork::server.GetIncomingRate() >> 10 );

		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {

			outgoingRate = idAsyncNetwork::server.GetClientOutgoingRate( i );
			incomingRate = idAsyncNetwork::server.GetClientIncomingRate( i );
			outgoingCompression = idAsyncNetwork::server.GetClientOutgoingCompression( i );
			incomingCompression = idAsyncNetwork::server.GetClientIncomingCompression( i );

			if ( outgoingRate != -1 && incomingRate != -1 ) {
				SCR_DrawTextRightAlign( y, "client %d: out rate = %d B/s (% -2.1f%%), in rate = %d B/s (% -2.1f%%)",
											i, outgoingRate, outgoingCompression, incomingRate, incomingCompression );
			}
		}

		idStr msg;
		idAsyncNetwork::server.GetAsyncStatsAvgMsg( msg );
		SCR_DrawTextRightAlign( y, msg.c_str() );

	} else if ( idAsyncNetwork::client.IsActive() ) {

		outgoingRate = idAsyncNetwork::client.GetOutgoingRate();
		incomingRate = idAsyncNetwork::client.GetIncomingRate();
		outgoingCompression = idAsyncNetwork::client.GetOutgoingCompression();
		incomingCompression = idAsyncNetwork::client.GetIncomingCompression();

		if ( outgoingRate != -1 && incomingRate != -1 ) {
			SCR_DrawTextRightAlign( y, "out rate = %d B/s (% -2.1f%%), in rate = %d B/s (% -2.1f%%)",
										outgoingRate, outgoingCompression, incomingRate, incomingCompression );
		}

		SCR_DrawTextRightAlign( y, "packet loss = %d%%, client prediction = %d",
									(int)idAsyncNetwork::client.GetIncomingPacketLoss(), idAsyncNetwork::client.GetPrediction() );

		SCR_DrawTextRightAlign( y, "predicted frames: %d", idAsyncNetwork::client.GetPredictedFrames() );

	}

	return y;
}

/*
==================
SCR_DrawSoundDecoders
==================
*/
float SCR_DrawSoundDecoders( float y ) {
	return 0;
}

//=========================================================================

/*
==============
Con_Clear_f
==============
*/
static void Con_Clear_f( const idCmdArgs &args ) {
	localConsole.Clear();
}

/*
==============
Con_Dump_f
==============
*/
static void Con_Dump_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: conDump <filename>\n" );
		return;
	}

	idStr fileName = args.Argv(1);
	fileName.DefaultFileExtension(".txt");

	common->Printf( "Dumped console text to %s.\n", fileName.c_str() );

	localConsole.Dump( fileName.c_str() );
}

/*
==============
idConsoleLocal::Init
==============
*/
void idConsoleLocal::Init( void ) {
	int		i;

	keyCatching = false;

	lastKeyEvent = -1;
	nextKeyEvent = CONSOLE_FIRSTREPEAT;
	lineWidth = CON_DEFAULT_LINE_WIDTH;
	smallCharWidth = SMALLCHAR_WIDTH;
	bigCharWidth = BIGCHAR_WIDTH;
	printHistory.Clear();

	consoleField.Clear();
	nextHistoryLine = 0;
	historyLine = 0;
	commandHistoryLoaded = false;

	for ( i = 0 ; i < COMMAND_HISTORY ; i++ ) {
		historyEditLines[i].Clear();
	}
	UpdateLayoutMetrics( true );
	LoadCommandHistory();

	cmdSystem->AddCommand( "clear", Con_Clear_f, CMD_FL_SYSTEM, "clears the console" );
	cmdSystem->AddCommand( "conDump", Con_Dump_f, CMD_FL_SYSTEM, "dumps the console text to a file" );
}

/*
==============
idConsoleLocal::Shutdown
==============
*/
void idConsoleLocal::Shutdown( void ) {
	SaveCommandHistory();
	cmdSystem->RemoveCommand( "clear" );
	cmdSystem->RemoveCommand( "conDump" );
}

/*
==============
LoadGraphics

Can't be combined with init, because init happens before
the renderSystem is initialized
==============
*/
void idConsoleLocal::LoadGraphics() {
// jmarshall
	charSetShader = declManager->FindMaterial( "fonts/english/bigchars" );
// jmarshall end
	whiteShader = declManager->FindMaterial( "_white" );
	consoleShader = declManager->FindMaterial( "console" );
	UpdateLayoutMetrics( true );
}

/*
================
idConsoleLocal::Active
================
*/
bool	idConsoleLocal::Active( void ) {
	return keyCatching;
}

/*
================
idConsoleLocal::ClearNotifyLines
================
*/
void	idConsoleLocal::ClearNotifyLines() {
	int		i;

	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		times[i] = 0;
	}
}

/*
================
idConsoleLocal::Close
================
*/
void	idConsoleLocal::Close() {
	keyCatching = false;
	SetDisplayFraction( 0 );
	displayFrac = 0;	// don't scroll to that point, go immediately
	ClearNotifyLines();
}

/*
================
idConsoleLocal::Clear
================
*/
void idConsoleLocal::Clear() {
	int		i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		text[i] = (idStr::ColorIndex(C_COLOR_CYAN)<<8) | ' ';
	}
	printHistory.Clear();

	Bottom();		// go to end
}

/*
================
idConsoleLocal::Dump

Save the console contents out to a file
================
*/
void idConsoleLocal::Dump( const char *fileName ) {
	int		l, x, i;
	short *	line;
	idFile *f;
	idList<char> buffer;
	const int width = lineWidth;
	buffer.SetNum( width + 3 );

	f = fileSystem->OpenFileWrite( fileName );
	if ( !f ) {
		common->Warning( "couldn't open %s", fileName );
		return;
	}

	// skip empty lines
	l = current - CON_MAX_TOTAL_LINES + 1;
	if ( l < 0 ) {
		l = 0;
	}
	for ( ; l <= current ; l++ )
	{
		line = LinePtr( l );
		for ( x = 0; x < width; x++ )
			if ( ( line[x] & 0xff ) > ' ' )
				break;
		if ( x != width )
			break;
	}

	// write the remaining lines
	for ( ; l <= current; l++ ) {
		line = LinePtr( l );
		for( i = 0; i < width; i++ ) {
			buffer[i] = static_cast<char>( line[i] & 0xff );
		}
		for ( x = width - 1; x >= 0; x-- ) {
			if ( buffer[x] <= ' ' ) {
				buffer[x] = 0;
			} else {
				break;
			}
		}
		buffer[x+1] = '\r';
		buffer[x+2] = '\n';
		buffer[x+3] = 0;
		f->Write( buffer.Ptr(), strlen( buffer.Ptr() ) );
	}

	fileSystem->CloseFile( f );
}

/*
================
idConsoleLocal::LoadCommandHistory
================
*/
void idConsoleLocal::LoadCommandHistory( void ) {
	if ( commandHistoryLoaded ) {
		return;
	}
	if ( fileSystem == NULL || !fileSystem->IsInitialized() ) {
		return;
	}
	commandHistoryLoaded = true;

	const char *fileBuffer = NULL;
	const int fileLength = fileSystem->ReadFile( kConsoleHistoryFileName, ( void ** )&fileBuffer );
	if ( fileLength < 0 || fileBuffer == NULL ) {
		return;
	}
	if ( fileLength == 0 ) {
		fileSystem->FreeFile( ( void * )fileBuffer );
		return;
	}

	int lineStart = 0;
	for ( int i = 0; i <= fileLength; ++i ) {
		const bool atEnd = ( i == fileLength );
		if ( atEnd && lineStart >= fileLength ) {
			break;
		}
		if ( !atEnd && fileBuffer[i] != '\n' ) {
			continue;
		}

		int lineLength = i - lineStart;
		if ( lineLength > 0 && fileBuffer[lineStart + lineLength - 1] == '\r' ) {
			lineLength--;
		}

		idStr line;
		if ( lineLength > 0 ) {
			line.Append( fileBuffer + lineStart, lineLength );
		}

		historyEditLines[nextHistoryLine % COMMAND_HISTORY].SetBuffer( line.c_str() );
		nextHistoryLine++;
		lineStart = i + 1;
	}

	historyLine = nextHistoryLine;
	fileSystem->FreeFile( ( void * )fileBuffer );
}

/*
================
idConsoleLocal::SaveCommandHistory
================
*/
void idConsoleLocal::SaveCommandHistory( void ) {
	if ( fileSystem == NULL || !fileSystem->IsInitialized() ) {
		return;
	}

	idFile *historyFile = fileSystem->OpenFileWrite( kConsoleHistoryFileName, "fs_savepath" );
	if ( historyFile == NULL ) {
		return;
	}

	const int historyCount = Min( nextHistoryLine, COMMAND_HISTORY );
	const int startLine = nextHistoryLine - historyCount;

	for ( int i = 0; i < historyCount; ++i ) {
		const char *line = historyEditLines[( startLine + i ) % COMMAND_HISTORY].GetBuffer();
		if ( line[0] != '\0' ) {
			historyFile->Write( line, strlen( line ) );
		}
		historyFile->Write( "\n", 1 );
	}

	fileSystem->CloseFile( historyFile );
}

/*
================
idConsoleLocal::PageUp
================
*/
void idConsoleLocal::PageUp( void ) {
	display -= 2;
	if ( current - display >= CON_MAX_TOTAL_LINES ) {
		display = current - CON_MAX_TOTAL_LINES + 1;
	}
}

/*
================
idConsoleLocal::PageDown
================
*/
void idConsoleLocal::PageDown( void ) {
	display += 2;
	if ( display > current ) {
		display = current;
	}
}

/*
================
idConsoleLocal::Top
================
*/
void idConsoleLocal::Top( void ) {
	display = 0;
}

/*
================
idConsoleLocal::Bottom
================
*/
void idConsoleLocal::Bottom( void ) {
	display = current;
}


/*
=============================================================================

CONSOLE LINE EDITING

==============================================================================
*/

/*
====================
KeyDownEvent

Handles history and console scrollback
====================
*/
void idConsoleLocal::KeyDownEvent( int key ) {
	if ( !commandHistoryLoaded ) {
		LoadCommandHistory();
	}
	
	// Execute F key bindings
	if ( key >= K_F1 && key <= K_F12 ) {
		idKeyInput::ExecKeyBinding( key );
		return;
	}

	// ctrl-L clears screen
	if ( key == 'l' && idKeyInput::IsDown( K_CTRL ) ) {
		Clear();
		return;
	}

	// enter finishes the line
	if ( key == K_ENTER || key == K_KP_ENTER ) {

		common->Printf ( "]%s\n", consoleField.GetBuffer() );

		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, consoleField.GetBuffer() );	// valid command
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );

		// copy line to history buffer
		historyEditLines[nextHistoryLine % COMMAND_HISTORY] = consoleField;
		nextHistoryLine++;
		historyLine = nextHistoryLine;
		SaveCommandHistory();

		consoleField.Clear();
		consoleField.SetWidthInChars( lineWidth );

		session->UpdateScreen();// force an update, because the command
								// may take some time
		return;
	}

	// command completion

	if ( key == K_TAB ) {
		consoleField.AutoComplete();
		return;
	}

	// command history (ctrl-p ctrl-n for unix style)

	if ( ( key == K_UPARROW ) ||
		 ( ( tolower(key) == 'p' ) && idKeyInput::IsDown( K_CTRL ) ) ) {
		if ( nextHistoryLine - historyLine < COMMAND_HISTORY && historyLine > 0 ) {
			historyLine--;
		}
		consoleField = historyEditLines[ historyLine % COMMAND_HISTORY ];
		return;
	}

	if ( ( key == K_DOWNARROW ) ||
		 ( ( tolower( key ) == 'n' ) && idKeyInput::IsDown( K_CTRL ) ) ) {
		if ( historyLine == nextHistoryLine ) {
			return;
		}
		historyLine++;
		consoleField = historyEditLines[ historyLine % COMMAND_HISTORY ];
		return;
	}

	// console scrolling
	if ( key == K_PGUP ) {
		PageUp();
		lastKeyEvent = eventLoop->Milliseconds();
		nextKeyEvent = CONSOLE_FIRSTREPEAT;
		return;
	}

	if ( key == K_PGDN ) {
		PageDown();
		lastKeyEvent = eventLoop->Milliseconds();
		nextKeyEvent = CONSOLE_FIRSTREPEAT;
		return;
	}

	if ( key == K_MWHEELUP ) {
		PageUp();
		return;
	}

	if ( key == K_MWHEELDOWN ) {
		PageDown();
		return;
	}

	// ctrl-home = top of console
	if ( key == K_HOME && idKeyInput::IsDown( K_CTRL ) ) {
		Top();
		return;
	}

	// ctrl-end = bottom of console
	if ( key == K_END && idKeyInput::IsDown( K_CTRL ) ) {
		Bottom();
		return;
	}

	// pass to the normal editline routine
	consoleField.KeyDownEvent( key );
}

/*
==============
Scroll
deals with scrolling text because we don't have key repeat
==============
*/
void idConsoleLocal::Scroll( ) {
	if (lastKeyEvent == -1 || (lastKeyEvent+200) > eventLoop->Milliseconds()) {
		return;
	}
	// console scrolling
	if ( idKeyInput::IsDown( K_PGUP ) ) {
		PageUp();
		nextKeyEvent = CONSOLE_REPEAT;
		return;
	}

	if ( idKeyInput::IsDown( K_PGDN ) ) {
		PageDown();
		nextKeyEvent = CONSOLE_REPEAT;
		return;
	}
}

/*
==============
SetDisplayFraction

Causes the console to start opening the desired amount.
==============
*/
void idConsoleLocal::SetDisplayFraction( float frac ) {
	finalFrac = frac;
	fracTime = com_frameTime;
}

/*
==============
UpdateDisplayFraction

Scrolls the console up or down based on conspeed
==============
*/
void idConsoleLocal::UpdateDisplayFraction( void ) {
	if ( con_speed.GetFloat() <= 0.1f ) {
		fracTime = com_frameTime;
		displayFrac = finalFrac;
		return;
	}

	// scroll towards the destination height
	if ( finalFrac < displayFrac ) {
		displayFrac -= con_speed.GetFloat() * ( com_frameTime - fracTime ) * 0.001f;
		if ( finalFrac > displayFrac ) {
			displayFrac = finalFrac;
		}
		fracTime = com_frameTime;
	} else if ( finalFrac > displayFrac ) {
		displayFrac += con_speed.GetFloat() * ( com_frameTime - fracTime ) * 0.001f;
		if ( finalFrac < displayFrac ) {
			displayFrac = finalFrac;
		}
		fracTime = com_frameTime;
	}
}

/*
==============
ProcessEvent
==============
*/
bool	idConsoleLocal::ProcessEvent( const sysEvent_t *event, bool forceAccept ) {
	bool consoleKey;
	consoleKey = event->evType == SE_KEY && ( event->evValue == Sys_GetConsoleKey( false ) || event->evValue == Sys_GetConsoleKey( true ) );

#if ID_CONSOLE_LOCK
	// If the console's not already down, and we have it turned off, check for ctrl+alt
	if ( !keyCatching && !com_allowConsole.GetBool() ) {
		if ( !idKeyInput::IsDown( K_CTRL ) || !idKeyInput::IsDown( K_ALT ) ) {
			consoleKey = false;
		}
	}
#endif

	// we always catch the console key event
	if ( !forceAccept && consoleKey ) {
		// ignore up events
		if ( event->evValue2 == 0 ) {
			return true;
		}

		consoleField.ClearAutoComplete();

		// a down event will toggle the destination lines
		if ( keyCatching ) {
			Close();
			Sys_GrabMouseCursor( true );
			cvarSystem->SetCVarBool( "ui_chat", false );
		} else {
			consoleField.Clear();
			keyCatching = true;
			if ( idKeyInput::IsDown( K_SHIFT ) ) {
				// if the shift key is down, don't open the console as much
				SetDisplayFraction( 0.2f );
			} else {
				SetDisplayFraction( 0.5f );
			}
			cvarSystem->SetCVarBool( "ui_chat", true );
		}
		return true;
	}

	// if we aren't key catching, dump all the other events
	if ( !forceAccept && !keyCatching ) {
		return false;
	}

	// handle key and character events
	if ( event->evType == SE_CHAR ) {
		// never send the console key as a character
		if ( event->evValue != Sys_GetConsoleKey( false ) && event->evValue != Sys_GetConsoleKey( true ) ) {
			consoleField.CharEvent( event->evValue );
		}
		return true;
	}

	if ( event->evType == SE_KEY ) {
		// ignore up key events
		if ( event->evValue2 == 0 ) {
			return true;
		}

		KeyDownEvent( event->evValue );
		return true;
	}

	// we don't handle things like mouse, joystick, and network packets
	return false;
}

/*
==============================================================================

PRINTING

==============================================================================
*/

/*
===============
Linefeed
===============
*/
void idConsoleLocal::Linefeed() {
	int		i;
	short *	line;

	// mark time for transparent overlay
	if ( current >= 0 ) {
		times[current % NUM_CON_TIMES] = com_frameTime;
	}

	x = 0;
	if ( display == current ) {
		display++;
	}
	current++;
	if ( current - display >= CON_MAX_TOTAL_LINES ) {
		display = current - CON_MAX_TOTAL_LINES + 1;
	}
	line = LinePtr( current );
	for ( i = 0; i < CON_LINE_STRIDE; i++ ) {
		line[i] = (idStr::ColorIndex(C_COLOR_CONSOLE)<<8) | ' ';
	}
}


/*
================
Print

Handles cursor positioning, line wrapping, etc
================
*/
void idConsoleLocal::Print( const char *txt ) {
#ifdef ID_ALLOW_TOOLS
	RadiantPrint( txt );

	if( com_editors & EDITOR_MATERIAL ) {
		MaterialEditorPrintConsole(txt);
	}
#endif

	if ( txt == NULL || txt[0] == '\0' ) {
		return;
	}

	UpdateLayoutMetrics();
	printHistory.Append( txt );
	if ( printHistory.Length() > CON_HISTORY_MAX_CHARS ) {
		printHistory = printHistory.Right( CON_HISTORY_MAX_CHARS );
	}

	PrintToBuffer( txt, true );
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
DrawInput

Draw the editline after a ] prompt
================
*/
void idConsoleLocal::DrawInput() {
	int autoCompleteLength;
	const float y = static_cast<float>( vislines - ( SMALLCHAR_HEIGHT * 2 ) );
	const float charWidth = smallCharWidth;

	UpdateLayoutMetrics();

	if ( consoleField.GetAutoCompleteLength() != 0 ) {
		autoCompleteLength = strlen( consoleField.GetBuffer() ) - consoleField.GetAutoCompleteLength();

		if ( autoCompleteLength > 0 ) {
			renderSystem->SetColor4( .8f, .2f, .2f, .45f );

			renderSystem->DrawStretchPic( 2.0f * charWidth + consoleField.GetAutoCompleteLength() * charWidth,
							y + 2.0f, autoCompleteLength * charWidth, SMALLCHAR_HEIGHT - 2.0f, 0, 0, 0, 0, whiteShader );

		}
	}

	renderSystem->SetColor( kConsoleBorderColor );

	Con_DrawSmallChar( 1.0f * charWidth, y, ']' );

	consoleField.Draw(
		idMath::FtoiFast( 2.0f * charWidth ),
		idMath::FtoiFast( y ),
		idMath::FtoiFast( SCREEN_WIDTH - 3.0f * charWidth ),
		true,
		charSetShader );
}


/*
================
DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void idConsoleLocal::DrawNotify() {
	int		x, v;
	short	*text_p;
	int		i;
	int		time;
	int		currentColor;
	const int width = lineWidth;
	const float charWidth = smallCharWidth;

	if ( con_noPrint.GetBool() ) {
		return;
	}

	currentColor = idStr::ColorIndex( C_COLOR_CONSOLE );
	renderSystem->SetColor( kConsoleBorderColor );

	v = 0;
	for ( i = current-NUM_CON_TIMES+1; i <= current; i++ ) {
		if ( i < 0 ) {
			continue;
		}
		time = times[i % NUM_CON_TIMES];
		if ( time == 0 ) {
			continue;
		}
		time = com_frameTime - time;
		if ( time > con_notifyTime.GetFloat() * 1000 ) {
			continue;
		}
		text_p = LinePtr( i );
		
		for ( x = 0; x < width; x++ ) {
			if ( ( text_p[x] & 0xff ) == ' ' ) {
				continue;
			}
			if ( idStr::ColorIndex(text_p[x]>>8) != currentColor ) {
				currentColor = idStr::ColorIndex(text_p[x]>>8);
				if ( currentColor == idStr::ColorIndex( C_COLOR_CONSOLE ) ) {
					renderSystem->SetColor( kConsoleBorderColor );
				} else {
					renderSystem->SetColor( idStr::ColorForIndex( currentColor ) );
				}
			}
			Con_DrawSmallChar( ( x + 1 ) * charWidth, static_cast<float>( v ), text_p[x] & 0xff );
		}

		v += SMALLCHAR_HEIGHT;
	}

	renderSystem->SetColor( colorWhite );
}

/*
================
DrawSolidConsole

Draws the console with the solid background
================
*/
void idConsoleLocal::DrawSolidConsole( float frac ) {
	int				i, x;
	float			y;
	int				rows;
	short			*text_p;
	int				row;
	int				lines;
	int				currentColor;
	const int width = lineWidth;
	const float charWidth = smallCharWidth;

	lines = idMath::FtoiFast( SCREEN_HEIGHT * frac );
	if ( lines <= 0 ) {
		return;
	}

	if ( lines > SCREEN_HEIGHT ) {
		lines = SCREEN_HEIGHT;
	}

	// draw the background
	y = frac * SCREEN_HEIGHT - 2;
	if ( y < 1.0f ) {
		y = 0.0f;
	} else {
		renderSystem->SetColor( kConsoleBackgroundColor );
		renderSystem->DrawStretchPic( 0, 0, SCREEN_WIDTH, y, 0, 0, 0, 0, whiteShader );
	}

	renderSystem->SetColor( kConsoleBorderColor );
	renderSystem->DrawStretchPic( 0, y, SCREEN_WIDTH, 2, 0, 0, 0, 0, whiteShader );
	renderSystem->SetColor( colorWhite );

	// draw the version number

	renderSystem->SetColor( kConsoleVersionColor );

	idStr version = PROJECT_VERSION;
	i = version.Length();

	for ( x = 0; x < i; x++ ) {
		Con_DrawSmallChar(
			SCREEN_WIDTH - ( i - x ) * charWidth,
			lines - ( SMALLCHAR_HEIGHT + SMALLCHAR_HEIGHT / 2.0f ),
			version[x] );

	}


	// draw the text
	vislines = lines;
	rows = ( lines - SMALLCHAR_HEIGHT ) / SMALLCHAR_HEIGHT;		// rows of text to draw

	y = lines - (SMALLCHAR_HEIGHT*3);

	// draw from the bottom up
	if ( display != current ) {
		// draw arrows to show the buffer is backscrolled
		renderSystem->SetColor( kConsoleBorderColor );
		for ( x = 0; x < width; x += 4 ) {
			Con_DrawSmallChar( ( x + 1 ) * charWidth, y, '^' );
		}
		y -= SMALLCHAR_HEIGHT;
		rows--;
	}
	
	row = display;

	if ( this->x == 0 ) {
		row--;
	}

	currentColor = idStr::ColorIndex( C_COLOR_CONSOLE );
	renderSystem->SetColor( kConsoleBorderColor );

	for ( i = 0; i < rows; i++, y -= SMALLCHAR_HEIGHT, row-- ) {
		if ( row < 0 ) {
			break;
		}
		if ( current - row >= CON_MAX_TOTAL_LINES ) {
			// past scrollback wrap point
			continue;	
		}

		text_p = LinePtr( row );

		for ( x = 0; x < width; x++ ) {
			if ( ( text_p[x] & 0xff ) == ' ' ) {
				continue;
			}

			if ( idStr::ColorIndex(text_p[x]>>8) != currentColor ) {
				currentColor = idStr::ColorIndex(text_p[x]>>8);
				if ( currentColor == idStr::ColorIndex( C_COLOR_CONSOLE ) ) {
					renderSystem->SetColor( kConsoleBorderColor );
				} else {
					renderSystem->SetColor( idStr::ColorForIndex( currentColor ) );
				}
			}
			Con_DrawSmallChar( ( x + 1 ) * charWidth, y, text_p[x] & 0xff );
		}
	}

	// draw the input prompt, user text, and cursor if desired
	DrawInput();

	renderSystem->SetColor( colorWhite );
}


/*
==============
Draw

ForceFullScreen is used by the editor
==============
*/
void	idConsoleLocal::Draw( bool forceFullScreen ) {
	float y = 0.0f;

	if ( !charSetShader ) {
		return;
	}

	UpdateLayoutMetrics();

	if ( forceFullScreen ) {
		// if we are forced full screen because of a disconnect, 
		// we want the console closed when we go back to a session state
		Close();
		// we are however catching keyboard input
		keyCatching = true;
	}

	Scroll();

	UpdateDisplayFraction();

	if ( forceFullScreen ) {
		DrawSolidConsole( 1.0f );
	} else if ( displayFrac ) {
		DrawSolidConsole( displayFrac );
	} else {
		// only draw the notify lines if the developer cvar is set,
		// or we are a debug build
		if ( !con_noPrint.GetBool() ) {
			DrawNotify();
		}
	}

	if ( com_showFPS.GetBool() ) {
		y = SCR_DrawFPS( 0 );
	}

	if ( com_showMemoryUsage.GetBool() ) {
		y = SCR_DrawMemoryUsage( y );
	}

	if ( com_showAsyncStats.GetBool() ) {
		y = SCR_DrawAsyncStats( y );
	}

	if ( com_showSoundDecoders.GetBool() ) {
		y = SCR_DrawSoundDecoders( y );
	}
}
