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




#include "Session_local.h"

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
#define CON_SCROLLBAR_BASE_WIDTH		2.0f
#define CON_SCROLLBAR_HOVER_GROW		3.0f
#define CON_SCROLLBAR_HIT_PAD			5.0f
#define CON_SCROLLBAR_SIDE_PAD			2.0f
#define CON_SCROLLBAR_MIN_THUMB			18.0f
#define CON_SCROLLBAR_LERP_SPEED		12.0f
#define CON_SELECTION_ALPHA				0.35f
#define CON_COMPLETION_MAX_MATCHES		64
#define CON_COMPLETION_MAX_VISIBLE		8
#define CON_TEXT_DRAG_THRESHOLD			4.0f
#define CON_MOUSE_CURSOR_SIZE			32.0f

#define	COMMAND_HISTORY			64
static const char *kConsoleHistoryFileName = "consolehistory.dat";

static const idVec4 kConsoleBorderColor( 0.9411765f, 0.6196079f, 0.0509804f, 1.0f ); // #f09e0d
static const idVec4 kConsoleVersionColor( 0.4509804f, 0.4509804f, 0.4509804f, 1.0f ); // #737373
static const idVec4 kConsoleBackgroundColor( 0.0f, 0.0f, 0.0f, 1.0f );
static const idVec4 kConsolePopupTextColor( 1.0f, 1.0f, 1.0f, 0.95f );
static const idVec4 kConsolePopupValueColor( 0.96862745f, 0.85882353f, 0.6117647f, 0.8f );
static const idVec4 kConsoleSelectionColor( 0.9411765f, 0.6196079f, 0.0509804f, CON_SELECTION_ALPHA );

// the console will query the cvar and command systems for
// command completion information

enum consoleFocus_t {
	CON_FOCUS_INPUT,
	CON_FOCUS_LOG
};

class idConsoleLocal : public idConsole {
public:
	virtual	void		Init( void );
	virtual void		Shutdown( void );
	virtual	void		LoadGraphics( void );
	virtual	bool		ProcessEvent( const sysEvent_t *event, bool forceAccept );
	virtual	bool		Active( void );
	virtual	void		ClearNotifyLines( void );
	virtual	void		Close( void );
	virtual void		SetProcFileOutOfDate( bool state );
	virtual void		SetAASFileOutOfDate( bool state );
	virtual	void		SetMousePosition( float x, float y );
	virtual void		ClampMousePosition( float &x, float &y ) const;
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
	bool				InputKeyDownEvent( int key );
	bool				MouseKeyEvent( int key, bool down );
	void				MouseMoveEvent( int dx, int dy );
	void				KeyDownEvent( int key );

	void				Linefeed();

	void				ClampDisplay( void );
	void				UpdateDisplayLine( void );
	float				GetOpenFraction( bool reduced ) const;
	int					GetFooterRows( void ) const;
	int					GetLogRowCount( void ) const;
	bool				GetScrollRange( int &minDisplay, int &maxDisplay, int *filled = NULL ) const;
	void				GetConsoleRect( float &x, float &y, float &w, float &h ) const;
	void				GetLogAreaRect( float &x, float &y, float &w, float &h ) const;
	bool				GetInputAreaRect( float &x, float &y, float &w, float &h ) const;
	void				GetInputDrawInfo( int &prestep, int &drawLen ) const;
	int					GetInputCursorFromMouse( void ) const;
	bool				GetLogPositionFromMouse( int &line, int &column ) const;
	void				ClampMouseToConsole( void );
	void				UpdateScrollbarHoverValue( float &hoverFrac, bool dragging, bool hot );
	void				GetScrollbarFrameGeometry( float areaX, float areaW, float areaY, float areaH, float hoverFrac,
							float &trackX, float &trackY, float &trackW, float &trackH, float *hitX = NULL, float *hitW = NULL ) const;
	void				GetScrollbarThumbGeometry( float trackY, float trackH, int visibleCount, int filled, float displayFrac,
							float &thumbY, float &thumbH ) const;
	bool				GetScrollbarGeometry( float hoverFrac, float &trackX, float &trackY, float &trackW, float &trackH,
							float &thumbY, float &thumbH, float *hitX = NULL, float *hitW = NULL ) const;
	bool				GetCompletionPopupGeometry( float &popupX, float &popupY, float &popupW, float &popupH,
							int &first, int &visibleCount );
	bool				GetCompletionScrollbarGeometry( float hoverFrac, float &trackX, float &trackY, float &trackW, float &trackH,
							float &thumbY, float &thumbH, float *hitX = NULL, float *hitW = NULL );
	bool				GetCompletionSelectionFromMouse( int &selection );
	void				SetScrollbarDisplayFraction( float frac );
	void				UpdateScrollbarDrag( void );
	void				SetCompletionScrollFraction( float frac );
	void				UpdateCompletionScrollbarDrag( void );
	void				UpdateScrollbarHover( void );
	void				UpdateCompletionScrollbarHover( void );

	void				ClearInputSelection( void );
	bool				HasInputSelection( void ) const;
	void				GetInputSelectionRange( int &start, int &end ) const;
	void				DeleteInputRange( int start, int end );
	void				DeleteInputSelection( void );
	int					SeekWordCursor( int cursor, int direction ) const;
	void				SetInputCursor( int cursor, bool keepSelection );
	void				SelectAllInput( void );
	void				InsertInputChar( int ch );
	int					BuildInputSelectionText( char *buffer, int bufferSize ) const;
	void				CopyInputSelection( void ) const;
	void				CutInputSelection( void );
	void				PasteClipboardToInput( void );
	void				InsertInputTextAt( const char *text, int cursor );

	int					CompareLogPosition( int line1, int column1, int line2, int column2 ) const;
	void				ClampLogPosition( int &line, int &column ) const;
	void				ClearLogSelection( void );
	bool				HasLogSelection( void ) const;
	void				GetLogSelectionRange( int &startLine, int &startColumn, int &endLine, int &endColumn ) const;
	void				SetLogCursor( int line, int column, bool keepSelection );
	int					BuildLogSelectionText( char *buffer, int bufferSize ) const;
	void				SelectAllLog( void );
	void				CopyLogSelection( void ) const;
	void				CopySelection( void ) const;
	void				ScrollToLogCursor( void );
	void				MoveLogCursorByChars( int delta, bool keepSelection );
	void				MoveLogCursorByLines( int delta, bool keepSelection );
	void				MoveLogCursorToBoundary( bool toStart, bool wholeLog, bool keepSelection );
	bool				HandleLogSelectionKey( int key );
	bool				IsInputSelectionHit( int cursor ) const;
	bool				IsLogSelectionHit( int line, int column ) const;

	void				ClearTextDragState( void );
	bool				BeginTextDrag( bool fromInput );
	void				UpdateTextDragTarget( void );
	void				FinishTextDrag( void );

	void				InvalidateCompletionState( void );
	void				RebuildCompletionState( void );
	void				DismissCompletionPopup( void );
	bool				CompletionPopupEnabled( void ) const;
	bool				HasActiveCompletionPopup( void ) const;
	int					GetCompletionVisibleCount( void ) const;
	void				ClampCompletionScroll( bool keepSelectionVisible );
	void				MoveCompletionSelection( int delta );
	void				RefreshCompletionState( void );
	void				ApplySelectedCompletion( int direction );
	bool				IsCurrentSegmentCompletionMatch( const char *match ) const;
	bool				ShouldHideMatchedCompletionPopup( void ) const;
	bool				GetCompletionCvarInfo( const char *match, char *value, int valueSize, bool *modified ) const;
	bool				ExtractCompletionCandidateToken( const char *candidateLine, char *token, int tokenSize ) const;
	void				ResolveCompletionReplacementRange( const char *fullSegment, int fullLen, int relativeCursor, int segmentStart,
							int &replaceOffset, int &replaceLength, int &replaceArgIndex, bool &firstArg ) const;
	int					FindCompletionSegmentBoundary( int cursor, bool findStart ) const;
	void				GetCompletionSegment( int cursor, int &segmentStart, int &segmentEnd ) const;
public:
	bool				CollectCompletionMatch( const char *match );
	void				SortCompletionMatches( void );
private:

	void				PageUp();
	void				PageDown();
	void				Top();
	void				Bottom();
	int					GetScrollStep( int lines = -1 ) const;

	void				DrawSolidRect( float x, float y, float w, float h, const idVec4 &color, float alphaScale = 1.0f ) const;
	void				DrawScrollbarVisual( float trackX, float trackY, float trackW, float trackH,
							float thumbY, float thumbH, float hoverFrac, float alphaScale, const idVec4 &lineColor ) const;
	void				DrawInputSelection( float x, float y, int prestep, int drawLen ) const;
	void				DrawLogSelectionRow( int line, float y ) const;
	void				DrawInputText( float x, float y, bool showCursor ) const;
	void				DrawInputDropCursor( float x, float y ) const;
	void				DrawCompletionPopup( void );
	void				DrawScrollbar( void );
	void				DrawMouseCursor( void );
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
	bool				procFileOutOfDate;
	bool				aasFileOutOfDate;

	short				text[CON_TEXTSIZE];
	int					current;		// line where next message will be printed
	int					x;				// offset in current line for next print
	int					display;		// bottom of console displays this line
	int					lastKeyEvent;	// time of last key event for scroll delay
	int					nextKeyEvent;	// keyboard repeat rate
	float				displayLine;	// smoothed display row for scrollback

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
	int					inputSelectionAnchor;
	int					logSelectionAnchorLine;
	int					logSelectionAnchorColumn;
	int					logSelectionLine;
	int					logSelectionColumn;
	float				mouseX;
	float				mouseY;
	float				scrollbarHover;
	float				scrollbarDragOffset;
	float				completionScrollbarHover;
	float				completionScrollbarDragOffset;
	int					completionCount;
	int					completionSelection;
	int					completionScroll;
	int					completionSegmentStart;
	int					completionSegmentEnd;
	int					completionReplaceArgIndex;
	int					completionReplaceOffset;
	int					completionReplaceLength;
	int					completionSnapshotCursor;
	bool				completionAppendSpace;
	bool				completionPrependSlash;
	bool				completionPopupVisible;
	bool				completionSnapshotValid;
	bool				mouseInitialized;
	bool				scrollbarDragging;
	bool				completionScrollbarDragging;
	bool				inputSelecting;
	bool				logSelecting;
	bool				textDragPending;
	bool				textDragging;
	bool				textDragFromInput;
	bool				textDragTargetInput;
	int					textDragSourceStart;
	int					textDragSourceEnd;
	int					textDragDropCursor;
	int					textDragTextLength;
	float				textDragStartMouseX;
	float				textDragStartMouseY;
	consoleFocus_t		focus;
	char				completionSnapshotBuffer[MAX_EDIT_LINE];
	char				completionMatches[CON_COMPLETION_MAX_MATCHES][MAX_EDIT_LINE];
	char				textDragText[MAX_EDIT_LINE];

	int					lineWidth;
	float				smallCharWidth;
	float				bigCharWidth;
	idStr				printHistory;
	char				completionFuzzyNeedle[MAX_EDIT_LINE];

	static idCVar		con_speed;
	static idCVar		con_height;
	static idCVar		con_notifyTime;
	static idCVar		con_noPrint;
	static idCVar		con_scrollSmooth;
	static idCVar		con_scrollSmoothSpeed;
	static idCVar		con_completionPopup;
	static idCVar		con_scrollLines;

	const idMaterial *	whiteShader;
	const idMaterial *	consoleShader;
	const idMaterial *	mouseCursorShader;
};

static idConsoleLocal localConsole;
idConsole	*console = &localConsole;

idCVar idConsoleLocal::con_speed( "con_speed", "3", CVAR_SYSTEM, "speed at which the console moves up and down" );
idCVar idConsoleLocal::con_height( "con_height", "0.5", CVAR_FLOAT | CVAR_SYSTEM | CVAR_ARCHIVE,
	"maximum open height fraction for the console", 0.1f, 1.0f );
idCVar idConsoleLocal::con_notifyTime( "con_notifyTime", "3", CVAR_SYSTEM, "time messages are displayed onscreen when console is pulled up" );
#ifdef DEBUG
idCVar idConsoleLocal::con_noPrint( "con_noPrint", "0", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "print on the console but not onscreen when console is pulled up" );
#else
idCVar idConsoleLocal::con_noPrint( "con_noPrint", "1", CVAR_BOOL|CVAR_SYSTEM|CVAR_NOCHEAT, "print on the console but not onscreen when console is pulled up" );
#endif
idCVar idConsoleLocal::con_scrollSmooth( "con_scrollSmooth", "1", CVAR_BOOL | CVAR_SYSTEM | CVAR_ARCHIVE, "smoothly animate console scrollback movements" );
idCVar idConsoleLocal::con_scrollSmoothSpeed( "con_scrollSmoothSpeed", "24", CVAR_FLOAT | CVAR_SYSTEM | CVAR_ARCHIVE, "scrollback smoothing speed", 1.0f, 96.0f );
idCVar idConsoleLocal::con_completionPopup( "con_completionPopup", "1", CVAR_BOOL | CVAR_SYSTEM | CVAR_ARCHIVE, "show the live console completion popup while typing" );
idCVar idConsoleLocal::con_scrollLines( "con_scrollLines", "6", CVAR_INTEGER | CVAR_SYSTEM | CVAR_ARCHIVE, "mouse wheel / page scroll step in console lines", 1.0f, 64.0f );

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

struct consoleCompletionCandidate_t {
	char	match[MAX_EDIT_LINE];
	int		category;
	int		primary;
	int		secondary;
	int		tertiary;
};

struct consoleFuzzyCompletionState_t {
	char						needle[MAX_EDIT_LINE];
	int							count;
	consoleCompletionCandidate_t	matches[CON_COMPLETION_MAX_MATCHES];
};

static void Con_LightenColor( const idVec4 &color, float amount, idVec4 &outColor ) {
	const float clampedAmount = idMath::ClampFloat( 0.0f, 1.0f, amount );

	for ( int i = 0; i < 3; ++i ) {
		outColor[i] = color[i] + ( 1.0f - color[i] ) * clampedAmount;
	}
	outColor[3] = color[3];
}

static int Con_CompletionLower( int ch ) {
	return tolower( static_cast<unsigned char>( ch ) );
}

static const char *Con_FindCompletionSegmentStart( const char *cmd ) {
	const char *segmentStart = cmd;
	for ( const char *scan = cmd; *scan != '\0'; ++scan ) {
		if ( *scan == ';' ) {
			segmentStart = scan + 1;
		}
	}
	return segmentStart;
}

static void NormalizeCompletionCommandString( const char *cmd, char *normalized, size_t normalizedSize ) {
	const char *segmentStart = Con_FindCompletionSegmentStart( cmd );

	while ( *segmentStart != '\0' && *segmentStart <= ' ' ) {
		++segmentStart;
	}

	idStr::Copynz( normalized, segmentStart, normalizedSize );
	if ( normalized[0] == '/' || normalized[0] == '\\' ) {
		memmove( normalized, normalized + 1, strlen( normalized ) );
	}
}

static bool Con_IsCompletionWordChar( int ch ) {
	ch = static_cast<unsigned char>( ch );
	return ( ( ch >= 'a' && ch <= 'z' ) ||
		( ch >= 'A' && ch <= 'Z' ) ||
		( ch >= '0' && ch <= '9' ) );
}

static bool Con_IsCompletionBoundary( const char *match, int index ) {
	if ( index <= 0 ) {
		return true;
	}

	return !Con_IsCompletionWordChar( match[index - 1] );
}

static bool Con_FindSubstringCompletion( const char *match, const char *needle, int *outPos, bool *outBoundary ) {
	const int needleLen = strlen( needle );
	int bestPos = -1;
	bool bestBoundary = false;

	if ( needleLen < 2 ) {
		return false;
	}

	for ( int i = 0; match[i] != '\0'; ++i ) {
		if ( idStr::Icmpn( match + i, needle, needleLen ) != 0 ) {
			continue;
		}

		const bool boundary = Con_IsCompletionBoundary( match, i );
		if ( bestPos < 0 || ( boundary && !bestBoundary ) || ( boundary == bestBoundary && i < bestPos ) ) {
			bestPos = i;
			bestBoundary = boundary;
			if ( bestPos == 0 ) {
				break;
			}
		}
	}

	if ( bestPos < 0 ) {
		return false;
	}

	if ( outPos != NULL ) {
		*outPos = bestPos;
	}
	if ( outBoundary != NULL ) {
		*outBoundary = bestBoundary;
	}
	return true;
}

static bool Con_FindSubsequenceCompletion( const char *match, const char *needle, int *outStart, int *outGapScore, bool *outBoundary ) {
	int start = -1;
	int previous = -1;
	int gapScore = 0;
	int needleIndex = 0;

	if ( needle[0] == '\0' || needle[1] == '\0' ) {
		return false;
	}

	for ( int i = 0; match[i] != '\0' && needle[needleIndex] != '\0'; ++i ) {
		if ( Con_CompletionLower( match[i] ) != Con_CompletionLower( needle[needleIndex] ) ) {
			continue;
		}

		if ( start < 0 ) {
			start = i;
		}
		if ( previous >= 0 ) {
			gapScore += i - previous - 1;
		}
		previous = i;
		++needleIndex;
	}

	if ( needle[needleIndex] != '\0' ) {
		return false;
	}

	if ( outStart != NULL ) {
		*outStart = start;
	}
	if ( outGapScore != NULL ) {
		*outGapScore = gapScore;
	}
	if ( outBoundary != NULL ) {
		*outBoundary = Con_IsCompletionBoundary( match, start );
	}
	return true;
}

static int Con_BoundedCompletionDistance( const char *needle, int needleLen, const char *candidate, int candidateLen, int maxDistance ) {
	int prevPrev[MAX_EDIT_LINE + 1];
	int prev[MAX_EDIT_LINE + 1];
	int curr[MAX_EDIT_LINE + 1];

	if ( needleLen < 1 || candidateLen < 1 || needleLen > MAX_EDIT_LINE || candidateLen > MAX_EDIT_LINE ) {
		return maxDistance + 1;
	}

	if ( idMath::Abs( needleLen - candidateLen ) > maxDistance ) {
		return maxDistance + 1;
	}

	for ( int j = 0; j <= candidateLen; ++j ) {
		prevPrev[j] = j;
		prev[j] = j;
	}

	for ( int i = 1; i <= needleLen; ++i ) {
		int rowMin;

		curr[0] = i;
		rowMin = curr[0];

		for ( int j = 1; j <= candidateLen; ++j ) {
			const int cost = ( Con_CompletionLower( needle[i - 1] ) == Con_CompletionLower( candidate[j - 1] ) ) ? 0 : 1;
			int best = prev[j] + 1;

			if ( curr[j - 1] + 1 < best ) {
				best = curr[j - 1] + 1;
			}
			if ( prev[j - 1] + cost < best ) {
				best = prev[j - 1] + cost;
			}
			if ( i > 1 && j > 1 &&
				Con_CompletionLower( needle[i - 1] ) == Con_CompletionLower( candidate[j - 2] ) &&
				Con_CompletionLower( needle[i - 2] ) == Con_CompletionLower( candidate[j - 1] ) &&
				prevPrev[j - 2] + 1 < best ) {
				best = prevPrev[j - 2] + 1;
			}

			curr[j] = best;
			if ( best < rowMin ) {
				rowMin = best;
			}
		}

		if ( rowMin > maxDistance ) {
			return maxDistance + 1;
		}

		memcpy( prevPrev, prev, ( candidateLen + 1 ) * sizeof( prevPrev[0] ) );
		memcpy( prev, curr, ( candidateLen + 1 ) * sizeof( prev[0] ) );
	}

	return prev[candidateLen];
}

static int Con_GetMaxCompletionDistance( int needleLen ) {
	if ( needleLen <= 3 ) {
		return 1;
	}
	if ( needleLen <= 6 ) {
		return 2;
	}
	return 3;
}

static int Con_FindBestCompletionDistance( const char *match, const char *needle, int maxDistance, int *outStart, bool *outBoundary ) {
	const int needleLen = strlen( needle );
	const int matchLen = strlen( match );
	const int minWindowLen = Max( 1, needleLen - maxDistance );
	const int maxWindowLen = needleLen + maxDistance;
	int bestDistance = maxDistance + 1;
	int bestStart = -1;
	bool bestBoundary = false;

	if ( needleLen < 3 || matchLen < 1 ) {
		return -1;
	}

	for ( int start = 0; start < matchLen; ++start ) {
		const int candidateMaxLen = matchLen - start;
		const bool boundary = Con_IsCompletionBoundary( match, start );

		for ( int windowLen = minWindowLen; windowLen <= maxWindowLen && windowLen <= candidateMaxLen; ++windowLen ) {
			const int limit = ( bestDistance <= maxDistance ) ? bestDistance - 1 : maxDistance;
			const int distance = Con_BoundedCompletionDistance( needle, needleLen, match + start, windowLen, limit );
			if ( distance > maxDistance ) {
				continue;
			}

			if ( distance < bestDistance ||
				( distance == bestDistance && boundary && !bestBoundary ) ||
				( distance == bestDistance && boundary == bestBoundary && start < bestStart ) ) {
				bestDistance = distance;
				bestStart = start;
				bestBoundary = boundary;
			}
		}
	}

	if ( bestDistance > maxDistance ) {
		return -1;
	}

	if ( outStart != NULL ) {
		*outStart = bestStart;
	}
	if ( outBoundary != NULL ) {
		*outBoundary = bestBoundary;
	}
	return bestDistance;
}

static bool Con_BuildFuzzyCompletionMatch( const char *match, const char *needle, consoleCompletionCandidate_t &outCandidate ) {
	int pos;
	int start;
	int metric;
	bool boundary;

	if ( match == NULL || match[0] == '\0' || needle == NULL || needle[0] == '\0' ) {
		return false;
	}

	idStr::Copynz( outCandidate.match, match, sizeof( outCandidate.match ) );
	outCandidate.secondary = strlen( match );
	outCandidate.tertiary = 0;

	if ( Con_FindSubstringCompletion( match, needle, &pos, &boundary ) ) {
		outCandidate.category = boundary ? 0 : 1;
		outCandidate.primary = pos;
		outCandidate.tertiary = pos;
		return true;
	}

	if ( Con_FindSubsequenceCompletion( match, needle, &start, &metric, &boundary ) ) {
		outCandidate.category = boundary ? 2 : 3;
		outCandidate.primary = metric;
		outCandidate.tertiary = start;
		return true;
	}

	metric = Con_FindBestCompletionDistance( match, needle, Con_GetMaxCompletionDistance( strlen( needle ) ), &start, &boundary );
	if ( metric >= 0 ) {
		outCandidate.category = boundary ? 4 : 5;
		outCandidate.primary = metric;
		outCandidate.tertiary = start;
		return true;
	}

	return false;
}

static int Con_CompareFuzzyCompletionMatches( const void *lhs, const void *rhs ) {
	const consoleCompletionCandidate_t &a = *reinterpret_cast<const consoleCompletionCandidate_t *>( lhs );
	const consoleCompletionCandidate_t &b = *reinterpret_cast<const consoleCompletionCandidate_t *>( rhs );

	if ( a.category != b.category ) {
		return a.category - b.category;
	}
	if ( a.primary != b.primary ) {
		return a.primary - b.primary;
	}
	if ( a.secondary != b.secondary ) {
		return a.secondary - b.secondary;
	}
	if ( a.tertiary != b.tertiary ) {
		return a.tertiary - b.tertiary;
	}
	return idStr::Icmp( a.match, b.match );
}

static bool Con_CollectCompletionMatchCallback( const char *match, void *context ) {
	return reinterpret_cast<idConsoleLocal *>( context )->CollectCompletionMatch( match );
}

static bool Con_CollectFuzzyCompletionMatchCallback( const char *match, void *context ) {
	consoleFuzzyCompletionState_t *state = reinterpret_cast<consoleFuzzyCompletionState_t *>( context );
	consoleCompletionCandidate_t candidate;

	if ( state == NULL || !Con_BuildFuzzyCompletionMatch( match, state->needle, candidate ) ) {
		return true;
	}

	for ( int i = 0; i < state->count; ++i ) {
		if ( idStr::Icmp( state->matches[i].match, candidate.match ) != 0 ) {
			continue;
		}

		if ( Con_CompareFuzzyCompletionMatches( &candidate, &state->matches[i] ) < 0 ) {
			state->matches[i] = candidate;
		}
		return true;
	}

	if ( state->count < CON_COMPLETION_MAX_MATCHES ) {
		state->matches[state->count++] = candidate;
		return true;
	}

	int worst = 0;
	for ( int i = 1; i < state->count; ++i ) {
		if ( Con_CompareFuzzyCompletionMatches( &state->matches[worst], &state->matches[i] ) < 0 ) {
			worst = i;
		}
	}

	if ( Con_CompareFuzzyCompletionMatches( &candidate, &state->matches[worst] ) < 0 ) {
		state->matches[worst] = candidate;
	}

	return true;
}

void idConsoleLocal::PrintToBuffer( const char *txt, bool markNotifyTime ) {
	int c, l;
	int color = idStr::ColorIndex( C_COLOR_CONSOLE );
	const int width = lineWidth;

	while ( ( c = *(const unsigned char *)txt ) != 0 ) {
		short *line = LinePtr( current );

		bool resetToDefault = false;
		const int colorEscapeLength = idStr::ColorEscapeLength( txt, NULL, &resetToDefault );
		if ( colorEscapeLength > 0 ) {
			if ( resetToDefault ) {
				color = idStr::ColorIndex( C_COLOR_CONSOLE );
			} else if ( colorEscapeLength == 2 ) {
				color = idStr::ColorIndex( *( txt + 1 ) );
			}
			txt += colorEscapeLength;
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
		times[current % NUM_CON_TIMES] = common->GetPresentationTime();
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
		times[current % NUM_CON_TIMES] = common->GetPresentationTime();
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
	static double	previousTimes[FPS_FRAMES];
	static int	index;
	int		i;
	int		fps;
	static	double	previous;
	double	t;
	double	frameTime;
	double	total;
	const double clockTicksPerSecond = Sys_ClockTicksPerSecond();

	// don't use serverTime, because that will be drifting to
	// correct for internet lag changes, timescales, timedemos, etc
	if ( clockTicksPerSecond <= 0.0 ) {
		return y + BIGCHAR_HEIGHT + 4;
	}

	t = Sys_GetClockTicks();
	if ( previous <= 0.0 || t <= previous ) {
		previous = t;
		return y + BIGCHAR_HEIGHT + 4;
	}

	frameTime = ( t - previous ) * 1000.0 / clockTicksPerSecond;
	previous = t;

	previousTimes[index % FPS_FRAMES] = frameTime;
	index++;
	if ( index > FPS_FRAMES ) {
		// average multiple frames together to smooth changes out a bit
		total = 0.0;
		for ( i = 0 ; i < FPS_FRAMES ; i++ ) {
			total += previousTimes[i];
		}
		if ( total <= 0.0 ) {
			total = 1.0;
		}
		fps = idMath::FtoiFast( ( 1000.0 * FPS_FRAMES / total ) + 0.5 );

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

static const char *SCR_FramePacingBoundLabel( openq4FramePacingBound_t boundMode ) {
	switch ( boundMode ) {
		case OPENQ4_FRAME_BOUND_SIMULATION:
			return "simulation";
		case OPENQ4_FRAME_BOUND_VSYNC:
			return "vsync";
		case OPENQ4_FRAME_BOUND_PRESENTATION_CAP:
			return "presentation-cap";
		case OPENQ4_FRAME_BOUND_UNCAPPED:
			return "uncapped";
		default:
			return "collecting";
	}
}

float SCR_DrawFramePacing( float y ) {
	const openq4FramePacingStats_t &stats = sessLocal.GetFramePacingStats();

	if ( !stats.valid ) {
		SCR_DrawTextRightAlign( y, "frame pacing: collecting samples" );
		return y;
	}

	if ( stats.presentationCap > 0 ) {
		SCR_DrawTextRightAlign(
			y,
			"frame pacing = %s (%s, r_swapInterval=%d, com_maxfps=%d)",
			SCR_FramePacingBoundLabel( stats.boundMode ),
			stats.multiplayer ? "MP" : "SP",
			stats.swapInterval,
			stats.presentationCap );
	} else {
		SCR_DrawTextRightAlign(
			y,
			"frame pacing = %s (%s, r_swapInterval=%d)",
			SCR_FramePacingBoundLabel( stats.boundMode ),
			stats.multiplayer ? "MP" : "SP",
			stats.swapInterval );
	}

	SCR_DrawTextRightAlign(
		y,
		"present avg = %.2f ms (%.1f Hz), last = %d ms, async avg = %.2f ms (%.1f Hz)",
		stats.avgFrameMsec,
		stats.avgFrameHz,
		stats.lastFrameMsec,
		stats.asyncStats.avgDeltaMsec,
		stats.asyncStats.avgHz );
	SCR_DrawTextRightAlign(
		y,
		"async jitter = %.2f ms, min/max = %d/%d ms, async work = %.2f ms",
		stats.asyncStats.avgJitterMsec,
		stats.asyncStats.minDeltaMsec,
		stats.asyncStats.maxDeltaMsec,
		stats.asyncStats.avgTimeConsumedMsec );
	SCR_DrawTextRightAlign(
		y,
		"tic delta/frame avg = %.2f (last %d), game tics/frame avg = %.2f (last %d)",
		stats.avgTicsPerFrame,
		stats.lastTicDelta,
		stats.avgGameTicsPerFrame,
		stats.lastGameTics );
	SCR_DrawTextRightAlign(
		y,
		"wait req = %d ms, actual = %d ms, overshoot avg = %.2f ms, wake jitter avg = %.2f ms",
		stats.lastRequestedWaitMsec,
		stats.lastWaitMsec,
		stats.avgWaitOvershootMsec,
		stats.avgWakeJitterMsec );

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
	procFileOutOfDate = false;
	aasFileOutOfDate = false;

	lastKeyEvent = -1;
	nextKeyEvent = CONSOLE_FIRSTREPEAT;
	lineWidth = CON_DEFAULT_LINE_WIDTH;
	smallCharWidth = SMALLCHAR_WIDTH;
	bigCharWidth = BIGCHAR_WIDTH;
	displayLine = 0.0f;
	printHistory.Clear();

	consoleField.Clear();
	inputSelectionAnchor = -1;
	logSelectionAnchorLine = 0;
	logSelectionAnchorColumn = 0;
	logSelectionLine = 0;
	logSelectionColumn = 0;
	mouseX = 0.0f;
	mouseY = 0.0f;
	scrollbarHover = 0.0f;
	scrollbarDragOffset = 0.0f;
	completionScrollbarHover = 0.0f;
	completionScrollbarDragOffset = 0.0f;
	completionCount = 0;
	completionSelection = 0;
	completionScroll = 0;
	completionSegmentStart = 0;
	completionSegmentEnd = 0;
	completionReplaceArgIndex = 0;
	completionReplaceOffset = 0;
	completionReplaceLength = 0;
	completionSnapshotCursor = 0;
	completionAppendSpace = false;
	completionPrependSlash = false;
	completionPopupVisible = false;
	completionSnapshotValid = false;
	mouseInitialized = false;
	scrollbarDragging = false;
	completionScrollbarDragging = false;
	inputSelecting = false;
	logSelecting = false;
	textDragPending = false;
	textDragging = false;
	textDragFromInput = false;
	textDragTargetInput = false;
	textDragSourceStart = 0;
	textDragSourceEnd = 0;
	textDragDropCursor = 0;
	textDragTextLength = 0;
	textDragStartMouseX = 0.0f;
	textDragStartMouseY = 0.0f;
	focus = CON_FOCUS_INPUT;
	completionSnapshotBuffer[0] = '\0';
	textDragText[0] = '\0';
	completionFuzzyNeedle[0] = '\0';
	whiteShader = NULL;
	consoleShader = NULL;
	mouseCursorShader = NULL;
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
	mouseCursorShader = declManager->FindMaterial( "gfx/guis/guicursor_arrow" );
	if ( mouseCursorShader != NULL ) {
		mouseCursorShader->SetSort( SS_GUI );
	}
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
	displayLine = static_cast<float>( display );
	mouseInitialized = false;
	scrollbarDragging = false;
	completionScrollbarDragging = false;
	inputSelecting = false;
	logSelecting = false;
	focus = CON_FOCUS_INPUT;
	ClearInputSelection();
	ClearLogSelection();
	ClearTextDragState();
	InvalidateCompletionState();
	ClearNotifyLines();
}

/*
================
idConsoleLocal::SetProcFileOutOfDate
================
*/
void idConsoleLocal::SetProcFileOutOfDate( bool state ) {
	procFileOutOfDate = state;
}

/*
================
idConsoleLocal::SetAASFileOutOfDate
================
*/
void idConsoleLocal::SetAASFileOutOfDate( bool state ) {
	aasFileOutOfDate = state;
}

/*
================
idConsoleLocal::SetMousePosition
================
*/
void idConsoleLocal::SetMousePosition( float x, float y ) {
	mouseX = x;
	mouseY = y;
	mouseInitialized = true;
	ClampMousePosition( mouseX, mouseY );
}

/*
================
idConsoleLocal::ClampMousePosition
================
*/
void idConsoleLocal::ClampMousePosition( float &x, float &y ) const {
	float consoleX, consoleY, consoleW, consoleH;
	GetConsoleRect( consoleX, consoleY, consoleW, consoleH );

	const float maxX = Max( consoleX, consoleX + consoleW - 1.0f );
	const float maxY = Max( consoleY, consoleY + consoleH - 1.0f );
	x = idMath::ClampFloat( consoleX, maxX, x );
	y = idMath::ClampFloat( consoleY, maxY, y );
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
	ClearInputSelection();
	ClearLogSelection();
	ClearTextDragState();
	InvalidateCompletionState();

	Bottom();		// go to end
	displayLine = static_cast<float>( display );
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
idConsoleLocal::ClampDisplay
================
*/
void idConsoleLocal::ClampDisplay( void ) {
	if ( display > current ) {
		display = current;
	}
	if ( display < 0 ) {
		display = 0;
	}
	if ( current - display >= CON_MAX_TOTAL_LINES ) {
		display = current - CON_MAX_TOTAL_LINES + 1;
	}
	if ( display < 0 ) {
		display = 0;
	}
}

/*
================
idConsoleLocal::UpdateDisplayLine
================
*/
void idConsoleLocal::UpdateDisplayLine( void ) {
	static int lastDisplayLineUpdateTime = 0;

	const float target = static_cast<float>( display );
	const int now = eventLoop->Milliseconds();
	const int frameMsec = ( lastDisplayLineUpdateTime > 0 ) ? ( now - lastDisplayLineUpdateTime ) : 0;
	lastDisplayLineUpdateTime = now;

	if ( !con_scrollSmooth.GetBool() || frameMsec <= 0 ) {
		displayLine = target;
		return;
	}

	if ( displayLine < 0.0f || displayLine > current + GetLogRowCount() + 1 ) {
		displayLine = target;
		return;
	}

	const float delta = target - displayLine;
	if ( idMath::Fabs( delta ) < 0.01f ) {
		displayLine = target;
		return;
	}

	const float speed = Max( 1.0f, con_scrollSmoothSpeed.GetFloat() );
	const float step = speed * frameMsec * 0.001f;
	if ( step >= idMath::Fabs( delta ) ) {
		displayLine = target;
	} else if ( delta > 0.0f ) {
		displayLine += step;
	} else {
		displayLine -= step;
	}
}

/*
================
idConsoleLocal::GetOpenFraction
================
*/
float idConsoleLocal::GetOpenFraction( bool reduced ) const {
	const float maxFrac = con_height.GetFloat();
	return reduced ? Min( 0.2f, maxFrac ) : maxFrac;
}

/*
================
idConsoleLocal::GetFooterRows
================
*/
int idConsoleLocal::GetFooterRows( void ) const {
	return 2;
}

/*
================
idConsoleLocal::GetLogRowCount
================
*/
int idConsoleLocal::GetLogRowCount( void ) const {
	int rows = vislines / SMALLCHAR_HEIGHT - GetFooterRows() + 1;
	if ( rows < 1 ) {
		rows = 1;
	}
	return rows;
}

/*
================
idConsoleLocal::GetScrollRange
================
*/
bool idConsoleLocal::GetScrollRange( int &minDisplay, int &maxDisplay, int *filled ) const {
	const int totalLines = ( current >= CON_MAX_TOTAL_LINES ) ? CON_MAX_TOTAL_LINES : current + 1;
	if ( filled != NULL ) {
		*filled = totalLines;
	}

	if ( totalLines <= GetLogRowCount() ) {
		minDisplay = current;
		maxDisplay = current;
		return false;
	}

	maxDisplay = current;
	minDisplay = current - totalLines + GetLogRowCount();
	return true;
}

/*
================
idConsoleLocal::GetConsoleRect
================
*/
void idConsoleLocal::GetConsoleRect( float &x, float &y, float &w, float &h ) const {
	x = 0.0f;
	y = 0.0f;
	w = SCREEN_WIDTH;
	h = static_cast<float>( vislines );
	if ( h <= 0.0f ) {
		h = SCREEN_HEIGHT * displayFrac;
		if ( keyCatching && finalFrac > 0.0f && h < SCREEN_HEIGHT * finalFrac ) {
			h = SCREEN_HEIGHT * finalFrac;
		}
		if ( h > SCREEN_HEIGHT ) {
			h = SCREEN_HEIGHT;
		}
	}
}

/*
================
idConsoleLocal::GetLogAreaRect
================
*/
void idConsoleLocal::GetLogAreaRect( float &x, float &y, float &w, float &h ) const {
	float consoleX, consoleY, consoleW, consoleH;
	GetConsoleRect( consoleX, consoleY, consoleW, consoleH );

	const int rows = GetLogRowCount();
	const float logBottom = consoleY + consoleH - SMALLCHAR_HEIGHT * GetFooterRows();
	float logTop = logBottom - rows * SMALLCHAR_HEIGHT;
	if ( logTop < consoleY ) {
		logTop = consoleY;
	}

	x = consoleX;
	y = logTop;
	w = consoleW;
	h = logBottom - logTop;
}

/*
================
idConsoleLocal::GetInputAreaRect
================
*/
bool idConsoleLocal::GetInputAreaRect( float &x, float &y, float &w, float &h ) const {
	float consoleX, consoleY, consoleW, consoleH;
	GetConsoleRect( consoleX, consoleY, consoleW, consoleH );

	if ( consoleW <= 0.0f || consoleH <= SMALLCHAR_HEIGHT * GetFooterRows() ) {
		return false;
	}

	x = consoleX + 2.0f * smallCharWidth;
	y = consoleY + consoleH - SMALLCHAR_HEIGHT * GetFooterRows();
	w = consoleW - 3.0f * smallCharWidth;
	h = SMALLCHAR_HEIGHT;
	return true;
}

/*
================
idConsoleLocal::GetInputDrawInfo
================
*/
void idConsoleLocal::GetInputDrawInfo( int &prestep, int &drawLen ) const {
	const int count = Max( 1, consoleField.GetWidthInChars() - 1 );
	const int len = consoleField.GetLength();
	int start = consoleField.GetScroll();

	if ( len <= count ) {
		start = 0;
	} else if ( start + count > len ) {
		start = len - count;
		if ( start < 0 ) {
			start = 0;
		}
	}

	drawLen = count;
	if ( start + drawLen > len ) {
		drawLen = len - start;
	}
	if ( drawLen < 0 ) {
		drawLen = 0;
	}

	prestep = start;
}

/*
================
idConsoleLocal::GetInputCursorFromMouse
================
*/
int idConsoleLocal::GetInputCursorFromMouse( void ) const {
	float inputX, inputY, inputW, inputH;
	if ( !GetInputAreaRect( inputX, inputY, inputW, inputH ) ) {
		return consoleField.GetCursor();
	}

	int prestep, drawLen;
	GetInputDrawInfo( prestep, drawLen );
	int cursor = prestep + idMath::FtoiFast( ( mouseX - inputX ) / smallCharWidth + 0.5f );
	if ( mouseX <= inputX ) {
		cursor = prestep;
	}
	cursor = idMath::ClampInt( 0, consoleField.GetLength(), cursor );
	return cursor;
}

/*
================
idConsoleLocal::GetLogPositionFromMouse
================
*/
bool idConsoleLocal::GetLogPositionFromMouse( int &line, int &column ) const {
	float logX, logY, logW, logH;
	GetLogAreaRect( logX, logY, logW, logH );
	if ( mouseX < logX || mouseX > logX + logW || mouseY < logY || mouseY > logY + logH ) {
		return false;
	}

	const int rows = GetLogRowCount();
	int rowIndex = idMath::FtoiFast( ( mouseY - logY ) / SMALLCHAR_HEIGHT );
	rowIndex = idMath::ClampInt( 0, rows - 1, rowIndex );

	if ( display != current && rows > 1 && rowIndex == rows - 1 ) {
		rowIndex = rows - 2;
	}

	line = display - ( rows - 1 - rowIndex );
	column = idMath::FtoiFast( ( mouseX - smallCharWidth ) / smallCharWidth );
	ClampLogPosition( line, column );
	return true;
}

/*
================
idConsoleLocal::ClampMouseToConsole
================
*/
void idConsoleLocal::ClampMouseToConsole( void ) {
	float consoleX, consoleY, consoleW, consoleH;
	GetConsoleRect( consoleX, consoleY, consoleW, consoleH );

	if ( !mouseInitialized ) {
		mouseX = consoleX + consoleW - 12.0f;
		mouseY = consoleY + ( consoleH > 1.0f ? consoleH * 0.5f : SCREEN_HEIGHT * 0.25f );
		mouseInitialized = true;
	}

	ClampMousePosition( mouseX, mouseY );
}

/*
================
idConsoleLocal::UpdateScrollbarHoverValue
================
*/
void idConsoleLocal::UpdateScrollbarHoverValue( float &hoverFrac, bool dragging, bool hot ) {
	const float target = ( dragging || hot ) ? 1.0f : 0.0f;
	const float step = 0.2f;

	if ( hoverFrac < target ) {
		hoverFrac = Min( target, hoverFrac + step );
	} else if ( hoverFrac > target ) {
		hoverFrac = Max( target, hoverFrac - step );
	}
}

/*
================
idConsoleLocal::GetScrollbarFrameGeometry
================
*/
void idConsoleLocal::GetScrollbarFrameGeometry( float areaX, float areaW, float areaY, float areaH, float hoverFrac, float &trackX, float &trackY, float &trackW, float &trackH, float *hitX, float *hitW ) const {
	const float maxWidth = CON_SCROLLBAR_BASE_WIDTH + CON_SCROLLBAR_HOVER_GROW;
	const float width = CON_SCROLLBAR_BASE_WIDTH + CON_SCROLLBAR_HOVER_GROW * hoverFrac;

	trackX = areaX + areaW - CON_SCROLLBAR_SIDE_PAD - width;
	trackY = areaY;
	trackW = width;
	trackH = areaH;

	if ( hitX != NULL ) {
		*hitX = areaX + areaW - CON_SCROLLBAR_SIDE_PAD - maxWidth - CON_SCROLLBAR_HIT_PAD;
	}
	if ( hitW != NULL ) {
		*hitW = maxWidth + CON_SCROLLBAR_HIT_PAD * 2.0f;
	}
}

/*
================
idConsoleLocal::GetScrollbarThumbGeometry
================
*/
void idConsoleLocal::GetScrollbarThumbGeometry( float trackY, float trackH, int visibleCount, int filled, float displayFrac, float &thumbY, float &thumbH ) const {
	float height = trackH;

	if ( filled > 0 && visibleCount > 0 ) {
		height = trackH * ( static_cast<float>( visibleCount ) / static_cast<float>( filled ) );
		height = idMath::ClampFloat( CON_SCROLLBAR_MIN_THUMB, trackH, height );
	}

	thumbH = height;
	if ( trackH <= height ) {
		thumbY = trackY;
	} else {
		thumbY = trackY + ( trackH - height ) * idMath::ClampFloat( 0.0f, 1.0f, displayFrac );
	}
}

/*
================
idConsoleLocal::GetScrollbarGeometry
================
*/
bool idConsoleLocal::GetScrollbarGeometry( float hoverFrac, float &trackX, float &trackY, float &trackW, float &trackH, float &thumbY, float &thumbH, float *hitX, float *hitW ) const {
	float consoleX, consoleY, consoleW, consoleH;
	float logX, logY, logW, logH;
	int minDisplay, maxDisplay, filled;

	if ( !GetScrollRange( minDisplay, maxDisplay, &filled ) ) {
		return false;
	}

	GetConsoleRect( consoleX, consoleY, consoleW, consoleH );
	if ( consoleW <= 0.0f || consoleH <= SMALLCHAR_HEIGHT * 4.0f ) {
		return false;
	}

	GetLogAreaRect( logX, logY, logW, logH );
	if ( logH <= 0.0f ) {
		return false;
	}

	GetScrollbarFrameGeometry( consoleX, consoleW, logY, logH, hoverFrac, trackX, trackY, trackW, trackH, hitX, hitW );

	const float displayFracValue = ( maxDisplay <= minDisplay ) ? 1.0f :
		( displayLine - minDisplay ) / static_cast<float>( maxDisplay - minDisplay );
	GetScrollbarThumbGeometry( logY, logH, GetLogRowCount(), filled, displayFracValue, thumbY, thumbH );
	return true;
}

/*
================
idConsoleLocal::SetScrollbarDisplayFraction
================
*/
void idConsoleLocal::SetScrollbarDisplayFraction( float frac ) {
	int minDisplay, maxDisplay;
	if ( !GetScrollRange( minDisplay, maxDisplay ) ) {
		return;
	}

	frac = idMath::ClampFloat( 0.0f, 1.0f, frac );
	if ( maxDisplay <= minDisplay ) {
		display = maxDisplay;
	} else {
		display = minDisplay + idMath::FtoiFast( frac * ( maxDisplay - minDisplay ) + 0.5f );
	}
	ClampDisplay();
	displayLine = static_cast<float>( display );
}

/*
================
idConsoleLocal::UpdateScrollbarDrag
================
*/
void idConsoleLocal::UpdateScrollbarDrag( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;

	if ( !scrollbarDragging || !idKeyInput::IsDown( K_MOUSE1 ) ) {
		return;
	}

	if ( !GetScrollbarGeometry( scrollbarHover, trackX, trackY, trackW, trackH, thumbY, thumbH ) ) {
		scrollbarDragging = false;
		return;
	}

	if ( trackH <= thumbH ) {
		SetScrollbarDisplayFraction( 1.0f );
		return;
	}

	const float frac = ( mouseY - scrollbarDragOffset - trackY ) / ( trackH - thumbH );
	SetScrollbarDisplayFraction( frac );
}

/*
================
idConsoleLocal::UpdateScrollbarHover
================
*/
void idConsoleLocal::UpdateScrollbarHover( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float hitX, hitW;
	bool hot = false;

	if ( !idKeyInput::IsDown( K_MOUSE1 ) ) {
		scrollbarDragging = false;
	}

	ClampMouseToConsole();

	if ( GetScrollbarGeometry( scrollbarHover, trackX, trackY, trackW, trackH, thumbY, thumbH, &hitX, &hitW ) ) {
		if ( scrollbarDragging ||
			( mouseX >= hitX && mouseX <= hitX + hitW && mouseY >= trackY && mouseY <= trackY + trackH ) ) {
			hot = true;
		}
	}

	UpdateScrollbarHoverValue( scrollbarHover, scrollbarDragging, hot );
	UpdateScrollbarDrag();
}

/*
================
idConsoleLocal::ClearInputSelection
================
*/
void idConsoleLocal::ClearInputSelection( void ) {
	inputSelectionAnchor = -1;
	inputSelecting = false;
}

/*
================
idConsoleLocal::HasInputSelection
================
*/
bool idConsoleLocal::HasInputSelection( void ) const {
	return inputSelectionAnchor >= 0 && inputSelectionAnchor != consoleField.GetCursor();
}

/*
================
idConsoleLocal::GetInputSelectionRange
================
*/
void idConsoleLocal::GetInputSelectionRange( int &start, int &end ) const {
	if ( !HasInputSelection() ) {
		start = consoleField.GetCursor();
		end = consoleField.GetCursor();
		return;
	}

	if ( inputSelectionAnchor < consoleField.GetCursor() ) {
		start = inputSelectionAnchor;
		end = consoleField.GetCursor();
	} else {
		start = consoleField.GetCursor();
		end = inputSelectionAnchor;
	}
}

/*
================
idConsoleLocal::DeleteInputRange
================
*/
void idConsoleLocal::DeleteInputRange( int start, int end ) {
	char *buffer = consoleField.GetBuffer();
	const int len = consoleField.GetLength();

	start = idMath::ClampInt( 0, len, start );
	end = idMath::ClampInt( 0, len, end );
	if ( end <= start ) {
		consoleField.SetCursor( start );
		ClearInputSelection();
		return;
	}

	memmove( buffer + start, buffer + end, len + 1 - end );
	consoleField.SetCursor( start );
	consoleField.ClampCursorAndScroll();
	ClearInputSelection();
	focus = CON_FOCUS_INPUT;
	ClearLogSelection();
	RebuildCompletionState();
}

/*
================
idConsoleLocal::DeleteInputSelection
================
*/
void idConsoleLocal::DeleteInputSelection( void ) {
	if ( !HasInputSelection() ) {
		return;
	}

	int start, end;
	GetInputSelectionRange( start, end );
	DeleteInputRange( start, end );
}

/*
================
idConsoleLocal::SeekWordCursor
================
*/
int idConsoleLocal::SeekWordCursor( int cursor, int direction ) const {
	const char *buffer = consoleField.GetBuffer();
	const int len = consoleField.GetLength();

	if ( direction > 0 ) {
		while ( cursor < len && buffer[cursor] == ' ' ) {
			++cursor;
		}
		while ( cursor < len && buffer[cursor] != ' ' ) {
			++cursor;
		}
		while ( cursor < len && buffer[cursor] == ' ' ) {
			++cursor;
		}
	} else {
		while ( cursor > 0 && buffer[cursor - 1] == ' ' ) {
			--cursor;
		}
		while ( cursor > 0 && buffer[cursor - 1] != ' ' ) {
			--cursor;
		}
	}

	return cursor;
}

/*
================
idConsoleLocal::SetInputCursor
================
*/
void idConsoleLocal::SetInputCursor( int cursor, bool keepSelection ) {
	const int oldCursor = consoleField.GetCursor();
	cursor = idMath::ClampInt( 0, consoleField.GetLength(), cursor );

	if ( keepSelection ) {
		if ( inputSelectionAnchor < 0 ) {
			inputSelectionAnchor = oldCursor;
		}
	} else {
		ClearInputSelection();
	}

	consoleField.SetCursor( cursor );
	consoleField.ClampCursorAndScroll();
	focus = CON_FOCUS_INPUT;
	ClearLogSelection();
	RebuildCompletionState();
}

/*
================
idConsoleLocal::SelectAllInput
================
*/
void idConsoleLocal::SelectAllInput( void ) {
	focus = CON_FOCUS_INPUT;
	inputSelectionAnchor = 0;
	consoleField.SetCursor( consoleField.GetLength() );
	consoleField.ClampCursorAndScroll();
	ClearLogSelection();
	RebuildCompletionState();
}

/*
================
idConsoleLocal::InsertInputChar
================
*/
void idConsoleLocal::InsertInputChar( int ch ) {
	char *buffer = consoleField.GetBuffer();
	const int len = consoleField.GetLength();
	int cursor = consoleField.GetCursor();

	if ( ch < ' ' ) {
		return;
	}

	DeleteInputSelection();
	cursor = consoleField.GetCursor();

	if ( idKeyInput::GetOverstrikeMode() ) {
		if ( cursor >= MAX_EDIT_LINE - 2 ) {
			return;
		}
		buffer[cursor] = static_cast<char>( ch );
		++cursor;
		if ( cursor > len ) {
			buffer[cursor] = '\0';
		}
	} else {
		if ( len >= MAX_EDIT_LINE - 2 ) {
			return;
		}
		memmove( buffer + cursor + 1, buffer + cursor, len + 1 - cursor );
		buffer[cursor] = static_cast<char>( ch );
		++cursor;
	}

	consoleField.SetCursor( cursor );
	consoleField.ClampCursorAndScroll();
	ClearInputSelection();
	focus = CON_FOCUS_INPUT;
	ClearLogSelection();
	RebuildCompletionState();
}

/*
================
idConsoleLocal::BuildInputSelectionText
================
*/
int idConsoleLocal::BuildInputSelectionText( char *buffer, int bufferSize ) const {
	if ( buffer == NULL || bufferSize < 1 || !HasInputSelection() ) {
		return 0;
	}

	int start, end;
	GetInputSelectionRange( start, end );
	int length = end - start;
	if ( length <= 0 ) {
		buffer[0] = '\0';
		return 0;
	}

	length = Min( length, bufferSize - 1 );
	memcpy( buffer, consoleField.GetBuffer() + start, length );
	buffer[length] = '\0';
	return length;
}

/*
================
idConsoleLocal::CopyInputSelection
================
*/
void idConsoleLocal::CopyInputSelection( void ) const {
	char text[MAX_EDIT_LINE];
	if ( BuildInputSelectionText( text, sizeof( text ) ) > 0 ) {
		Sys_SetClipboardData( text );
	}
}

/*
================
idConsoleLocal::CutInputSelection
================
*/
void idConsoleLocal::CutInputSelection( void ) {
	if ( !HasInputSelection() ) {
		return;
	}
	CopyInputSelection();
	DeleteInputSelection();
}

/*
================
idConsoleLocal::PasteClipboardToInput
================
*/
void idConsoleLocal::PasteClipboardToInput( void ) {
	char *text = Sys_GetClipboardData();
	if ( text == NULL ) {
		return;
	}

	DeleteInputSelection();
	focus = CON_FOCUS_INPUT;
	for ( int i = 0; text[i] != '\0'; ++i ) {
		if ( text[i] >= ' ' ) {
			InsertInputChar( text[i] );
		}
	}

	Mem_Free( text );
}

/*
================
idConsoleLocal::InsertInputTextAt
================
*/
void idConsoleLocal::InsertInputTextAt( const char *text, int cursor ) {
	bool lastWasConvertedSpace = false;

	SetInputCursor( cursor, false );
	if ( text == NULL || text[0] == '\0' ) {
		return;
	}

	for ( int i = 0; text[i] != '\0'; ++i ) {
		int ch = static_cast<unsigned char>( text[i] );
		if ( ch == '\r' || ch == '\n' || ch == '\t' ) {
			if ( lastWasConvertedSpace ) {
				continue;
			}
			ch = ' ';
			lastWasConvertedSpace = true;
		} else {
			lastWasConvertedSpace = false;
			if ( ch < ' ' ) {
				continue;
			}
		}

		InsertInputChar( ch );
	}
}

/*
================
idConsoleLocal::CompareLogPosition
================
*/
int idConsoleLocal::CompareLogPosition( int line1, int column1, int line2, int column2 ) const {
	if ( line1 < line2 ) {
		return -1;
	}
	if ( line1 > line2 ) {
		return 1;
	}
	if ( column1 < column2 ) {
		return -1;
	}
	if ( column1 > column2 ) {
		return 1;
	}
	return 0;
}

/*
================
idConsoleLocal::ClampLogPosition
================
*/
void idConsoleLocal::ClampLogPosition( int &line, int &column ) const {
	const int oldestLine = Max( 0, current - CON_MAX_TOTAL_LINES + 1 );
	line = idMath::ClampInt( oldestLine, current, line );
	column = idMath::ClampInt( 0, lineWidth, column );
}

/*
================
idConsoleLocal::ClearLogSelection
================
*/
void idConsoleLocal::ClearLogSelection( void ) {
	logSelectionAnchorLine = logSelectionLine;
	logSelectionAnchorColumn = logSelectionColumn;
	logSelecting = false;
}

/*
================
idConsoleLocal::HasLogSelection
================
*/
bool idConsoleLocal::HasLogSelection( void ) const {
	return CompareLogPosition( logSelectionAnchorLine, logSelectionAnchorColumn, logSelectionLine, logSelectionColumn ) != 0;
}

/*
================
idConsoleLocal::GetLogSelectionRange
================
*/
void idConsoleLocal::GetLogSelectionRange( int &startLine, int &startColumn, int &endLine, int &endColumn ) const {
	if ( CompareLogPosition( logSelectionAnchorLine, logSelectionAnchorColumn, logSelectionLine, logSelectionColumn ) <= 0 ) {
		startLine = logSelectionAnchorLine;
		startColumn = logSelectionAnchorColumn;
		endLine = logSelectionLine;
		endColumn = logSelectionColumn;
	} else {
		startLine = logSelectionLine;
		startColumn = logSelectionColumn;
		endLine = logSelectionAnchorLine;
		endColumn = logSelectionAnchorColumn;
	}
}

/*
================
idConsoleLocal::SetLogCursor
================
*/
void idConsoleLocal::SetLogCursor( int line, int column, bool keepSelection ) {
	ClampLogPosition( line, column );

	if ( !keepSelection ) {
		logSelectionAnchorLine = line;
		logSelectionAnchorColumn = column;
	}

	logSelectionLine = line;
	logSelectionColumn = column;
	focus = CON_FOCUS_LOG;
	ClearInputSelection();
	InvalidateCompletionState();
}

/*
================
idConsoleLocal::BuildLogSelectionText
================
*/
int idConsoleLocal::BuildLogSelectionText( char *buffer, int bufferSize ) const {
	if ( buffer == NULL || bufferSize < 1 || !HasLogSelection() ) {
		return 0;
	}

	int startLine, startColumn, endLine, endColumn;
	GetLogSelectionRange( startLine, startColumn, endLine, endColumn );
	int length = 0;

	for ( int line = startLine; line <= endLine; ++line ) {
		const short *lineText = LinePtr( line );
		int segmentStart = ( line == startLine ) ? startColumn : 0;
		int segmentEnd = ( line == endLine ) ? endColumn : lineWidth;
		segmentStart = idMath::ClampInt( 0, lineWidth, segmentStart );
		segmentEnd = idMath::ClampInt( segmentStart, lineWidth, segmentEnd );

		int copyEnd = segmentEnd;
		while ( copyEnd > segmentStart && ( lineText[copyEnd - 1] & 0xff ) == ' ' ) {
			--copyEnd;
		}

		for ( int i = segmentStart; i < copyEnd && length < bufferSize - 1; ++i ) {
			buffer[length++] = static_cast<char>( lineText[i] & 0xff );
		}
		if ( line < endLine && length < bufferSize - 1 ) {
			buffer[length++] = '\n';
		}
	}

	buffer[length] = '\0';
	return length;
}

/*
================
idConsoleLocal::SelectAllLog
================
*/
void idConsoleLocal::SelectAllLog( void ) {
	focus = CON_FOCUS_LOG;
	ClearInputSelection();
	logSelectionAnchorLine = Max( 0, current - CON_MAX_TOTAL_LINES + 1 );
	logSelectionAnchorColumn = 0;
	logSelectionLine = current;
	logSelectionColumn = lineWidth;
	InvalidateCompletionState();
}

/*
================
idConsoleLocal::CopyLogSelection
================
*/
void idConsoleLocal::CopyLogSelection( void ) const {
	if ( !HasLogSelection() ) {
		return;
	}

	int startLine, startColumn, endLine, endColumn;
	GetLogSelectionRange( startLine, startColumn, endLine, endColumn );
	idList<char> text;
	text.SetNum( ( endLine - startLine + 1 ) * ( lineWidth + 1 ) + 1 );
	const int length = BuildLogSelectionText( text.Ptr(), text.Num() );
	text[length] = '\0';
	Sys_SetClipboardData( text.Ptr() );
}

/*
================
idConsoleLocal::CopySelection
================
*/
void idConsoleLocal::CopySelection( void ) const {
	if ( HasInputSelection() ) {
		CopyInputSelection();
	} else if ( HasLogSelection() ) {
		CopyLogSelection();
	}
}

/*
================
idConsoleLocal::ScrollToLogCursor
================
*/
void idConsoleLocal::ScrollToLogCursor( void ) {
	const int rows = GetLogRowCount();
	const int topLine = display - ( rows - 1 );

	if ( logSelectionLine > display ) {
		display = logSelectionLine;
	} else if ( logSelectionLine < topLine ) {
		display = logSelectionLine + rows - 1;
	}

	ClampDisplay();
	displayLine = static_cast<float>( display );
}

/*
================
idConsoleLocal::MoveLogCursorByChars
================
*/
void idConsoleLocal::MoveLogCursorByChars( int delta, bool keepSelection ) {
	int line = logSelectionLine;
	int column = logSelectionColumn;
	const int oldestLine = Max( 0, current - CON_MAX_TOTAL_LINES + 1 );

	while ( delta < 0 ) {
		if ( column > 0 ) {
			--column;
		} else if ( line > oldestLine ) {
			--line;
			column = lineWidth;
		}
		++delta;
	}

	while ( delta > 0 ) {
		if ( column < lineWidth ) {
			++column;
		} else if ( line < current ) {
			++line;
			column = 0;
		}
		--delta;
	}

	SetLogCursor( line, column, keepSelection );
	ScrollToLogCursor();
}

/*
================
idConsoleLocal::MoveLogCursorByLines
================
*/
void idConsoleLocal::MoveLogCursorByLines( int delta, bool keepSelection ) {
	SetLogCursor( logSelectionLine + delta, logSelectionColumn, keepSelection );
	ScrollToLogCursor();
}

/*
================
idConsoleLocal::MoveLogCursorToBoundary
================
*/
void idConsoleLocal::MoveLogCursorToBoundary( bool toStart, bool wholeLog, bool keepSelection ) {
	int line = logSelectionLine;
	int column = logSelectionColumn;

	if ( wholeLog ) {
		line = toStart ? Max( 0, current - CON_MAX_TOTAL_LINES + 1 ) : current;
		column = toStart ? 0 : lineWidth;
	} else {
		column = toStart ? 0 : lineWidth;
	}

	SetLogCursor( line, column, keepSelection );
	ScrollToLogCursor();
}

/*
================
idConsoleLocal::HandleLogSelectionKey
================
*/
bool idConsoleLocal::HandleLogSelectionKey( int key ) {
	if ( focus != CON_FOCUS_LOG || !idKeyInput::IsDown( K_CTRL ) || !idKeyInput::IsDown( K_SHIFT ) ) {
		return false;
	}

	switch ( key ) {
		case K_LEFTARROW:
			MoveLogCursorByChars( -1, true );
			return true;
		case K_RIGHTARROW:
			MoveLogCursorByChars( 1, true );
			return true;
		case K_UPARROW:
		case K_KP_UPARROW:
			MoveLogCursorByLines( -1, true );
			return true;
		case K_DOWNARROW:
		case K_KP_DOWNARROW:
			MoveLogCursorByLines( 1, true );
			return true;
		case K_PGUP:
			MoveLogCursorByLines( -GetScrollStep( 0 ), true );
			return true;
		case K_PGDN:
			MoveLogCursorByLines( GetScrollStep( 0 ), true );
			return true;
		case K_HOME:
			MoveLogCursorToBoundary( true, true, true );
			return true;
		case K_END:
			MoveLogCursorToBoundary( false, true, true );
			return true;
		default:
			return false;
	}
}

/*
================
idConsoleLocal::IsInputSelectionHit
================
*/
bool idConsoleLocal::IsInputSelectionHit( int cursor ) const {
	if ( !HasInputSelection() ) {
		return false;
	}

	int start, end;
	GetInputSelectionRange( start, end );
	return cursor >= start && cursor <= end;
}

/*
================
idConsoleLocal::IsLogSelectionHit
================
*/
bool idConsoleLocal::IsLogSelectionHit( int line, int column ) const {
	if ( !HasLogSelection() ) {
		return false;
	}

	int startLine, startColumn, endLine, endColumn;
	GetLogSelectionRange( startLine, startColumn, endLine, endColumn );
	return CompareLogPosition( line, column, startLine, startColumn ) >= 0 &&
		CompareLogPosition( line, column, endLine, endColumn ) <= 0;
}

/*
================
idConsoleLocal::ClearTextDragState
================
*/
void idConsoleLocal::ClearTextDragState( void ) {
	textDragPending = false;
	textDragging = false;
	textDragFromInput = false;
	textDragTargetInput = false;
	textDragSourceStart = 0;
	textDragSourceEnd = 0;
	textDragDropCursor = consoleField.GetCursor();
	textDragTextLength = 0;
	textDragText[0] = '\0';
}

/*
================
idConsoleLocal::BeginTextDrag
================
*/
bool idConsoleLocal::BeginTextDrag( bool fromInput ) {
	textDragTextLength = fromInput ? BuildInputSelectionText( textDragText, sizeof( textDragText ) ) :
		BuildLogSelectionText( textDragText, sizeof( textDragText ) );

	if ( textDragTextLength < 1 ) {
		ClearTextDragState();
		return false;
	}

	textDragging = true;
	textDragPending = false;
	textDragFromInput = fromInput;
	textDragTargetInput = false;
	inputSelecting = false;
	logSelecting = false;
	return true;
}

/*
================
idConsoleLocal::UpdateTextDragTarget
================
*/
void idConsoleLocal::UpdateTextDragTarget( void ) {
	float inputX, inputY, inputW, inputH;
	if ( !textDragging ) {
		return;
	}

	if ( GetInputAreaRect( inputX, inputY, inputW, inputH ) &&
		mouseX >= inputX && mouseX <= inputX + inputW &&
		mouseY >= inputY && mouseY <= inputY + inputH ) {
		textDragTargetInput = true;
		textDragDropCursor = GetInputCursorFromMouse();
	} else {
		textDragTargetInput = false;
	}
}

/*
================
idConsoleLocal::FinishTextDrag
================
*/
void idConsoleLocal::FinishTextDrag( void ) {
	if ( !textDragging ) {
		return;
	}

	if ( textDragTargetInput && textDragTextLength > 0 ) {
		int dropCursor = textDragDropCursor;

		if ( textDragFromInput ) {
			if ( dropCursor > textDragSourceStart && dropCursor < textDragSourceEnd ) {
				ClearTextDragState();
				return;
			}

			if ( dropCursor > textDragSourceStart ) {
				dropCursor -= textDragSourceEnd - textDragSourceStart;
			}

			DeleteInputRange( textDragSourceStart, textDragSourceEnd );
		}

		InsertInputTextAt( textDragText, dropCursor );
	}

	ClearTextDragState();
}

/*
================
idConsoleLocal::CompletionPopupEnabled
================
*/
bool idConsoleLocal::CompletionPopupEnabled( void ) const {
	return con_completionPopup.GetBool();
}

/*
================
idConsoleLocal::InvalidateCompletionState
================
*/
void idConsoleLocal::InvalidateCompletionState( void ) {
	completionCount = 0;
	completionSelection = 0;
	completionScroll = 0;
	completionSegmentStart = 0;
	completionSegmentEnd = 0;
	completionReplaceArgIndex = 0;
	completionReplaceOffset = 0;
	completionReplaceLength = 0;
	completionAppendSpace = false;
	completionPrependSlash = false;
	completionPopupVisible = false;
	completionSnapshotValid = false;
	completionSnapshotCursor = 0;
	completionScrollbarDragging = false;
	completionScrollbarHover = 0.0f;
	completionScrollbarDragOffset = 0.0f;
	completionSnapshotBuffer[0] = '\0';
	completionFuzzyNeedle[0] = '\0';
}

/*
================
idConsoleLocal::RebuildCompletionState
================
*/
void idConsoleLocal::RebuildCompletionState( void ) {
	// Keep the popup model current for rapid input events instead of waiting
	// for the next draw pass to rebuild completion candidates.
	InvalidateCompletionState();
	RefreshCompletionState();
}

/*
================
idConsoleLocal::DismissCompletionPopup
================
*/
void idConsoleLocal::DismissCompletionPopup( void ) {
	completionPopupVisible = false;
	completionScroll = 0;
	completionSnapshotValid = true;
	completionSnapshotCursor = consoleField.GetCursor();
	idStr::Copynz( completionSnapshotBuffer, consoleField.GetBuffer(), sizeof( completionSnapshotBuffer ) );
}

/*
================
idConsoleLocal::HasActiveCompletionPopup
================
*/
bool idConsoleLocal::HasActiveCompletionPopup( void ) const {
	return CompletionPopupEnabled() && completionPopupVisible && focus == CON_FOCUS_INPUT && !textDragging && completionCount > 0;
}

/*
================
idConsoleLocal::GetCompletionVisibleCount
================
*/
int idConsoleLocal::GetCompletionVisibleCount( void ) const {
	return idMath::ClampInt( 0, CON_COMPLETION_MAX_VISIBLE, completionCount );
}

/*
================
idConsoleLocal::ClampCompletionScroll
================
*/
void idConsoleLocal::ClampCompletionScroll( bool keepSelectionVisible ) {
	const int visibleCount = GetCompletionVisibleCount();
	int maxScroll = completionCount - visibleCount;
	if ( maxScroll < 0 ) {
		maxScroll = 0;
	}

	completionScroll = idMath::ClampInt( 0, maxScroll, completionScroll );
	if ( !keepSelectionVisible || visibleCount < 1 || completionCount < 1 ) {
		return;
	}

	completionSelection = idMath::ClampInt( 0, completionCount - 1, completionSelection );
	if ( completionSelection < completionScroll ) {
		completionScroll = completionSelection;
	} else if ( completionSelection >= completionScroll + visibleCount ) {
		completionScroll = completionSelection - visibleCount + 1;
	}
	completionScroll = idMath::ClampInt( 0, maxScroll, completionScroll );
}

/*
================
idConsoleLocal::MoveCompletionSelection
================
*/
void idConsoleLocal::MoveCompletionSelection( int delta ) {
	if ( completionCount < 1 || delta == 0 ) {
		return;
	}

	if ( delta < 0 ) {
		completionSelection = ( completionSelection + completionCount - 1 ) % completionCount;
	} else {
		completionSelection = ( completionSelection + 1 ) % completionCount;
	}
	ClampCompletionScroll( true );
}

/*
================
idConsoleLocal::CollectCompletionMatch
================
*/
bool idConsoleLocal::CollectCompletionMatch( const char *match ) {
	char token[MAX_EDIT_LINE];

	if ( match == NULL || match[0] == '\0' ) {
		return true;
	}

	if ( !ExtractCompletionCandidateToken( match, token, sizeof( token ) ) ) {
		return true;
	}

	for ( int i = 0; i < completionCount; ++i ) {
		if ( idStr::Icmp( completionMatches[i], token ) == 0 ) {
			return true;
		}
	}

	if ( completionCount >= CON_COMPLETION_MAX_MATCHES ) {
		return false;
	}

	idStr::Copynz( completionMatches[completionCount], token, sizeof( completionMatches[completionCount] ) );
	++completionCount;
	return true;
}

static int Con_CompareCompletionStrings( const void *lhs, const void *rhs ) {
	return idStr::Icmp( reinterpret_cast<const char *>( lhs ), reinterpret_cast<const char *>( rhs ) );
}

/*
================
idConsoleLocal::SortCompletionMatches
================
*/
void idConsoleLocal::SortCompletionMatches( void ) {
	qsort( completionMatches, completionCount, sizeof( completionMatches[0] ), Con_CompareCompletionStrings );
}

/*
================
idConsoleLocal::GetCompletionSegment
================
*/
void idConsoleLocal::GetCompletionSegment( int cursor, int &segmentStart, int &segmentEnd ) const {
	const char *buffer = consoleField.GetBuffer();
	const int len = consoleField.GetLength();

	cursor = idMath::ClampInt( 0, len, cursor );
	segmentStart = 0;
	segmentEnd = len;

	for ( int i = 0; i < cursor; ++i ) {
		if ( buffer[i] == ';' ) {
			segmentStart = i + 1;
		}
	}
	for ( int i = cursor; i < len; ++i ) {
		if ( buffer[i] == ';' ) {
			segmentEnd = i;
			break;
		}
	}
}

/*
================
idConsoleLocal::FindCompletionSegmentBoundary
================
*/
int idConsoleLocal::FindCompletionSegmentBoundary( int cursor, bool findStart ) const {
	int segmentStart, segmentEnd;
	GetCompletionSegment( cursor, segmentStart, segmentEnd );
	return findStart ? segmentStart : segmentEnd;
}

/*
================
idConsoleLocal::IsCurrentSegmentCompletionMatch
================
*/
bool idConsoleLocal::IsCurrentSegmentCompletionMatch( const char *match ) const {
	if ( match == NULL || match[0] == '\0' ) {
		return false;
	}

	const int matchLen = static_cast<int>( strlen( match ) );
	if ( matchLen != completionReplaceLength || completionReplaceOffset < 0 ) {
		return false;
	}

	const int bufferLen = consoleField.GetLength();
	if ( completionReplaceOffset + completionReplaceLength > bufferLen ) {
		return false;
	}

	return idStr::Icmpn( consoleField.GetBuffer() + completionReplaceOffset, match, matchLen ) == 0;
}

/*
================
idConsoleLocal::ShouldHideMatchedCompletionPopup
================
*/
bool idConsoleLocal::ShouldHideMatchedCompletionPopup( void ) const {
	if ( completionCount < 1 || completionSelection < 0 || completionSelection >= completionCount ) {
		return false;
	}

	if ( !IsCurrentSegmentCompletionMatch( completionMatches[completionSelection] ) ) {
		return false;
	}

	const int cursor = consoleField.GetCursor();
	if ( cursor != completionReplaceOffset + completionReplaceLength ) {
		return false;
	}

	const char next = consoleField.GetBuffer()[cursor];
	return next == '\0' || next <= ' ' || next == ';';
}

/*
================
idConsoleLocal::ResolveCompletionReplacementRange
================
*/
void idConsoleLocal::ResolveCompletionReplacementRange( const char *fullSegment, int fullLen, int relativeCursor, int segmentStart,
	int &replaceOffset, int &replaceLength, int &replaceArgIndex, bool &firstArg ) const {
	idLexer lexer;
	idToken token;
	int argIndex = 0;

	replaceOffset = segmentStart + idMath::ClampInt( 0, fullLen, relativeCursor );
	replaceLength = 0;
	replaceArgIndex = 0;
	firstArg = true;

	if ( fullSegment == NULL || fullLen <= 0 ) {
		return;
	}

	relativeCursor = idMath::ClampInt( 0, fullLen, relativeCursor );

	lexer.SetFlags( LEXFL_NOERRORS | LEXFL_NOWARNINGS | LEXFL_NOSTRINGCONCAT |
		LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWIPADDRESSES );
	lexer.LoadMemory( fullSegment, fullLen, "idConsoleLocal::ResolveCompletionReplacementRange" );

	while ( lexer.ReadToken( &token ) ) {
		const int tokenStart = idMath::ClampInt( 0, fullLen, lexer.GetLastWhiteSpaceEnd() );
		const int tokenEnd = idMath::ClampInt( tokenStart, fullLen, lexer.GetFileOffset() );

		if ( relativeCursor <= tokenStart ) {
			replaceOffset = segmentStart + relativeCursor;
			replaceLength = 0;
			replaceArgIndex = argIndex;
			firstArg = ( argIndex == 0 );
			return;
		}

		if ( relativeCursor > tokenEnd ) {
			++argIndex;
			continue;
		}

		replaceOffset = segmentStart + tokenStart;
		replaceLength = tokenEnd - tokenStart;
		replaceArgIndex = argIndex;
		firstArg = ( argIndex == 0 );

		if ( firstArg && replaceLength > 0 ) {
			const char firstChar = fullSegment[tokenStart];
			if ( firstChar == '\\' || firstChar == '/' ) {
				++replaceOffset;
				--replaceLength;
			}
		}
		return;
	}

	replaceArgIndex = argIndex;
	firstArg = ( argIndex == 0 );
}

/*
================
idConsoleLocal::GetCompletionCvarInfo
================
*/
bool idConsoleLocal::GetCompletionCvarInfo( const char *match, char *value, int valueSize, bool *modified ) const {
	if ( modified != NULL ) {
		*modified = false;
	}
	if ( value != NULL && valueSize > 0 ) {
		value[0] = '\0';
	}
	if ( match == NULL || match[0] == '\0' ) {
		return false;
	}

	idCVar *cvar = cvarSystem->Find( match );
	if ( cvar == NULL ) {
		return false;
	}

	if ( modified != NULL ) {
		*modified = cvar->IsModified();
	}
	if ( value != NULL && valueSize > 0 ) {
		const char *cvarValue = cvar->GetString();
		idStr::Copynz( value, ( cvarValue != NULL && cvarValue[0] != '\0' ) ? cvarValue : "\"\"", valueSize );
	}
	return true;
}

/*
================
idConsoleLocal::ExtractCompletionCandidateToken
================
*/
bool idConsoleLocal::ExtractCompletionCandidateToken( const char *candidateLine, char *token, int tokenSize ) const {
	idCmdArgs args;
	const char *candidateToken;

	if ( token != NULL && tokenSize > 0 ) {
		token[0] = '\0';
	}

	if ( candidateLine == NULL || candidateLine[0] == '\0' || token == NULL || tokenSize <= 0 ) {
		return false;
	}

	args.TokenizeString( candidateLine, false );
	if ( args.Argc() <= 0 ) {
		return false;
	}

	if ( completionReplaceArgIndex >= 0 && completionReplaceArgIndex < args.Argc() ) {
		candidateToken = args.Argv( completionReplaceArgIndex );
	} else if ( args.Argc() == 1 ) {
		candidateToken = args.Argv( 0 );
	} else {
		return false;
	}

	if ( candidateToken == NULL || candidateToken[0] == '\0' ) {
		return false;
	}

	if ( completionReplaceArgIndex == 0 && ( candidateToken[0] == '\\' || candidateToken[0] == '/' ) ) {
		++candidateToken;
	}

	if ( candidateToken[0] == '\0' ) {
		return false;
	}

	idStr::Copynz( token, candidateToken, tokenSize );
	return true;
}

/*
================
idConsoleLocal::RefreshCompletionState
================
*/
void idConsoleLocal::RefreshCompletionState( void ) {
	char prefixBuffer[MAX_EDIT_LINE];
	char fullSegment[MAX_EDIT_LINE];
	char currentToken[MAX_EDIT_LINE];
	char previousMatch[MAX_EDIT_LINE];
	bool appendSpace = false;
	bool usedFuzzyMatches = false;
	const char *buffer = consoleField.GetBuffer();
	const int cursor = consoleField.GetCursor();
	const int len = consoleField.GetLength();
	bool firstArg = false;

	if ( focus != CON_FOCUS_INPUT || textDragging ) {
		completionCount = 0;
		completionSelection = 0;
		completionScroll = 0;
		completionReplaceArgIndex = 0;
		completionReplaceOffset = cursor;
		completionReplaceLength = 0;
		completionAppendSpace = false;
		completionPrependSlash = false;
		completionPopupVisible = false;
		completionSnapshotValid = true;
		completionSnapshotCursor = cursor;
		idStr::Copynz( completionSnapshotBuffer, buffer, sizeof( completionSnapshotBuffer ) );
		return;
	}

	if ( completionSnapshotValid &&
		completionSnapshotCursor == cursor &&
		idStr::Icmp( completionSnapshotBuffer, buffer ) == 0 ) {
		return;
	}

	if ( completionCount > 0 && completionSelection >= 0 && completionSelection < completionCount ) {
		idStr::Copynz( previousMatch, completionMatches[completionSelection], sizeof( previousMatch ) );
	} else {
		previousMatch[0] = '\0';
	}

	completionCount = 0;
	completionSelection = 0;
	completionScroll = 0;
	completionReplaceArgIndex = 0;
	completionReplaceOffset = cursor;
	completionReplaceLength = 0;
	completionAppendSpace = false;
	completionPrependSlash = false;

	GetCompletionSegment( cursor, completionSegmentStart, completionSegmentEnd );
	const int prefixLength = idMath::ClampInt( 0, MAX_EDIT_LINE - 1, cursor - completionSegmentStart );
	memcpy( prefixBuffer, buffer + completionSegmentStart, prefixLength );
	prefixBuffer[prefixLength] = '\0';

	const int fullSegmentLength = idMath::ClampInt( 0, MAX_EDIT_LINE - 1, completionSegmentEnd - completionSegmentStart );
	memcpy( fullSegment, buffer + completionSegmentStart, fullSegmentLength );
	fullSegment[fullSegmentLength] = '\0';

	ResolveCompletionReplacementRange( fullSegment, fullSegmentLength, cursor - completionSegmentStart, completionSegmentStart,
		completionReplaceOffset, completionReplaceLength, completionReplaceArgIndex, firstArg );

	if ( completionReplaceLength > 0 &&
		completionReplaceOffset >= 0 &&
		completionReplaceOffset + completionReplaceLength <= len ) {
		const int tokenLength = idMath::ClampInt( 0, MAX_EDIT_LINE - 1, completionReplaceLength );
		memcpy( currentToken, buffer + completionReplaceOffset, tokenLength );
		currentToken[tokenLength] = '\0';
	} else {
		currentToken[0] = '\0';
	}

	idEditField::QueryCompletionMatches( prefixBuffer, &appendSpace, Con_CollectCompletionMatchCallback, this );

	if ( completionCount < 1 && currentToken[0] != '\0' && currentToken[1] != '\0' ) {
		consoleFuzzyCompletionState_t fuzzyState;

		completionCount = 0;
		fuzzyState.count = 0;
		idStr::Copynz( completionFuzzyNeedle, currentToken, sizeof( completionFuzzyNeedle ) );
		idStr::Copynz( fuzzyState.needle, completionFuzzyNeedle, sizeof( fuzzyState.needle ) );
		idEditField::QueryCompletionCandidates( prefixBuffer, Con_CollectFuzzyCompletionMatchCallback, &fuzzyState );

		if ( fuzzyState.count > 0 ) {
			qsort( fuzzyState.matches, fuzzyState.count, sizeof( fuzzyState.matches[0] ), Con_CompareFuzzyCompletionMatches );
			completionCount = Min( fuzzyState.count, CON_COMPLETION_MAX_MATCHES );
			for ( int i = 0; i < completionCount; ++i ) {
				idStr::Copynz( completionMatches[i], fuzzyState.matches[i].match, sizeof( completionMatches[i] ) );
			}
			appendSpace = ( completionCount == 1 );
			usedFuzzyMatches = true;
		} else {
			completionCount = 0;
		}
	}

	if ( completionCount < 1 ) {
		completionReplaceArgIndex = 0;
		completionReplaceOffset = cursor;
		completionReplaceLength = 0;
		completionAppendSpace = false;
		completionPrependSlash = false;
		completionPopupVisible = false;
		completionScroll = 0;
		completionSnapshotValid = true;
		completionSnapshotCursor = cursor;
		idStr::Copynz( completionSnapshotBuffer, buffer, sizeof( completionSnapshotBuffer ) );
		return;
	}

	completionAppendSpace = appendSpace;
	completionPrependSlash = ( completionSegmentStart == 0 && firstArg &&
		completionReplaceOffset == 0 && buffer[0] != '\\' && buffer[0] != '/' );
	if ( !usedFuzzyMatches ) {
		SortCompletionMatches();
	}

	if ( previousMatch[0] != '\0' ) {
		for ( int i = 0; i < completionCount; ++i ) {
			if ( idStr::Icmp( completionMatches[i], previousMatch ) == 0 ) {
				completionSelection = i;
				break;
			}
		}
	}

	if ( completionReplaceLength > 0 ) {
		for ( int i = 0; i < completionCount; ++i ) {
			if ( static_cast<int>( strlen( completionMatches[i] ) ) == completionReplaceLength &&
				idStr::Icmpn( buffer + completionReplaceOffset, completionMatches[i], completionReplaceLength ) == 0 ) {
				completionSelection = i;
				break;
			}
		}
	}

	if ( ShouldHideMatchedCompletionPopup() ) {
		completionCount = 0;
		completionSelection = 0;
		completionScroll = 0;
		completionReplaceArgIndex = 0;
		completionReplaceOffset = cursor;
		completionReplaceLength = 0;
		completionAppendSpace = false;
		completionPopupVisible = false;
		completionPrependSlash = false;
		completionSnapshotValid = true;
		completionSnapshotCursor = cursor;
		idStr::Copynz( completionSnapshotBuffer, buffer, sizeof( completionSnapshotBuffer ) );
		return;
	}

	completionAppendSpace = ( completionCount == 1 );
	ClampCompletionScroll( true );
	completionPopupVisible = true;
	completionSnapshotValid = true;
	completionSnapshotCursor = cursor;
	idStr::Copynz( completionSnapshotBuffer, buffer, sizeof( completionSnapshotBuffer ) );
}

/*
================
idConsoleLocal::ApplySelectedCompletion
================
*/
void idConsoleLocal::ApplySelectedCompletion( int direction ) {
	char completedBuffer[MAX_EDIT_LINE];
	if ( !CompletionPopupEnabled() ) {
		consoleField.AutoComplete();
		InvalidateCompletionState();
		return;
	}

	RefreshCompletionState();
	if ( completionCount < 1 ) {
		consoleField.AutoComplete();
		InvalidateCompletionState();
		return;
	}

	if ( direction < 0 ) {
		if ( !IsCurrentSegmentCompletionMatch( completionMatches[completionSelection] ) ) {
			completionSelection = completionCount - 1;
		} else if ( completionCount > 1 ) {
			completionSelection = ( completionSelection + completionCount - 1 ) % completionCount;
		}
	} else if ( direction > 0 && IsCurrentSegmentCompletionMatch( completionMatches[completionSelection] ) && completionCount > 1 ) {
		completionSelection = ( completionSelection + 1 ) % completionCount;
	}

	const char *selectedMatch = completionMatches[completionSelection];
	const char *buffer = consoleField.GetBuffer();
	const int matchLen = static_cast<int>( strlen( selectedMatch ) );
	const int len = consoleField.GetLength();
	int replaceOffset = completionReplaceOffset;
	int replaceLength = completionReplaceLength;
	int suffixOffset = replaceOffset + replaceLength;
	int outLen = 0;

	if ( replaceOffset < 0 ) {
		replaceOffset = 0;
	} else if ( replaceOffset > len ) {
		replaceOffset = len;
	}
	if ( replaceLength < 0 ) {
		replaceLength = 0;
	}
	suffixOffset = replaceOffset + replaceLength;
	if ( suffixOffset > len ) {
		suffixOffset = len;
	}

	bool addSpace = false;
	const char next = buffer[suffixOffset];
	if ( completionAppendSpace && ( next == '\0' || next == ';' || next > ' ' ) ) {
		addSpace = true;
	}

	if ( completionPrependSlash && outLen < MAX_EDIT_LINE - 1 ) {
		completedBuffer[outLen++] = '/';
	}

	int copyLen = Min( replaceOffset, MAX_EDIT_LINE - 1 - outLen );
	if ( copyLen > 0 ) {
		memcpy( completedBuffer + outLen, buffer, copyLen );
		outLen += copyLen;
	}

	copyLen = Min( matchLen, MAX_EDIT_LINE - 1 - outLen );
	if ( copyLen > 0 ) {
		memcpy( completedBuffer + outLen, selectedMatch, copyLen );
		outLen += copyLen;
	}
	if ( addSpace && outLen < MAX_EDIT_LINE - 1 ) {
		completedBuffer[outLen++] = ' ';
	}

	copyLen = Min( len - suffixOffset, MAX_EDIT_LINE - 1 - outLen );
	if ( copyLen > 0 ) {
		memcpy( completedBuffer + outLen, buffer + suffixOffset, copyLen );
		outLen += copyLen;
	}
	completedBuffer[outLen] = '\0';

	consoleField.SetBuffer( completedBuffer );
	consoleField.SetCursor( ( completionPrependSlash ? 1 : 0 ) + replaceOffset + matchLen + ( addSpace ? 1 : 0 ) );
	consoleField.ClampCursorAndScroll();
	ClearInputSelection();
	focus = CON_FOCUS_INPUT;
	ClearLogSelection();
	InvalidateCompletionState();
}

/*
================
idConsoleLocal::GetCompletionPopupGeometry
================
*/
bool idConsoleLocal::GetCompletionPopupGeometry( float &popupX, float &popupY, float &popupW, float &popupH, int &first, int &visibleCount ) {
	if ( !CompletionPopupEnabled() ) {
		completionPopupVisible = false;
		return false;
	}

	RefreshCompletionState();
	if ( completionCount < 1 || textDragging || focus != CON_FOCUS_INPUT ) {
		completionPopupVisible = false;
		return false;
	}

	completionPopupVisible = true;
	visibleCount = GetCompletionVisibleCount();
	ClampCompletionScroll( false );
	first = completionScroll;

	int longest = 8;
	for ( int i = 0; i < completionCount; ++i ) {
		char cvarValue[MAX_EDIT_LINE];
		bool modified;
		int matchLen = strlen( completionMatches[i] );
		if ( GetCompletionCvarInfo( completionMatches[i], cvarValue, sizeof( cvarValue ), &modified ) ) {
			matchLen += 3 + strlen( cvarValue );
		}
		if ( matchLen > longest ) {
			longest = matchLen;
		}
	}

	float consoleX, consoleY, consoleW, consoleH;
	GetConsoleRect( consoleX, consoleY, consoleW, consoleH );
	const int maxChars = Max( 8, idMath::FtoiFast( ( consoleW - 6.0f * smallCharWidth ) / smallCharWidth ) );
	longest = Min( longest, maxChars );

	float scrollbarReserve = 0.0f;
	if ( completionCount > visibleCount ) {
		scrollbarReserve = CON_SCROLLBAR_BASE_WIDTH + CON_SCROLLBAR_HOVER_GROW + CON_SCROLLBAR_SIDE_PAD * 2.0f + 1.0f;
	}

	popupW = ( longest + 2 ) * smallCharWidth + scrollbarReserve;
	popupH = visibleCount * SMALLCHAR_HEIGHT + 4.0f;
	popupX = 2.0f * smallCharWidth;
	if ( popupX + popupW > consoleX + consoleW - smallCharWidth ) {
		popupX = consoleX + consoleW - popupW - smallCharWidth;
	}
	if ( popupX < consoleX + smallCharWidth ) {
		popupX = consoleX + smallCharWidth;
	}

	popupY = vislines - SMALLCHAR_HEIGHT * GetFooterRows() - popupH - 4.0f;
	if ( popupY < 0.0f ) {
		popupY = 0.0f;
	}
	return true;
}

/*
================
idConsoleLocal::GetCompletionScrollbarGeometry
================
*/
bool idConsoleLocal::GetCompletionScrollbarGeometry( float hoverFrac, float &trackX, float &trackY, float &trackW, float &trackH, float &thumbY, float &thumbH, float *hitX, float *hitW ) {
	float popupX, popupY, popupW, popupH;
	int first, visibleCount;

	if ( !GetCompletionPopupGeometry( popupX, popupY, popupW, popupH, first, visibleCount ) ) {
		return false;
	}
	if ( completionCount <= visibleCount || visibleCount < 1 ) {
		return false;
	}

	const float contentY = popupY + 2.0f;
	const float contentH = visibleCount * SMALLCHAR_HEIGHT;
	GetScrollbarFrameGeometry( popupX + 1.0f, popupW - 2.0f, contentY, contentH, hoverFrac, trackX, trackY, trackW, trackH, hitX, hitW );

	const int maxScroll = completionCount - visibleCount;
	const float displayFracValue = ( maxScroll <= 0 ) ? 0.0f : static_cast<float>( first ) / static_cast<float>( maxScroll );
	GetScrollbarThumbGeometry( contentY, contentH, visibleCount, completionCount, displayFracValue, thumbY, thumbH );
	return true;
}

/*
================
idConsoleLocal::GetCompletionSelectionFromMouse
================
*/
bool idConsoleLocal::GetCompletionSelectionFromMouse( int &selection ) {
	float popupX, popupY, popupW, popupH;
	int first, visibleCount;

	if ( !GetCompletionPopupGeometry( popupX, popupY, popupW, popupH, first, visibleCount ) ) {
		return false;
	}
	if ( mouseX < popupX || mouseX > popupX + popupW || mouseY < popupY + 2.0f || mouseY >= popupY + 2.0f + visibleCount * SMALLCHAR_HEIGHT ) {
		return false;
	}

	const int rowIndex = idMath::FtoiFast( ( mouseY - ( popupY + 2.0f ) ) / SMALLCHAR_HEIGHT );
	if ( rowIndex < 0 || rowIndex >= visibleCount ) {
		return false;
	}

	selection = first + rowIndex;
	return true;
}

/*
================
idConsoleLocal::SetCompletionScrollFraction
================
*/
void idConsoleLocal::SetCompletionScrollFraction( float frac ) {
	const int visibleCount = GetCompletionVisibleCount();
	const int maxScroll = completionCount - visibleCount;
	if ( maxScroll <= 0 ) {
		completionScroll = 0;
		return;
	}

	completionScroll = idMath::FtoiFast( idMath::ClampFloat( 0.0f, 1.0f, frac ) * maxScroll + 0.5f );
	ClampCompletionScroll( false );
}

/*
================
idConsoleLocal::UpdateCompletionScrollbarDrag
================
*/
void idConsoleLocal::UpdateCompletionScrollbarDrag( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;

	if ( !completionScrollbarDragging || !idKeyInput::IsDown( K_MOUSE1 ) ) {
		return;
	}

	if ( !GetCompletionScrollbarGeometry( completionScrollbarHover, trackX, trackY, trackW, trackH, thumbY, thumbH ) ) {
		completionScrollbarDragging = false;
		return;
	}

	if ( trackH <= thumbH ) {
		completionScroll = 0;
		return;
	}

	const float frac = ( mouseY - completionScrollbarDragOffset - trackY ) / ( trackH - thumbH );
	SetCompletionScrollFraction( frac );
}

/*
================
idConsoleLocal::UpdateCompletionScrollbarHover
================
*/
void idConsoleLocal::UpdateCompletionScrollbarHover( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float hitX, hitW;
	bool hot = false;

	if ( !idKeyInput::IsDown( K_MOUSE1 ) ) {
		completionScrollbarDragging = false;
	}

	ClampMouseToConsole();

	if ( GetCompletionScrollbarGeometry( completionScrollbarHover, trackX, trackY, trackW, trackH, thumbY, thumbH, &hitX, &hitW ) ) {
		if ( completionScrollbarDragging ||
			( mouseX >= hitX && mouseX <= hitX + hitW && mouseY >= trackY && mouseY <= trackY + trackH ) ) {
			hot = true;
		}
	}

	UpdateScrollbarHoverValue( completionScrollbarHover, completionScrollbarDragging, hot );
	UpdateCompletionScrollbarDrag();
}

/*
================
idConsoleLocal::GetScrollStep
================
*/
int idConsoleLocal::GetScrollStep( int lines ) const {
	if ( lines > 0 ) {
		return lines;
	}

	const int maxLines = Max( 1, GetLogRowCount() - 1 );
	if ( lines == 0 ) {
		return maxLines;
	}

	int step = con_scrollLines.GetInteger();
	if ( step < 1 ) {
		step = 1;
	}
	if ( step > maxLines ) {
		step = maxLines;
	}
	return step;
}

/*
================
idConsoleLocal::PageUp
================
*/
void idConsoleLocal::PageUp( void ) {
	display -= GetScrollStep();
	ClampDisplay();
}

/*
================
idConsoleLocal::PageDown
================
*/
void idConsoleLocal::PageDown( void ) {
	display += GetScrollStep();
	ClampDisplay();
}

/*
================
idConsoleLocal::Top
================
*/
void idConsoleLocal::Top( void ) {
	display = 0;
	ClampDisplay();
}

/*
================
idConsoleLocal::Bottom
================
*/
void idConsoleLocal::Bottom( void ) {
	display = current;
	ClampDisplay();
}


/*
=============================================================================

CONSOLE LINE EDITING

==============================================================================
*/

/*
====================
idConsoleLocal::InputKeyDownEvent
====================
*/
bool idConsoleLocal::InputKeyDownEvent( int key ) {
	const int lowerKey = ( key >= 0 && key < 128 ) ? tolower( key ) : key;

	if ( HandleLogSelectionKey( key ) ) {
		return true;
	}

	if ( HasActiveCompletionPopup() ) {
		switch ( key ) {
			case K_UPARROW:
			case K_KP_UPARROW:
			case K_MWHEELUP:
				MoveCompletionSelection( -1 );
				return true;
			case K_DOWNARROW:
			case K_KP_DOWNARROW:
			case K_MWHEELDOWN:
				MoveCompletionSelection( 1 );
				return true;
			case K_PGUP:
				completionSelection = Max( 0, completionSelection - CON_COMPLETION_MAX_VISIBLE );
				ClampCompletionScroll( true );
				return true;
			case K_PGDN:
				completionSelection = Min( completionCount - 1, completionSelection + CON_COMPLETION_MAX_VISIBLE );
				ClampCompletionScroll( true );
				return true;
			case K_HOME:
				completionSelection = 0;
				ClampCompletionScroll( true );
				return true;
			case K_END:
				completionSelection = completionCount - 1;
				ClampCompletionScroll( true );
				return true;
			case K_ENTER:
			case K_KP_ENTER:
				ApplySelectedCompletion( 0 );
				DismissCompletionPopup();
				return true;
			default:
				break;
		}

		if ( idKeyInput::IsDown( K_CTRL ) ) {
			if ( lowerKey == 'p' ) {
				MoveCompletionSelection( -1 );
				return true;
			}
			if ( lowerKey == 'n' ) {
				MoveCompletionSelection( 1 );
				return true;
			}
		}
	}

	if ( idKeyInput::IsDown( K_CTRL ) ) {
		switch ( lowerKey ) {
			case 'a':
				if ( focus == CON_FOCUS_LOG ) {
					SelectAllLog();
				} else {
					SelectAllInput();
				}
				return true;
			case 'c':
				CopySelection();
				return true;
			case 'v':
				PasteClipboardToInput();
				return true;
			case 'x':
				CutInputSelection();
				return true;
			default:
				break;
		}
	}

	if ( key == K_INS || key == K_KP_INS ) {
		if ( idKeyInput::IsDown( K_SHIFT ) ) {
			PasteClipboardToInput();
		} else {
			idKeyInput::SetOverstrikeMode( !idKeyInput::GetOverstrikeMode() );
		}
		return true;
	}

	if ( key == K_TAB ) {
		ApplySelectedCompletion( idKeyInput::IsDown( K_SHIFT ) ? -1 : 1 );
		return true;
	}

	switch ( key ) {
		case K_BACKSPACE:
			if ( HasInputSelection() ) {
				DeleteInputSelection();
			} else if ( consoleField.GetCursor() > 0 ) {
				DeleteInputRange( consoleField.GetCursor() - 1, consoleField.GetCursor() );
			}
			return true;
		case K_DEL:
			if ( HasInputSelection() ) {
				DeleteInputSelection();
			} else if ( consoleField.GetCursor() < consoleField.GetLength() ) {
				DeleteInputRange( consoleField.GetCursor(), consoleField.GetCursor() + 1 );
			}
			return true;
		case K_LEFTARROW:
			if ( idKeyInput::IsDown( K_SHIFT ) ) {
				SetInputCursor( idKeyInput::IsDown( K_CTRL ) ? SeekWordCursor( consoleField.GetCursor(), -1 ) : consoleField.GetCursor() - 1, true );
			} else if ( HasInputSelection() ) {
				int start, end;
				GetInputSelectionRange( start, end );
				SetInputCursor( start, false );
			} else {
				SetInputCursor( idKeyInput::IsDown( K_CTRL ) ? SeekWordCursor( consoleField.GetCursor(), -1 ) : consoleField.GetCursor() - 1, false );
			}
			return true;
		case K_RIGHTARROW:
			if ( idKeyInput::IsDown( K_SHIFT ) ) {
				SetInputCursor( idKeyInput::IsDown( K_CTRL ) ? SeekWordCursor( consoleField.GetCursor(), 1 ) : consoleField.GetCursor() + 1, true );
			} else if ( HasInputSelection() ) {
				int start, end;
				GetInputSelectionRange( start, end );
				SetInputCursor( end, false );
			} else {
				SetInputCursor( idKeyInput::IsDown( K_CTRL ) ? SeekWordCursor( consoleField.GetCursor(), 1 ) : consoleField.GetCursor() + 1, false );
			}
			return true;
		case K_HOME:
			SetInputCursor( 0, idKeyInput::IsDown( K_SHIFT ) );
			return true;
		case K_END:
			SetInputCursor( consoleField.GetLength(), idKeyInput::IsDown( K_SHIFT ) );
			return true;
		default:
			return false;
	}
}

/*
====================
idConsoleLocal::MouseMoveEvent
====================
*/
void idConsoleLocal::MouseMoveEvent( int dx, int dy ) {
	int line, column;

	ClampMouseToConsole();
	mouseX += dx;
	mouseY += dy;
	ClampMouseToConsole();

	if ( scrollbarDragging ) {
		UpdateScrollbarDrag();
		return;
	}
	if ( completionScrollbarDragging ) {
		UpdateCompletionScrollbarDrag();
		return;
	}

	if ( textDragPending && idKeyInput::IsDown( K_MOUSE1 ) ) {
		const float moveX = idMath::Fabs( mouseX - textDragStartMouseX );
		const float moveY = idMath::Fabs( mouseY - textDragStartMouseY );
		if ( moveX >= CON_TEXT_DRAG_THRESHOLD || moveY >= CON_TEXT_DRAG_THRESHOLD ) {
			BeginTextDrag( textDragFromInput );
		}
	}

	if ( textDragging ) {
		UpdateTextDragTarget();
		return;
	}

	if ( inputSelecting && idKeyInput::IsDown( K_MOUSE1 ) ) {
		SetInputCursor( GetInputCursorFromMouse(), true );
	}
	if ( logSelecting && idKeyInput::IsDown( K_MOUSE1 ) && GetLogPositionFromMouse( line, column ) ) {
		SetLogCursor( line, column, true );
	}

	UpdateScrollbarDrag();
}

/*
====================
idConsoleLocal::MouseKeyEvent
====================
*/
bool idConsoleLocal::MouseKeyEvent( int key, bool down ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float hitX, hitW;
	float inputX, inputY, inputW, inputH;
	int completionHit;
	int line, column;

	if ( !down ) {
		if ( key == K_MOUSE1 ) {
			scrollbarDragging = false;
			completionScrollbarDragging = false;
			inputSelecting = false;
			logSelecting = false;
			if ( textDragging ) {
				FinishTextDrag();
			} else if ( textDragPending ) {
				if ( textDragFromInput ) {
					SetInputCursor( GetInputCursorFromMouse(), false );
				} else if ( GetLogPositionFromMouse( line, column ) ) {
					SetLogCursor( line, column, false );
				}
				ClearTextDragState();
			}
		}
		return key >= K_MOUSE1 && key <= K_MOUSE5;
	}

	if ( key < K_MOUSE1 || key > K_MOUSE5 ) {
		return false;
	}

	ClampMouseToConsole();

	if ( key == K_MOUSE1 &&
		GetCompletionScrollbarGeometry( completionScrollbarHover, trackX, trackY, trackW, trackH, thumbY, thumbH, &hitX, &hitW ) &&
		mouseX >= hitX && mouseX <= hitX + hitW && mouseY >= trackY && mouseY <= trackY + trackH ) {
		ClearTextDragState();
		scrollbarDragging = false;
		completionScrollbarDragging = true;
		inputSelecting = false;
		logSelecting = false;
		completionScrollbarDragOffset = ( mouseY >= thumbY && mouseY <= thumbY + thumbH ) ? ( mouseY - thumbY ) : ( thumbH * 0.5f );
		UpdateCompletionScrollbarDrag();
		return true;
	}

	if ( key == K_MOUSE1 && GetCompletionSelectionFromMouse( completionHit ) ) {
		ClearTextDragState();
		scrollbarDragging = false;
		completionScrollbarDragging = false;
		inputSelecting = false;
		logSelecting = false;
		completionSelection = completionHit;
		ApplySelectedCompletion( 0 );
		DismissCompletionPopup();
		return true;
	}

	if ( key == K_MOUSE1 &&
		GetScrollbarGeometry( scrollbarHover, trackX, trackY, trackW, trackH, thumbY, thumbH, &hitX, &hitW ) &&
		mouseX >= hitX && mouseX <= hitX + hitW && mouseY >= trackY && mouseY <= trackY + trackH ) {
		ClearTextDragState();
		scrollbarDragging = true;
		inputSelecting = false;
		logSelecting = false;
		scrollbarDragOffset = ( mouseY >= thumbY && mouseY <= thumbY + thumbH ) ? ( mouseY - thumbY ) : ( thumbH * 0.5f );
		UpdateScrollbarDrag();
		return true;
	}

	if ( key == K_MOUSE1 && GetInputAreaRect( inputX, inputY, inputW, inputH ) &&
		mouseY >= inputY && mouseY <= inputY + inputH && mouseX >= 0.0f && mouseX <= SCREEN_WIDTH ) {
		const int inputCursor = GetInputCursorFromMouse();

		if ( !idKeyInput::IsDown( K_SHIFT ) && IsInputSelectionHit( inputCursor ) ) {
			GetInputSelectionRange( textDragSourceStart, textDragSourceEnd );
			textDragPending = true;
			textDragFromInput = true;
			textDragStartMouseX = mouseX;
			textDragStartMouseY = mouseY;
			inputSelecting = false;
			logSelecting = false;
			focus = CON_FOCUS_INPUT;
			return true;
		}

		ClearTextDragState();
		SetInputCursor( inputCursor, idKeyInput::IsDown( K_SHIFT ) );
		if ( !idKeyInput::IsDown( K_SHIFT ) ) {
			ClearLogSelection();
		}
		inputSelecting = true;
		logSelecting = false;
		return true;
	}

	if ( key == K_MOUSE1 && GetLogPositionFromMouse( line, column ) ) {
		if ( !idKeyInput::IsDown( K_SHIFT ) && IsLogSelectionHit( line, column ) ) {
			textDragPending = true;
			textDragFromInput = false;
			textDragStartMouseX = mouseX;
			textDragStartMouseY = mouseY;
			inputSelecting = false;
			logSelecting = false;
			return true;
		}

		ClearTextDragState();
		ClearInputSelection();
		SetLogCursor( line, column, idKeyInput::IsDown( K_SHIFT ) && focus == CON_FOCUS_LOG );
		inputSelecting = false;
		logSelecting = true;
		return true;
	}

	return true;
}

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

	if ( key >= K_F1 && key <= K_F12 ) {
		idKeyInput::ExecKeyBinding( key );
		return;
	}

	if ( key == 'l' && idKeyInput::IsDown( K_CTRL ) ) {
		Clear();
		return;
	}

	if ( key == K_ENTER || key == K_KP_ENTER ) {
		if ( HasActiveCompletionPopup() ) {
			ApplySelectedCompletion( 0 );
			DismissCompletionPopup();
			return;
		}

		common->Printf( "]%s\n", consoleField.GetBuffer() );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, consoleField.GetBuffer() );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );

		historyEditLines[nextHistoryLine % COMMAND_HISTORY] = consoleField;
		nextHistoryLine++;
		historyLine = nextHistoryLine;
		SaveCommandHistory();

		consoleField.Clear();
		consoleField.SetWidthInChars( lineWidth );
		ClearInputSelection();
		ClearLogSelection();
		focus = CON_FOCUS_INPUT;
		RebuildCompletionState();

		session->UpdateScreen();
		return;
	}

	if ( InputKeyDownEvent( key ) ) {
		return;
	}

	if ( ( key == K_UPARROW ) ||
		 ( ( tolower( key ) == 'p' ) && idKeyInput::IsDown( K_CTRL ) ) ) {
		if ( nextHistoryLine - historyLine < COMMAND_HISTORY && historyLine > 0 ) {
			historyLine--;
		}
		consoleField = historyEditLines[historyLine % COMMAND_HISTORY];
		consoleField.SetWidthInChars( lineWidth );
		ClearInputSelection();
		ClearLogSelection();
		focus = CON_FOCUS_INPUT;
		RebuildCompletionState();
		return;
	}

	if ( ( key == K_DOWNARROW ) ||
		 ( ( tolower( key ) == 'n' ) && idKeyInput::IsDown( K_CTRL ) ) ) {
		if ( historyLine == nextHistoryLine ) {
			return;
		}
		historyLine++;
		consoleField = historyEditLines[historyLine % COMMAND_HISTORY];
		consoleField.SetWidthInChars( lineWidth );
		ClearInputSelection();
		ClearLogSelection();
		focus = CON_FOCUS_INPUT;
		RebuildCompletionState();
		return;
	}

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
	if ( key == K_HOME && idKeyInput::IsDown( K_CTRL ) ) {
		Top();
		return;
	}
	if ( key == K_END && idKeyInput::IsDown( K_CTRL ) ) {
		Bottom();
		return;
	}

	consoleField.KeyDownEvent( key );
	RebuildCompletionState();
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
	fracTime = common->GetPresentationTime();
}

/*
==============
UpdateDisplayFraction

Scrolls the console up or down based on conspeed
==============
*/
void idConsoleLocal::UpdateDisplayFraction( void ) {
	if ( con_speed.GetFloat() <= 0.1f ) {
		fracTime = common->GetPresentationTime();
		displayFrac = finalFrac;
		return;
	}

	// scroll towards the destination height
	if ( finalFrac < displayFrac ) {
		displayFrac -= con_speed.GetFloat() * ( common->GetPresentationTime() - fracTime ) * 0.001f;
		if ( finalFrac > displayFrac ) {
			displayFrac = finalFrac;
		}
		fracTime = common->GetPresentationTime();
	} else if ( finalFrac > displayFrac ) {
		displayFrac += con_speed.GetFloat() * ( common->GetPresentationTime() - fracTime ) * 0.001f;
		if ( finalFrac < displayFrac ) {
			displayFrac = finalFrac;
		}
		fracTime = common->GetPresentationTime();
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

	if ( session != NULL && session->IsMainMenuIntroPlaying() ) {
		if ( keyCatching ) {
			Close();
			Sys_GrabMouseCursor( false );
			cvarSystem->SetCVarBool( "ui_chat", false );
		}
		return consoleKey;
	}

	// When disabled, keep the legacy retail requirement to hold Ctrl+Alt while
	// pressing the console key to open the console.
	if ( !keyCatching && !con_allowConsole.GetBool() ) {
		if ( !idKeyInput::IsDown( K_CTRL ) || !idKeyInput::IsDown( K_ALT ) ) {
			consoleKey = false;
		}
	}

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
			focus = CON_FOCUS_INPUT;
			mouseInitialized = false;
			scrollbarDragging = false;
			completionScrollbarDragging = false;
			ClearInputSelection();
			ClearLogSelection();
			ClearTextDragState();
			InvalidateCompletionState();
			// Holding Shift keeps the reduced-open behavior without exceeding the configured console height.
			SetDisplayFraction( GetOpenFraction( idKeyInput::IsDown( K_SHIFT ) ) );
			cvarSystem->SetCVarBool( "ui_chat", true );
			Sys_GrabMouseCursor( false );
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
			InsertInputChar( event->evValue );
		}
		return true;
	}

	if ( event->evType == SE_MOUSE ) {
		MouseMoveEvent( event->evValue, event->evValue2 );
		return true;
	}

	if ( event->evType == SE_KEY ) {
		if ( event->evValue >= K_MOUSE1 && event->evValue <= K_MOUSE5 ) {
			return MouseKeyEvent( event->evValue, event->evValue2 != 0 );
		}

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
		times[current % NUM_CON_TIMES] = common->GetPresentationTime();
	}

	x = 0;
	if ( display == current ) {
		display++;
		displayLine = static_cast<float>( display );
	}
	current++;
	if ( current - display >= CON_MAX_TOTAL_LINES ) {
		display = current - CON_MAX_TOTAL_LINES + 1;
		displayLine = static_cast<float>( display );
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


void idConsoleLocal::DrawSolidRect( float x, float y, float w, float h, const idVec4 &color, float alphaScale ) const {
	if ( w <= 0.0f || h <= 0.0f ) {
		return;
	}

	renderSystem->SetColor4( color[0], color[1], color[2], color[3] * alphaScale );
	renderSystem->DrawStretchPic( x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, whiteShader );
}

void idConsoleLocal::DrawScrollbarVisual( float trackX, float trackY, float trackW, float trackH, float thumbY, float thumbH, float hoverFrac, float alphaScale, const idVec4 &lineColor ) const {
	idVec4 trackColor;
	idVec4 thumbColor;

	trackColor = lineColor;
	trackColor[3] = 0.14f + hoverFrac * 0.08f;
	Con_LightenColor( lineColor, 0.18f + hoverFrac * 0.3f, thumbColor );
	thumbColor[3] = 0.6f + hoverFrac * 0.2f;

	DrawSolidRect( trackX, trackY, trackW, trackH, trackColor, alphaScale );
	DrawSolidRect( trackX, thumbY, trackW, thumbH, thumbColor, alphaScale );
}

void idConsoleLocal::DrawInputSelection( float x, float y, int prestep, int drawLen ) const {
	if ( !HasInputSelection() ) {
		return;
	}

	int start, end;
	GetInputSelectionRange( start, end );
	const int visibleStart = Max( start, prestep );
	const int visibleEnd = Min( end, prestep + drawLen );
	if ( visibleEnd <= visibleStart ) {
		return;
	}

	DrawSolidRect( x + ( visibleStart - prestep ) * smallCharWidth, y,
		( visibleEnd - visibleStart ) * smallCharWidth, SMALLCHAR_HEIGHT, kConsoleSelectionColor );
}

void idConsoleLocal::DrawLogSelectionRow( int line, float y ) const {
	if ( !HasLogSelection() ) {
		return;
	}

	int startLine, startColumn, endLine, endColumn;
	GetLogSelectionRange( startLine, startColumn, endLine, endColumn );
	if ( line < startLine || line > endLine ) {
		return;
	}

	const int segmentStart = ( line == startLine ) ? startColumn : 0;
	const int segmentEnd = ( line == endLine ) ? endColumn : lineWidth;
	if ( segmentEnd <= segmentStart ) {
		return;
	}

	DrawSolidRect( ( segmentStart + 1 ) * smallCharWidth, y,
		( segmentEnd - segmentStart ) * smallCharWidth, SMALLCHAR_HEIGHT, kConsoleSelectionColor );
}

void idConsoleLocal::DrawInputText( float x, float y, bool showCursor ) const {
	const char *buffer = consoleField.GetBuffer();
	int prestep, drawLen;
	GetInputDrawInfo( prestep, drawLen );
	const int safeDrawLen = Max( 0, Min( drawLen, MAX_EDIT_LINE - 1 ) );
	char visible[MAX_EDIT_LINE];

	memcpy( visible, buffer + prestep, safeDrawLen );
	visible[safeDrawLen] = '\0';

	DrawInputSelection( x, y, prestep, safeDrawLen );
	Con_DrawSmallStringExt( x, y, visible, colorWhite, false );

	if ( !showCursor || ( ( com_ticNumber >> 4 ) & 1 ) != 0 ) {
		return;
	}

	const int cursorChar = idKeyInput::GetOverstrikeMode() ? 11 : 10;
	Con_DrawSmallChar( x + ( consoleField.GetCursor() - prestep ) * smallCharWidth, y, cursorChar );
}

void idConsoleLocal::DrawInputDropCursor( float x, float y ) const {
	if ( !textDragging || !textDragTargetInput ) {
		return;
	}

	int prestep, drawLen;
	GetInputDrawInfo( prestep, drawLen );
	int dropCursor = textDragDropCursor;
	dropCursor = idMath::ClampInt( prestep, prestep + drawLen, dropCursor );

	idVec4 accent = kConsoleBorderColor;
	accent[3] = 0.9f;
	DrawSolidRect( x + ( dropCursor - prestep ) * smallCharWidth, y, 2.0f, SMALLCHAR_HEIGHT, accent );
}

void idConsoleLocal::DrawCompletionPopup( void ) {
	float popupX, popupY, popupW, popupH;
	int first, visibleCount;

	if ( !GetCompletionPopupGeometry( popupX, popupY, popupW, popupH, first, visibleCount ) ) {
		return;
	}

	float trackX = 0.0f, trackY = 0.0f, trackW = 0.0f, trackH = 0.0f;
	float thumbY = 0.0f, thumbH = 0.0f;
	const bool hasScrollbar = GetCompletionScrollbarGeometry( completionScrollbarHover, trackX, trackY, trackW, trackH, thumbY, thumbH );
	const float textStartX = popupX + smallCharWidth;
	const float textRightX = hasScrollbar ? ( trackX - CON_SCROLLBAR_SIDE_PAD ) : ( popupX + popupW - smallCharWidth );
	const float rowWidth = Max( 1.0f, textRightX - ( popupX + 1.0f ) );
	const int maxDrawChars = Max( 1, idMath::FtoiFast( ( textRightX - textStartX ) / smallCharWidth ) );

	idVec4 popupBorder;
	idVec4 popupBackground = kConsoleBackgroundColor;
	idVec4 selectionColor = kConsoleSelectionColor;
	Con_LightenColor( kConsoleBorderColor, 0.25f, popupBorder );
	popupBorder[3] = 0.7f;
	popupBackground[3] = 0.92f;
	selectionColor[3] = 0.85f;

	DrawSolidRect( popupX, popupY, popupW, popupH, popupBackground );
	DrawSolidRect( popupX, popupY, popupW, 1.0f, popupBorder );
	DrawSolidRect( popupX, popupY + popupH - 1.0f, popupW, 1.0f, popupBorder );
	DrawSolidRect( popupX, popupY, 1.0f, popupH, popupBorder );
	DrawSolidRect( popupX + popupW - 1.0f, popupY, 1.0f, popupH, popupBorder );

	for ( int i = 0; i < visibleCount; ++i ) {
		const char *match = completionMatches[first + i];
		const float rowY = popupY + 2.0f + i * SMALLCHAR_HEIGHT;
		char cvarValue[MAX_EDIT_LINE];
		bool modified = false;
		const bool isCvar = GetCompletionCvarInfo( match, cvarValue, sizeof( cvarValue ), &modified );
		int nameChars = Min( static_cast<int>( strlen( match ) ), maxDrawChars );
		int valueChars = 0;
		float valueX = textRightX;

		if ( isCvar && maxDrawChars > 6 ) {
			valueChars = Min( static_cast<int>( strlen( cvarValue ) ), maxDrawChars - 4 );
			valueX = textRightX - valueChars * smallCharWidth;
			nameChars = Min( nameChars, Max( 1, maxDrawChars - valueChars - 3 ) );
		}

		if ( first + i == completionSelection ) {
			DrawSolidRect( popupX + 1.0f, rowY, rowWidth, SMALLCHAR_HEIGHT, selectionColor );
		}

		renderSystem->SetColor( kConsolePopupTextColor );
		for ( int j = 0; j < nameChars; ++j ) {
			Con_DrawSmallChar( textStartX + j * smallCharWidth, rowY, match[j] );
		}

		if ( valueChars > 0 ) {
			const idVec4 &valueColor = modified ? kConsoleBorderColor : kConsolePopupValueColor;
			renderSystem->SetColor( valueColor );
			Con_DrawSmallChar( valueX - 3.0f * smallCharWidth, rowY, ' ' );
			Con_DrawSmallChar( valueX - 2.0f * smallCharWidth, rowY, '=' );
			Con_DrawSmallChar( valueX - 1.0f * smallCharWidth, rowY, ' ' );
			for ( int j = 0; j < valueChars; ++j ) {
				Con_DrawSmallChar( valueX + j * smallCharWidth, rowY, cvarValue[j] );
			}
		}
	}

	if ( hasScrollbar ) {
		DrawScrollbarVisual( trackX, trackY, trackW, trackH, thumbY, thumbH, completionScrollbarHover, 1.0f, kConsoleBorderColor );
	}
}

void idConsoleLocal::DrawScrollbar( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	if ( !GetScrollbarGeometry( scrollbarHover, trackX, trackY, trackW, trackH, thumbY, thumbH ) ) {
		return;
	}
	DrawScrollbarVisual( trackX, trackY, trackW, trackH, thumbY, thumbH, scrollbarHover, 1.0f, kConsoleBorderColor );
}

void idConsoleLocal::DrawMouseCursor( void ) {
	if ( !keyCatching ) {
		return;
	}

	ClampMouseToConsole();

	if ( mouseCursorShader != NULL ) {
		const float cursorWidth = CON_MOUSE_CURSOR_SIZE * Con_GetConsoleXScale();
		renderSystem->SetColor( colorWhite );
		renderSystem->DrawStretchPic( mouseX, mouseY, cursorWidth, CON_MOUSE_CURSOR_SIZE,
			0.0f, 0.0f, 1.0f, 1.0f, mouseCursorShader );
		return;
	}

	const idVec4 &cursorColor = kConsoleBorderColor;
	DrawSolidRect( mouseX, mouseY, 3.0f, 13.0f, cursorColor );
	DrawSolidRect( mouseX, mouseY, 10.0f, 3.0f, cursorColor );
	DrawSolidRect( mouseX + 3.0f, mouseY + 3.0f, 3.0f, 3.0f, cursorColor );
	DrawSolidRect( mouseX + 6.0f, mouseY + 6.0f, 3.0f, 3.0f, cursorColor );
	DrawSolidRect( mouseX + 3.0f, mouseY + 10.0f, 7.0f, 3.0f, cursorColor );
}

/*
================
DrawInput

Draw the editline after a ] prompt
================
*/
void idConsoleLocal::DrawInput() {
	const float y = static_cast<float>( vislines - ( SMALLCHAR_HEIGHT * GetFooterRows() ) );

	renderSystem->SetColor( kConsoleBorderColor );
	Con_DrawSmallChar( 1.0f * smallCharWidth, y, ']' );
	DrawInputText( 2.0f * smallCharWidth, y, true );
	DrawInputDropCursor( 2.0f * smallCharWidth, y );
	DrawCompletionPopup();
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
		time = common->GetPresentationTime() - time;
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
	int				rows;
	short			*text_p;
	int				row;
	int				lines;
	int				currentColor;
	float			y;
	float			drawY;
	float			markerY;
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
		renderSystem->DrawStretchPic( 0, 0, SCREEN_WIDTH, y, 0, 0, 1, 1, whiteShader );
	}

	renderSystem->SetColor( kConsoleBorderColor );
	renderSystem->DrawStretchPic( 0, y, SCREEN_WIDTH, 2, 0, 0, 1, 1, whiteShader );
	renderSystem->SetColor( colorWhite );

	renderSystem->SetColor( kConsoleVersionColor );
	idStr version = OPENQ4_PRODUCT_VERSION;
	for ( i = 0; i < version.Length(); ++i ) {
		Con_DrawSmallChar( SCREEN_WIDTH - ( version.Length() - i ) * charWidth,
			lines - ( SMALLCHAR_HEIGHT + SMALLCHAR_HEIGHT / 2.0f ), version[i] );
	}

	// draw the text
	vislines = lines;
	UpdateScrollbarHover();
	UpdateCompletionScrollbarHover();
	rows = GetLogRowCount();
	markerY = lines - ( SMALLCHAR_HEIGHT * ( GetFooterRows() + 1 ) );
	drawY = markerY;
	row = idMath::FtoiFast( displayLine );
	if ( static_cast<float>( row ) < displayLine ) {
		++row;
	}
	drawY += ( row - displayLine ) * SMALLCHAR_HEIGHT;

	// draw from the bottom up
	if ( display != current ) {
		// draw arrows to show the buffer is backscrolled
		renderSystem->SetColor( kConsoleBorderColor );
		for ( x = 0; x < width; x += 4 ) {
			Con_DrawSmallChar( ( x + 1 ) * charWidth, markerY, '^' );
		}
		drawY -= SMALLCHAR_HEIGHT;
		--row;
	}

	currentColor = idStr::ColorIndex( C_COLOR_CONSOLE );
	renderSystem->SetColor( kConsoleBorderColor );

	for ( i = 0; i <= rows; i++, drawY -= SMALLCHAR_HEIGHT, row-- ) {
		if ( row < 0 ) {
			break;
		}
		if ( current - row >= CON_MAX_TOTAL_LINES ) {
			continue;
		}

		text_p = LinePtr( row );
		DrawLogSelectionRow( row, drawY );

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
			Con_DrawSmallChar( ( x + 1 ) * charWidth, drawY, text_p[x] & 0xff );
		}
	}

	DrawInput();
	DrawScrollbar();
	DrawMouseCursor();

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
	UpdateDisplayLine();

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

	if ( com_showFramePacing.GetInteger() > 0 ) {
		y = SCR_DrawFramePacing( y );
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

	if ( procFileOutOfDate ) {
		SCR_DrawTextRightAlign( y, "PROC" );
	}

	if ( !idAsyncNetwork::client.IsActive() && !idAsyncNetwork::server.IsActive() && aasFileOutOfDate ) {
		SCR_DrawTextRightAlign( y, "AAS" );
	}
}
