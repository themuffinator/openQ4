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




typedef struct {
	const char	*name;
	int		keynum;
	const char	*strId;	// localized string id
} keyname_t;

// keys that can be set without a special name
static const char unnamedkeys[] = "*,-=./[\\]1234567890abcdefghijklmnopqrstuvwxyz";

#if defined( MACOS_X )
const char* OSX_GetLocalizedString( const char* );
#endif

// names not in this list can either be lowercase ascii, or '0xnn' hex sequences
keyname_t keynames[] =
{
	{"TAB",				K_TAB,				"#str_07018"},
	{"ENTER",			K_ENTER,			"#str_07019"},
	{"ESCAPE",			K_ESCAPE,			"#str_07020"},
	{"SPACE",			K_SPACE,			"#str_07021"},
	{"BACKSPACE",		K_BACKSPACE,		"#str_07022"},
	{"UPARROW",			K_UPARROW,			"#str_07023"},
	{"DOWNARROW",		K_DOWNARROW,		"#str_07024"},
	{"LEFTARROW",		K_LEFTARROW,		"#str_07025"},
	{"RIGHTARROW",		K_RIGHTARROW,		"#str_07026"},

	{"ALT",				K_ALT,				"#str_07027"},
	{"RIGHTALT",		K_RIGHT_ALT,		"#str_107651"},
	{"LEFTALT",			K_ALT,				"#str_07027"},
	{"CTRL",			K_CTRL,				"#str_07028"},
	{"LEFTCTRL",		K_CTRL,				"#str_07028"},
	{"RIGHTCTRL",		K_CTRL,				"#str_07028"},
	{"SHIFT",			K_SHIFT,			"#str_07029"},
	{"LEFTSHIFT",		K_SHIFT,			"#str_07029"},
	{"RIGHTSHIFT",		K_SHIFT,			"#str_07029"},

	{"LWIN", 			K_LWIN, 			"#str_07030"},
	{"RWIN", 			K_RWIN, 			"#str_07031"},
	{"MENU", 			K_MENU, 			"#str_07032"},

	{"COMMAND",			K_COMMAND,			"#str_07033"},

	{"CAPSLOCK",		K_CAPSLOCK,			"#str_07034"},
	{"SCROLL",			K_SCROLL,			"#str_07035"},
	{"PRINTSCREEN",		K_PRINT_SCR,		"#str_07179"},
	
	{"F1", 				K_F1, 				"#str_07036"},
	{"F2", 				K_F2, 				"#str_07037"},
	{"F3", 				K_F3, 				"#str_07038"},
	{"F4", 				K_F4, 				"#str_07039"},
	{"F5", 				K_F5, 				"#str_07040"},
	{"F6", 				K_F6, 				"#str_07041"},
	{"F7", 				K_F7, 				"#str_07042"},
	{"F8", 				K_F8, 				"#str_07043"},
	{"F9", 				K_F9, 				"#str_07044"},
	{"F10", 			K_F10, 				"#str_07045"},
	{"F11", 			K_F11, 				"#str_07046"},
	{"F12", 			K_F12, 				"#str_07047"},

	{"INS", 			K_INS, 				"#str_07048"},
	{"DEL", 			K_DEL, 				"#str_07049"},
	{"PGDN", 			K_PGDN, 			"#str_07050"},
	{"PGUP", 			K_PGUP, 			"#str_07051"},
	{"HOME", 			K_HOME, 			"#str_07052"},
	{"END",				K_END,				"#str_07053"},

	{"MOUSE1", 			K_MOUSE1, 			"#str_07054"},
	{"MOUSE2", 			K_MOUSE2, 			"#str_07055"},
	{"MOUSE3", 			K_MOUSE3, 			"#str_07056"},
	{"MOUSE4", 			K_MOUSE4, 			"#str_07057"},
	{"MOUSE5", 			K_MOUSE5, 			"#str_07058"},
	{"MOUSE6", 			K_MOUSE6, 			"#str_07059"},
	{"MOUSE7", 			K_MOUSE7, 			"#str_07060"},
	{"MOUSE8", 			K_MOUSE8, 			"#str_07061"},

	{"MWHEELUP",		K_MWHEELUP,			"#str_07131"},
	{"MWHEELDOWN",		K_MWHEELDOWN,		"#str_07132"},

	{"JOY1", 			K_JOY1, 			"#str_07062"},
	{"JOY2", 			K_JOY2, 			"#str_07063"},
	{"JOY3", 			K_JOY3, 			"#str_07064"},
	{"JOY4", 			K_JOY4, 			"#str_07065"},
	{"JOY5", 			K_JOY5, 			"#str_07066"},
	{"JOY6", 			K_JOY6, 			"#str_07067"},
	{"JOY7", 			K_JOY7, 			"#str_07068"},
	{"JOY8", 			K_JOY8, 			"#str_07069"},
	{"JOY9", 			K_JOY9, 			"#str_07070"},
	{"JOY10", 			K_JOY10, 			"#str_07071"},
	{"JOY11", 			K_JOY11, 			"#str_07072"},
	{"JOY12", 			K_JOY12, 			"#str_07073"},
	{"JOY13", 			K_JOY13, 			"#str_07074"},
	{"JOY14", 			K_JOY14, 			"#str_07075"},
	{"JOY15", 			K_JOY15, 			"#str_07076"},
	{"JOY16", 			K_JOY16, 			"#str_07077"},
	{"JOY17", 			K_JOY17, 			"#str_07078"},
	{"JOY18", 			K_JOY18, 			"#str_07079"},
	{"JOY19", 			K_JOY19, 			"#str_07080"},
	{"JOY20", 			K_JOY20, 			"#str_07081"},
	{"JOY21", 			K_JOY21, 			"#str_07082"},
	{"JOY22", 			K_JOY22, 			"#str_07083"},
	{"JOY23", 			K_JOY23, 			"#str_07084"},
	{"JOY24", 			K_JOY24, 			"#str_07085"},
	{"JOY25", 			K_JOY25, 			"#str_07086"},
	{"JOY26", 			K_JOY26, 			"#str_07087"},
	{"JOY27", 			K_JOY27, 			"#str_07088"},
	{"JOY28", 			K_JOY28, 			"#str_07089"},
	{"JOY29", 			K_JOY29, 			"#str_07090"},
	{"JOY30", 			K_JOY30, 			"#str_07091"},
	{"JOY31", 			K_JOY31, 			"#str_07092"},
	{"JOY32", 			K_JOY32, 			"#str_07093"},

	{"AUX1", 			K_AUX1, 			"#str_07094"},
	{"AUX2", 			K_AUX2, 			"#str_07095"},
	{"AUX3", 			K_AUX3, 			"#str_07096"},
	{"AUX4", 			K_AUX4, 			"#str_07097"},
	{"AUX5", 			K_AUX5, 			"#str_07098"},
	{"AUX6", 			K_AUX6, 			"#str_07099"},
	{"AUX7", 			K_AUX7, 			"#str_07100"},
	{"AUX8", 			K_AUX8, 			"#str_07101"},
	{"AUX9", 			K_AUX9, 			"#str_07102"},
	{"AUX10", 			K_AUX10, 			"#str_07103"},
	{"AUX11", 			K_AUX11, 			"#str_07104"},
	{"AUX12", 			K_AUX12, 			"#str_07105"},
	{"AUX13", 			K_AUX13, 			"#str_07106"},
	{"AUX14", 			K_AUX14, 			"#str_07107"},
	{"AUX15", 			K_AUX15, 			"#str_07108"},
	{"AUX16", 			K_AUX16, 			"#str_07109"},

	{"KP_HOME",			K_KP_HOME,			"#str_07110"},
	{"KP_UPARROW",		K_KP_UPARROW,		"#str_07111"},
	{"KP_PGUP",			K_KP_PGUP,			"#str_07112"},
	{"KP_LEFTARROW",	K_KP_LEFTARROW, 	"#str_07113"},
	{"KP_5",			K_KP_5,				"#str_07114"},
	{"KP_RIGHTARROW",	K_KP_RIGHTARROW,	"#str_07115"},
	{"KP_END",			K_KP_END,			"#str_07116"},
	{"KP_DOWNARROW",	K_KP_DOWNARROW,		"#str_07117"},
	{"KP_PGDN",			K_KP_PGDN,			"#str_07118"},
	{"KP_ENTER",		K_KP_ENTER,			"#str_07119"},
	{"KP_INS",			K_KP_INS, 			"#str_07120"},
	{"KP_DEL",			K_KP_DEL, 			"#str_07121"},
	{"KP_SLASH",		K_KP_SLASH, 		"#str_07122"},
	{"KP_MINUS",		K_KP_MINUS, 		"#str_07123"},
	{"KP_PLUS",			K_KP_PLUS,			"#str_07124"},
	{"KP_NUMLOCK",		K_KP_NUMLOCK,		"#str_07125"},
	{"KP_STAR",			K_KP_STAR,			"#str_07126"},
	{"KP_EQUALS",		K_KP_EQUALS,		"#str_07127"},

	{"PAUSE",			K_PAUSE,			"#str_07128"},
	
	{"SEMICOLON",		';',				"#str_07129"},	// because a raw semicolon separates commands
	{"APOSTROPHE",		'\'',				"#str_07130"},	// because a raw apostrophe messes with parsing

	{NULL,				0,					NULL}
};



static const int	MAX_KEYS = 256;

class idKey {
public:
					idKey( void ) { down = false; repeats = 0; usercmdAction = 0; }
	bool			down;
	int				repeats;		// if > 1, it is autorepeating
	idStr			binding;
	int				usercmdAction;	// for testing by the asyncronous usercmd generation
};

bool		key_overstrikeMode = false;
idKey *		keys = NULL;

enum keyInputFamily_t {
	KEY_INPUT_FAMILY_DESKTOP,
	KEY_INPUT_FAMILY_CONTROLLER
};

static keyInputFamily_t key_lastInputFamily = KEY_INPUT_FAMILY_DESKTOP;
static bool key_lastPointerTouch = false;

static bool Key_IsValidKeyNum( int keynum ) {
	return keys != NULL && keynum >= 0 && keynum < MAX_KEYS;
}

static bool BindingsEquivalent( const char *lhs, const char *rhs ) {
	if ( !lhs || !rhs || !lhs[0] || !rhs[0] ) {
		return false;
	}

	if ( idStr::Icmp( lhs, rhs ) == 0 ) {
		return true;
	}

	if ( usercmdGen == NULL ) {
		return false;
	}

	const int lhsAction = usercmdGen->CommandStringUsercmdData( lhs );
	if ( lhsAction == 0 ) {
		return false;
	}

	return lhsAction == usercmdGen->CommandStringUsercmdData( rhs );
}

#define ID_DOOM_LEGACY

#ifdef ID_DOOM_LEGACY

const char *		cheatCodes[] = {
	"iddqd",		// Invincibility
	"idkfa",		// All weapons, keys, ammo, and 200% armor
	"idfa",			// Reset ammunition
	"idspispopd",	// Walk through walls
	"idclip",		// Walk through walls
	"idchoppers",	// Chainsaw
/*
	"idbeholds",	// Berserker strength
	"idbeholdv",	// Temporary invincibility
	"idbeholdi",	// Temporary invisibility
	"idbeholda",	// Full automap
	"idbeholdr",	// Anti-radiation suit
	"idbeholdl",	// Light amplification visor
	"idclev",		// Level select
	"iddt",			// Toggle full map; full map and objects; normal map
	"idmypos",		// Display coordinates and heading
	"idmus",		// Change music to indicated level
	"fhhall",		// Kill all enemies in level
	"fhshh",		// Invisible to enemies until attack
*/
	NULL
};
char		lastKeys[32];
int			lastKeyIndex;

#endif

/*
===================
idKeyInput::ArgCompletion_KeyName
===================
*/
void idKeyInput::ArgCompletion_KeyName( const idCmdArgs &args, void(*callback)( const char *s ) ) {
	const keyname_t *kn;
	int i;

	for( i = 0; i < sizeof( unnamedkeys ) - 1; i++ ) {
		callback( va( "%s %c", args.Argv( 0 ), unnamedkeys[ i ] ) );
	}

	for ( kn = keynames; kn->name; kn++ ) {
		callback( va( "%s %s", args.Argv( 0 ), kn->name ) );
	}
}

/*
===================
idKeyInput::GetOverstrikeMode
===================
*/
bool idKeyInput::GetOverstrikeMode( void ) {
	return key_overstrikeMode;
}

/*
===================
idKeyInput::SetOverstrikeMode
===================
*/
void idKeyInput::SetOverstrikeMode( bool state ) {
	key_overstrikeMode = state;
}

/*
===================
idKeyInput::IsDown
===================
*/
bool idKeyInput::IsDown( int keynum ) {
	if ( !Key_IsValidKeyNum( keynum ) ) {
		return false;
	}

	return keys[keynum].down;
}

/*
===================
idKeyInput::LastInputWasController

Whether the latest key, button or axis input came from a controller, for
prompts that show the active device's glyphs.
===================
*/
bool idKeyInput::LastInputWasController( void ) {
	return key_lastInputFamily == KEY_INPUT_FAMILY_CONTROLLER;
}

/*
===================
idKeyInput::NotePointerSource / LastPointerWasTouch

The platform layer names each pointer input's source: a finger routed as the
primary pointer, or a mouse.
===================
*/
void idKeyInput::NotePointerSource( bool touch ) {
	key_lastPointerTouch = touch;
}

bool idKeyInput::LastPointerWasTouch( void ) {
	return key_lastPointerTouch;
}

/*
===================
Key_MenuConfirmSwapped

in_swapConfirmBack 1 keeps confirm on the south face button and 2 moves it
east; 0 follows the controller, swapping for a Nintendo layout, whose A sits
east (section 13.5). A prompt style forced to a family (in_promptStyle 1-4)
stands for the controller a remapping layer hides, so its layout decides;
generic prompts (5) name positions and leave the layout to the controller.
===================
*/
static idCVar in_swapConfirmBack( "in_swapConfirmBack", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ARCHIVE,
	"menu confirm and back on a controller: 0 automatic (Nintendo layouts confirm on the east button), "
	"1 the south button confirms, 2 the east button confirms", 0, 2 );

static bool Key_MenuConfirmSwapped( int setting, int promptStyle, const char *family ) {
	if ( setting == 1 ) {
		return false;
	}
	if ( setting == 2 ) {
		return true;
	}
	if ( promptStyle >= 1 && promptStyle <= 4 ) {
		return promptStyle == 3;
	}
	return family != NULL && idStr::Icmp( family, "nintendo" ) == 0;
}

/*
===================
idKeyInput::MenuConfirmSwapped
===================
*/
bool idKeyInput::MenuConfirmSwapped( void ) {
	return Key_MenuConfirmSwapped( in_swapConfirmBack.GetInteger(), cvarSystem->GetCVarInteger( "in_promptStyle" ),
		cvarSystem->GetCVarString( "in_gamepadFamily" ) );
}

/*
===================
idKeyInput::MenuControllerKey

The south and east face buttons trade places in menus while the confirm
button is swapped; every other key is itself.
===================
*/
int idKeyInput::MenuControllerKey( int keynum ) {
	if ( ( keynum == K_JOY3 || keynum == K_JOY4 ) && MenuConfirmSwapped() ) {
		return keynum == K_JOY3 ? K_JOY4 : K_JOY3;
	}
	return keynum;
}

/*
===================
idKeyInput::StringToKeyNum

Returns a key number to be used to index keys[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.

0x11 will be interpreted as raw hex, which will allow new controlers
to be configured even if they don't have defined names.
===================
*/
int idKeyInput::StringToKeyNum( const char *str ) {
	const keyname_t	*kn;
	
	if ( !str || !str[0] ) {
		return -1;
	}
	if ( !str[1] ) {
		return (unsigned char)(str[0]);
	}

	// check for hex code
	if ( str[0] == '0' && str[1] == 'x' && strlen( str ) == 4 ) {
		int		n1, n2;
		
		n1 = str[2];
		if ( n1 >= '0' && n1 <= '9' ) {
			n1 -= '0';
		} else if ( n1 >= 'a' && n1 <= 'f' ) {
			n1 = n1 - 'a' + 10;
		} else {
			n1 = 0;
		}

		n2 = str[3];
		if ( n2 >= '0' && n2 <= '9' ) {
			n2 -= '0';
		} else if ( n2 >= 'a' && n2 <= 'f' ) {
			n2 = n2 - 'a' + 10;
		} else {
			n2 = 0;
		}

		return n1 * 16 + n2;
	}

	// scan for a text match
	for ( kn = keynames; kn->name; kn++ ) {
		if ( !idStr::Icmp( str, kn->name ) ) {
			return kn->keynum;
		}
	}

	return -1;
}

/*
===================
idKeyInput::KeyNumToString

Returns a string (either a single ascii char, a K_* name, or a 0x11 hex string) for the
given keynum.
===================
*/
const char *idKeyInput::KeyNumToString( int keynum, bool localized ) {
	const keyname_t	*kn;	
	static	char	tinystr[5];
	int			i, j;

	if ( keynum == -1 ) {
		return "<KEY NOT FOUND>";
	}

	if ( keynum < 0 || keynum > 255 ) {
		return "<OUT OF RANGE>";
	}

	// check for printable ascii (don't use quote)
	if ( keynum > 32 && keynum < 127 && keynum != '"' && keynum != ';' && keynum != '\'' ) {
		tinystr[0] = Sys_MapCharForKey( keynum );
		tinystr[1] = 0;
		return tinystr;
	}

	// check for a key string
	for ( kn = keynames; kn->name; kn++ ) {
		if ( keynum == kn->keynum ) {
			if ( !localized || kn->strId[0] != '#' ) {
				return kn->name;
			} else {
#if defined( MACOS_X )
				
				switch ( kn->keynum ) {
					case K_ENTER:		
					case K_BACKSPACE:	
					case K_ALT:			
					case K_INS:
					case K_PRINT_SCR:
						return OSX_GetLocalizedString( kn->name );
						break;
					default :
						return common->GetLanguageDict()->GetString( kn->strId ); break;
				}
#else
				return common->GetLanguageDict()->GetString( kn->strId );
#endif
			}
		}
	}

	// check for European high-ASCII characters
	if ( localized && keynum >= 161 && keynum <= 255 ) {
		tinystr[0] = keynum;
		tinystr[1] = 0;
		return tinystr;
	}

	// make a hex string
	i = keynum >> 4;
	j = keynum & 15;

	tinystr[0] = '0';
	tinystr[1] = 'x';
	tinystr[2] = i > 9 ? i - 10 + 'a' : i + '0';
	tinystr[3] = j > 9 ? j - 10 + 'a' : j + '0';
	tinystr[4] = 0;

	return tinystr;
}

/*
===================
idKeyInput::SetBinding
===================
*/
static void Key_NoteLayoutChanged( void );

void idKeyInput::SetBinding( int keynum, const char *binding ) {
	if ( !Key_IsValidKeyNum( keynum ) ) {
		return;
	}

	// Clear out all button states so we aren't stuck forever thinking this key is held down
	usercmdGen->Clear();

	// allocate memory for new binding
	keys[keynum].binding = binding;

	// find the action for the async command generation
	keys[keynum].usercmdAction = usercmdGen->CommandStringUsercmdData( binding );
	// the layout chooser shows what the buttons are after any rebinding
	Key_NoteLayoutChanged();

	// consider this like modifying an archived cvar, so the
	// file write will be triggered at the next oportunity
	cvarSystem->SetModifiedFlags( CVAR_ARCHIVE );
}


/*
===================
idKeyInput::GetBinding
===================
*/
const char *idKeyInput::GetBinding( int keynum ) {
	if ( !Key_IsValidKeyNum( keynum ) ) {
		return "";
	}

	return keys[ keynum ].binding;
}

/*
===================
idKeyInput::GetUsercmdAction
===================
*/
int idKeyInput::GetUsercmdAction( int keynum ) {
	if ( !Key_IsValidKeyNum( keynum ) ) {
		return 0;
	}

	// openQ4 (PAD-06): a context action takes the button's place
	const char *context = ContextBinding( keynum );
	if ( context != NULL ) {
		return usercmdGen->CommandStringUsercmdData( context );
	}

	return keys[ keynum ].usercmdAction;
}

static bool Key_IsControllerKey( int keynum ) {
	const char *keyCode = idKeyInput::KeyNumToString( keynum, false );
	return idStr::Icmpn( keyCode, "JOY", 3 ) == 0 || idStr::Icmpn( keyCode, "AUX", 3 ) == 0;
}

static bool Key_IsMouseKey( int keynum ) {
	return keynum >= K_MOUSE1 && keynum <= K_MWHEELUP;
}

/*
==============================================================

	Controller context actions (PAD-06)

	The default layout has no button for voting, Ready, spectating, chat,
	the buy menu, voice chat or quick save and load: there are more commands
	than buttons. A context gives a few buttons one of those commands while
	it lasts, without spending a button for good:

	  a vote is open            D-pad left votes Yes, D-pad right votes No
	  scoreboard held, in a     A Ready, X Spectate, Y Chat, B Team chat,
	  match                     RB Buy menu
	  objectives held, in the   A Quick save, X Quick load
	  campaign
	  in a match                D-pad up is voice chat, while the View button
	                            still shows the scores

	A command the player has put on a controller button of their own is left
	to that button, and in_joystickContextActions 0 turns them all off.

==============================================================
*/

static idCVar in_joystickContextActions( "in_joystickContextActions", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL,
	"controller buttons take the vote, match and quick save commands while a vote is open or the scoreboard or objectives button is held" );
// The game sets this while a vote is open and this player has not voted.
static idCVar ui_voteOpen( "ui_voteOpen", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ROM | CVAR_NOCHEAT,
	"set by the game while a vote is open and waiting for this player" );

struct keyContextAction_t {
	int				key;
	const char *	binding;
};

static const keyContextAction_t key_voteActions[] = {
	{ K_JOY12, "_impulse28" },			// D-pad left: vote yes
	{ K_JOY11, "_impulse29" },			// D-pad right: vote no
};
static const keyContextAction_t key_matchActions[] = {
	{ K_JOY3, "_impulse17" },			// A: ready
	{ K_JOY6, "_impulse22" },			// X: spectate
	{ K_JOY5, "clientMessageMode" },	// Y: chat
	{ K_JOY4, "clientMessageMode 1" },	// B: team chat
	{ K_JOY2, "buymenu" },				// RB: buy menu
};
static const keyContextAction_t key_campaignActions[] = {
	{ K_JOY3, "savegame quick" },		// A: quick save
	{ K_JOY6, "loadgame quick" },		// X: quick load
};
static const keyContextAction_t key_voiceActions[] = {
	{ K_JOY9, "_voicechat" },			// D-pad up: voice chat
};

// A controller button the player bound to the command themselves, or -1.
static int Key_ControllerKeyFor( const char *binding ) {
	for ( int i = 0; i < MAX_KEYS; i++ ) {
		if ( Key_IsControllerKey( i ) && BindingsEquivalent( keys[i].binding.c_str(), binding ) ) {
			return i;
		}
	}
	return -1;
}

static bool Key_IsScoresBinding( const char *binding ) {
	return BindingsEquivalent( binding, "_impulse19" ) || BindingsEquivalent( binding, "_showScores" );
}

// The controller button held for the scoreboard or the objectives, or -1.
static int Key_HeldScoresKey( void ) {
	for ( int i = 0; i < MAX_KEYS; i++ ) {
		if ( keys[i].down && Key_IsControllerKey( i ) && Key_IsScoresBinding( keys[i].binding.c_str() ) ) {
			return i;
		}
	}
	return -1;
}

// A second controller button for the scores, so D-pad up can be spared.
static bool Key_ScoresHasSpareKey( int key ) {
	if ( !Key_IsValidKeyNum( key ) || !Key_IsScoresBinding( keys[key].binding.c_str() ) ) {
		return false;
	}
	for ( int i = 0; i < MAX_KEYS; i++ ) {
		if ( i != key && Key_IsControllerKey( i ) && Key_IsScoresBinding( keys[i].binding.c_str() ) ) {
			return true;
		}
	}
	return false;
}

static const char *Key_FindContextAction( const keyContextAction_t *actions, int count, int keynum ) {
	for ( int i = 0; i < count; i++ ) {
		if ( actions[i].key == keynum && Key_ControllerKeyFor( actions[i].binding ) < 0 ) {
			return actions[i].binding;
		}
	}
	return NULL;
}

/*
===================
idKeyInput::ContextBinding
===================
*/
const char *idKeyInput::ContextBinding( int keynum ) {
	if ( !in_joystickContextActions.GetBool() || !Key_IsValidKeyNum( keynum ) || !Key_IsControllerKey( keynum ) ) {
		return NULL;
	}
	const bool match = idAsyncNetwork::IsActive();
	if ( match && ui_voteOpen.GetBool() ) {
		const char *vote = Key_FindContextAction( key_voteActions, sizeof( key_voteActions ) / sizeof( key_voteActions[0] ), keynum );
		if ( vote != NULL ) {
			return vote;
		}
	}
	const int held = Key_HeldScoresKey();
	if ( held >= 0 && held != keynum ) {
		const char *layer = match ?
			Key_FindContextAction( key_matchActions, sizeof( key_matchActions ) / sizeof( key_matchActions[0] ), keynum ) :
			Key_FindContextAction( key_campaignActions, sizeof( key_campaignActions ) / sizeof( key_campaignActions[0] ), keynum );
		if ( layer != NULL ) {
			return layer;
		}
	}
	if ( match && Key_ScoresHasSpareKey( keynum ) ) {
		return Key_FindContextAction( key_voiceActions, sizeof( key_voiceActions ) / sizeof( key_voiceActions[0] ), keynum );
	}
	return NULL;
}

// For a prompt: the context button that reaches a command, and the button to
// hold with it (-1: none), when the player's own bindings give it no
// controller button. The prompt names the buttons a controller player can press.
static int Key_ContextKeyForPrompt( const char *bind, int &holdKey ) {
	holdKey = -1;
	if ( !in_joystickContextActions.GetBool() || bind == NULL || bind[0] == '\0' ) {
		return -1;
	}
	const bool match = idAsyncNetwork::IsActive();
	if ( match ) {
		for ( int i = 0; i < static_cast<int>( sizeof( key_voteActions ) / sizeof( key_voteActions[0] ) ); i++ ) {
			if ( BindingsEquivalent( key_voteActions[i].binding, bind ) ) {
				return key_voteActions[i].key;
			}
		}
		for ( int i = 0; i < static_cast<int>( sizeof( key_voiceActions ) / sizeof( key_voiceActions[0] ) ); i++ ) {
			if ( BindingsEquivalent( key_voiceActions[i].binding, bind ) && Key_ScoresHasSpareKey( key_voiceActions[i].key ) ) {
				return key_voiceActions[i].key;
			}
		}
	}
	const keyContextAction_t *layer = match ? key_matchActions : key_campaignActions;
	const int count = match ? static_cast<int>( sizeof( key_matchActions ) / sizeof( key_matchActions[0] ) ) :
		static_cast<int>( sizeof( key_campaignActions ) / sizeof( key_campaignActions[0] ) );
	for ( int i = 0; i < count; i++ ) {
		if ( BindingsEquivalent( layer[i].binding, bind ) ) {
			// held with the scores or objectives button: View where it is still bound there
			for ( int key = 0; key < MAX_KEYS; key++ ) {
				if ( Key_IsControllerKey( key ) && key != layer[i].key && Key_IsScoresBinding( keys[key].binding.c_str() ) ) {
					holdKey = key;
					if ( key == K_JOY8 ) {
						break;
					}
				}
			}
			return holdKey >= 0 ? layer[i].key : -1;
		}
	}
	return -1;
}

static idStr &Key_NextBindingPresentationBuffer( void ) {
	// Several shipped HUD strings request two bindings in one va() call.  A
	// rotating set keeps those values independent while idStr avoids fixed-buffer
	// truncation in both compact prompts and settings rows.
	static idStr buffers[16];
	static int bufferIndex = 0;
	idStr &result = buffers[bufferIndex++ & 15];
	result.Clear();
	return result;
}

static void Key_AppendBindingPresentation( idStr &result, int keynum, bool emphasized = false, const char *separator = NULL ) {
	if ( result.Length() > 0 ) {
		result.Append( separator != NULL ? separator : common->GetLanguageDict()->GetString( "#str_07183" ) );
	}
	result.Append( va( "^i%c%02x", emphasized ? 'K' : 'k', keynum & 0xff ) );
}

/*
===================
Key_Unbind_f
===================
*/
void Key_Unbind_f( const idCmdArgs &args ) {
	int		b;

	if ( args.Argc() != 2 ) {
		common->Printf( "unbind <key> : remove commands from a key\n" );
		return;
	}
	
	b = idKeyInput::StringToKeyNum( args.Argv(1) );
	if ( b == -1 ) {
		// If it wasn't a key, it could be a command
		if ( !idKeyInput::UnbindBinding( args.Argv(1) ) ) {
			common->Printf( "\"%s\" isn't a valid key\n", args.Argv(1) );
		}
	} else {
		idKeyInput::SetBinding( b, "" );
	}
}

/*
===================
Key_Unbindall_f
===================
*/
void Key_Unbindall_f( const idCmdArgs &args ) {
	int		i;
	
	for ( i = 0; i < MAX_KEYS; i++ ) {
		idKeyInput::SetBinding( i, "" );
	}
}

/*
==============================================================

	Controller layouts and separate resets (PAD-06)

	default.cfg holds the shipped bindings for every device. Reset Controller
	and Reset Keyboard and Mouse each restore their own half of it and leave
	the other half alone; exec default.cfg would reset both, and the player's
	name with them. The layout presets are small configs of controller binds,
	applied only when the player chooses one.

==============================================================
*/

static const char *const key_layoutNames[] = { "custom", "classic", "bumperjumper", "onehandedleft", "onehandedright" };
static const int KEY_LAYOUT_COUNT = sizeof( key_layoutNames ) / sizeof( key_layoutNames[0] );
static idCVar in_joystickLayout( "in_joystickLayout", "1", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT,
	"controller button layout: 1 classic, 2 bumper jumper, 3 one-handed (left), 4 one-handed (right); 0 = custom, shown when the "
	"buttons match none of them. Choosing one rebinds the controller's buttons.", 0, KEY_LAYOUT_COUNT - 1 );
static bool key_layoutDirty = true;
static bool key_layoutReady = false;

static void Key_NoteLayoutChanged( void ) {
	key_layoutDirty = true;
}

// The settings a layout or a reset may write besides bindings.
static bool Key_LayoutMaySet( const char *name ) {
	return !idStr::Icmp( name, "in_joystickSouthpaw" ) || !idStr::Icmp( name, "in_toggleZoom" ) || !idStr::Icmp( name, "in_toggleCrouch" );
}

/*
===================
Key_ApplyConfigBinds

Runs the bind lines of a config for one device family: the controller's keys,
or every other key. With settings, the few seta lines a layout may carry are
run too. With bindings given, nothing is run and the binds are collected
instead, for comparing a layout with what is bound now.
===================
*/
static bool Key_ApplyConfigBinds( const char *fileName, bool controller, bool settings, idStr *bindings ) {
	char *buffer = NULL;
	const int length = fileSystem->ReadFile( fileName, (void **)&buffer, NULL );
	if ( length < 0 || buffer == NULL ) {
		return false;
	}
	idStr text( buffer, 0, length );
	fileSystem->FreeFile( buffer );
	int start = 0;
	while ( start < text.Length() ) {
		int end = text.Find( '\n', start );
		if ( end < 0 ) {
			end = text.Length();
		}
		idStr line = text.Mid( start, end - start );
		start = end + 1;
		const int comment = line.Find( "//" );
		if ( comment >= 0 ) {
			line.CapLength( comment );
		}
		line.StripTrailingWhitespace();
		idCmdArgs args;
		args.TokenizeString( line.c_str(), false );
		if ( args.Argc() >= 3 && !idStr::Icmp( args.Argv( 0 ), "bind" ) ) {
			const int key = idKeyInput::StringToKeyNum( args.Argv( 1 ) );
			if ( key < 0 || Key_IsControllerKey( key ) != controller ) {
				continue;
			}
			if ( bindings != NULL ) {
				bindings[key] = args.Args( 2 );
			} else {
				idKeyInput::SetBinding( key, args.Args( 2 ) );
			}
		} else if ( settings && bindings == NULL && args.Argc() >= 3 && !idStr::Icmp( args.Argv( 0 ), "seta" ) && Key_LayoutMaySet( args.Argv( 1 ) ) ) {
			cvarSystem->SetCVarString( args.Argv( 1 ), args.Argv( 2 ) );
		}
	}
	return true;
}

static const char *Key_LayoutFile( int layout ) {
	return va( "controller_layout_%s.cfg", key_layoutNames[ layout ] );
}

static void Key_UnbindFamily( bool controller ) {
	for ( int i = 0; i < MAX_KEYS; i++ ) {
		if ( Key_IsControllerKey( i ) == controller && keys[i].binding.Length() > 0 ) {
			idKeyInput::SetBinding( i, "" );
		}
	}
}

// Classic is the shipped default layout; the others are their own configs.
static bool Key_LayoutBindings( int layout, idStr *bindings ) {
	if ( layout == 1 ) {
		const bool found = Key_ApplyConfigBinds( "default.cfg", true, false, bindings );
		(void)Key_ApplyConfigBinds( "openq4_defaults.cfg", true, false, bindings );
		return found;
	}
	return Key_ApplyConfigBinds( Key_LayoutFile( layout ), true, false, bindings );
}

static void Key_ApplyControllerLayout( int layout ) {
	if ( layout <= 0 || layout >= KEY_LAYOUT_COUNT ) {
		return;
	}
	Key_UnbindFamily( true );
	if ( layout == 1 ) {
		(void)Key_ApplyConfigBinds( "default.cfg", true, false, NULL );
		(void)Key_ApplyConfigBinds( "openq4_defaults.cfg", true, false, NULL );
	} else if ( !Key_ApplyConfigBinds( Key_LayoutFile( layout ), true, true, NULL ) ) {
		common->Warning( "controller layout '%s' is missing; restoring the classic layout", key_layoutNames[ layout ] );
		(void)Key_ApplyConfigBinds( "default.cfg", true, false, NULL );
	}
	key_layoutDirty = true;
	common->Printf( "controller: %s layout\n", key_layoutNames[ layout ] );
}

// Which layout the controller's buttons are now, or 0 for none of them.
static int Key_CurrentControllerLayout( void ) {
	static idStr wanted[MAX_KEYS];
	for ( int layout = 1; layout < KEY_LAYOUT_COUNT; layout++ ) {
		for ( int i = 0; i < MAX_KEYS; i++ ) {
			wanted[i].Clear();
		}
		if ( !Key_LayoutBindings( layout, wanted ) ) {
			continue;
		}
		bool same = true;
		for ( int i = 0; same && i < MAX_KEYS; i++ ) {
			if ( !Key_IsControllerKey( i ) ) {
				continue;
			}
			same = wanted[i].Length() == 0 ? keys[i].binding.Length() == 0 : BindingsEquivalent( wanted[i].c_str(), keys[i].binding.c_str() );
		}
		if ( same ) {
			return layout;
		}
	}
	return 0;
}

/*
===================
idKeyInput::UpdateControllerLayout

Once a frame. A layout chosen in the menu is applied; after any rebinding the
chooser shows the layout the buttons now are, or Custom.
===================
*/
void idKeyInput::UpdateControllerLayout( void ) {
	if ( !key_layoutReady ) {
		// the first frame after the configs ran: show what the profile has
		key_layoutReady = true;
		in_joystickLayout.ClearModified();
		key_layoutDirty = true;
	}
	if ( in_joystickLayout.IsModified() ) {
		in_joystickLayout.ClearModified();
		Key_ApplyControllerLayout( in_joystickLayout.GetInteger() );
	}
	if ( key_layoutDirty ) {
		key_layoutDirty = false;
		const int layout = Key_CurrentControllerLayout();
		if ( layout != in_joystickLayout.GetInteger() ) {
			in_joystickLayout.SetInteger( layout );
		}
		in_joystickLayout.ClearModified();
	}
}

/*
===================
Key_ControllerLayout_f
===================
*/
static void Key_ControllerLayout_f( const idCmdArgs &args ) {
	for ( int layout = 1; layout < KEY_LAYOUT_COUNT; layout++ ) {
		if ( !idStr::Icmp( args.Argv( 1 ), key_layoutNames[ layout ] ) ) {
			Key_ApplyControllerLayout( layout );
			return;
		}
	}
	common->Printf( "usage: controllerLayout <classic|bumperjumper|onehandedleft|onehandedright>\n"
		"current: %s\n", key_layoutNames[ idMath::ClampInt( 0, KEY_LAYOUT_COUNT - 1, Key_CurrentControllerLayout() ) ] );
}

// Settings each reset returns to its default. The controller's are every
// in_joystick*, in_gyro* and in_touchpad* setting and the prompt choices.
static const char *const key_controllerSettings[] = {
	"in_joystick", "in_joystickDevice", "in_joystickSouthpaw", "in_joystickDeadZone", "in_joystickLookDeadZone",
	"in_joystickMoveDeadZone", "in_joystickLookOuterDeadZone", "in_joystickMoveOuterDeadZone", "in_joystickMoveDeadZoneAxial",
	"in_joystickLookSensitivity", "in_joystickYawSpeed", "in_joystickPitchSpeed", "in_joystickLookCurve", "in_joystickMoveCurve",
	"in_joystickTurnBoost", "in_joystickTurnBoostRamp", "in_joystickResponse", "in_joystickInvertLook",
	"in_joystickTriggerThreshold", "in_joystickRumble", "in_joystickRumbleScale", "in_joystickLowBatteryRumbleThreshold",
	"in_joystickLowBatteryRumbleScale", "in_joystickLowBatteryNotice", "in_joystickTriggerRumble", "in_joystickAdaptiveTriggers",
	"in_joystickDirectionalRumble", "in_joystickLight", "in_joystickPlayerLight", "in_joystickContextActions",
	"in_joystickUseDedicatedLookAxes", "in_joystickMoveAxisX", "in_joystickMoveAxisY", "in_joystickLookAxisX",
	"in_joystickLookAxisY", "in_joystickUpAxis", "in_joystickUpAxisNegative",
	"in_gyro", "in_gyroSensitivity", "in_gyroYawRatio", "in_gyroPitchRatio", "in_gyroDeadZone", "in_gyroSmoothing",
	"in_gyroSpace", "in_gyroActivation", "in_gyroMenuPointer", "in_gyroInvertYaw", "in_gyroInvertPitch",
	"in_touchpadMode", "in_touchpadSensitivity", "in_promptStyle", "in_swapConfirmBack", "in_weaponWheelSensitivity",
	"in_aimAssist",
};
static const char *const key_keyboardMouseSettings[] = {
	"sensitivity", "m_pitch", "m_yaw", "m_smooth", "m_strafeSmooth", "m_strafeScale", "m_cpi", "m_filter", "m_maxMouseDelta",
	"cl_mouseAccel", "cl_mouseAccelOffset", "cl_mouseAccelPower", "cl_mouseSensCap", "in_freeLook",
};

static void Key_ResetSettings( const char *const *names, int count ) {
	for ( int i = 0; i < count; i++ ) {
		// a build without the controller layer has no such setting
		if ( cvarSystem->Find( names[i] ) != NULL ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "reset %s\n", names[i] ) );
		}
	}
}

/*
===================
Key_ResetController_f

Reset Controller: the controller's buttons and settings as shipped. Keyboard
and mouse bindings and settings are left as they are. The measured gyro
calibrations are kept: they describe the hardware, not a preference.
===================
*/
static void Key_ResetController_f( const idCmdArgs &args ) {
	(void)args;
	Key_ResetSettings( key_controllerSettings, sizeof( key_controllerSettings ) / sizeof( key_controllerSettings[0] ) );
	Key_ApplyControllerLayout( 1 );
	common->Printf( "controller: bindings and settings reset\n" );
}

/*
===================
Key_ResetKeyboardMouse_f

Reset Keyboard and Mouse: the keyboard's and mouse's bindings and the mouse
settings as shipped. Controller buttons and settings are left as they are.
===================
*/
static void Key_ResetKeyboardMouse_f( const idCmdArgs &args ) {
	(void)args;
	Key_ResetSettings( key_keyboardMouseSettings, sizeof( key_keyboardMouseSettings ) / sizeof( key_keyboardMouseSettings[0] ) );
	Key_UnbindFamily( false );
	(void)Key_ApplyConfigBinds( "default.cfg", false, false, NULL );
	(void)Key_ApplyConfigBinds( "openq4_defaults.cfg", false, false, NULL );
	common->Printf( "keyboard and mouse: bindings and settings reset\n" );
}

/*
===================
Key_Bind_f
===================
*/
void Key_Bind_f( const idCmdArgs &args ) {
	int			i, c, b;
	idStr		cmd;
	
	c = args.Argc();

	if ( c < 2 ) {
		common->Printf( "bind <key> [command] : attach a command to a key\n" );
		return;
	}
	b = idKeyInput::StringToKeyNum( args.Argv(1) );
	if ( b == -1 ) {
		common->Printf( "\"%s\" isn't a valid key\n", args.Argv(1) );
		return;
	}

	if ( c == 2 ) {
		if ( keys[b].binding.Length() ) {
			common->Printf( "\"%s\" = \"%s\"\n", args.Argv(1), keys[b].binding.c_str() );
		}
		else {
			common->Printf( "\"%s\" is not bound\n", args.Argv(1) );
		}
		return;
	}
	
	// copy the rest of the command line
	for ( i = 2; i < c; i++ ) {
		cmd += args.Argv( i );
		if ( i != (c-1) ) {
			cmd += " ";
		}
	}

	idKeyInput::SetBinding( b, cmd.c_str() );
}

/*
============
Key_BindUnBindTwo_f

binds keynum to bindcommand and unbinds if there are already two binds on the key
============
*/
void Key_BindUnBindTwo_f( const idCmdArgs &args ) {
	int c = args.Argc();
	if ( c < 3 ) {
		common->Printf( "bindunbindtwo <keynum> [command]\n" );
		return;
	}
	int key = atoi( args.Argv( 1 ) );
	idStr bind = args.Argv( 2 );
	if ( idKeyInput::NumBinds( bind ) >= 2 && !idKeyInput::KeyIsBoundTo( key, bind ) ) {
		idKeyInput::UnbindBinding( bind );
	}
	idKeyInput::SetBinding( key, bind );
}



/*
============
idKeyInput::WriteBindings

Writes lines containing "bind key value"
============
*/
void idKeyInput::WriteBindings( idFile *f ) {
	int		i;

	f->Printf( "unbindall\n" );

	for ( i = 0; i < MAX_KEYS; i++ ) {
		if ( keys[i].binding.Length() ) {
			const char *name = KeyNumToString( i, false );

			// handle the escape character nicely
			if ( !strcmp( name, "\\" ) ) {
				f->Printf( "bind \"\\\" \"%s\"\n", keys[i].binding.c_str() );
			} else {
				f->Printf( "bind \"%s\" \"%s\"\n", KeyNumToString( i, false ), keys[i].binding.c_str() );
			}
		}
	}
}

/*
============
Key_ListBinds_f
============
*/
void Key_ListBinds_f( const idCmdArgs &args ) {
	int		i;

	for ( i = 0; i < MAX_KEYS; i++ ) {
		if ( keys[i].binding.Length() ) {
			common->Printf( "%s \"%s\"\n", idKeyInput::KeyNumToString( i, false ), keys[i].binding.c_str() );
		}
	}
}

/*
============
Key_TestPromptDevice_f

openQ4: testPromptDevice controller|desktop sets the device the prompts name
until the next real input, so headless captures can show controller glyphs.
============
*/
static void Key_TestPromptDevice_f( const idCmdArgs &args ) {
	if ( args.Argc() == 2 ) {
		key_lastInputFamily = idStr::Icmp( args.Argv( 1 ), "controller" ) == 0 ? KEY_INPUT_FAMILY_CONTROLLER : KEY_INPUT_FAMILY_DESKTOP;
	}
	common->Printf( "prompt device: %s\n", key_lastInputFamily == KEY_INPUT_FAMILY_CONTROLLER ? "controller" : "desktop" );
}

/*
============
idKeyInput::KeysFromBinding
returns the localized name of the key for the binding
============
*/
/*
============
Key_AppendContextPrompt

openQ4 (PAD-06): the context buttons that reach a command, as a prompt: the
button to hold, where the context is a held one, then the button itself.
False where no context gives the command a button.
============
*/
static bool Key_AppendContextPrompt( idStr &keyName, const char *bind, bool emphasized ) {
	int holdKey = -1;
	const int contextKey = Key_ContextKeyForPrompt( bind, holdKey );
	if ( contextKey < 0 ) {
		return false;
	}
	if ( holdKey >= 0 ) {
		Key_AppendBindingPresentation( keyName, holdKey, emphasized );
		keyName += " + ";
	}
	Key_AppendBindingPresentation( keyName, contextKey, emphasized );
	return true;
}

static const char *Key_KeysFromBinding( const char *bind, bool emphasized ) {
	// GUI text understands ^ikHH as a procedural key presentation, where HH is
	// the full eight-bit id key number.  ^iKHH carries the same key with the
	// larger center-prompt presentation. Runtime hints deliberately show one key
	// from the device family the player used most recently; presenting every
	// persistent desktop and controller binding makes short HUD prompts noisy.
	idStr &keyName = Key_NextBindingPresentationBuffer();
	int firstKeyboard = -1;
	int firstMouse = -1;
	int firstController = -1;

	if ( bind && *bind ) {
		for ( int i = 0; i < MAX_KEYS; i++ ) {
			if ( BindingsEquivalent( idKeyInput::GetBinding( i ), bind ) ) {
				if ( Key_IsControllerKey( i ) ) {
					if ( firstController < 0 ) {
						firstController = i;
					}
				} else if ( Key_IsMouseKey( i ) ) {
					if ( firstMouse < 0 ) {
						firstMouse = i;
					}
				} else if ( firstKeyboard < 0 ) {
					firstKeyboard = i;
				}
			}
		}
	}

	int selected = -1;
	if ( key_lastInputFamily == KEY_INPUT_FAMILY_CONTROLLER ) {
		selected = firstController;
		if ( selected < 0 ) {
			selected = firstMouse >= 0 ? firstMouse : firstKeyboard;
		}
	} else {
		selected = firstMouse >= 0 ? firstMouse : firstKeyboard;
		if ( selected < 0 ) {
			selected = firstController;
		}
	}

	// openQ4 (PAD-06): a controller player with no button of their own for the
	// command is shown the context buttons that reach it
	if ( key_lastInputFamily == KEY_INPUT_FAMILY_CONTROLLER && firstController < 0 &&
			Key_AppendContextPrompt( keyName, bind, emphasized ) ) {
		return keyName.c_str();
	}

	if ( selected >= 0 ) {
		Key_AppendBindingPresentation( keyName, selected, emphasized );
	} else {
		keyName = common->GetLanguageDict()->GetString( "#str_07133" );
	}
	return keyName.c_str();
}

const char *idKeyInput::KeysFromBinding( const char *bind ) {
	return Key_KeysFromBinding( bind, false );
}

/*
============
idKeyInput::KeysFromBindingForPrompt

Returns the active-family binding using the emphasized center-prompt token.
============
*/
const char *idKeyInput::KeysFromBindingForPrompt( const char *bind ) {
	return Key_KeysFromBinding( bind, true );
}

/*
============
idKeyInput::KeysFromBindingForMenu

The settings widget shows a broader, device-independent list than gameplay
hints.  Bound pathological console-authored sets so the GUI's fixed line
buffer and stock-width row remain deterministic; the +N suffix preserves an
honest indication that additional bindings exist.
============
*/
const char *idKeyInput::KeysFromBindingForMenu( const char *bind ) {
	static const int MAX_MENU_BINDINGS = 6;
	idStr &keyName = Key_NextBindingPresentationBuffer();
	int shown = 0;
	int omitted = 0;
	if ( bind && *bind ) {
		for ( int i = 0; i < MAX_KEYS; i++ ) {
			if ( BindingsEquivalent( keys[i].binding.c_str(), bind ) ) {
				if ( shown < MAX_MENU_BINDINGS ) {
					// A row of key glyphs reads as a list without a word between
					// them, and the word cost the row a glyph's width each time.
					Key_AppendBindingPresentation( keyName, i, false, " " );
					shown++;
				} else {
					omitted++;
				}
			}
		}
	}
	if ( omitted > 0 ) {
		keyName.Append( va( " +%d", omitted ) );
	}
	if ( keyName.IsEmpty() ) {
		keyName = common->GetLanguageDict()->GetString( "#str_07133" );
	}
	return keyName.c_str();
}

/*
============
idKeyInput::BindingFromKey
returns the binding for the localized name of the key
============
*/
const char *idKeyInput::BindingFromKey( const char *key ) {
	const int keyNum = idKeyInput::StringToKeyNum( key );
	if ( keyNum<0 || keyNum >= MAX_KEYS ) {
		return NULL;
	}
	return keys[keyNum].binding.c_str();
}

/*
============
idKeyInput::UnbindBinding
============
*/
bool idKeyInput::UnbindBinding( const char *binding ) {
	bool unbound = false;
	int i;

	if ( binding && *binding ) {
		for ( i = 0; i < MAX_KEYS; i++ ) {
			if ( BindingsEquivalent( keys[i].binding.c_str(), binding ) ) {
				SetBinding( i, "" );
				unbound = true;
			}
		}
	}
	return unbound;
}

/*
============
idKeyInput::NumBinds
============
*/
int idKeyInput::NumBinds( const char *binding ) {
	int i, count = 0;

	if ( binding && *binding ) {
		for ( i = 0; i < MAX_KEYS; i++ ) {
			if ( BindingsEquivalent( keys[i].binding.c_str(), binding ) ) {
				count++;
			}
		}
	}
	return count;
}

/*
============
idKeyInput::KeyIsBountTo
============
*/
bool idKeyInput::KeyIsBoundTo( int keynum, const char *binding ) {
	if ( keynum >= 0 && keynum < MAX_KEYS ) {
		return BindingsEquivalent( keys[keynum].binding.c_str(), binding );
	}
	return false;
}

/*
============
idKeyInput::IsControllerKey
============
*/
bool idKeyInput::IsControllerKey( int keynum ) {
	return Key_IsValidKeyNum( keynum ) && Key_IsControllerKey( keynum );
}

/*
============
idKeyInput::BindForMenu

A settings menu's bind: the key joins the action's keys. An action keeps two
keys of each device family, so a third keyboard or mouse key replaces the two
it has and leaves its controller buttons, and a third controller button
replaces those and leaves the keyboard's. The stock bindunbindtwo counted both
families together, so binding a key to an action that came with a key and a
button of each cleared the controller.
============
*/
void idKeyInput::BindForMenu( int keynum, const char *binding ) {
	if ( !Key_IsValidKeyNum( keynum ) || binding == NULL || !*binding ) {
		return;
	}
	if ( !KeyIsBoundTo( keynum, binding ) ) {
		const bool controller = Key_IsControllerKey( keynum );
		int family = 0;
		for ( int i = 0; i < MAX_KEYS; i++ ) {
			if ( Key_IsControllerKey( i ) == controller && BindingsEquivalent( keys[i].binding.c_str(), binding ) ) {
				family++;
			}
		}
		if ( family >= 2 ) {
			for ( int i = 0; i < MAX_KEYS; i++ ) {
				if ( Key_IsControllerKey( i ) == controller && BindingsEquivalent( keys[i].binding.c_str(), binding ) ) {
					SetBinding( i, "" );
				}
			}
		}
	}
	SetBinding( keynum, binding );
}

/*
===================
idKeyInput::PreliminaryKeyEvent

Tracks global key up/down state
Called by the system for both key up and key down events
===================
*/
void idKeyInput::PreliminaryKeyEvent( int keynum, bool down ) {
	if ( !Key_IsValidKeyNum( keynum ) ) {
		return;
	}

	keys[keynum].down = down;
	if ( down ) {
		// openQ4 VR: the headset's menu pointer clicks as the mouse does, but it
		// is a controller in the player's hand, and prompts must name its buttons
		const bool vrPointer = Key_IsMouseKey( keynum ) && VR_HasInputFocus();
		key_lastInputFamily = ( Key_IsControllerKey( keynum ) || vrPointer ) ? KEY_INPUT_FAMILY_CONTROLLER : KEY_INPUT_FAMILY_DESKTOP;
	}

#ifdef ID_DOOM_LEGACY
	if ( down ) {
		lastKeys[ 0 + ( lastKeyIndex & 15 )] = keynum;
		lastKeys[16 + ( lastKeyIndex & 15 )] = keynum;
		lastKeyIndex = ( lastKeyIndex + 1 ) & 15;
		for ( int i = 0; cheatCodes[i] != NULL; i++ ) {
			int l = idLib::SizeToInt( strlen( cheatCodes[i] ), "idKeyInput::PreliminaryKeyEvent" );
			assert( l <= 16 );
			if ( idStr::Icmpn( lastKeys + 16 + ( lastKeyIndex & 15 ) - l, cheatCodes[i], l ) == 0 ) {
				common->Printf( "your memory serves you well!\n" );
				break;
			}
		}
	}
#endif
}

/*
===================
idKeyInput::PreliminaryMouseEvent
===================
*/
void idKeyInput::PreliminaryMouseEvent( int deltaX, int deltaY ) {
	if ( deltaX != 0 || deltaY != 0 ) {
		// openQ4 VR: while the headset has the controllers, cursor motion is the
		// weapon hand pointing at a menu (Sys_PostVRPointer), not a desk mouse
		key_lastInputFamily = VR_HasInputFocus() ? KEY_INPUT_FAMILY_CONTROLLER : KEY_INPUT_FAMILY_DESKTOP;
	}
}

/*
===================
idKeyInput::PreliminaryJoystickEvent

System joystick axes use the normalized -127..127 range.  Ignore small values
so a resting stick cannot continually steal hint ownership from the mouse.
===================
*/
void idKeyInput::PreliminaryJoystickEvent( int value ) {
	if ( idMath::Abs( value ) >= 16 ) {
		key_lastInputFamily = KEY_INPUT_FAMILY_CONTROLLER;
	}
}

/*
=================
idKeyInput::ExecKeyBinding
=================
*/
bool idKeyInput::ExecKeyBinding( int keynum ) {
	if ( !Key_IsValidKeyNum( keynum ) ) {
		return false;
	}

	// openQ4 (PAD-06): a context action takes the button's place
	const char *context = ContextBinding( keynum );
	if ( context != NULL ) {
		if ( usercmdGen->CommandStringUsercmdData( context ) ) {
			return false;
		}
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, context );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
		return true;
	}

	// commands that are used by the async thread
	// don't add text
	if ( keys[keynum].usercmdAction ) {
		return false;
	}

	// send the bound action
	if ( keys[keynum].binding.Length() ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, keys[keynum].binding.c_str() );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
	}
	return true;
}

/*
===================
idKeyInput::ClearStates
===================
*/
void idKeyInput::ClearStates( void ) {
	int i;
	// Lifecycle events may arrive before input initialization or after shutdown
	// while the fatal-error console still pumps the platform event queue.
	if ( keys == NULL ) return;

	for ( i = 0; i < MAX_KEYS; i++ ) {
		if ( keys[i].down ) {
			PreliminaryKeyEvent( i, false );
		}
		keys[i].down = false;
	}

	// clear the usercommand states
	usercmdGen->Clear();
}

/*
===================
idKeyInput::Init
===================
*/
void idKeyInput::Init( void ) {
	key_lastInputFamily = KEY_INPUT_FAMILY_DESKTOP;

	keys = new idKey[MAX_KEYS];

	// register our functions
	cmdSystem->AddCommand( "bind", Key_Bind_f, CMD_FL_SYSTEM, "binds a command to a key", idKeyInput::ArgCompletion_KeyName );
	cmdSystem->AddCommand( "bindunbindtwo", Key_BindUnBindTwo_f, CMD_FL_SYSTEM, "binds a key but unbinds it first if there are more than two binds" );
	cmdSystem->AddCommand( "unbind", Key_Unbind_f, CMD_FL_SYSTEM, "unbinds any command from a key", idKeyInput::ArgCompletion_KeyName );
	cmdSystem->AddCommand( "unbindall", Key_Unbindall_f, CMD_FL_SYSTEM, "unbinds any commands from all keys" );
	cmdSystem->AddCommand( "controllerLayout", Key_ControllerLayout_f, CMD_FL_SYSTEM, "rebinds the controller's buttons to a layout: classic, bumperjumper, onehandedleft or onehandedright" );
	cmdSystem->AddCommand( "resetController", Key_ResetController_f, CMD_FL_SYSTEM, "restores the controller's shipped bindings and settings; keyboard and mouse are left alone" );
	cmdSystem->AddCommand( "resetKeyboardMouse", Key_ResetKeyboardMouse_f, CMD_FL_SYSTEM, "restores the keyboard's and mouse's shipped bindings and the mouse settings; the controller is left alone" );
	cmdSystem->AddCommand( "listBinds", Key_ListBinds_f, CMD_FL_SYSTEM, "lists key bindings" );
	cmdSystem->AddCommand( "testPromptDevice", Key_TestPromptDevice_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "sets the device prompts name until the next input: controller or desktop" );
}

/*
===================
idKeyInput::Shutdown
===================
*/
void idKeyInput::Shutdown( void ) {
	delete [] keys;
	keys = NULL;
}
