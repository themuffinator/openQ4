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




#include <algorithm>
#include <vector>

#include "NativeInputDispatch.h"
#include "Session_local.h"
#include "NativeInputPublications.h"
#include "../ui/Rectangle.h"
#include "ArenaCampaign.h"
#include "../ui/ListGUILocal.h"
#include "../ui/RetainedUI.h"
#include "../ui/UserInterfaceManaged.h"
#include "../ui/UserInterfaceRetained.h"
#ifndef ID_DEDICATED
#include "../ui/retained/PlayerSetupRequest.h"
#endif
#include "../sound/snd_local.h"

#if defined( USE_SDL3 )
#include <SDL3/SDL.h>
#endif

static bool Session_AndroidSetupAvailable() {
#if defined( __ANDROID__ ) && !defined( OPENQ4_SIGMATOUCH )
	return true;
#else
	return false;
#endif
}

// OpenQ4Activity's SETUP_PAGE_* arguments: setup's home, or the touch layout editor.
enum { ANDROID_SETUP_HOME = 0, ANDROID_SETUP_TOUCH_LAYOUT = 1 };

static void Session_OpenAndroidSetup( int page = 0 ) {
#if defined( __ANDROID__ ) && !defined( OPENQ4_SIGMATOUCH )
	// SDL posts to OpenQ4Activity's main-thread handler. The existing Android
	// lifecycle owns input release, audio suspension and the paused SP return.
	// Keep the engine and its storage lease alive while setup is in front.
	if ( !SDL_SendAndroidMessage( 0x8000, page ) ) {
		common->Warning( "Could not open Android setup: %s", SDL_GetError() );
	}
#else
	(void)page;
#endif
}

extern idCVar com_skipLogoVideos;
#ifdef OPENQ4_RENDERER_VK_MODULE
extern thread_local glconfig_t glConfig;
#else
extern glconfig_t glConfig;
#endif

idCVar	idSessionLocal::gui_configServerRate( "gui_configServerRate", "0", CVAR_GUI | CVAR_ARCHIVE | CVAR_ROM | CVAR_INTEGER, "" );
idCVar gui_set_sys_scroll( "gui_set_sys_scroll", "0", CVAR_GUI | CVAR_INTEGER, "display menu scroll step", 0, 28 );
idCVar gui_set_audio_scroll( "gui_set_audio_scroll", "0", CVAR_GUI | CVAR_INTEGER, "audio menu scroll step", 0, 6 );
idCVar gui_set_game_scroll( "gui_set_game_scroll", "0", CVAR_GUI | CVAR_INTEGER, "game menu scroll step", 0, 78 );
// The opt-ins are unarchived, so each lasts until the player quits, and
// CVAR_NOCHEAT, because the engine counts any other unarchived CVar as a
// cheat: joining or hosting a server without cheats resets it to its default,
// and a player cannot set it during a match, where both pages open.
idCVar ui_retainedSystem( "ui_retainedSystem", "0", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT, "use the retained SYSTEM page even while ui_retained is 0; ui_retained includes it while every stock setting works there" );
idCVar ui_retainedMultiplayer( "ui_retainedMultiplayer", "0", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT, "use the in-development retained multiplayer menu card, which ui_retained includes only once all its pages are built" );
// The single gate for the retained (RmlUi) interface, enabled by default. Each
// screen presents its stock GUI instead when its retained document is missing
// or cannot load, or when the legacy menu it covers lacks a page it hands off
// to (see idSessionLocal::FindRetainedGui). While it is 0 no session route
// creates a retained view and the stock GUIs present every screen. Archived,
// so an explicit player preference persists across launches.
idCVar ui_retained( "ui_retained", "1", CVAR_GUI | CVAR_BOOL | CVAR_ARCHIVE, "use the retained interface by default; 0 selects the stock interface" );
idCVar ui_hudScale( "ui_hudScale", "1", CVAR_GUI | CVAR_FLOAT | CVAR_ARCHIVE, "retained status bar scale, independent of menus", .5f, 2.0f );
idCVar ui_hudSafeInset( "ui_hudSafeInset", "0", CVAR_GUI | CVAR_FLOAT | CVAR_ARCHIVE, "retained status bar safe inset in percent of viewport", 0.0f, 10.0f );
idCVar ui_retainedCaptureLoadFade( "ui_retainedCaptureLoadFade", "0", CVAR_GUI | CVAR_BOOL | CVAR_NOCHEAT, "diagnostic: use the engine screenshot command to capture the middle and end of the retained loading fade" );

static bool Session_RetainedScreensEnabled( void ) {
#ifdef ID_DEDICATED
	return false;
#else
	return ui_retained.GetBool();
#endif
}

// The retained SYSTEM page, reached from the legacy menu's SYSTEM button.
static const char *const RETAINED_SYSTEM_GUI = "guis/menu/settings/system.q4ui";
// Settings the stock SYSTEM page offers that the retained page has no control
// for yet. While any remain, the gate leaves the stock SYSTEM page in place
// and only ui_retainedSystem opts into the retained one. ui_retained_gate.py
// keeps this list equal to what the two pages offer.
static const char *const RETAINED_SYSTEM_MISSING_SETTINGS[] = {
	NULL
};
// Settings the retained page has a control for that cannot yet do what the
// stock page did, which keep it out of the gate too. None remain: a
// Performance Preset or Auto-Detect applies through the combined executor
// (SystemApplyClass::Combined), and the size fields take typed digits.
// ui_retained_gate.py derives this list from the code, so the page stays in
// the gate while both lists are empty and leaves it when a reason returns.
static const char *const RETAINED_SYSTEM_INCOMPLETE_SETTINGS[] = {
	NULL
};

static bool Session_RetainedSystemEnabled( void ) {
	return ui_retainedSystem.GetBool() || ( Session_RetainedScreensEnabled() &&
		RETAINED_SYSTEM_MISSING_SETTINGS[0] == NULL && RETAINED_SYSTEM_INCOMPLETE_SETTINGS[0] == NULL );
}

// The multiplayer menu card (section 14.18), which covers the game's own menu
// while it is open over a match: Escape, and Welcome while the player has not
// answered the join offer.
static const char *const RETAINED_MP_ESCAPE_GUI = "guis/menu/mp_escape.q4ui";
static const char *const RETAINED_MP_WELCOME_GUI = "guis/menu/mp_welcome.q4ui";
// The version of the game's answer to retainedMultiplayerCover that the card
// relies on; a game module without it keeps its stock menu.
static const int RETAINED_MP_PROTOCOL = 4;
// Each card's pages that still hand off to their stock pages: none now. While
// any remain, the gate leaves that card's stock menu in place and only
// ui_retainedMultiplayer opts into it. ui_retained_gate.py keeps each list
// equal to its card's hand-off pages. (A built page may still open its stock
// page for what it cannot show: the rows of a Match Control list past the
// card's.)
static const char *const RETAINED_MP_ESCAPE_MISSING_PAGES[] = {
	NULL
};
static const char *const RETAINED_MP_WELCOME_MISSING_PAGES[] = {
	NULL
};
// What the stock Create Server page offers that Multiplayer's retained Create
// Server sub-page does not yet: nothing now. While anything remains, CREATE
// SERVER opens the stock page and only ui_retainedMultiplayer opts into the
// sub-page. ui_retained_gate.py keeps this list equal to what the two pages
// offer.
static const char *const RETAINED_CREATE_SERVER_MISSING[] = {
	NULL
};
// What the stock Join Game page (the server browser) offers that
// Multiplayer's retained Join Game sub-page does not yet: nothing now. While
// anything remains, JOIN GAME opens the stock browser and only
// ui_retainedMultiplayer opts into the sub-page. ui_retained_gate.py keeps
// this list equal to what the two pages offer (the stock PunkBuster column, a
// discontinued service, needs no replacement).
static const char *const RETAINED_JOIN_GAME_MISSING[] = {
	NULL
};
// The Team page's action slots (card.team_action); the game derives each
// slot's action again from the player's state when it is chosen.
static const int RETAINED_MP_TEAM_SLOTS = 3;
// The Vote page's drafted fields, whose controls request "<verb> <value>",
// in the order of the game's retainedVoteField_t; the game checks each value
// against its field's rules.
static const char *const RETAINED_MP_VOTE_FIELDS[] = {
	"mpVoteMap", "mpVoteGameType", "mpVoteTimeLimit", "mpVoteFragLimit", "mpVoteCaptureLimit", "mpVoteTourneyLimit",
	"mpVoteControlTime", "mpVoteBalance", "mpVoteShuffle", "mpVoteRestart", "mpVoteBuying", "mpVoteKick"
};
// The Admin page's drafted server settings, whose controls request "<verb>
// <value>", in the order of the game's retainedVoteField_t without the kick;
// the game checks each value against its field's rules.
static const char *const RETAINED_MP_ADMIN_FIELDS[] = {
	"mpAdminMap", "mpAdminGameType", "mpAdminTimeLimit", "mpAdminFragLimit", "mpAdminCaptureLimit", "mpAdminTourneyLimit",
	"mpAdminControlTime", "mpAdminBalance", "mpAdminShuffle", "mpAdminRestart", "mpAdminBuying"
};
// The card's text fields' longest entries in bytes: the remote console
// password, a console command, the Match page's referee credential, and the
// player's name and clan as the stock fields took them.
static const int RETAINED_MP_PASSWORD_BYTES = 64;
static const int RETAINED_MP_COMMAND_BYTES = 200;
static const int RETAINED_MP_CREDENTIAL_BYTES = 64;
static const int RETAINED_MP_NAME_BYTES = 23;
static const int RETAINED_MP_CLAN_BYTES = 12;
// The Admin page's player and ban lists (RETAINED_ADMIN_PLAYERS and
// RETAINED_ADMIN_BANS in the game).
static const int RETAINED_MP_ADMIN_BANS = 32;
// The Settings pages' value controls, "<verb> <row>": the three model lists
// (the game's slots 0 to 2) and the crosshair.
static const char *const RETAINED_MP_APPEARANCE_VALUES[] = {
	"mpModelSelf", "mpModelEnemy", "mpModelTeam", "mpCrosshair"
};
// The stock rail color swatches (card.rail); the game holds their settings.
static const int RETAINED_MP_RAIL_COLORS = 7;
// The Players page names players by client number (card.client), which the
// game checks against its lists again.
static const int RETAINED_MP_CLIENTS = MAX_ASYNC_CLIENTS;
// The Match page's actions, by the card's index (card.match_op): the fixed
// Match Control tokens the game's own menu emits for them, so the card never
// sends text of its own (idMultiplayerGame::HandleMatchControlCommand). The
// first, a refresh, asks the game to project its view again.
static const char *const RETAINED_MP_MATCH_TOKENS[] = {
	"refresh", "ready_toggle", "team_ready_toggle", "arm_force_ready", "timeout", "tech_pause", "resume",
	"arm_forfeit", "arm_abort", "referee_logout", "action_side_a", "action_side_b",
	"follow_prev", "follow_next", "follow_free", "confirm", "cancel_confirm",
	"team_join_marine", "team_join_strogg", "team_spectate", "queue_join", "queue_defer", "queue_leave",
	"roster_accept", "roster_leave", "roster_invite", "arm_roster_remove", "arm_roster_substitute", "role_assign",
	"team_lock_toggle", "broadcaster_set", "arm_participant_remove", "series_contestant_bind",
	"proposal_create", "proposal_yes", "proposal_no", "proposal_abstain", "proposal_cancel",
	"rules_select_profile", "rules_stage_field", "arm_rules_commit", "rules_discard",
	"series_stage", "arm_series_start", "arm_series_cancel", "arm_series_advance",
	"arm_veto_ban", "arm_veto_pick", "arm_veto_decider", "arm_veto_side_marine", "arm_veto_side_strogg"
};
static const int RETAINED_MP_MATCH_ACTIONS = static_cast<int>( sizeof( RETAINED_MP_MATCH_TOKENS ) / sizeof( RETAINED_MP_MATCH_TOKENS[0] ) );
// The Match page's lists, by the card's index (card.match_list): the stock
// list on the game's menu and the fixed token that selects its row. The card
// names a row by its index (card.match_row); the session sets the stock list's
// own selection to it, and the game reads it back and checks it against its
// model again, as for the stock list.
static const char *const RETAINED_MP_MATCH_LISTS[][2] = {
	{ "match_team_rows", "select_team_row" }, { "match_replacement_rows", "select_replacement_row" },
	{ "match_proposal_rows", "select_proposal_row" }, { "match_profile_rows", "select_profile_row" },
	{ "match_rule_rows", "select_rule_row" }, { "match_series_map_rows", "select_series_map" }
};
static const int RETAINED_MP_MATCH_LIST_COUNT = static_cast<int>( sizeof( RETAINED_MP_MATCH_LISTS ) / sizeof( RETAINED_MP_MATCH_LISTS[0] ) );
// The most rows a Match Control list holds (MP_MATCH_CONTROL_MAX_TEAM_ROWS).
static const int RETAINED_MP_MATCH_ROWS = 128;
// The Match page's choices, "<verb> <value>", which the stock choices keep on
// the game's menu and the game reads as it acts: the role an invitation or an
// assignment gives, 1 to 4 (the protocol's roster roles), the proposal a
// ballot or a cancellation goes to, 0 the global one and 1 the team's, and
// the format a staged series takes, best of one, three or five.
static const char *const RETAINED_MP_MATCH_CHOICES[] = { "mpMatchRole", "mpMatchScope", "mpMatchSeriesProfile" };
static const int RETAINED_MP_MATCH_ROLES = 4;
static const char *const RETAINED_MP_MATCH_SCOPES[] = { "global", "side" };
static const char *const RETAINED_MP_MATCH_SERIES_PROFILES[] = { "best_of_one", "best_of_three", "best_of_five" };
// The value to stage for a Match Control rule, "mpMatchRuleValue <value>",
// which the stock field keeps on the game's menu: at most the largest value a
// rule takes (the readiness threshold's basis points). The game checks the
// rule's own bounds as it stages it.
static const int RETAINED_MP_MATCH_RULE_VALUE_MAX = 10000;
// The stock menu buttons the card's hand-offs press, by the card's page
// index (card.stock_page), in the generator's order.
static const char *const RETAINED_MP_STOCK_PAGES[] = {
	"main_b_jointeam", "main_b_players", "main_b_vote", "main_b_matchcontrol",
	"main_b_settings", "main_b_voiceconfig", "main_b_serverinfo", "main_b_admin"
};
// The buttons the Welcome card's hand-offs press, by its page index: the join
// panel's while the join offer stands (the stock menu's initial_join), the
// menu column's once the panel has closed; NULL for a page with no stock
// hand-off.
static const char *const RETAINED_MP_WELCOME_STOCK_PAGES[] = {
	NULL, NULL, NULL, "qj_b_settings"
};
static const char *const RETAINED_MP_WELCOME_MENU_PAGES[] = {
	NULL, NULL, NULL, "main_b_settings"
};
// The Welcome card's Join page actions (card.welcome_action); the game derives
// each one's action again from the player's state when it is chosen.
static const int RETAINED_MP_WELCOME_SLOTS = 4;
// How long a hand-off waits for the stock menu's column to show its button.
static const int RETAINED_MP_HANDOFF_MSEC = 500;

static bool Session_RetainedMultiplayerEnabled( bool welcome ) {
	const char *const *missing = welcome ? RETAINED_MP_WELCOME_MISSING_PAGES : RETAINED_MP_ESCAPE_MISSING_PAGES;
	return ui_retainedMultiplayer.GetBool() || ( Session_RetainedScreensEnabled() && missing[0] == NULL );
}

// The match summary card (section 14.14) covers the game's summary at the end
// of a match as the menu cards cover its menu; all its pages are built.
static const char *const RETAINED_MP_SUMMARY_GUI = "guis/menu/mp_summary.q4ui";
// The single-player Arena's pause card (section 13.7) covers the game's Arena
// pause menu as the cards cover the multiplayer menu; all its actions are built.
static const char *const RETAINED_ARENA_PAUSE_GUI = "guis/menu/arena_pause.q4ui";

#ifndef ID_DEDICATED
static bool Session_RetainedSummaryEnabled( void ) {
	return ui_retainedMultiplayer.GetBool() || Session_RetainedScreensEnabled();
}

static bool Session_RetainedCreateServerEnabled( void ) {
	return Session_RetainedScreensEnabled() && ( ui_retainedMultiplayer.GetBool() || RETAINED_CREATE_SERVER_MISSING[0] == NULL );
}

static bool Session_RetainedJoinGameEnabled( void ) {
	return Session_RetainedScreensEnabled() && ( ui_retainedMultiplayer.GetBool() || RETAINED_JOIN_GAME_MISSING[0] == NULL );
}

static bool Session_RetainedArenaPauseEnabled( void ) {
	return ui_retainedMultiplayer.GetBool() || Session_RetainedScreensEnabled();
}

// The stock button a card's hand-off to `page` presses, or NULL; `joinPanel`
// says whether the stock menu shows the join panel.
static const char *Session_RetainedMultiplayerStockPage( bool welcome, int page, bool joinPanel ) {
	const char *const *pages = !welcome ? RETAINED_MP_STOCK_PAGES : joinPanel ? RETAINED_MP_WELCOME_STOCK_PAGES : RETAINED_MP_WELCOME_MENU_PAGES;
	const int count = welcome ? static_cast<int>( sizeof( RETAINED_MP_WELCOME_STOCK_PAGES ) / sizeof( RETAINED_MP_WELCOME_STOCK_PAGES[0] ) )
		: static_cast<int>( sizeof( RETAINED_MP_STOCK_PAGES ) / sizeof( RETAINED_MP_STOCK_PAGES[0] ) );
	return page >= 0 && page < count ? pages[ page ] : NULL;
}

// A value control's request, "<verb> <value>": the verb's index in `verbs`
// and a whole number from -1 to 999.
static bool Session_RetainedValueRequest( const char *request, const char *const *verbs, int count, int &field, int &value ) {
	const char *space = strchr( request, ' ' );
	if ( space == NULL || space == request ) {
		return false;
	}
	const int verbLength = static_cast<int>( space - request );
	field = -1;
	for ( int i = 0; i < count; i++ ) {
		if ( static_cast<int>( strlen( verbs[ i ] ) ) == verbLength && !idStr::Icmpn( request, verbs[ i ], verbLength ) ) {
			field = i;
		}
	}
	const char *number = space + 1;
	const int length = static_cast<int>( strlen( number ) );
	bool digits = field >= 0 && length >= 1 && length <= 3;
	for ( int i = 0; digits && i < length; i++ ) {
		digits = ( number[ i ] >= '0' && number[ i ] <= '9' ) || ( i == 0 && number[ i ] == '-' && length > 1 );
	}
	value = digits ? atoi( number ) : 0;
	return digits && value >= -1 && value <= 999;
}

// The retained AUDIO page (tools/ui/retained_audio.py): its document, its
// value verbs in the order HandleRetainedAudioRequest takes them, the device
// list's rows and the speaker counts its choice names.
static const char *const RETAINED_AUDIO_GUI = "guis/menu/settings/audio.q4ui";
static const char *const RETAINED_AUDIO_VERBS[] = {
	"audioVolume", "audioMusic", "audioDialogue", "audioBackend", "audioDevice", "audioEax", "audioSpeakers", "audioHrtf",
	"audioSubtitles", "audioSubtitleSize", "audioSubtitleBackground", "audioSubtitleSpeakers"
};
static const int RETAINED_AUDIO_VERB_COUNT = sizeof( RETAINED_AUDIO_VERBS ) / sizeof( RETAINED_AUDIO_VERBS[0] );
static const int RETAINED_AUDIO_DEVICES = 16;
static const int RETAINED_AUDIO_SPEAKERS[] = { 0, 2, 6 };
// The retained GAME OPTIONS page (tools/ui/retained_game.py): its document,
// the verbs of the rows the stock page computes, in the order
// HandleRetainedGameRequest takes them, and the languages its list names, in
// #str_229908's order.
static const char *const RETAINED_GAME_GUI = "guis/menu/settings/game.q4ui";
static const char *const RETAINED_GAME_VERBS[] = {
	"gameGunPosition", "gameForceModel", "gameCrosshair", "gameCorpseTime", "gameLanguage"
};
static const int RETAINED_GAME_VERB_COUNT = sizeof( RETAINED_GAME_VERBS ) / sizeof( RETAINED_GAME_VERBS[0] );
static const char *const RETAINED_GAME_LANGUAGES[] = {
	"english", "spanish", "french", "italian", "polish", "russian", "german", "brazilian", "czech", "hungarian", "turkish", "ukrainian"
};
static const int RETAINED_GAME_LANGUAGE_COUNT = sizeof( RETAINED_GAME_LANGUAGES ) / sizeof( RETAINED_GAME_LANGUAGES[0] );
// The retained CONTROLS page (tools/ui/retained_controls.py): its document and
// the bindings of its rows, in its order (the stock page's tabs in turn).
static const char *const RETAINED_CONTROLS_GUI = "guis/menu/settings/controls.q4ui";
static const char *const RETAINED_CONTROLS_BINDINGS[] = {
	"_forward", "_back", "_moveleft", "_moveright", "_moveUp", "_moveDown", "_left", "_right", "_strafe", "_speed", "_impulse0",
	"_impulse1", "_impulse2", "_impulse3", "_impulse4", "_impulse5", "_impulse6", "_impulse7", "_impulse8", "_impulse9",
	"_impulse10", "buymenu", "_quickChat", "_attack", "_impulse15", "_impulse14", "_impulse13", "_lookUp", "_lookDown", "_mlook",
	"centerview", "_zoom", "_impulse19", "_impulse50", "screenshot", "_weaponWheel", "savegame quick", "loadgame quick",
	"clientMessageMode", "clientMessageMode 1", "_impulse28", "_impulse29", "_ingamestats", "_impulse17", "emote salute",
	"emote cheer", "emote taunt", "emote grab_a", "_voicechat"
};
static const int RETAINED_CONTROLS_ROWS = sizeof( RETAINED_CONTROLS_BINDINGS ) / sizeof( RETAINED_CONTROLS_BINDINGS[0] );
// The retained SETTINGS page (tools/ui/retained_settings_page.py): its
// sections, each with its verb and the stock settings screen's panel
// (desktop::dest) that stands in where its page cannot open; the screen
// takes over after the page leaves, as the Multiplayer page's stock pages do.
typedef struct {
	const char *	verb;
	const char *	section;
	int				dest;
} retainedSettingsSection_t;
static const retainedSettingsSection_t RETAINED_SETTINGS_SECTIONS[] = {
	{ "settingsControls",	"controls",	4 },
	{ "settingsGame",		"game",		21 },
	{ "settingsDisplay",	"display",	22 },
	{ "settingsAudio",		"audio",	36 },
};
static const int RETAINED_SETTINGS_SECTION_COUNT = sizeof( RETAINED_SETTINGS_SECTIONS ) / sizeof( RETAINED_SETTINGS_SECTIONS[0] );
// The sections that are its sub-pages (the generator's DEEPER): each's kind,
// section, document, and the event that takes the SETTINGS page one level
// deeper to it (BeginRetainedSubpage). DISPLAY's is the SYSTEM page, where
// ui_retainedSystem has it on.
typedef struct {
	retainedSubpage_t	kind;
	const char *		section;
	const char *		gui;
	const char *		enter;
} retainedSettingsSubpage_t;
static const retainedSettingsSubpage_t RETAINED_SETTINGS_SUBPAGES[] = {
	{ RETAINED_SUBPAGE_SETTINGS_CONTROLS,	"controls",	RETAINED_CONTROLS_GUI,	"subpageEnterControls" },
	{ RETAINED_SUBPAGE_SETTINGS_GAME,		"game",		RETAINED_GAME_GUI,		"subpageEnterGame" },
	{ RETAINED_SUBPAGE_SETTINGS_AUDIO,		"audio",	RETAINED_AUDIO_GUI,		"subpageEnterAudio" },
	{ RETAINED_SUBPAGE_SETTINGS_SYSTEM,		"display",	RETAINED_SYSTEM_GUI,	"subpageEnterDisplay" },
};
static const retainedSettingsSubpage_t *Session_RetainedSettingsSubpage( retainedSubpage_t kind ) {
	for ( const retainedSettingsSubpage_t &subpage : RETAINED_SETTINGS_SUBPAGES ) {
		if ( subpage.kind == kind ) {
			return &subpage;
		}
	}
	return NULL;
}

// "mpMatchRuleValue <value>": a whole number from 0 to the largest value a
// Match Control rule takes, in at most five digits.
static bool Session_RetainedRuleValueRequest( const char *request, int &value ) {
	static const char verb[] = "mpMatchRuleValue ";
	const int verbLength = static_cast<int>( sizeof( verb ) ) - 1;
	if ( idStr::Icmpn( request, verb, verbLength ) != 0 ) {
		return false;
	}
	const char *number = request + verbLength;
	const int length = static_cast<int>( strlen( number ) );
	bool digits = length >= 1 && length <= 5;
	for ( int i = 0; digits && i < length; i++ ) {
		digits = number[ i ] >= '0' && number[ i ] <= '9';
	}
	value = digits ? atoi( number ) : 0;
	return digits && value <= RETAINED_MP_MATCH_RULE_VALUE_MAX;
}

// The name of the key a prompt would show for `binding` (from the device
// family the player used last), or "" when none is bound. The key lists name
// a key by number ("^ikHH"), which only the stock GUIs draw.
static idStr Session_RetainedBoundKey( const char *binding ) {
	const char *keys = idKeyInput::KeysFromBinding( binding );
	if ( keys == NULL || keys[0] != '^' || keys[1] != 'i' || ( keys[2] != 'k' && keys[2] != 'K' ) ) {
		return "";
	}
	int keynum = 0;
	for ( int i = 3; i < 5; i++ ) {
		const char c = keys[ i ];
		const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if ( digit < 0 ) {
			return "";
		}
		keynum = keynum * 16 + digit;
	}
	const char *name = idKeyInput::KeyNumToString( keynum, true );
	return name != NULL && idStr::Icmpn( name, "#str_", 5 ) != 0 ? name : "";
}
#endif


static const int MENU_CONTROLLER_AXIS_THRESHOLD = 50;
static const int MENU_CONTROLLER_REPEAT_INITIAL_MSEC = 320;
static const int MENU_CONTROLLER_REPEAT_MSEC = 110;
// The right stick's rest zone in a retained menu, out of 127.
static const int MENU_CONTROLLER_DETAIL_REST = 16;

typedef struct menuControllerRepeat_s {
	int		key;
	bool	fromAnalog;
	int		nextTime;
} menuControllerRepeat_t;

typedef struct mainMenuGunPositionPreset_s {
	float	x;
	float	y;
	float	z;
	bool	weaponFovEffect;
} mainMenuGunPositionPreset_t;

static menuControllerRepeat_t menuControllerRepeat = { 0, false, 0 };

static const int MENU_SETTINGS_PAGE_SCROLL_STEP = 6;
static const mainMenuGunPositionPreset_t MAINMENU_GUN_POSITION_PRESETS[] = {
	{ 1.0f, 2.0f, -1.0f, true },
	{ 1.0f, -5.0f, -1.0f, true },
	{ 1.0f, 0.0f, -1.0f, true }
};
static const char *MAINMENU_FORCE_MODEL_MARINE = "model_player_marine_helmeted_bright";
static const char *MAINMENU_FORCE_MODEL_STROGG = "model_player_tactical_transfer_bright";
static const float MAINMENU_CORPSE_TIME_PRESET_VALUES[] = {
	0.0f,
	-1.0f,
	5.0f,
	10.0f,
	15.0f,
	30.0f,
	60.0f
};

static bool ApplyMainMenuSettingsScrollPage( idUserInterface *gui, const char *pageName, bool requireVisiblePage );
static void SyncMainMenuSettingsScrollPages( idUserInterface *gui );

static int MenuControllerAbs( int value ) {
	return value < 0 ? -value : value;
}

static void ClearMenuControllerRepeatState( void ) {
	menuControllerRepeat.key = 0;
	menuControllerRepeat.fromAnalog = false;
	menuControllerRepeat.nextTime = 0;
}

static int GetHeldControllerMenuKey( void ) {
	if ( idKeyInput::IsDown( K_JOY9 ) ) {
		return K_JOY9;
	}
	if ( idKeyInput::IsDown( K_JOY10 ) ) {
		return K_JOY10;
	}
	if ( idKeyInput::IsDown( K_JOY12 ) ) {
		return K_JOY12;
	}
	if ( idKeyInput::IsDown( K_JOY11 ) ) {
		return K_JOY11;
	}
	if ( idKeyInput::IsDown( K_JOY1 ) ) {
		return K_JOY1;
	}
	if ( idKeyInput::IsDown( K_JOY2 ) ) {
		return K_JOY2;
	}
	return 0;
}

static int GetAnalogControllerMenuKey( void ) {
	int moveX = 0;
	int moveY = 0;
	const bool haveX = Sys_GetJoystickAxisState( AXIS_YAW, moveX );
	const bool haveY = Sys_GetJoystickAxisState( AXIS_PITCH, moveY );
	if ( !haveX && !haveY ) {
		return 0;
	}

	const int absX = MenuControllerAbs( moveX );
	const int absY = MenuControllerAbs( moveY );
	if ( absX < MENU_CONTROLLER_AXIS_THRESHOLD && absY < MENU_CONTROLLER_AXIS_THRESHOLD ) {
		return 0;
	}

	if ( absX > absY ) {
		return moveX > 0 ? K_JOY11 : K_JOY12;
	}
	return moveY > 0 ? K_JOY9 : K_JOY10;
}

static int GetControllerMenuNavigationKey( bool &fromAnalog ) {
	fromAnalog = false;

	const int heldKey = GetHeldControllerMenuKey();
	if ( heldKey != 0 ) {
		return heldKey;
	}

	const int analogKey = GetAnalogControllerMenuKey();
	if ( analogKey != 0 ) {
		fromAnalog = true;
		return analogKey;
	}

	return 0;
}

static void PumpControllerMenuNavigation( idSessionLocal *session ) {
	if ( session == NULL ) {
		ClearMenuControllerRepeatState();
		return;
	}

	bool fromAnalog = false;
	const int key = GetControllerMenuNavigationKey( fromAnalog );
	if ( key == 0 ) {
		ClearMenuControllerRepeatState();
		return;
	}

	const int now = common->GetPresentationTime();
	if ( menuControllerRepeat.key != key || menuControllerRepeat.fromAnalog != fromAnalog ) {
		menuControllerRepeat.key = key;
		menuControllerRepeat.fromAnalog = fromAnalog;
		menuControllerRepeat.nextTime = now + ( fromAnalog ? 0 : MENU_CONTROLLER_REPEAT_INITIAL_MSEC );
	}

	if ( now < menuControllerRepeat.nextTime ) {
		return;
	}

	sysEvent_t event;
	memset( &event, 0, sizeof( event ) );
	event.evType = SE_KEY;
	event.evValue = key;
	event.evValue2 = 1;

	session->MenuEvent( &event );
	if ( session->RetainedInputTarget() ) {
		// Retained controls pair every press with its release, and repeat a
		// press until it is released; the stick never sends a release. This
		// pump emits one navigation step per repeat, so complete it at once.
		event.evValue2 = 0;
		session->MenuEvent( &event );
	}
	menuControllerRepeat.nextTime = now + MENU_CONTROLLER_REPEAT_MSEC;
}

// The right stick (section 13.5; the left one when Southpaw swaps them) in a
// retained menu: up and down scroll its detail area, left and right turn its
// model preview. Past the rest zone the rate follows the deflection, timed
// from the previous frame of the same push (at most 100 ms of it).
#ifndef ID_DEDICATED
static int menuDetailStickTime = -1;

static float MenuDetailStickAxis( int value ) {
	const int beyond = MenuControllerAbs( value ) - MENU_CONTROLLER_DETAIL_REST;
	if ( beyond <= 0 ) {
		return 0.0f;
	}
	const float deflection = idMath::ClampFloat( 0.0f, 1.0f, beyond / static_cast<float>( 127 - MENU_CONTROLLER_DETAIL_REST ) );
	return value < 0 ? -deflection : deflection;
}
#endif

static void PumpControllerDetailStick( idSessionLocal *session ) {
#ifndef ID_DEDICATED
	idUserInterface *gui = session != NULL ? session->RetainedStickTarget() : NULL;
	int lookX = 0;
	int lookY = 0;
	if ( gui == NULL || !Sys_GetJoystickAxisState( AXIS_SIDE, lookX ) || !Sys_GetJoystickAxisState( AXIS_FORWARD, lookY ) ) {
		menuDetailStickTime = -1;
		return;
	}
	// Inverted look turns the view in play; the menus keep the stick's sense.
	if ( cvarSystem->GetCVarBool( "in_joystickInvertLook" ) ) {
		lookY = -lookY;
	}
	const float x = MenuDetailStickAxis( lookX );
	const float y = MenuDetailStickAxis( lookY );
	if ( x == 0.0f && y == 0.0f ) {
		menuDetailStickTime = -1;
		return;
	}
	const int now = common->GetPresentationTime();
	const int gap = now - menuDetailStickTime;
	const int elapsed = menuDetailStickTime >= 0 && gap > 0 && gap <= 250 ? Min( gap, 100 ) : 0;
	menuDetailStickTime = now;
	if ( elapsed > 0 ) {
		session->RetainedDetailStick( gui, x, y, elapsed * 0.001f );
	}
#endif
}

static void ApplyMainMenuGunPositionChoice( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	const int choice = gui->State().GetInt( "g_gunXYZ" );
	const int numChoices = sizeof( MAINMENU_GUN_POSITION_PRESETS ) / sizeof( MAINMENU_GUN_POSITION_PRESETS[0] );
	if ( choice < 0 || choice >= numChoices ) {
		return;
	}

	const mainMenuGunPositionPreset_t &preset = MAINMENU_GUN_POSITION_PRESETS[ choice ];
	cvarSystem->SetCVarFloat( "g_gunX", preset.x );
	cvarSystem->SetCVarFloat( "g_gunY", preset.y );
	cvarSystem->SetCVarFloat( "g_gunZ", preset.z );
	if ( cvarSystem->Find( "g_weaponFovEffect" ) != NULL ) {
		cvarSystem->SetCVarBool( "g_weaponFovEffect", preset.weaponFovEffect );
	}
}

static void ApplyMainMenuForceModelChoice( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	const bool enabled = gui->State().GetBool( "ui_proskins" );
	cvarSystem->SetCVarString( "g_forceModel", enabled ? MAINMENU_FORCE_MODEL_MARINE : "" );
	cvarSystem->SetCVarString( "g_forceMarineModel", enabled ? MAINMENU_FORCE_MODEL_MARINE : "" );
	cvarSystem->SetCVarString( "g_forceStroggModel", enabled ? MAINMENU_FORCE_MODEL_STROGG : "" );
}

static void ApplyMainMenuCorpseTimeChoice( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	const int choice = gui->State().GetInt( "corpse_time_choice" );
	const int numChoices = sizeof( MAINMENU_CORPSE_TIME_PRESET_VALUES ) / sizeof( MAINMENU_CORPSE_TIME_PRESET_VALUES[0] );
	if ( choice < 0 || choice >= numChoices ) {
		return;
	}

	const char *const targetCvar = gui->State().GetInt( "ingame" ) == 2 ? "g_corpseRemoveDelayMP" : "g_corpseRemoveDelaySP";
	cvarSystem->SetCVarFloat( targetCvar, MAINMENU_CORPSE_TIME_PRESET_VALUES[ choice ] );
}

#ifndef ID_DEDICATED
// The items of a stock choice list: "a";"b", each item quoted with backslash
// escapes as idSoundHardware_OpenAL::AppendChoiceItem joins them, or bare.
static void Session_ChoiceListItems( const char *list, idStrList &items ) {
	items.Clear();
	if ( list == NULL || list[0] == '\0' ) {
		return;
	}
	idStr item;
	bool quoted = false;
	for ( const char *at = list; ; ++at ) {
		if ( quoted && *at == '\\' && at[1] != '\0' ) {
			item += *++at;
		} else if ( *at == '"' ) {
			quoted = !quoted;
		} else if ( *at == '\0' || ( *at == ';' && !quoted ) ) {
			items.Append( item );
			item.Clear();
			if ( *at == '\0' ) {
				return;
			}
		} else {
			item += *at;
		}
	}
}
#endif

/*
=================
BuildMainMenuAudioDeviceChoices
=================
*/
static void BuildMainMenuAudioDeviceChoices( idStr &choiceNames, idStr &choiceValues ) {
#if defined( USE_OPENAL )
	idSoundHardware_OpenAL::BuildDeviceChoiceStrings( cvarSystem->GetCVarString( "s_deviceName" ), choiceNames, choiceValues );
#else
	choiceNames = common->GetLanguageDict()->GetString( "#str_229913" );
	choiceValues = "";
#endif
}

/*
=================
BuildMainMenuDisplayChoices
=================
*/
static void BuildMainMenuDisplayChoices( idStr &choiceNames, idStr &choiceValues, int &displayCountOut ) {
	choiceNames = common->GetLanguageDict()->GetString( "#str_229914" );
	choiceValues = "-1";
	displayCountOut = 0;

#if defined( USE_SDL3 )
	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &displayCount );
	if ( displays == NULL || displayCount <= 0 ) {
		if ( displays != NULL ) {
			SDL_free( displays );
		}
		return;
	}
	displayCountOut = displayCount;

	for ( int i = 0; i < displayCount; ++i ) {
		const char *displayName = SDL_GetDisplayName( displays[i] );
		if ( displayName == NULL || displayName[0] == '\0' ) {
			displayName = "Display";
		}

		idStr label = va( "%d: %s", i + 1, displayName );
		label.Replace( ";", "," );
		choiceNames += ";";
		choiceNames += label;
		choiceValues += ";";
		choiceValues += va( "%d", i );
	}

	SDL_free( displays );
#endif
}

typedef struct mainMenuVidMode_s {
	int	mode;
	int	width;
	int	height;
} mainMenuVidMode_t;

static const mainMenuVidMode_t mainMenuVidModes[] = {
	{ 11, 640, 480 },
	{ 12, 720, 480 },
	{ 13, 720, 576 },
	{ 14, 800, 480 },
	{ 15, 800, 600 },
	{ 16, 854, 480 },
	{ 17, 960, 540 },
	{ 18, 960, 600 },
	{ 19, 1024, 576 },
	{ 20, 1024, 600 },
	{ 21, 1024, 768 },
	{ 22, 1152, 648 },
	{ 23, 1152, 720 },
	{ 24, 1152, 768 },
	{ 25, 1152, 864 },
	{ 0, 1280, 720 },
	{ 26, 1280, 768 },
	{ 27, 1280, 800 },
	{ 28, 1280, 854 },
	{ 29, 1280, 960 },
	{ 30, 1280, 1024 },
	{ 31, 1360, 768 },
	{ 1, 1366, 768 },
	{ 32, 1400, 900 },
	{ 33, 1400, 1050 },
	{ 34, 1440, 900 },
	{ 35, 1440, 960 },
	{ 36, 1536, 864 },
	{ 2, 1600, 900 },
	{ 37, 1600, 1200 },
	{ 38, 1680, 1050 },
	{ 3, 1920, 1080 },
	{ 4, 1920, 1200 },
	{ 39, 1920, 1280 },
	{ 40, 2048, 1080 },
	{ 41, 2048, 1152 },
	{ 42, 2048, 1536 },
	{ 43, 2160, 1440 },
	{ 44, 2240, 1260 },
	{ 45, 2240, 1400 },
	{ 46, 2256, 1504 },
	{ 47, 2304, 1440 },
	{ 48, 2400, 1350 },
	{ 49, 2400, 1600 },
	{ 50, 2520, 1680 },
	{ 5, 2560, 1080 },
	{ 6, 2560, 1440 },
	{ 51, 2560, 1600 },
	{ 52, 2560, 1664 },
	{ 53, 2736, 1824 },
	{ 54, 2880, 1620 },
	{ 55, 2880, 1800 },
	{ 56, 2880, 1864 },
	{ 57, 2880, 1920 },
	{ 58, 3000, 2000 },
	{ 59, 3024, 1964 },
	{ 60, 3200, 1800 },
	{ 61, 3200, 2000 },
	{ 62, 3240, 2160 },
	{ 7, 3440, 1440 },
	{ 63, 3456, 2160 },
	{ 64, 3456, 2234 },
	{ 65, 3840, 1080 },
	{ 66, 3840, 1200 },
	{ 67, 3840, 1600 },
	{ 8, 3840, 2160 },
	{ 68, 3840, 2400 },
	{ 69, 3840, 2560 },
	{ 70, 4096, 2160 },
	{ 9, 5120, 1440 },
	{ 71, 5120, 2160 },
	{ 10, 5120, 2880 },
	{ 72, 6016, 3384 },
	{ 73, 7680, 2160 },
	{ 74, 7680, 4320 }
};

typedef struct mainMenuDisplayModeOption_s {
	int		width;
	int		height;
	int		legacyMode;
	bool	desktopNative;
	bool	custom;
	idStr	label;
} mainMenuDisplayModeOption_t;

static void AppendMainMenuChoice( idStr &choiceNames, idStr &choiceValues, const char *label, int value ) {
	if ( choiceNames.Length() > 0 ) {
		choiceNames += ";";
		choiceValues += ";";
	}

	choiceNames += label;
	choiceValues += va( "%d", value );
}

static const mainMenuVidMode_t *FindMainMenuVidModeByMode( int mode ) {
	for ( int i = 0; i < static_cast<int>( sizeof( mainMenuVidModes ) / sizeof( mainMenuVidModes[0] ) ); ++i ) {
		if ( mainMenuVidModes[i].mode == mode ) {
			return &mainMenuVidModes[i];
		}
	}
	return NULL;
}

static const mainMenuVidMode_t *FindMainMenuVidModeByResolution( int width, int height ) {
	for ( int i = 0; i < static_cast<int>( sizeof( mainMenuVidModes ) / sizeof( mainMenuVidModes[0] ) ); ++i ) {
		if ( mainMenuVidModes[i].width == width && mainMenuVidModes[i].height == height ) {
			return &mainMenuVidModes[i];
		}
	}
	return NULL;
}

static bool MainMenuDisplayModeResolutionExists( const idList<mainMenuDisplayModeOption_t> &options, int width, int height ) {
	for ( int i = 0; i < options.Num(); ++i ) {
		if ( options[i].desktopNative || options[i].custom ) {
			continue;
		}
		if ( options[i].width == width && options[i].height == height ) {
			return true;
		}
	}
	return false;
}

static int MainMenuGreatestCommonDivisor( int a, int b ) {
	a = idMath::Abs( a );
	b = idMath::Abs( b );
	while ( b != 0 ) {
		const int remainder = a % b;
		a = b;
		b = remainder;
	}
	return a > 0 ? a : 1;
}

static idStr MainMenuDisplayAspectRatioLabel( int width, int height ) {
	if ( width <= 0 || height <= 0 ) {
		return "";
	}

	typedef struct commonAspectRatio_s {
		int width;
		int height;
	} commonAspectRatio_t;
	static const commonAspectRatio_t commonAspectRatios[] = {
		{ 5, 4 },
		{ 4, 3 },
		{ 3, 2 },
		{ 5, 3 },
		{ 16, 10 },
		{ 16, 9 },
		{ 21, 9 },
		{ 32, 10 },
		{ 32, 9 }
	};

	const float displayAspect = static_cast<float>( width ) / static_cast<float>( height );
	int bestIndex = -1;
	float bestDelta = idMath::INFINITY;
	for ( int i = 0; i < static_cast<int>( sizeof( commonAspectRatios ) / sizeof( commonAspectRatios[0] ) ); ++i ) {
		const float targetAspect = static_cast<float>( commonAspectRatios[i].width ) / static_cast<float>( commonAspectRatios[i].height );
		const float delta = idMath::Fabs( displayAspect - targetAspect );
		if ( delta < bestDelta ) {
			bestDelta = delta;
			bestIndex = i;
		}
	}

	if ( bestIndex >= 0 ) {
		const float targetAspect = static_cast<float>( commonAspectRatios[bestIndex].width ) / static_cast<float>( commonAspectRatios[bestIndex].height );
		if ( bestDelta <= targetAspect * 0.04f ) {
			return va( "%d:%d", commonAspectRatios[bestIndex].width, commonAspectRatios[bestIndex].height );
		}
	}

	const int gcd = MainMenuGreatestCommonDivisor( width, height );
	const int reducedWidth = width / gcd;
	const int reducedHeight = height / gcd;
	if ( reducedWidth <= 64 && reducedHeight <= 64 ) {
		return va( "%d:%d", reducedWidth, reducedHeight );
	}

	return "";
}

static idStr MainMenuDisplayResolutionLabel( int width, int height ) {
	if ( width <= 0 || height <= 0 ) {
		return "";
	}

	const idStr aspectLabel = MainMenuDisplayAspectRatioLabel( width, height );
	if ( aspectLabel.Length() > 0 ) {
		return va( "%dx%d (%s)", width, height, aspectLabel.c_str() );
	}
	return va( "%dx%d", width, height );
}

static bool MainMenuDisplayModeOptionLess( const mainMenuDisplayModeOption_t &a, const mainMenuDisplayModeOption_t &b ) {
	if ( a.width != b.width ) {
		return a.width < b.width;
	}
	return a.height < b.height;
}

static void AppendMainMenuDisplayModeOption(
	idList<mainMenuDisplayModeOption_t> &options,
	const char *label,
	int width,
	int height,
	int legacyMode,
	bool desktopNative,
	bool custom ) {
	mainMenuDisplayModeOption_t option;
	option.width = width;
	option.height = height;
	option.legacyMode = legacyMode;
	option.desktopNative = desktopNative;
	option.custom = custom;
	option.label = label;
	option.label.Replace( ";", "," );

	options.Append( option );
}

static void InsertMainMenuDisplayModeOptionSorted(
	idList<mainMenuDisplayModeOption_t> &options,
	const char *label,
	int width,
	int height,
	int legacyMode ) {
	mainMenuDisplayModeOption_t option;
	option.width = width;
	option.height = height;
	option.legacyMode = legacyMode;
	option.desktopNative = false;
	option.custom = false;
	option.label = label;
	option.label.Replace( ";", "," );

	int insertIndex = 0;
	while ( insertIndex < options.Num() && MainMenuDisplayModeOptionLess( options[insertIndex], option ) ) {
		insertIndex++;
	}
	options.Insert( option, insertIndex );
}

static void BuildMainMenuDisplayModeChoiceStrings(
	const idList<mainMenuDisplayModeOption_t> &options,
	idStr *choiceNames,
	idStr *choiceValues ) {
	if ( choiceNames == NULL || choiceValues == NULL ) {
		return;
	}

	choiceNames->Clear();
	choiceValues->Clear();
	for ( int i = 0; i < options.Num(); ++i ) {
		AppendMainMenuChoice( *choiceNames, *choiceValues, options[i].label.c_str(), i );
	}
}

static int MainMenuRoundRefreshRate( float refreshRate ) {
	if ( refreshRate <= 0.0f ) {
		return 0;
	}
	return static_cast<int>( refreshRate + 0.5f );
}

#if defined( USE_SDL3 )
static SDL_DisplayID GetMainMenuSelectedSDLDisplay( void ) {
	SDL_DisplayID display = 0;
	const int requestedScreen = cvarSystem->GetCVarInteger( "r_screen" );

	int displayCount = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &displayCount );
	if ( displays != NULL && displayCount > 0 ) {
		if ( requestedScreen >= 0 && requestedScreen < displayCount ) {
			display = displays[requestedScreen];
		}
		if ( display == 0 ) {
			display = SDL_GetPrimaryDisplay();
		}
		if ( display == 0 ) {
			display = displays[0];
		}
	}
	if ( displays != NULL ) {
		SDL_free( displays );
	}

	if ( display == 0 ) {
		display = SDL_GetPrimaryDisplay();
	}
	return display;
}
#endif

static bool MainMenuGetSelectedDisplayDesktopResolution( int *width, int *height ) {
	if ( width == NULL || height == NULL ) {
		return false;
	}

#if defined( USE_SDL3 )
	SDL_DisplayID display = GetMainMenuSelectedSDLDisplay();
	if ( display != 0 ) {
		const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode( display );
		if ( desktopMode != NULL && desktopMode->w > 0 && desktopMode->h > 0 ) {
			*width = desktopMode->w;
			*height = desktopMode->h;
			return true;
		}
	}
#endif

	return Sys_GetDesktopResolution( width, height );
}

static void BuildMainMenuDisplayModeChoices(
	idList<mainMenuDisplayModeOption_t> &options,
	idStr *choiceNames,
	idStr *choiceValues ) {
	options.Clear();
	if ( choiceNames != NULL ) {
		choiceNames->Clear();
	}
	if ( choiceValues != NULL ) {
		choiceValues->Clear();
	}

	int desktopWidth = 0;
	int desktopHeight = 0;
	idStr desktopLabel = common->GetLanguageDict()->GetString( "#str_229973" );

#if defined( USE_SDL3 )
	SDL_DisplayID display = GetMainMenuSelectedSDLDisplay();
#endif

	if ( MainMenuGetSelectedDisplayDesktopResolution( &desktopWidth, &desktopHeight ) && desktopWidth > 0 && desktopHeight > 0 ) {
		desktopLabel = va( "%s %dx%d", desktopLabel.c_str(), desktopWidth, desktopHeight );
	}
	AppendMainMenuDisplayModeOption( options, desktopLabel.c_str(), desktopWidth, desktopHeight, -2, true, false );

	idList<mainMenuDisplayModeOption_t> displayModes;

#if defined( USE_SDL3 )
	if ( display != 0 ) {
		int modeCount = 0;
		SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes( display, &modeCount );
		if ( modes != NULL && modeCount > 0 ) {
			for ( int i = 0; i < modeCount; ++i ) {
				const SDL_DisplayMode *mode = modes[i];
				if ( mode == NULL || mode->w < 320 || mode->h < 240 ) {
					continue;
				}
				if ( MainMenuDisplayModeResolutionExists( displayModes, mode->w, mode->h ) ) {
					continue;
				}

				const mainMenuVidMode_t *legacyMode = FindMainMenuVidModeByResolution( mode->w, mode->h );
				InsertMainMenuDisplayModeOptionSorted(
					displayModes,
					MainMenuDisplayResolutionLabel( mode->w, mode->h ).c_str(),
					mode->w,
					mode->h,
					legacyMode != NULL ? legacyMode->mode : -1 );
			}
		}
		if ( modes != NULL ) {
			SDL_free( modes );
		}
	}
#endif

	for ( int i = 0; i < displayModes.Num(); ++i ) {
		options.Append( displayModes[i] );
	}

	const int customWidth = idMath::ClampInt( 320, 16384, cvarSystem->GetCVarInteger( "r_customWidth" ) );
	const int customHeight = idMath::ClampInt( 240, 16384, cvarSystem->GetCVarInteger( "r_customHeight" ) );
	idStr customLabel = va( "%s %dx%d", common->GetLanguageDict()->GetString( "#str_229974" ), customWidth, customHeight );
	AppendMainMenuDisplayModeOption( options, customLabel.c_str(), customWidth, customHeight, -1, false, true );

	BuildMainMenuDisplayModeChoiceStrings( options, choiceNames, choiceValues );
}

static int FindMainMenuDisplayModeChoice( const idList<mainMenuDisplayModeOption_t> &options ) {
	const int currentMode = cvarSystem->GetCVarInteger( "r_mode" );
	int targetWidth = 0;
	int targetHeight = 0;
	bool preferCustom = false;

	if ( currentMode == -2 ) {
		for ( int i = 0; i < options.Num(); ++i ) {
			if ( options[i].desktopNative ) {
				return i;
			}
		}
		return 0;
	}

	if ( currentMode == -1 ) {
		targetWidth = idMath::ClampInt( 320, 16384, cvarSystem->GetCVarInteger( "r_customWidth" ) );
		targetHeight = idMath::ClampInt( 240, 16384, cvarSystem->GetCVarInteger( "r_customHeight" ) );
		preferCustom = true;
	} else {
		const mainMenuVidMode_t *mode = FindMainMenuVidModeByMode( currentMode );
		if ( mode != NULL ) {
			targetWidth = mode->width;
			targetHeight = mode->height;
		}
	}

	if ( targetWidth <= 0 || targetHeight <= 0 ) {
		return 0;
	}

	for ( int i = 0; i < options.Num(); ++i ) {
		if ( !options[i].desktopNative && !options[i].custom && options[i].width == targetWidth && options[i].height == targetHeight ) {
			return i;
		}
	}

	if ( preferCustom ) {
		for ( int i = 0; i < options.Num(); ++i ) {
			if ( options[i].custom ) {
				return i;
			}
		}
	}

	return 0;
}

static bool MainMenuRefreshRateExists( const idList<int> &refreshRates, int refreshRate ) {
	for ( int i = 0; i < refreshRates.Num(); ++i ) {
		if ( refreshRates[i] == refreshRate ) {
			return true;
		}
	}
	return false;
}

static void AddMainMenuRefreshRate( idList<int> &refreshRates, int refreshRate ) {
	if ( refreshRate <= 0 || MainMenuRefreshRateExists( refreshRates, refreshRate ) ) {
		return;
	}

	int insertIndex = 1; // keep Auto at index 0
	while ( insertIndex < refreshRates.Num() && refreshRates[insertIndex] < refreshRate ) {
		insertIndex++;
	}
	refreshRates.Insert( refreshRate, insertIndex );
}

static bool MainMenuGetRequestedFullscreenResolution( int &width, int &height ) {
	width = 0;
	height = 0;

	const int currentMode = cvarSystem->GetCVarInteger( "r_mode" );
	if ( currentMode == -2 ) {
		return MainMenuGetSelectedDisplayDesktopResolution( &width, &height ) && width > 0 && height > 0;
	}

	if ( currentMode == -1 ) {
		width = idMath::ClampInt( 320, 16384, cvarSystem->GetCVarInteger( "r_customWidth" ) );
		height = idMath::ClampInt( 240, 16384, cvarSystem->GetCVarInteger( "r_customHeight" ) );
		return true;
	}

	const mainMenuVidMode_t *mode = FindMainMenuVidModeByMode( currentMode );
	if ( mode == NULL ) {
		return false;
	}

	width = mode->width;
	height = mode->height;
	return true;
}

static void BuildMainMenuRefreshChoices( idStr &choiceNames, idStr &choiceValues ) {
	idList<int> refreshRates;
	refreshRates.Append( 0 );

	int targetWidth = 0;
	int targetHeight = 0;
	const bool haveTargetResolution = MainMenuGetRequestedFullscreenResolution( targetWidth, targetHeight );

#if defined( USE_SDL3 )
	SDL_DisplayID display = GetMainMenuSelectedSDLDisplay();
	if ( display != 0 ) {
		const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode( display );
		if ( haveTargetResolution && desktopMode != NULL && desktopMode->w == targetWidth && desktopMode->h == targetHeight ) {
			AddMainMenuRefreshRate( refreshRates, MainMenuRoundRefreshRate( desktopMode->refresh_rate ) );
		}

		int modeCount = 0;
		SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes( display, &modeCount );
		if ( modes != NULL && modeCount > 0 ) {
			for ( int i = 0; i < modeCount; ++i ) {
				const SDL_DisplayMode *mode = modes[i];
				if ( mode == NULL ) {
					continue;
				}
				if ( !haveTargetResolution || mode->w != targetWidth || mode->h != targetHeight ) {
					continue;
				}
				AddMainMenuRefreshRate( refreshRates, MainMenuRoundRefreshRate( mode->refresh_rate ) );
			}
		}
		if ( modes != NULL ) {
			SDL_free( modes );
		}
	}
#endif

	choiceNames.Clear();
	choiceValues.Clear();
	for ( int i = 0; i < refreshRates.Num(); ++i ) {
		const int refreshRate = refreshRates[i];
		const char *label = refreshRate == 0
			? common->GetLanguageDict()->GetString( "#str_229914" )
			: va( "%d Hz", refreshRate );
		AppendMainMenuChoice( choiceNames, choiceValues, label, refreshRate );
	}
}

static void SetMainMenuVideoGuiVars( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	idStr choiceNames;
	idStr choiceValues;
	idList<mainMenuDisplayModeOption_t> displayModeOptions;
	BuildMainMenuDisplayModeChoices( displayModeOptions, &choiceNames, &choiceValues );

	gui->SetStateString( "display_mode_choices", choiceNames.c_str() );
	gui->SetStateString( "display_mode_values", choiceValues.c_str() );
	gui->SetStateInt( "display_mode_choice", FindMainMenuDisplayModeChoice( displayModeOptions ) );

	// Compatibility aliases for the original aspect-bucketed GUI widgets.
	gui->SetStateString( "4_3_choices", choiceNames.c_str() );
	gui->SetStateString( "4_3_values", choiceValues.c_str() );
	gui->SetStateString( "16_9_choices", choiceNames.c_str() );
	gui->SetStateString( "16_9_values", choiceValues.c_str() );
	gui->SetStateString( "16_10_choices", choiceNames.c_str() );
	gui->SetStateString( "16_10_values", choiceValues.c_str() );

	BuildMainMenuRefreshChoices( choiceNames, choiceValues );
	gui->SetStateString( "display_refresh_choices", choiceNames.c_str() );
	gui->SetStateString( "display_refresh_values", choiceValues.c_str() );
}

static void SetMainMenuQualityGuiVars( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	gui->SetStateInt( "r_specularEnabled", cvarSystem->GetCVarBool( "r_skipSpecular" ) ? 0 : 1 );
	gui->SetStateInt( "r_bumpEnabled", cvarSystem->GetCVarBool( "r_skipBump" ) ? 0 : 1 );
	gui->SetStateInt( "r_skyEnabled", cvarSystem->GetCVarBool( "r_skipSky" ) ? 0 : 1 );
	// Apple GL 2.1 compatibility state: while ARB2 light interactions are
	// bypassed the shadow/specular/bump toggles have no visible effect, and
	// SMAA post-AA needs GLSL 1.30; the settings GUI greys those rows.
	const glconfig_t &rendererConfig = renderSystem->GetGLConfig();
	gui->SetStateInt( "r_interactionsBypassed", rendererConfig.disableARB2Interactions ? 1 : 0 );
	gui->SetStateInt( "r_postAAAvailable", ( rendererConfig.GLSLProgramAvailable && rendererConfig.GLSL130Available ) ? 1 : 0 );
}

static void SyncMainMenuAspectVisibility( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	gui->HandleNamedEvent( "forceAspect0" );
}

static void PopulateMainMenuAudioDeviceChoices( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	idStr choiceNames;
	idStr choiceValues;
	BuildMainMenuAudioDeviceChoices( choiceNames, choiceValues );
	gui->SetStateString( "device_name", choiceNames.c_str() );
	gui->SetStateString( "device_value", choiceValues.c_str() );
}

static void PopulateMainMenuDisplayChoices( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	idStr choiceNames;
	idStr choiceValues;
	int displayCount = 0;
	BuildMainMenuDisplayChoices( choiceNames, choiceValues, displayCount );
	gui->SetStateString( "display_names", choiceNames.c_str() );
	gui->SetStateString( "display_values", choiceValues.c_str() );
	gui->SetStateInt( "display_count", displayCount );
	SetMainMenuVideoGuiVars( gui );
}

static void RefreshMainMenuDisplayChoices( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	SetMainMenuVideoGuiVars( gui );
	SyncMainMenuAspectVisibility( gui );
	gui->StateChanged( common->GetPresentationTime() );
}

static void RefreshMainMenuPerformancePresetGuiVars( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	SetMainMenuVideoGuiVars( gui );
	SetMainMenuQualityGuiVars( gui );
	SyncMainMenuAspectVisibility( gui );
	gui->StateChanged( common->GetPresentationTime() );
}

static void AppendMainMenuCommandArgsUntilSeparator( const idCmdArgs &sourceArgs, int &argIndex, idCmdArgs &commandArgs ) {
	while ( argIndex < sourceArgs.Argc() && idStr::Cmp( sourceArgs.Argv( argIndex ), ";" ) != 0 ) {
		commandArgs.AppendArg( sourceArgs.Argv( argIndex++ ) );
	}
}

static void ApplyMainMenuDisplayModeChoice( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	idList<mainMenuDisplayModeOption_t> displayModeOptions;
	BuildMainMenuDisplayModeChoices( displayModeOptions, NULL, NULL );
	if ( displayModeOptions.Num() <= 0 ) {
		return;
	}

	int choice = gui->State().GetInt( "display_mode_choice" );
	choice = idMath::ClampInt( 0, displayModeOptions.Num() - 1, choice );
	const mainMenuDisplayModeOption_t &option = displayModeOptions[choice];

	if ( option.desktopNative ) {
		cvarSystem->SetCVarInteger( "r_mode", -2 );
	} else {
		cvarSystem->SetCVarInteger( "r_customWidth", idMath::ClampInt( 320, 16384, option.width ) );
		cvarSystem->SetCVarInteger( "r_customHeight", idMath::ClampInt( 240, 16384, option.height ) );
		cvarSystem->SetCVarInteger( "r_mode", ( !option.custom && option.legacyMode >= 0 ) ? option.legacyMode : -1 );
	}

	RefreshMainMenuDisplayChoices( gui );
}

static void ApplyMainMenuCustomDisplaySize( idUserInterface *gui ) {
	cvarSystem->SetCVarInteger( "r_mode", -1 );
	RefreshMainMenuDisplayChoices( gui );
}

/*
=================
MapDeclSupports

A map names each gametype it supports as a boolean key. Some spell the name
without its spaces (the Awakening's maps.def has "TeamDM" for "Team DM"), so
both spellings count.
=================
*/
static bool MapDeclSupports( const idDict *dict, const char *gameType ) {
	if ( dict->GetBool( gameType ) ) {
		return true;
	}
	idStr compact;
	for ( const char *c = gameType; *c; c++ ) {
		if ( *c != ' ' ) {
			compact.Append( *c );
		}
	}
	return compact.Length() != idStr::Length( gameType ) && dict->GetBool( compact );
}

/*
=================
MapSupportsStartServerGameType
=================
*/
static bool MapSupportsStartServerGameType( const idDict *dict, const char *gameType ) {
	if ( !dict || !gameType || !gameType[0] ) {
		return false;
	}

	// Match the multiplayer vote/admin behavior so DM and Team DM can use any standard MP map type.
	if ( !idStr::Icmp( gameType, "DM" ) || !idStr::Icmp( gameType, "Team DM" ) ) {
		return MapDeclSupports( dict, "DM" ) ||
			MapDeclSupports( dict, "Team DM" ) ||
			MapDeclSupports( dict, "CTF" ) ||
			MapDeclSupports( dict, "Tourney" ) ||
			MapDeclSupports( dict, "Arena CTF" );
	}
	if ( !idStr::Icmp( gameType, "Duel" ) ) {
		return MapDeclSupports( dict, "DM" );
	}
	if ( !idStr::Icmp( gameType, "Clan Arena" ) ||
		!idStr::Icmp( gameType, "Freeze Tag" ) ||
		!idStr::Icmp( gameType, "Red Rover" ) ) {
		return MapDeclSupports( dict, "Team DM" );
	}
	if ( !idStr::Icmp( gameType, "One Flag CTF" ) ) {
		return MapDeclSupports( dict, "CTF" );
	}
	if ( !idStr::Icmp( gameType, "Arena One Flag CTF" ) ) {
		return MapDeclSupports( dict, "Arena CTF" );
	}

	return MapDeclSupports( dict, gameType );
}

/*
=================
MapSupportsAnyMPGameType
=================
*/
static bool MapSupportsAnyMPGameType( const idDict *dict ) {
	if ( !dict ) {
		return false;
	}

	return dict->GetBool( "DM" ) ||
		dict->GetBool( "Team DM" ) ||
		dict->GetBool( "CTF" ) ||
		dict->GetBool( "Tourney" ) ||
		dict->GetBool( "Arena CTF" ) ||
		dict->GetBool( "DeadZone" );
}

/*
=================
GetListenServerPlayerWarningLimit
=================
*/
static int GetListenServerPlayerWarningLimit( const int serverRatePreset, const int maxClientRate ) {
	switch ( serverRatePreset ) {
		case 1:
			// Low Upload
			return 3;
		case 2:
			// Balanced Upload
			return 4;
		case 3:
			// High Upload
			return 5;
		case 4:
		case 5:
			// Fiber/Modern and LAN presets should not trigger the legacy 4-player listen warning.
			return 16;
		default:
			break;
	}

	// Manual mode: infer a safe listen target from net_serverMaxClientRate.
	if ( maxClientRate <= 8000 ) {
		return 3;
	}
	if ( maxClientRate <= 9500 ) {
		return 4;
	}
	if ( maxClientRate <= 10500 ) {
		return 5;
	}
	return 16;
}

/*
=================
CommitStartServerMapSelection
=================
*/
static void CommitStartServerMapSelection( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	if ( sessLocal.guiMainMenu_MapList == NULL ) {
		return;
	}

	int mapNum = sessLocal.guiMainMenu_MapList->GetSelection( NULL, 0 );
	if ( mapNum < 0 && sessLocal.guiMainMenu_MapList->Num() > 0 ) {
		sessLocal.guiMainMenu_MapList->SetSelection( 0 );
		mapNum = sessLocal.guiMainMenu_MapList->GetSelection( NULL, 0 );
	}
	if ( mapNum < 0 ) {
		return;
	}

	const idDict *dict = fileSystem->GetMapDecl( mapNum );
	if ( dict ) {
		cvarSystem->SetCVarString( "si_map", dict->GetString( "path" ) );
	}
}

/*
=================
Main-menu MP model helpers
=================
*/
static const char *mainMenuMPTeamNames[] = {
	"marine",
	"strogg"
};

static bool MainMenuIsTeamGame( void ) {
	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
	return !idStr::Icmp( gameType, "Team DM" ) ||
		!idStr::Icmp( gameType, "CTF" ) ||
		!idStr::Icmp( gameType, "One Flag CTF" ) ||
		!idStr::Icmp( gameType, "Arena CTF" ) ||
		!idStr::Icmp( gameType, "Arena One Flag CTF" ) ||
		!idStr::Icmp( gameType, "Clan Arena" ) ||
		!idStr::Icmp( gameType, "Freeze Tag" ) ||
		!idStr::Icmp( gameType, "Red Rover" ) ||
		!idStr::Icmp( gameType, "DeadZone" );
}

static int MainMenuResolveModelTeam( void ) {
	if ( !MainMenuIsTeamGame() ) {
		return -1;
	}

	const char *uiTeam = cvarSystem->GetCVarString( "ui_team" );
	if ( !idStr::Icmp( uiTeam, "Strogg" ) ) {
		return 1;
	}

	// The retail MP settings flow always has a valid active team in team games.
	return 0;
}

static idStr MainMenuModelCVarName( const int menuModelTeam ) {
	if ( menuModelTeam >= 0 && menuModelTeam < 2 ) {
		return va( "ui_model_%s", mainMenuMPTeamNames[ menuModelTeam ] );
	}
	return "ui_model";
}

static int MainMenuMPSettingsGameTypeState( void ) {
	const char *gameType = cvarSystem->GetCVarString( "si_gameType" );
	if ( gameType == NULL || gameType[0] == '\0' || !idStr::Icmp( gameType, "singleplayer" ) || !idStr::Icmp( gameType, "DM" ) ) {
		return 1;
	}
	if ( !idStr::Icmp( gameType, "Tourney" ) ) {
		return 2;
	}
	if ( !idStr::Icmp( gameType, "Team DM" ) ) {
		return 3;
	}
	if ( !idStr::Icmp( gameType, "CTF" ) ) {
		return 4;
	}
	if ( !idStr::Icmp( gameType, "One Flag CTF" ) ) {
		return 5;
	}
	if ( !idStr::Icmp( gameType, "Arena CTF" ) ) {
		return 6;
	}
	if ( !idStr::Icmp( gameType, "Arena One Flag CTF" ) ) {
		return 7;
	}
	if ( !idStr::Icmp( gameType, "DeadZone" ) ) {
		return 8;
	}
	if ( !idStr::Icmp( gameType, "Duel" ) ) {
		return 9;
	}
	if ( !idStr::Icmp( gameType, "Clan Arena" ) ) {
		return 10;
	}
	if ( !idStr::Icmp( gameType, "Freeze Tag" ) ) {
		return 11;
	}
	if ( !idStr::Icmp( gameType, "Red Rover" ) ) {
		return 12;
	}
	return 1;
}

static void MainMenuSyncMPSettingsGuiState( idUserInterface *gui, const int menuModelTeam ) {
	if ( !gui ) {
		return;
	}

	gui->SetStateInt( "gametype", MainMenuMPSettingsGameTypeState() );
	gui->SetStateInt( "player_team", menuModelTeam >= 0 ? menuModelTeam : 0 );
}

static bool MainMenuExtractPlayerModelInfo( const char *declName, idStr &modelOut, idStr &uiHeadOut, idStr &skinOut, idStr &descriptionOut, idStr &teamOut ) {
	modelOut.Clear();
	uiHeadOut.Clear();
	skinOut.Clear();
	descriptionOut.Clear();
	teamOut.Clear();

	if ( !declName || !declName[0] ) {
		return false;
	}

	const rvDeclPlayerModel *playerModel = static_cast<const rvDeclPlayerModel *>( declManager->FindType( DECL_PLAYER_MODEL, declName, false ) );
	if ( playerModel ) {
		modelOut = playerModel->model;
		uiHeadOut = playerModel->uiHead;
		skinOut = playerModel->skin;
		descriptionOut = playerModel->description;
		teamOut = playerModel->team;
		return true;
	}

	// Fallback for content variants that still define these as entityDef blocks.
	const idDeclEntityDef *entityModel = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, declName, false ) );
	if ( !entityModel ) {
		return false;
	}

	modelOut = entityModel->dict.GetString( "model" );
	uiHeadOut = entityModel->dict.GetString( "def_head_ui" );
	if ( !uiHeadOut.Length() ) {
		uiHeadOut = entityModel->dict.GetString( "def_head" );
	}
	skinOut = entityModel->dict.GetString( "skin" );
	descriptionOut = entityModel->dict.GetString( "description" );
	teamOut = entityModel->dict.GetString( "team" );
	return modelOut.Length() > 0 || uiHeadOut.Length() > 0 || skinOut.Length() > 0;
}

static bool MainMenuModelAllowedForTeam( const idStr &modelTeam, const bool isTeamGame, const int menuModelTeam ) {
	if ( !isTeamGame || menuModelTeam < 0 || menuModelTeam >= 2 ) {
		return true;
	}
	if ( !modelTeam.Length() ) {
		return true;
	}

	return idStr::Icmp( modelTeam.c_str(), mainMenuMPTeamNames[ menuModelTeam ] ) == 0;
}

static void MainMenuApplyPlayerModelPreview( idUserInterface *gui, const idStr &modelName, const idStr &headName, const idStr &skinName ) {
	if ( !gui ) {
		return;
	}

	gui->SetStateString( "player_model_name", modelName.c_str() );
	gui->SetStateString( "player_head_model_name", headName.c_str() );
	gui->SetStateString( "player_skin_name", skinName.c_str() );
	gui->SetStateString( "player_head_skin_name", "" );

	if ( headName.Length() ) {
		const idDeclEntityDef *head = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, headName.c_str(), false ) );
		if ( head && head->dict.GetString( "skin" ) ) {
			gui->SetStateString( "player_head_skin_name", head->dict.GetString( "skin" ) );
		}
	}

	gui->SetStateBool( "need_update", true );
}

static bool MainMenuFirstModelFromList( const idStr &buildValues, const bool isTeamGame, const int menuModelTeam, idStr &modelDeclOut, idStr &modelOut, idStr &headOut, idStr &skinOut ) {
	idStr remaining = buildValues;

	while ( remaining.Length() ) {
		idStr token = remaining;
		const int split = remaining.Find( ";" );
		if ( split >= 0 ) {
			token = remaining.Left( split );
			remaining = remaining.Right( remaining.Length() - split - 1 );
		} else {
			remaining.Clear();
		}

		token.StripLeading( ' ' );
		token.StripTrailing( ' ' );
		if ( !token.Length() ) {
			continue;
		}

		idStr description;
		idStr team;
		if ( !MainMenuExtractPlayerModelInfo( token.c_str(), modelOut, headOut, skinOut, description, team ) ) {
			continue;
		}
		if ( !MainMenuModelAllowedForTeam( team, isTeamGame, menuModelTeam ) ) {
			continue;
		}

		modelDeclOut = token;
		return true;
	}

	return false;
}

static void MainMenuAppendModelChoice( idStr &buildValues, idStr &buildNames, const char *declName, const char *displayName ) {
	if ( buildValues.Length() ) {
		buildValues += ";";
		buildNames += ";";
	}

	buildValues += declName;
	buildNames += ( displayName && displayName[0] ) ? displayName : declName;
}

static void MainMenuBuildModelChoices( const bool isTeamGame, const int menuModelTeam, idStrList &values, idStrList &names ) {
	values.Clear(); names.Clear();

	// Only declName and description are read out of these, but parsing a
	// playerModel decl precaches its model, head, skin and sounds as a side
	// effect. Walking the whole list therefore loaded every multiplayer
	// character before the main menu could draw -- several seconds of startup
	// spent on a dropdown the player may never open.
	idSuppressPlayerModelMediaCaching suppressPrecache;

	const int numModels = declManager->GetNumDecls( DECL_PLAYER_MODEL );
	for ( int i = 0; i < numModels; ++i ) {
		const rvDeclPlayerModel *playerModel = static_cast<const rvDeclPlayerModel *>( declManager->DeclByIndex( DECL_PLAYER_MODEL, i, true ) );
		if ( !playerModel ) {
			continue;
		}

		const char *declName = playerModel->GetName();
		if ( !declName || !declName[0] ) {
			continue;
		}

		if ( !MainMenuModelAllowedForTeam( playerModel->team, isTeamGame, menuModelTeam ) ) {
			continue;
		}

		const char *localizedName = playerModel->description.Length() ? common->GetLocalizedString( playerModel->description.c_str() ) : "";
		values.Append( declName );
		names.Append( localizedName && localizedName[0] ? localizedName : declName );
	}
}

static void MainMenuBuildModelList( const bool isTeamGame, const int menuModelTeam, idStr &buildValues, idStr &buildNames ) {
	idStrList values, names;
	MainMenuBuildModelChoices( isTeamGame, menuModelTeam, values, names );
	buildValues.Clear(); buildNames.Clear();
	for ( int i = 0; i < values.Num(); ++i ) MainMenuAppendModelChoice( buildValues, buildNames, values[i].c_str(), names[i].c_str() );
}

static void SetMainMenuMPModelVars( idUserInterface *gui ) {
	if ( !gui ) {
		return;
	}

	const bool isTeamGame = MainMenuIsTeamGame();
	const int menuModelTeam = MainMenuResolveModelTeam();
	MainMenuSyncMPSettingsGuiState( gui, menuModelTeam );

	// Look the fallback up only when it is needed. FindType parses the decl, and
	// parsing a player entityDef precaches everything it references -- models,
	// animations, weapons and sounds. Resolving both unconditionally paid that
	// cost for "player_marine_mp" on every main menu build and then threw the
	// result away whenever the _ui variant existed, which it normally does.
	const idDeclEntityDef *def = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, "player_marine_mp_ui", false ) );
	if ( def == NULL ) {
		def = static_cast<const idDeclEntityDef *>( declManager->FindType( DECL_ENTITYDEF, "player_marine_mp", false ) );
	}

	idStr buildValues;
	idStr buildNames;
	MainMenuBuildModelList( isTeamGame, menuModelTeam, buildValues, buildNames );

	gui->SetStateString( "model_values", buildValues.c_str() );
	gui->SetStateString( "model_names", buildNames.c_str() );
	gui->SetStateBool( "player_model_updated", true );

	const idStr modelCVar = MainMenuModelCVarName( menuModelTeam );
	const char *selectedModelDecl = cvarSystem->GetCVarString( modelCVar.c_str() );
	if ( ( !selectedModelDecl || !selectedModelDecl[0] ) && def ) {
		if ( menuModelTeam >= 0 && menuModelTeam < 2 ) {
			selectedModelDecl = def->dict.GetString( va( "def_default_model_%s", mainMenuMPTeamNames[ menuModelTeam ] ) );
		} else {
			selectedModelDecl = def->dict.GetString( "def_default_model" );
		}
	}

	idStr selectedDecl = selectedModelDecl ? selectedModelDecl : "";
	idStr selectedModelName;
	idStr selectedHeadName;
	idStr selectedSkinName;
	idStr selectedDescription;
	idStr selectedTeam;
	const bool selectedValid = selectedDecl.Length() &&
		MainMenuExtractPlayerModelInfo( selectedDecl.c_str(), selectedModelName, selectedHeadName, selectedSkinName, selectedDescription, selectedTeam ) &&
		MainMenuModelAllowedForTeam( selectedTeam, isTeamGame, menuModelTeam );

	if ( !selectedValid ) {
		if ( MainMenuFirstModelFromList( buildValues, isTeamGame, menuModelTeam, selectedDecl, selectedModelName, selectedHeadName, selectedSkinName ) ) {
			cvarSystem->SetCVarString( modelCVar.c_str(), selectedDecl.c_str() );
		}
	}

	// The list above was built with precaching suppressed, so restore it for the
	// one model the menu actually displays.
	if ( selectedDecl.Length() ) {
		DeclPlayerModel_CacheMediaForDecl(
			static_cast<const rvDeclPlayerModel *>( declManager->FindType( DECL_PLAYER_MODEL, selectedDecl.c_str(), false ) ) );
	}
	// Both stock and retained renderDef previews use these animation classes.
	// Reference them with the selected player's media, including inside the
	// normal level-load precache window, before a later preview needs them.
	declManager->FindType( DECL_ENTITYDEF, "player_ui_anims", false );
	declManager->FindType( DECL_ENTITYDEF, "weapon_blaster_gui", false );

	if ( !selectedModelName.Length() && def ) {
		selectedModelName = def->dict.GetString( "model" );
		selectedHeadName = def->dict.GetString( "def_head_ui" );
		if ( !selectedHeadName.Length() ) {
			selectedHeadName = def->dict.GetString( "def_head" );
		}
		selectedSkinName = def->dict.GetString( "skin" );
	}

	if ( selectedModelName.Length() || selectedHeadName.Length() || selectedSkinName.Length() ) {
		MainMenuApplyPlayerModelPreview( gui, selectedModelName, selectedHeadName, selectedSkinName );
	}

	// Stage editable user fields while this menu is active.
	cvarSystem->SetCVarString( "gui_ui_name", cvarSystem->GetCVarString( "ui_name" ) );
	cvarSystem->SetCVarString( "gui_ui_clan", cvarSystem->GetCVarString( "ui_clan" ) );
}

// Player Setup is an editor inside the Multiplayer page, not a child route.
// The session owns the complete catalog; a bounded choice page does not cap it.
bool idSessionLocal::PublishRetainedPlayerSetup( idUserInterface *gui, bool rebuild ) {
#ifndef ID_DEDICATED
	using namespace openq4::ui;
	if ( !gui || !guiMainMenu || !gameEdit ) return false;
	const idStr cvar = MainMenuModelCVarName( MainMenuResolveModelTeam() );
	const idStr language = cvarSystem->GetCVarString( "sys_lang" );
	const idStr current = cvarSystem->GetCVarString( cvar.c_str() );
	const int declarations = declManager->GetNumDecls( DECL_PLAYER_MODEL );
	const bool contextChanged = retainedSetupCvar != cvar || retainedSetupLanguage != language || retainedSetupDeclCount != declarations;
	const bool selectedChanged = retainedSetupValue != current;
	if ( rebuild || contextChanged || selectedChanged || !gui->State().GetString("setup.body")[0] ) {
		if ( (rebuild || contextChanged) && retainedSetupCatalog >= 0x7fffffffu ) return false;
		SetMainMenuMPModelVars( guiMainMenu );
		MainMenuBuildModelChoices( MainMenuIsTeamGame(), MainMenuResolveModelTeam(), retainedSetupModels, retainedSetupNames );
		retainedSetupCvar = cvar; retainedSetupLanguage = language; retainedSetupDeclCount = declarations;
		retainedSetupValue = cvarSystem->GetCVarString( cvar.c_str() );
		if ( rebuild || contextChanged ) ++retainedSetupCatalog;
		const int selected = retainedSetupModels.FindIndex( retainedSetupValue );
		retainedSetupFirst = selected >= 0 ? selected / int(PlayerSetupModelRows) * int(PlayerSetupModelRows) : 0;
		gui->SetStateString( "setup.body", guiMainMenu->State().GetString("player_model_name") );
		gui->SetStateString( "setup.head", guiMainMenu->State().GetString("player_head_model_name") );
		gui->SetStateString( "setup.skin", guiMainMenu->State().GetString("player_skin_name") );
		gui->SetStateString( "setup.head_skin", guiMainMenu->State().GetString("player_head_skin_name") );
		gui->SetStateString( "setup.model_name", selected >= 0 ? Session_RetainedPlayerText(retainedSetupNames[selected].c_str(),256,1).c_str() : "" );
	}
	if ( !gui->State().GetString("setup.body")[0] ) return false;
	// An epoch changes before replacing a page or catalog. Deferred choices
	// retain their old epoch and cannot name another model through a reused row.
	if ( gui->State().GetInt("setup.catalog") != int(retainedSetupCatalog) ) {
		const int count = Min( int(PlayerSetupModelRows), Max(0,retainedSetupModels.Num()-retainedSetupFirst) );
		for ( unsigned int row = 0; row < PlayerSetupModelRows; ++row ) {
			const int index = retainedSetupFirst + int(row);
			gui->SetStateString( va("setup.model.%u.name",row), int(row) < count ? Session_RetainedPlayerText(retainedSetupNames[index].c_str(),256,1).c_str() : "" );
		}
		gui->SetStateInt( "setup.model_count", count );
		gui->SetStateInt( "setup.catalog", int(retainedSetupCatalog) );
		gui->SetStateBool( "setup.previous", retainedSetupFirst > 0 );
		gui->SetStateBool( "setup.next", retainedSetupFirst + count < retainedSetupModels.Num() );
	}
	const int selected = retainedSetupModels.FindIndex( retainedSetupValue );
	gui->SetStateInt( "setup.model_row", selected >= retainedSetupFirst && selected < retainedSetupFirst + int(PlayerSetupModelRows) ? selected-retainedSetupFirst : -1 );
	for ( const char *field : {"name","clan"} ) {
		const char *value = cvarSystem->GetCVarString( va("ui_%s",field) );
		gui->SetStateString( va("setup.%s",field), value );
		gui->SetStateString( va("setup.observed_%s",field), value );
	}
	gui->SetStateString( "setup.observed_model", retainedSetupValue.c_str() );
	const char *tint = cvarSystem->GetCVarString("ui_hitscanTint");
	gui->SetStateString( "setup.observed_rail", tint );
	float hue=0,saturation=0,brightness=0;
	int rail = -1;
	if ( sscanf(tint,"%f %f %f",&hue,&saturation,&brightness)==3 && saturation==1 && brightness==1 ) {
		static const int hues[7] = {0,30,60,120,180,240,330};
		for ( int i = 0; i < 7; ++i ) if ( fabs(hue-hues[i]) < .001f ) rail=i;
	}
	gui->SetStateInt( "setup.rail", rail );
	gui->SetStateFloat( "setup.rotation", retainedSetupRotation );
	gui->StateChanged( common->GetPresentationTime() );
	return true;
#else
	return false;
#endif
}

bool idSessionLocal::OpenRetainedPlayerSetup( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	retainedSetupRotation = 200;
	if ( !PublishRetainedPlayerSetup(gui,true) ) return false;
	gui->SetStateString("setup.notice","");
	gui->HandleNamedEvent("setupOpen");
	PumpApplicationActions(gui);
	return true;
#else
	return false;
#endif
}

void idSessionLocal::HandleRetainedPlayerSetupRequest( idUserInterface *gui, const char *payload ) {
#ifndef ID_DEDICATED
	using namespace openq4::ui;
	if ( gui != guiActive || gui != guiRetainedMpPage || !gui->State().GetBool("setup.open") ) return;
	PlayerSetupRequest request; std::string error;
	if ( !DecodePlayerSetupRequest(payload,request,error) ) { common->Warning("retained UI: %s",error.c_str()); return; }
	const auto notice = [&](const char *key) { gui->SetStateString("setup.notice",common->GetLocalizedString(key)); gui->StateChanged(common->GetPresentationTime()); };
	if ( !PublishRetainedPlayerSetup(gui) || request.catalog != retainedSetupCatalog ) { notice("#str_231548"); return; }
	const char *cvar = nullptr;
	idStr wanted;
	switch (request.field) {
		case PlayerSetupField::Name: cvar="ui_name"; wanted=std::get<std::string>(request.value).c_str(); break;
		case PlayerSetupField::Clan: cvar="ui_clan"; wanted=std::get<std::string>(request.value).c_str(); break;
		case PlayerSetupField::Model: {
			const long long index = static_cast<long long>(retainedSetupFirst) + int(std::get<double>(request.value));
			if ( index < 0 || index >= retainedSetupModels.Num() ) { notice("#str_231548"); return; }
			idStr model,head,skin,description,team;
			wanted=retainedSetupModels[int(index)];
			if ( !MainMenuExtractPlayerModelInfo(wanted.c_str(),model,head,skin,description,team) ||
				!MainMenuModelAllowedForTeam(team,MainMenuIsTeamGame(),MainMenuResolveModelTeam()) ) { notice("#str_231548"); return; }
			cvar=retainedSetupCvar.c_str(); break;
		}
		case PlayerSetupField::Rail: {
			static const int hues[7] = {0,30,60,120,180,240,330};
			cvar="ui_hitscanTint"; wanted=va("%d 1 1",hues[int(std::get<double>(request.value))]); break;
		}
		case PlayerSetupField::Page: {
			const bool next=std::get<double>(request.value)>0;
			if ( retainedSetupCatalog >= 0x7fffffffu || (next ? retainedSetupFirst + int(PlayerSetupModelRows) >= retainedSetupModels.Num() : retainedSetupFirst==0) ) return;
			retainedSetupFirst += next ? int(PlayerSetupModelRows) : -int(PlayerSetupModelRows);
			++retainedSetupCatalog; break;
		}
		case PlayerSetupField::Rotation: retainedSetupRotation=float(std::get<double>(request.value)); break;
	}
	if ( cvar ) {
		if ( request.expected != cvarSystem->GetCVarString(cvar) ) { notice("#str_231548"); return; }
		cvarSystem->SetCVarString(cvar,wanted.c_str());
		if ( wanted != cvarSystem->GetCVarString(cvar) ) { notice("#str_231549"); return; }
		if ( request.field==PlayerSetupField::Name || request.field==PlayerSetupField::Clan )
			cvarSystem->SetCVarString(request.field==PlayerSetupField::Name ? "gui_ui_name" : "gui_ui_clan",wanted.c_str());
	}
	gui->SetStateString("setup.notice","");
	PublishRetainedPlayerSetup(gui);
	if ( cvarSystem->GetCVarBool("ui_retainedTrace") ) common->Printf("RETAINED_PLAYER_SETUP applied=1 field=%d catalog=%u\n",int(request.field),retainedSetupCatalog);
#endif
}

bool idSessionLocal::RetainedDetailStick( idUserInterface *gui, float x, float y, float seconds ) {
#ifndef ID_DEDICATED
	const bool scrolled = y != 0.0f && UI_RetainedDetailStick( gui, y, seconds );
	const bool turned = x != 0.0f && TurnRetainedPlayerSetup( gui, x, seconds );
	return scrolled || turned;
#else
	return false;
#endif
}

// Full deflection turns the preview half a turn a second, wrapping within the
// Rotation row's 0-360 degrees; the row's slider follows.
bool idSessionLocal::TurnRetainedPlayerSetup( idUserInterface *gui, float x, float seconds ) {
#ifndef ID_DEDICATED
	if ( gui == NULL || gui != guiActive || gui != guiRetainedMpPage || !gui->State().GetBool( "setup.open" ) ||
			!( seconds > 0.0f ) || x == 0.0f || x != x ) {
		return false;
	}
	float rotation = fmodf( retainedSetupRotation + idMath::ClampFloat( -1.0f, 1.0f, x ) * 180.0f * seconds, 360.0f );
	if ( rotation < 0.0f ) {
		rotation += 360.0f;
	}
	retainedSetupRotation = rotation;
	gui->SetStateFloat( "setup.rotation", retainedSetupRotation );
	gui->StateChanged( common->GetPresentationTime() );
	return true;
#else
	return false;
#endif
}

/*
==============
Main menu custom crosshair

The Game Options preview row draws gui::crossImage, and the game draws
g_crosshairCustomFile in place of each weapon's own art when
g_crosshairCustom is set. The picker walks the mtr_crosshair keys of the
multiplayer UI player def in file order, as the retail session menu did.
==============
*/
static const char *MAINMENU_CROSSHAIR_PREFIX = "mtr_crosshair";

static const idDict *MainMenuCrosshairDict( void ) {
	// Resolved without caching, as retail did, so parsing the def never
	// precaches player media on the menu path. player_marine_mp lists the same
	// twenty crosshairs.
	const idDecl *decl = declManager->FindType( DECL_ENTITYDEF, "player_marine_mp_ui", false, true );
	if ( decl == NULL ) {
		decl = declManager->FindType( DECL_ENTITYDEF, "player_marine_mp", false, true );
	}
	return decl != NULL ? &static_cast<const idDeclEntityDef *>( decl )->dict : NULL;
}

// Returns the entry that names the current crosshair (step 0), or the one
// after it (step > 0) or before it (step < 0), wrapping at both ends. A current
// value that names no entry, such as the cvar's default "0", resolves to the
// first entry, or to the last one when stepping back.
static const idKeyValue *MainMenuFindCrosshair( const idDict &dict, const char *current, int step ) {
	const idKeyValue *first = dict.MatchPrefix( MAINMENU_CROSSHAIR_PREFIX );
	const idKeyValue *last = NULL;
	const idKeyValue *previous = NULL;
	const idKeyValue *found = NULL;
	const idKeyValue *next = NULL;
	for ( const idKeyValue *kv = first; kv != NULL; kv = dict.MatchPrefix( MAINMENU_CROSSHAIR_PREFIX, kv ) ) {
		if ( found == NULL ) {
			if ( kv->GetValue().Icmp( current ) == 0 ) {
				found = kv;
			} else {
				previous = kv;
			}
		} else if ( next == NULL ) {
			next = kv;
		}
		last = kv;
	}
	if ( found == NULL ) {
		return step < 0 ? last : first;
	}
	if ( step > 0 ) {
		return next != NULL ? next : first;
	}
	if ( step < 0 ) {
		return previous != NULL ? previous : last;
	}
	return found;
}

// Shows the crosshair in the preview and makes it the custom crosshair.
static void MainMenuPublishCrosshair( idUserInterface *gui, int step ) {
	const idDict *dict = MainMenuCrosshairDict();
	if ( gui == NULL || dict == NULL ) {
		return;
	}
	const idKeyValue *crosshair = MainMenuFindCrosshair( *dict, cvarSystem->GetCVarString( "g_crosshairCustomFile" ), step );
	if ( crosshair == NULL ) {
		return;
	}
	const char *materialName = crosshair->GetValue().c_str();
	gui->SetStateString( "crossImage", materialName );
	const idMaterial *material = declManager->FindMaterial( materialName );
	if ( material != NULL ) {
		material->SetSort( SS_GUI );
	}
	cvarSystem->SetCVarString( "g_crosshairCustomFile", materialName );
}

static void CommitMainMenuMPSettings( void ) {
	if ( idStr::Cmp( cvarSystem->GetCVarString( "gui_ui_name" ), cvarSystem->GetCVarString( "ui_name" ) ) ) {
		cvarSystem->SetCVarString( "ui_name", cvarSystem->GetCVarString( "gui_ui_name" ) );
	}
	if ( idStr::Cmp( cvarSystem->GetCVarString( "gui_ui_clan" ), cvarSystem->GetCVarString( "ui_clan" ) ) ) {
		cvarSystem->SetCVarString( "ui_clan", cvarSystem->GetCVarString( "gui_ui_clan" ) );
	}
}

// implements the setup for, and commands from, the main menu

/*
==============
idSessionLocal::GetActiveMenu
==============
*/
idUserInterface *idSessionLocal::GetActiveMenu( void ) {
	return guiActive;
}

/*
==============
idSessionLocal::StartMainMenu
==============
*/

// Building the main menu was over half of startup at one point; keep the split
// visible so a regression there is obvious rather than folded into "startup".
extern idCVar com_showLevelLoadTimes;

static int menuProfileSaveVarsMsec = 0;
static int menuProfileMainVarsMsec = 0;

void idSessionLocal::StartMenu( bool playIntro ) {
	if ( guiMainMenu == NULL ) {
		// A dedicated server's session has no menus, and its console is the only
		// interface: a request for the menu closes whatever GUI is open instead.
		SetGUI( NULL, NULL );
		return;
	}
	const bool shouldPlayIntro = playIntro && !com_skipLogoVideos.GetBool();
	if ( guiSystem != NULL && ( guiActive == guiSystem || guiMsgRestore == guiSystem ) ) {
		ReturnSystemSettings();
		return;
	}

	if ( guiActive == guiMainMenu ) {
		return;
	}

	if ( readDemo ) {
		// if we're playing a demo, esc kills it
		UnloadMap();
	}

	// pause the game sound world
	if ( sw != NULL && !sw->IsPaused() ) {
		sw->Pause();
	}

	// start playing the menu sounds
	SetPlayingSoundWorld( menuSoundWorld );

	const int menuProfileStart = Sys_Milliseconds();
	menuProfileSaveVarsMsec = 0;
	menuProfileMainVarsMsec = 0;

	const int setGuiStart = Sys_Milliseconds();
	SetGUI( guiMainMenu, NULL );
	const int setGuiMsec = Sys_Milliseconds() - setGuiStart;

	const int introStart = Sys_Milliseconds();
	guiMainMenu->HandleNamedEvent( shouldPlayIntro ? "playIntro" : "noIntro" );
	const int introMsec = Sys_Milliseconds() - introStart;
	menuIntroBlackoutActive = true;
	menuIntroBlackoutAwaitMenuMusic = shouldPlayIntro;
	menuIntroBlackoutFadeStart = -1;
	fallbackMenuStartTime = -1;
	if ( !shouldPlayIntro ) {
		// Ensure menu music always restarts when returning from gameplay.
		guiMainMenu->HandleNamedEvent( "MusicRestart" );
	}


	if(fileSystem->HasD3XP()) {
		guiMainMenu->SetStateString("game_list", common->GetLanguageDict()->GetString( "#str_07202" ));
	} else {
		guiMainMenu->SetStateString("game_list", common->GetLanguageDict()->GetString( "#str_107212" ));
	}

	console->Close();

	if ( com_showLevelLoadTimes.GetBool() ) {
		const int menuProfileTotal = Sys_Milliseconds() - menuProfileStart;
		common->Printf(
			"Main menu phases: setGUI=%d (saveVars=%d mainVars=%d other=%d) introEvent=%d rest=%d total=%d msec\n",
			setGuiMsec,
			menuProfileSaveVarsMsec,
			menuProfileMainVarsMsec,
			setGuiMsec - menuProfileSaveVarsMsec - menuProfileMainVarsMsec,
			introMsec,
			menuProfileTotal - setGuiMsec - introMsec,
			menuProfileTotal );
	}
}

bool idSessionLocal::IsMainMenuIntroPlaying() const {
	return guiActive == guiMainMenu &&
		guiMainMenu != NULL &&
		guiMainMenu->GetStateInt( "desktop::video_check", "0" ) != 0;
}

/*
=================
idSessionLocal::SetGUI
=================
*/
void idSessionLocal::SetGUI( idUserInterface *gui, HandleGuiCommand_t handle ) {
	if ( RetainedUI_IsOpen() ) RetainedUI_Close();
	const char	*cmd;
	const bool arenaSoundRoute =
		( guiActive != NULL && idStr::Icmp( guiActive->Name(), "guis/arena_pause.gui" ) == 0 ) ||
		( gui != NULL && idStr::Icmp( gui->Name(), "guis/arena_pause.gui" ) == 0 );
	const bool resumeSystemParent = guiSystem != NULL && gui == guiSystemParent;
	if ( guiSystem != NULL && gui != guiSystem ) {
		// Explicit replacement/stop is forced teardown. Ordinary Back uses the
		// guarded Return operation and cannot discard a dirty/recovering owner.
		CloseSystemSettings();
	}

	if ( guiActive && guiActive != gui ) {
		idUserInterface *previous = guiActive;
		openq4::NativeInputBeforeSessionChange();
		guiActive = NULL;
		previous->Activate( false, common->GetPresentationTime() );
		PumpApplicationActions( previous );
	}
	openq4::NativeInputBeforeSessionChange();
	guiActive = gui;
	guiHandle = handle;
	if ( guiMsgRestore ) {
		common->DPrintf( "idSessionLocal::SetGUI: cleared an active message box\n" );
		guiMsgRestore = NULL;
	}
	if ( arenaSoundRoute ) {
		SetPlayingSoundWorld();
	}
	if ( !guiActive ) {
		return;
	}

	if ( guiActive == guiMainMenu && !resumeSystemParent ) {
		// Opening ESC must never wait for unbounded filesystem, device, display,
		// or declaration enumeration. Those catalogs are refreshed when their
		// page is opened; the title-screen path still primes them for stock GUIs.
		const bool refreshCatalogs = !mapSpawned;
		const int saveVarsStart = Sys_Milliseconds();
		if ( refreshCatalogs ) {
			SetSaveGameGuiVars();
		}
		menuProfileSaveVarsMsec = Sys_Milliseconds() - saveVarsStart;
		const int mainVarsStart = Sys_Milliseconds();
		SetMainMenuGuiVars( refreshCatalogs );
		menuProfileMainVarsMsec = Sys_Milliseconds() - mainVarsStart;
	} else if ( guiActive == guiRestartMenu ) {
		SetSaveGameGuiVars();
	}

	sysEvent_t  ev;
	memset( &ev, 0, sizeof( ev ) );
	ev.evType = SE_NONE;

	cmd = guiActive->HandleEvent( &ev, common->GetPresentationTime() );
	guiActive->Activate( true, common->GetPresentationTime() );
	PumpApplicationActions( guiActive );
}

/*
===============
idSessionLocal::ExitMenu
===============
*/
void idSessionLocal::ExitMenu( void ) {
	if ( guiSystem != NULL && ( guiActive == guiSystem || guiMsgRestore == guiSystem ) ) {
		ReturnSystemSettings();
		return;
	}
	idUserInterface *previous = guiActive;
	const bool arenaPause = previous != NULL && idStr::Icmp( previous->Name(), "guis/arena_pause.gui" ) == 0;
	openq4::NativeInputBeforeSessionChange();
	guiActive = NULL;
	if ( previous ) {
		previous->Activate( false, common->GetPresentationTime() );
		PumpApplicationActions( previous );
	}
	if ( guiActive != NULL ) return;

	// go back to the game sounds
	SetPlayingSoundWorld( sw );

	// Arena releases only its own pause on the next game frame. The ordinary
	// session menu owns the SP pause; it must not release a borrowed Arena pause.
	if ( !arenaPause && sw != NULL && sw->IsPaused() ) {
		sw->UnPause();
	}
}

// The SYSTEM button opens the retained page while it is enabled (by its own
// opt-in, or by the gate once complete) and has not fallen back to the stock
// page this session.
bool idSessionLocal::RetainedInputTarget() const {
	if ( guiActive == NULL ) {
		return false;
	}
	if ( guiRetainedHome != NULL && guiActive == guiMainMenu ) {
		return true;
	}
	// The multiplayer card takes the game menu's input while it covers it,
	// and repeats a press that is never released as the home screen would.
	if ( RetainedMultiplayerCovers() ) {
		return true;
	}
	return UI_IsRetainedPath( guiActive->Name() );
}

idUserInterface *idSessionLocal::RetainedStickTarget() const {
	if ( guiActive == NULL || ( retainedSubpageFrom != NULL && guiActive == retainedSubpageFrom ) ) {
		return NULL;
	}
	if ( guiRetainedHome != NULL && guiActive == guiMainMenu ) {
		return RetainedHomeInputBlocked() ? NULL : guiRetainedHome;
	}
	if ( RetainedMultiplayerCovers() ) {
		return guiRetainedMultiplayer;
	}
	return UI_IsRetainedPath( guiActive->Name() ) ? guiActive : NULL;
}

bool idSessionLocal::RetainedSystemAvailable() const {
	return Session_RetainedSystemEnabled() && retainedStock.FindIndex( RETAINED_SYSTEM_GUI ) < 0;
}

bool idSessionLocal::OpenSystemSettings() {
#ifdef ID_DEDICATED
	return false;
#else
	if ( !Session_RetainedSystemEnabled() || systemGuiTransition || guiTest != NULL || RetainedUI_IsOpen() ) return false;
	if ( guiSystem != NULL ) return guiActive == guiSystem;
	if ( guiMainMenu == NULL || !RetainedSettingsParent() || guiMsgRestore != NULL ) return false;
	// Load and validate its application contract before deactivating the
	// parent. A page that is missing or invalid leaves the stock SYSTEM page
	// in use for the rest of the session.
	idUserInterface *child = FindRetainedGui( RETAINED_SYSTEM_GUI, false, false );
	if ( child == NULL ) return false;
	if ( !UI_RetainedSettingsDocument( child ) ) {
		common->Warning( "retained UI: '%s' is not a SYSTEM settings page; the stock page stays in use", child->Name() );
		retainedStock.Append( RETAINED_SYSTEM_GUI );
		uiManager->DeAlloc( child );
		return false;
	}
	guiSystemParent = guiActive;
	guiSystemParentHandle = guiHandle;
	guiSystem = child;
	systemGuiTransition = true;
	SetGUI( child, NULL );
	systemGuiTransition = false;
	SetPlayingSoundWorld();
	return guiActive == child;
#endif
}

// The SYSTEM button, and the multiplayer menu's SYSTEM route through the main
// menu, open the retained page. A page that fell back opens the stock one, now
// and from then on. No current state refuses a page that is still available
// when these requests arrive; should one, that request opens the stock page
// instead of vanishing, and the button keeps the retained page.
void idSessionLocal::OpenSystemSettingsRoute( bool fromMultiplayer ) {
	// The stock action asks for the retained page again if it ever reads a
	// stale gui::retainedSystem; a nested request then does nothing.
	static bool routing = false;
	if ( routing || guiMainMenu == NULL || OpenSystemSettings() ) {
		return;
	}
	routing = true;
	const bool available = RetainedSystemAvailable();
	if ( available ) {
		common->DWarning( "retained UI: the SYSTEM page could not open; the stock page opens for this request" );
	}
	if ( fromMultiplayer ) {
		// The stock route reads no state; it shows the stock SYSTEM page.
		if ( !available ) {
			guiMainMenu->SetStateBool( "retainedSystem", false );
			guiMainMenu->StateChanged( common->GetPresentationTime() );
		}
		guiMainMenu->HandleNamedEvent( "fromMp_toSystemStock" );
		routing = false;
		return;
	}
	// The button's action opens the stock page only while gui::retainedSystem
	// is clear: from now on once the page fell back, else for this action.
	guiMainMenu->SetStateBool( "retainedSystem", false );
	guiMainMenu->StateChanged( common->GetPresentationTime() );
	idStr command;
	if ( UI_RunLegacyWindowAction( guiMainMenu, "set_b_system", false, command ) && command.Length() > 0 ) {
		DispatchCommand( guiMainMenu, command.c_str() );
	}
	if ( available ) {
		guiMainMenu->SetStateBool( "retainedSystem", true );
		guiMainMenu->StateChanged( common->GetPresentationTime() );
	}
	routing = false;
}

/*
===============
idSessionLocal::RetainedAudioAvailable

The AUDIO button opens the retained page while the retained screens are on
and the page has not fallen back to the stock one this session.
===============
*/
bool idSessionLocal::RetainedAudioAvailable() const {
#ifdef ID_DEDICATED
	return false;
#else
	return Session_RetainedScreensEnabled() && retainedStock.FindIndex( RETAINED_AUDIO_GUI ) < 0;
#endif
}

/*
===============
idSessionLocal::OpenRetainedAudio

The retained AUDIO page over the settings screen that asked for it, its
values read back first. A page that is missing or cannot load leaves the
stock page in use (FindRetainedGui).
===============
*/
bool idSessionLocal::OpenRetainedAudio() {
#ifdef ID_DEDICATED
	return false;
#else
	if ( !RetainedAudioAvailable() || guiMainMenu == NULL || !RetainedSettingsParent() || guiMsgRestore != NULL || guiTest != NULL ) {
		return false;
	}
	if ( guiRetainedAudio != NULL && UI_RetainedViewFailed( guiRetainedAudio ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_AUDIO_GUI );
		retainedStock.AddUnique( RETAINED_AUDIO_GUI );
		guiRetainedAudio = NULL;
		return false;
	}
	if ( guiRetainedAudio == NULL ) {
		guiRetainedAudio = FindRetainedGui( RETAINED_AUDIO_GUI, true, false );
	}
	if ( guiRetainedAudio == NULL ) {
		return false;
	}
	retainedAudioParent = guiActive;
	retainedAudioParentHandle = guiHandle;
	PublishRetainedAudio( guiRetainedAudio );
	SetGUI( guiRetainedAudio, NULL );
	return guiActive == guiRetainedAudio;
#endif
}

/*
===============
idSessionLocal::OpenRetainedAudioRoute

The AUDIO button opens the retained page, or the stock one where it could
not open: from then on once the page fell back, else for this press.
===============
*/
void idSessionLocal::OpenRetainedAudioRoute() {
	static bool routing = false;
	if ( routing || guiMainMenu == NULL || OpenRetainedAudio() ) {
		return;
	}
	routing = true;
	const bool available = RetainedAudioAvailable();
	guiMainMenu->SetStateBool( "retainedAudio", false );
	guiMainMenu->StateChanged( common->GetPresentationTime() );
	idStr command;
	if ( UI_RunLegacyWindowAction( guiMainMenu, "set_b_audio", false, command ) && command.Length() > 0 ) {
		DispatchCommand( guiMainMenu, command.c_str() );
	}
	if ( available ) {
		guiMainMenu->SetStateBool( "retainedAudio", true );
		guiMainMenu->StateChanged( common->GetPresentationTime() );
	}
	routing = false;
}

/*
===============
idSessionLocal::ReturnRetainedAudio

BACK, or the back input, returns to the settings screen that opened the page:
to the SETTINGS page one level up (BeginRetainedSubpage), else at once.
===============
*/
void idSessionLocal::ReturnRetainedAudio() {
	if ( guiRetainedAudio == NULL || guiActive != guiRetainedAudio || retainedAudioParent == NULL ) {
		return;
	}
	if ( guiRetainedSettingsPage != NULL && retainedAudioParent == guiRetainedSettingsPage ) {
		BeginRetainedSubpage( guiRetainedAudio, false, RETAINED_SUBPAGE_SETTINGS_AUDIO );
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	idUserInterface *parent = retainedAudioParent;
	const HandleGuiCommand_t handler = retainedAudioParentHandle;
	retainedAudioParent = NULL;
	retainedAudioParentHandle = NULL;
	SetGUI( parent, handler );
}

/*
===============
idSessionLocal::PublishRetainedAudio

The AUDIO page's values as the session reads them back: the three volumes
and the subtitle size and background as whole percents, the sound system,
the device (the row of s_deviceName in the device list, which it publishes
too, read as a device's text), EAX, the speakers' choice (Auto, 2 or 6),
HRTF and the three subtitle switches.
===============
*/
void idSessionLocal::PublishRetainedAudio( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	if ( gui == NULL ) {
		return;
	}
	const auto percent = []( const char *cvar, float scale ) {
		return idMath::ClampInt( 0, 999, idMath::Ftoi( cvarSystem->GetCVarFloat( cvar ) * scale + 0.5f ) );
	};
	gui->SetStateInt( "audio.volume", percent( "s_volume", 100.0f ) );
	gui->SetStateInt( "audio.music", percent( "s_musicVolume", 100.0f ) );
	gui->SetStateInt( "audio.dialogue", percent( "s_dialogueVolume", 100.0f ) );
	gui->SetStateInt( "audio.backend", cvarSystem->GetCVarBool( "s_useOpenAL" ) ? 1 : 0 );
	idStr names, values;
	BuildMainMenuAudioDeviceChoices( names, values );
	idStrList nameList, valueList;
	Session_ChoiceListItems( names.c_str(), nameList );
	Session_ChoiceListItems( values.c_str(), valueList );
	const int count = Min( nameList.Num(), RETAINED_AUDIO_DEVICES );
	int device = 0;
	for ( int i = 0; i < count; ++i ) {
		gui->SetStateString( va( "audio.device.%d.label", i ), Session_RetainedPlayerText( nameList[ i ].c_str(), 128, 1 ).c_str() );
		if ( i < valueList.Num() && !valueList[ i ].Icmp( cvarSystem->GetCVarString( "s_deviceName" ) ) ) {
			device = i;
		}
	}
	for ( int i = count; i < RETAINED_AUDIO_DEVICES; ++i ) {
		gui->SetStateString( va( "audio.device.%d.label", i ), "" );
	}
	gui->SetStateInt( "audio.device.count", count );
	gui->SetStateInt( "audio.device", device );
	gui->SetStateBool( "audio.eax", cvarSystem->GetCVarBool( "s_useEAXReverb" ) );
	int speakers = 0;
	for ( int i = 0; i < 3; ++i ) {
		if ( cvarSystem->GetCVarInteger( "s_numberOfSpeakers" ) == RETAINED_AUDIO_SPEAKERS[ i ] ) {
			speakers = i;
		}
	}
	gui->SetStateInt( "audio.speakers", speakers );
	gui->SetStateInt( "audio.hrtf", idMath::ClampInt( 0, 2, cvarSystem->GetCVarInteger( "s_openALHRTF" ) ) );
	gui->SetStateBool( "audio.subtitles", cvarSystem->GetCVarBool( "ui_subtitles" ) );
	gui->SetStateInt( "audio.subtitleSize", percent( "ui_subtitleScale", 100.0f ) );
	gui->SetStateInt( "audio.subtitleBackground", percent( "ui_subtitleBackground", 100.0f ) );
	gui->SetStateBool( "audio.subtitleSpeakers", cvarSystem->GetCVarBool( "ui_subtitleSpeakers" ) );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::HandleRetainedAudioRequest

The AUDIO page's verbs: each value applies at once, as the stock page's
controls do, the device rows through the stock page's own commands (sound
drivar, sound eax, sound speakers), which restart the sound system and may
ask with the stock message box; the page then reads every value back. BACK
returns to the settings screen.
===============
*/
void idSessionLocal::HandleRetainedAudioRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( !idStr::Icmp( request, "audioBack" ) ) {
		ReturnRetainedAudio();
		return;
	}
	int field = -1, value = 0;
	if ( !Session_RetainedValueRequest( request, RETAINED_AUDIO_VERBS, RETAINED_AUDIO_VERB_COUNT, field, value ) ) {
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	switch ( field ) {
		case 0: cvarSystem->SetCVarFloat( "s_volume", idMath::ClampInt( 0, 100, value ) / 100.0f ); break;
		case 1: cvarSystem->SetCVarFloat( "s_musicVolume", idMath::ClampInt( 0, 100, value ) / 100.0f ); break;
		case 2: cvarSystem->SetCVarFloat( "s_dialogueVolume", idMath::ClampInt( 0, 100, value ) / 100.0f ); break;
		case 3:
			cvarSystem->SetCVarBool( "s_useOpenAL", value != 0 );
			HandleMainMenuCommands( "sound drivar" );
			break;
		case 4: {
			idStr names, values;
			BuildMainMenuAudioDeviceChoices( names, values );
			idStrList valueList;
			Session_ChoiceListItems( values.c_str(), valueList );
			if ( value < 0 || value >= valueList.Num() || value >= RETAINED_AUDIO_DEVICES ) {
				return;
			}
			cvarSystem->SetCVarString( "s_deviceName", valueList[ value ].c_str() );
			HandleMainMenuCommands( "sound drivar" );
			break;
		}
		case 5:
			cvarSystem->SetCVarBool( "s_useEAXReverb", value != 0 );
			HandleMainMenuCommands( "sound eax" );
			break;
		case 6:
			cvarSystem->SetCVarInteger( "s_numberOfSpeakers", RETAINED_AUDIO_SPEAKERS[ idMath::ClampInt( 0, 2, value ) ] );
			HandleMainMenuCommands( "sound speakers" );
			break;
		case 7:
			cvarSystem->SetCVarInteger( "s_openALHRTF", idMath::ClampInt( 0, 2, value ) );
			HandleMainMenuCommands( "sound drivar" );
			break;
		case 8: cvarSystem->SetCVarBool( "ui_subtitles", value != 0 ); break;
		case 9: cvarSystem->SetCVarFloat( "ui_subtitleScale", idMath::ClampInt( 100, 200, value ) / 100.0f ); break;
		case 10: cvarSystem->SetCVarFloat( "ui_subtitleBackground", idMath::ClampInt( 0, 100, value ) / 100.0f ); break;
		case 11: cvarSystem->SetCVarBool( "ui_subtitleSpeakers", value != 0 ); break;
		default: return;
	}
	PublishRetainedAudio( gui );
#endif
}

/*
===============
idSessionLocal::RetainedGameAvailable

The GAME OPTIONS button opens the retained page while the retained screens
are on and the page has not fallen back to the stock one this session.
===============
*/
bool idSessionLocal::RetainedGameAvailable() const {
#ifdef ID_DEDICATED
	return false;
#else
	return Session_RetainedScreensEnabled() && retainedStock.FindIndex( RETAINED_GAME_GUI ) < 0;
#endif
}

/*
===============
idSessionLocal::OpenRetainedGame

The retained GAME OPTIONS page over the settings screen that asked for it,
the rows the stock page computes read back first. A page that is missing or
cannot load leaves the stock page in use (FindRetainedGui).
===============
*/
bool idSessionLocal::OpenRetainedGame() {
#ifdef ID_DEDICATED
	return false;
#else
	if ( !RetainedGameAvailable() || guiMainMenu == NULL || !RetainedSettingsParent() || guiMsgRestore != NULL || guiTest != NULL ) {
		return false;
	}
	if ( guiRetainedGame != NULL && UI_RetainedViewFailed( guiRetainedGame ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_GAME_GUI );
		retainedStock.AddUnique( RETAINED_GAME_GUI );
		guiRetainedGame = NULL;
		return false;
	}
	if ( guiRetainedGame == NULL ) {
		guiRetainedGame = FindRetainedGui( RETAINED_GAME_GUI, true, false );
	}
	if ( guiRetainedGame == NULL ) {
		return false;
	}
	retainedGameParent = guiActive;
	retainedGameParentHandle = guiHandle;
	guiRetainedGame->SetStateBool( "game.languageReloaded", false );
	PublishRetainedGame( guiRetainedGame );
	SetGUI( guiRetainedGame, NULL );
	return guiActive == guiRetainedGame;
#endif
}

/*
===============
idSessionLocal::OpenRetainedGameRoute

The GAME OPTIONS button, and the multiplayer menu's route through the main
menu, open the retained page, or the stock one where it could not open: from
then on once the page fell back, else for this request.
===============
*/
void idSessionLocal::OpenRetainedGameRoute( bool fromMultiplayer ) {
	static bool routing = false;
	if ( routing || guiMainMenu == NULL || OpenRetainedGame() ) {
		return;
	}
	routing = true;
	const bool available = RetainedGameAvailable();
	if ( fromMultiplayer ) {
		// The stock route reads no state; it shows the stock page.
		if ( !available ) {
			guiMainMenu->SetStateBool( "retainedGame", false );
			guiMainMenu->StateChanged( common->GetPresentationTime() );
		}
		guiMainMenu->HandleNamedEvent( "fromMp_toGameoptionsStock" );
		routing = false;
		return;
	}
	guiMainMenu->SetStateBool( "retainedGame", false );
	guiMainMenu->StateChanged( common->GetPresentationTime() );
	idStr command;
	if ( UI_RunLegacyWindowAction( guiMainMenu, "set_b_gameoptions", false, command ) && command.Length() > 0 ) {
		DispatchCommand( guiMainMenu, command.c_str() );
	}
	if ( available ) {
		guiMainMenu->SetStateBool( "retainedGame", true );
		guiMainMenu->StateChanged( common->GetPresentationTime() );
	}
	routing = false;
}

/*
===============
idSessionLocal::ReturnRetainedGame

BACK, or the back input, returns to the settings screen that opened the page:
to the SETTINGS page one level up (BeginRetainedSubpage), else at once.
===============
*/
void idSessionLocal::ReturnRetainedGame() {
	if ( guiRetainedGame == NULL || guiActive != guiRetainedGame || retainedGameParent == NULL ) {
		return;
	}
	if ( guiRetainedSettingsPage != NULL && retainedGameParent == guiRetainedSettingsPage ) {
		BeginRetainedSubpage( guiRetainedGame, false, RETAINED_SUBPAGE_SETTINGS_GAME );
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	idUserInterface *parent = retainedGameParent;
	const HandleGuiCommand_t handler = retainedGameParentHandle;
	retainedGameParent = NULL;
	retainedGameParentHandle = NULL;
	SetGUI( parent, handler );
}

/*
===============
idSessionLocal::PublishRetainedGame

The rows the stock page computes, as the session reads them back: the gun
position whose offsets g_gunX/Y/Z hold (none for others), the forced pro
skins, the custom crosshair's place among the crosshairs (1 for the first)
and its picture, the corpse time preset this game's delay holds (Custom,
past the presets, for others) and the language. The rows that name a CVar
read it themselves.
===============
*/
void idSessionLocal::PublishRetainedGame( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	if ( gui == NULL ) {
		return;
	}
	int gunPosition = -1;
	const int gunPositions = sizeof( MAINMENU_GUN_POSITION_PRESETS ) / sizeof( MAINMENU_GUN_POSITION_PRESETS[0] );
	for ( int i = 0; i < gunPositions; i++ ) {
		const mainMenuGunPositionPreset_t &preset = MAINMENU_GUN_POSITION_PRESETS[ i ];
		if ( cvarSystem->GetCVarFloat( "g_gunX" ) == preset.x && cvarSystem->GetCVarFloat( "g_gunY" ) == preset.y &&
			cvarSystem->GetCVarFloat( "g_gunZ" ) == preset.z ) {
			gunPosition = i;
		}
	}
	gui->SetStateInt( "game.gunPosition", gunPosition );
	gui->SetStateBool( "game.forceModel", cvarSystem->GetCVarString( "g_forceModel" )[0] != '\0' );
	const idDict *crosshairs = MainMenuCrosshairDict();
	if ( crosshairs != NULL ) {
		const idKeyValue *current = MainMenuFindCrosshair( *crosshairs, cvarSystem->GetCVarString( "g_crosshairCustomFile" ), 0 );
		int place = 1;
		for ( const idKeyValue *kv = crosshairs->MatchPrefix( MAINMENU_CROSSHAIR_PREFIX ); kv != NULL && kv != current;
			kv = crosshairs->MatchPrefix( MAINMENU_CROSSHAIR_PREFIX, kv ) ) {
			place++;
		}
		gui->SetStateInt( "game.crosshair", place );
		gui->SetStateString( "game.crosshairImage", current != NULL ? current->GetValue().c_str() : "" );
	}
	const char *corpseDelay = guiMainMenu != NULL && guiMainMenu->State().GetInt( "ingame" ) == 2 ? "g_corpseRemoveDelayMP" : "g_corpseRemoveDelaySP";
	const int corpseTimes = sizeof( MAINMENU_CORPSE_TIME_PRESET_VALUES ) / sizeof( MAINMENU_CORPSE_TIME_PRESET_VALUES[0] );
	int corpseTime = corpseTimes;
	for ( int i = 0; i < corpseTimes; i++ ) {
		if ( cvarSystem->GetCVarFloat( corpseDelay ) == MAINMENU_CORPSE_TIME_PRESET_VALUES[ i ] ) {
			corpseTime = i;
			break;
		}
	}
	gui->SetStateInt( "game.corpseTime", corpseTime );
	int language = 0;
	for ( int i = 0; i < RETAINED_GAME_LANGUAGE_COUNT; i++ ) {
		if ( !idStr::Icmp( cvarSystem->GetCVarString( "sys_lang" ), RETAINED_GAME_LANGUAGES[ i ] ) ) {
			language = i;
		}
	}
	gui->SetStateInt( "game.language", language );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::HandleRetainedGameRequest

The GAME OPTIONS page's computed rows, each applied at once as the stock
page applies it, through the stock main menu's own choices so that page
agrees: the gun position (ApplyMainMenuGunPositionChoice), the pro skins
(ApplyMainMenuForceModelChoice), the custom crosshair, the corpse time
(ApplyMainMenuCorpseTimeChoice) and the language, which reloads the language
and the menus as the stock list does. The page then reads them back. BACK
returns to the settings screen.
===============
*/
void idSessionLocal::HandleRetainedGameRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( !idStr::Icmp( request, "gameBack" ) ) {
		ReturnRetainedGame();
		return;
	}
	int field = -1, value = 0;
	if ( guiMainMenu == NULL || !Session_RetainedValueRequest( request, RETAINED_GAME_VERBS, RETAINED_GAME_VERB_COUNT, field, value ) ) {
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	switch ( field ) {
		case 0:
			if ( value < 0 || value >= static_cast<int>( sizeof( MAINMENU_GUN_POSITION_PRESETS ) / sizeof( MAINMENU_GUN_POSITION_PRESETS[0] ) ) ) {
				return;
			}
			guiMainMenu->SetStateInt( "g_gunXYZ", value );
			ApplyMainMenuGunPositionChoice( guiMainMenu );
			break;
		case 1:
			guiMainMenu->SetStateBool( "ui_proskins", value != 0 );
			ApplyMainMenuForceModelChoice( guiMainMenu );
			break;
		case 2: {
			// The crosshair at this place (1 for the first), shown in the stock
			// page's preview as it is chosen.
			const idDict *crosshairs = MainMenuCrosshairDict();
			const idKeyValue *chosen = crosshairs != NULL ? crosshairs->MatchPrefix( MAINMENU_CROSSHAIR_PREFIX ) : NULL;
			for ( int place = 1; chosen != NULL && place < value; place++ ) {
				chosen = crosshairs->MatchPrefix( MAINMENU_CROSSHAIR_PREFIX, chosen );
			}
			if ( value < 1 || chosen == NULL ) {
				return;
			}
			cvarSystem->SetCVarString( "g_crosshairCustomFile", chosen->GetValue().c_str() );
			MainMenuPublishCrosshair( guiMainMenu, 0 );
			break;
		}
		case 3:
			if ( value < 0 || value >= static_cast<int>( sizeof( MAINMENU_CORPSE_TIME_PRESET_VALUES ) / sizeof( MAINMENU_CORPSE_TIME_PRESET_VALUES[0] ) ) ) {
				return;
			}
			guiMainMenu->SetStateInt( "corpse_time_choice", value );
			ApplyMainMenuCorpseTimeChoice( guiMainMenu );
			break;
		case 4:
			if ( value < 0 || value >= RETAINED_GAME_LANGUAGE_COUNT ) {
				return;
			}
			if ( idStr::Icmp( cvarSystem->GetCVarString( "sys_lang" ), RETAINED_GAME_LANGUAGES[ value ] ) ) {
				cvarSystem->SetCVarString( "sys_lang", RETAINED_GAME_LANGUAGES[ value ] );
				guiMainMenu->SetStateInt( "curr_lang", value );
				// The reload re-activates the page; it returns to Language.
				gui->SetStateBool( "game.languageReloaded", true );
				HandleMainMenuCommands( "reloadLanguage ; reloadGuis" );
			}
			break;
		default:
			return;
	}
	PublishRetainedGame( gui );
#endif
}

/*
===============
idSessionLocal::RetainedControlsAvailable

The CONTROLS button opens the retained page while the retained screens are
on and the page has not fallen back to the stock one this session.
===============
*/
bool idSessionLocal::RetainedControlsAvailable() const {
#ifdef ID_DEDICATED
	return false;
#else
	return Session_RetainedScreensEnabled() && retainedStock.FindIndex( RETAINED_CONTROLS_GUI ) < 0;
#endif
}

/*
===============
idSessionLocal::OpenRetainedControls

The retained CONTROLS page over the settings screen that asked for it, the
bindings read back first. A page that is missing or cannot load leaves the
stock page in use (FindRetainedGui).
===============
*/
bool idSessionLocal::OpenRetainedControls() {
#ifdef ID_DEDICATED
	return false;
#else
	if ( !RetainedControlsAvailable() || guiMainMenu == NULL || !RetainedSettingsParent() || guiMsgRestore != NULL || guiTest != NULL ) {
		return false;
	}
	if ( guiRetainedControls != NULL && UI_RetainedViewFailed( guiRetainedControls ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_CONTROLS_GUI );
		retainedStock.AddUnique( RETAINED_CONTROLS_GUI );
		guiRetainedControls = NULL;
		return false;
	}
	if ( guiRetainedControls == NULL ) {
		guiRetainedControls = FindRetainedGui( RETAINED_CONTROLS_GUI, true, false );
	}
	if ( guiRetainedControls == NULL ) {
		return false;
	}
	retainedControlsParent = guiActive;
	retainedControlsParentHandle = guiHandle;
	retainedControlsCapture = -1;
	retainedControlsCaptured = -1;
	PublishRetainedControls( guiRetainedControls );
	SetGUI( guiRetainedControls, NULL );
	return guiActive == guiRetainedControls;
#endif
}

/*
===============
idSessionLocal::OpenRetainedControlsRoute

The CONTROLS button, and the multiplayer menu's route through the main menu,
open the retained page, or the stock one where it could not open: from then
on once the page fell back, else for this request.
===============
*/
void idSessionLocal::OpenRetainedControlsRoute( bool fromMultiplayer ) {
	static bool routing = false;
	if ( routing || guiMainMenu == NULL || OpenRetainedControls() ) {
		return;
	}
	routing = true;
	const bool available = RetainedControlsAvailable();
	if ( fromMultiplayer ) {
		// The stock route reads no state; it shows the stock page.
		if ( !available ) {
			guiMainMenu->SetStateBool( "retainedControls", false );
			guiMainMenu->StateChanged( common->GetPresentationTime() );
		}
		guiMainMenu->HandleNamedEvent( "fromMp_toControlsStock" );
		routing = false;
		return;
	}
	guiMainMenu->SetStateBool( "retainedControls", false );
	guiMainMenu->StateChanged( common->GetPresentationTime() );
	idStr command;
	if ( UI_RunLegacyWindowAction( guiMainMenu, "set_b_controls", false, command ) && command.Length() > 0 ) {
		DispatchCommand( guiMainMenu, command.c_str() );
	}
	if ( available ) {
		guiMainMenu->SetStateBool( "retainedControls", true );
		guiMainMenu->StateChanged( common->GetPresentationTime() );
	}
	routing = false;
}

/*
===============
idSessionLocal::ReturnRetainedControls

BACK, or the back input, returns to the settings screen that opened the page:
to the SETTINGS page one level up (BeginRetainedSubpage), else at once.
===============
*/
void idSessionLocal::ReturnRetainedControls() {
	if ( guiRetainedControls == NULL || guiActive != guiRetainedControls || retainedControlsParent == NULL ) {
		return;
	}
	if ( guiRetainedSettingsPage != NULL && retainedControlsParent == guiRetainedSettingsPage ) {
		BeginRetainedSubpage( guiRetainedControls, false, RETAINED_SUBPAGE_SETTINGS_CONTROLS );
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	idUserInterface *parent = retainedControlsParent;
	const HandleGuiCommand_t handler = retainedControlsParentHandle;
	retainedControlsParent = NULL;
	retainedControlsParentHandle = NULL;
	retainedControlsCapture = -1;
	SetGUI( parent, handler );
}

/*
===============
idSessionLocal::PublishRetainedControls

Each row's keys, as the language names them and joins a list, or the stock
page's word for none; the row waiting for a key says so instead.
===============
*/
void idSessionLocal::PublishRetainedControls( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	if ( gui == NULL ) {
		return;
	}
	gui->SetKeyBindingNames();
	if ( retainedControlsCapture >= 0 && retainedControlsCapture < RETAINED_CONTROLS_ROWS ) {
		gui->SetStateString( va( "controls.%d.keys", retainedControlsCapture ),
			common->GetLanguageDict()->GetString( "#str_07000" ) );
	}
	gui->SetStateInt( "controls.capture", retainedControlsCapture );
	// TOUCH LAYOUT: the built-in touch controls' editor lives in the Android setup.
	gui->SetStateBool( "controls.touchLayout", Session_AndroidSetupAvailable() );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::HandleRetainedControlsRequest

The CONTROLS page's verbs on the focused row: controlsBind waits for its new
key (RetainedControlsKey), controlsClear clears it as the stock field's
Delete does. BACK returns to the settings screen. controlsTouchLayout opens
the Android setup's touch layout editor; the game waits behind it as for any
return from setup.
===============
*/
void idSessionLocal::HandleRetainedControlsRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( !idStr::Icmp( request, "controlsBack" ) ) {
		ReturnRetainedControls();
		return;
	}
	if ( !idStr::Icmp( request, "controlsTouchLayout" ) ) {
		if ( Session_AndroidSetupAvailable() ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			Session_OpenAndroidSetup( ANDROID_SETUP_TOUCH_LAYOUT );
		}
		return;
	}
	const bool bind = !idStr::Icmp( request, "controlsBind" );
	const bool clear = !idStr::Icmp( request, "controlsClear" );
	if ( !bind && !clear ) {
		return;
	}
	const std::string focus = UI_RetainedFocusedControl( gui );
	static const char prefix[] = "controls_bind_";
	if ( focus.compare( 0, sizeof( prefix ) - 1, prefix ) != 0 ) {
		return;
	}
	const int row = atoi( focus.c_str() + sizeof( prefix ) - 1 );
	if ( row < 0 || row >= RETAINED_CONTROLS_ROWS || focus != va( "%s%d", prefix, row ) ) {
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	if ( bind ) {
		retainedControlsCapture = row;
		retainedControlsCaptured = -1;
	} else {
		idKeyInput::UnbindBinding( RETAINED_CONTROLS_BINDINGS[ row ] );
		if ( guiMainMenu != NULL ) {
			guiMainMenu->SetKeyBindingNames();
		}
	}
	PublishRetainedControls( gui );
#endif
}

/*
===============
idSessionLocal::RetainedControlsKey

While the CONTROLS page waits for a key it takes the next key pressed, as
the stock binding field does: Escape, Start or Back cancel, Backspace or
Delete clear the row, any other key joins its keys (at most two of each device
family, idKeyInput::BindForMenu). The key's release and characters are its own. Out of
a wait, Delete or Backspace on a row clears it.
===============
*/
bool idSessionLocal::RetainedControlsKey( const sysEvent_t *event ) {
#ifdef ID_DEDICATED
	return false;
#else
	if ( event == NULL || guiRetainedControls == NULL || guiActive != guiRetainedControls ) {
		return false;
	}
	if ( event->evType == SE_KEY && !event->evValue2 && event->evValue == retainedControlsCaptured ) {
		retainedControlsCaptured = -1;
		return true;
	}
	if ( retainedControlsCapture < 0 ) {
		if ( event->evType == SE_KEY && event->evValue2 && ( event->evValue == K_DEL || event->evValue == K_BACKSPACE ) ) {
			HandleRetainedControlsRequest( guiRetainedControls, "controlsClear" );
			return true;
		}
		return false;
	}
	if ( event->evType == SE_CHAR ) {
		return true;
	}
	if ( event->evType != SE_KEY || !event->evValue2 ) {
		// The release of the key that began the wait still reaches the page.
		return false;
	}
	const int key = event->evValue;
	const char *binding = RETAINED_CONTROLS_BINDINGS[ retainedControlsCapture ];
	if ( key == K_BACKSPACE || key == K_DEL ) {
		idKeyInput::UnbindBinding( binding );
	} else if ( key != K_ESCAPE && key != K_JOY7 && key != K_JOY8 ) {
		idKeyInput::BindForMenu( key, binding );
	}
	if ( guiMainMenu != NULL ) {
		guiMainMenu->SetKeyBindingNames();
	}
	retainedControlsCapture = -1;
	retainedControlsCaptured = key;
	HandleMainMenuCommands( "play main_menu_selection" );
	PublishRetainedControls( guiRetainedControls );
	return true;
#endif
}

bool idSessionLocal::ReturnSystemSettings() {
#ifdef ID_DEDICATED
	return false;
#else
	if ( systemGuiTransition || guiSystem == NULL || guiActive != guiSystem ) return false;
	if ( !UI_RetainedSettingsCanReturn( guiSystem ) ) {
		// An authored Back may open a dirty-state modal or request recovery. A
		// recursive dismiss from that event must not replay it indefinitely.
		if ( systemGuiBackEvent ) return false;
		systemGuiBackEvent = true;
		idUserInterface *child = guiSystem;
		child->HandleNamedEvent( "onBack" );
		PumpApplicationActions( child );
		systemGuiBackEvent = false;
		// The authored dismiss may have started the climb to the SETTINGS page.
		return ( guiSystem == NULL && guiActive != child ) || retainedSubpageFrom == child;
	}
	idUserInterface *parent = guiSystemParent;
	const HandleGuiCommand_t handler = guiSystemParentHandle;
	if ( parent == NULL ) return false;
	if ( guiRetainedSettingsPage != NULL && parent == guiRetainedSettingsPage ) {
		// One level up to the SETTINGS page, onto DISPLAY: the page leaves
		// and the SETTINGS page takes over (PresentRetainedSubpage).
		idUserInterface *child = guiSystem;
		BeginRetainedSubpage( child, false, RETAINED_SUBPAGE_SETTINGS_SYSTEM );
		return guiSystem == NULL || retainedSubpageFrom == child;
	}
	SetGUI( parent, handler );
	SetPlayingSoundWorld();
	return guiActive == parent && guiSystem == NULL;
#endif
}

void idSessionLocal::CloseSystemSettings() {
	if ( guiSystem == NULL ) return;
	idUserInterface *child = guiSystem;
	// Clear every owning/reference slot before callbacks can pump another
	// lifecycle action. Manager dispatch permits this targeted drain before free.
	guiSystem = guiSystemParent = NULL;
	guiSystemParentHandle = NULL;
	if ( guiActive == child ) { openq4::NativeInputBeforeSessionChange(); guiActive = NULL; }
	if ( guiMsgRestore == child ) guiMsgRestore = NULL;
	const bool previousTransition = systemGuiTransition;
	systemGuiTransition = true;
	child->Activate( false, common->GetPresentationTime() );
	PumpApplicationActions( child );
	uiManager->DeAlloc( child );
	systemGuiTransition = previousTransition;
}

void idSessionLocal::ReportSystemSettings() {
	bool canReturn = false;
#ifndef ID_DEDICATED
	canReturn = guiSystem != NULL && UI_RetainedSettingsCanReturn( guiSystem );
#endif
	common->Printf( "OPENQ4_SYSTEM enabled=%d active=%s parent=%s child=%d guiTest=%d menu=%d map=%d multiplayer=%d menuSound=%d canReturn=%d\n",
		Session_RetainedSystemEnabled() ? 1 : 0, guiActive ? guiActive->Name() : "-",
		guiSystemParent ? guiSystemParent->Name() : "-", guiSystem != NULL && guiActive == guiSystem ? 1 : 0,
		guiTest != NULL ? 1 : 0, guiActive != NULL ? 1 : 0, mapSpawned ? 1 : 0, IsMultiplayer() ? 1 : 0,
		menuSoundWorld != NULL && requestedSoundWorld == menuSoundWorld ? 1 : 0, canReturn ? 1 : 0 );
}

/*
===============
idListSaveGameCompare
===============
*/
ID_INLINE int idListSaveGameCompare( const fileTIME_T *a, const fileTIME_T *b ) {
	// Newest first; saves written in the same second keep their list order
	// (alphabetical, see GetSaveGameList), so a refresh never swaps them.
	if ( a->timeStamp != b->timeStamp ) {
		return b->timeStamp > a->timeStamp ? 1 : -1;
	}
	return a->index - b->index;
}

static const int SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES = 8192;
static const int SESSION_MENU_MAX_SAVEGAME_DICT_KV = 16384;
static const int SESSION_MENU_MAX_SAVEGAME_BASENAME = 96;
static const float SESSION_MENU_SAVE_PREVIEW_X = 25.0f;
static const float SESSION_MENU_SAVE_PREVIEW_Y = 78.0f;
static const float SESSION_MENU_SAVE_PREVIEW_MAX_WIDTH = 183.0f;
static const float SESSION_MENU_SAVE_PREVIEW_MAX_HEIGHT = 137.0f;
static const float SESSION_MENU_GUI_ASPECT = 640.0f / 480.0f;

typedef struct sessionMenuSaveDescription_s {
	idStr	saveName;
	idStr	description;
	idStr	screenshot;
	bool	noOverwrite;
} sessionMenuSaveDescription_t;

typedef struct sessionMenuSaveDetails_s {
	int		skill;			// g_skill, -1 unknown
	int		playSeconds;	// the run's play time, -1 unknown
} sessionMenuSaveDetails_t;

static idRectangle Session_MenuFitSaveGamePreviewRect( int imageWidth, int imageHeight ) {
	float imageAspect = SESSION_MENU_GUI_ASPECT;
	if ( imageWidth > 0 && imageHeight > 0 ) {
		imageAspect = static_cast<float>( imageWidth ) / static_cast<float>( imageHeight );
	}

	// With aspect correction enabled, authored GUI units have a uniform physical
	// scale.  In retail stretch mode, compensate for the viewport's non-uniform
	// 640x480 mapping so the final on-screen rectangle still matches the image.
	float logicalAspect = imageAspect;
	if ( !cvarSystem->GetCVarBool( "ui_aspectCorrection" ) ) {
		float viewportWidth = static_cast<float>( engineWindowState.uiViewportWidth );
		float viewportHeight = static_cast<float>( engineWindowState.uiViewportHeight );
		if ( viewportWidth <= 0.0f || viewportHeight <= 0.0f ) {
			viewportWidth = static_cast<float>( engineWindowState.vidWidth );
			viewportHeight = static_cast<float>( engineWindowState.vidHeight );
		}
		if ( viewportWidth > 0.0f && viewportHeight > 0.0f ) {
			const float viewportStretch = ( viewportWidth / viewportHeight ) / SESSION_MENU_GUI_ASPECT;
			if ( viewportStretch > 0.0f ) {
				logicalAspect /= viewportStretch;
			}
		}
	}

	float width = SESSION_MENU_SAVE_PREVIEW_MAX_WIDTH;
	float height = SESSION_MENU_SAVE_PREVIEW_MAX_HEIGHT;
	const float availableAspect = width / height;
	if ( logicalAspect > availableAspect ) {
		height = width / logicalAspect;
	} else {
		width = height * logicalAspect;
	}

	return idRectangle(
		SESSION_MENU_SAVE_PREVIEW_X + ( SESSION_MENU_SAVE_PREVIEW_MAX_WIDTH - width ) * 0.5f,
		SESSION_MENU_SAVE_PREVIEW_Y + ( SESSION_MENU_SAVE_PREVIEW_MAX_HEIGHT - height ) * 0.5f,
		width,
		height );
}

static void Session_MenuSetSaveGamePreviewRect( idUserInterface *gui, const idMaterial *material ) {
	if ( gui == NULL ) {
		return;
	}

	const int imageWidth = material != NULL ? material->GetImageWidth() : 0;
	const int imageHeight = material != NULL ? material->GetImageHeight() : 0;
	const idRectangle previewRect = Session_MenuFitSaveGamePreviewRect( imageWidth, imageHeight );
	gui->SetStateFloat( "loadgame_preview_x", previewRect.x );
	gui->SetStateFloat( "loadgame_preview_y", previewRect.y );
	gui->SetStateFloat( "loadgame_preview_w", previewRect.w );
	gui->SetStateFloat( "loadgame_preview_h", previewRect.h );
}

static bool Session_MenuIsSafeSaveSlotName( const idStr &slotName ) {
	if ( slotName.IsEmpty() || slotName.Length() > SESSION_MENU_MAX_SAVEGAME_BASENAME ) {
		return false;
	}

	idStr scrubbedName = slotName;
	sessLocal.ScrubSaveGameFileName( scrubbedName );
	return scrubbedName.Icmp( slotName ) == 0;
}

static bool Session_MenuSaveDescriptionMatchesSlot( const idStr &slotName, const idStr &descriptionSaveName ) {
	if ( descriptionSaveName.IsEmpty() ) {
		return false;
	}

	idStr scrubbedName = descriptionSaveName;
	sessLocal.ScrubSaveGameFileName( scrubbedName );
	return scrubbedName.Icmp( slotName ) == 0;
}

static bool Session_MenuIsSafeSaveMaterialPath( const idStr &path ) {
	if ( path.IsEmpty() ) {
		return true;
	}
	if ( path.Length() >= MAX_STRING_CHARS || path[0] == '/' || path[0] == '\\' ) {
		return false;
	}
	if ( idStr::FindText( path.c_str(), ".." ) >= 0 ) {
		return false;
	}

	for ( int i = 0; i < path.Length(); i++ ) {
		const unsigned char ch = static_cast<unsigned char>( path[i] );
		if ( ch <= ' ' || ch >= 128 || ch == '\\' || ch == ':' || ch == '"' || ch == '<' || ch == '>' || ch == '|' ) {
			return false;
		}
	}

	return true;
}

static bool Session_MenuReadSaveGameInt( idFile *file, int &value, const char *fieldName, const char *savePath ) {
	const int offset = file->Tell();
	const int bytesRead = file->ReadInt( value );
	if ( bytesRead != sizeof( value ) ) {
		common->Warning( "Savegame '%s' is truncated while reading %s at offset %d (read %d of %d)",
			savePath ? savePath : "<unknown>", fieldName ? fieldName : "integer", offset, bytesRead, static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static bool Session_MenuReadSaveGameString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath ) {
	int len = 0;
	const int lengthOffset = file->Tell();
	if ( !Session_MenuReadSaveGameInt( file, len, fieldName, savePath ) ) {
		string.Clear();
		return false;
	}

	const int remainingBytes = Max( 0, file->Length() - file->Tell() );
	if ( len < 0 || len > maxLength || len > remainingBytes ) {
		common->Warning( "Savegame '%s' has invalid %s length %d at offset %d (remaining %d, max %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "string",
			len,
			lengthOffset,
			remainingBytes,
			maxLength );
		string.Clear();
		return false;
	}

	string.Fill( ' ', len );
	if ( len > 0 ) {
		const int dataOffset = file->Tell();
		const int bytesRead = file->Read( &string[0], len );
		if ( bytesRead != len ) {
			common->Warning( "Savegame '%s' is truncated while reading %s data at offset %d (read %d of %d)",
				savePath ? savePath : "<unknown>", fieldName ? fieldName : "string", dataOffset, bytesRead, len );
			string.Clear();
			return false;
		}
	}

	return true;
}

static bool Session_MenuReadSaveGameCString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath ) {
	string.Clear();

	const int offset = file->Tell();
	for ( int len = 0; len < maxLength; len++ ) {
		char ch = '\0';
		const int bytesRead = file->Read( &ch, 1 );
		if ( bytesRead != 1 ) {
			common->Warning( "Savegame '%s' is truncated while reading %s at offset %d (read %d of 1)",
				savePath ? savePath : "<unknown>", fieldName ? fieldName : "string", file->Tell(), bytesRead );
			string.Clear();
			return false;
		}
		if ( ch == '\0' ) {
			return true;
		}
		string += ch;
	}

	common->Warning( "Savegame '%s' has unterminated %s at offset %d (max %d)",
		savePath ? savePath : "<unknown>", fieldName ? fieldName : "string", offset, maxLength );
	string.Clear();
	return false;
}

// A header dictionary, kept in `dict` when there is one.
static bool Session_MenuReadSaveGameDict( idFile *file, idDict *dict, const char *fieldName, const char *savePath ) {
	if ( dict != NULL ) {
		dict->Clear();
	}
	int count = 0;
	if ( !Session_MenuReadSaveGameInt( file, count, fieldName, savePath ) ) {
		return false;
	}
	if ( count < 0 || count > SESSION_MENU_MAX_SAVEGAME_DICT_KV ) {
		common->Warning( "Savegame '%s' has invalid %s key/value count %d (max %d)",
			savePath ? savePath : "<unknown>",
			fieldName ? fieldName : "dictionary",
			count,
			SESSION_MENU_MAX_SAVEGAME_DICT_KV );
		return false;
	}

	for ( int i = 0; i < count; i++ ) {
		idStr key;
		idStr value;
		if ( !Session_MenuReadSaveGameCString( file, key, MAX_STRING_CHARS, va( "%s key %d", fieldName ? fieldName : "dictionary", i ), savePath ) ||
			 !Session_MenuReadSaveGameCString( file, value, MAX_STRING_CHARS, va( "%s value %d", fieldName ? fieldName : "dictionary", i ), savePath ) ) {
			return false;
		}
		if ( dict != NULL ) {
			dict->Set( key, value );
		}
	}

	return true;
}

static bool Session_MenuSkipSaveGameDict( idFile *file, const char *fieldName, const char *savePath ) {
	return Session_MenuReadSaveGameDict( file, NULL, fieldName, savePath );
}

static bool Session_MenuIsSupportedSaveGameName( const idStr &gameName ) {
	return gameName.Icmp( SAVEGAME_GAME_NAME_RETAIL ) == 0 ||
		gameName.Icmp( SAVEGAME_GAME_NAME_LEGACY_OPENQ4 ) == 0;
}

static bool Session_MenuSaveGameHeaderUsesEntityFilter( const idStr &gameName ) {
	return gameName.Icmp( SAVEGAME_GAME_NAME_RETAIL ) == 0;
}

static bool Session_MenuIsCompatibleSaveGameVersion( const int version ) {
	return version == SAVEGAME_VERSION ||
		version == LEGACY_OPENQ4_SAVEGAME_VERSION ||
		version == LEGACY_OPENQ4_SAVEGAME_VERSION_ALT;
}

// A save's header: its game name, version, map, entity filter and each
// client's persistent player info. Whether this build loads the save; with
// `player`, the first client's persistent info.
static bool Session_MenuReadSaveGameHeader( const idStr &slotName, idDict *player ) {
	if ( !sessLocal.FinishPendingSave( true ) ) return false;
	if ( !Session_MenuIsSafeSaveSlotName( slotName ) ) {
		common->Warning( "Ignoring savegame slot with unsafe filename '%s'", slotName.c_str() );
		return false;
	}

	idStr savePath = va( "savegames/%s.save", slotName.c_str() );
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	idFile *file = fileSystem->OpenFileRead( savePath.c_str(), true, game.Length() ? game.c_str() : NULL );
	if ( file == NULL ) {
		return false;
	}

	idStr gamename;
	idStr saveMap;
	idStr entityFilter;
	int version = 0;
	bool valid = true;
	valid = valid && Session_MenuReadSaveGameString( file, gamename, MAX_STRING_CHARS, "game name", savePath.c_str() );
	valid = valid && Session_MenuIsSupportedSaveGameName( gamename );
	valid = valid && Session_MenuReadSaveGameInt( file, version, "version", savePath.c_str() );
	valid = valid && Session_MenuIsCompatibleSaveGameVersion( version );
	valid = valid && Session_MenuReadSaveGameString( file, saveMap, MAX_STRING_CHARS, "map name", savePath.c_str() );
	if ( valid && Session_MenuSaveGameHeaderUsesEntityFilter( gamename ) ) {
		valid = Session_MenuReadSaveGameString( file, entityFilter, MAX_STRING_CHARS, "entity filter", savePath.c_str() );
	}
	for ( int i = 0; valid && i < MAX_ASYNC_CLIENTS; i++ ) {
		valid = i == 0 && player != NULL ?
			Session_MenuReadSaveGameDict( file, player, va( "persistent player info %d", i ), savePath.c_str() ) :
			Session_MenuSkipSaveGameDict( file, va( "persistent player info %d", i ), savePath.c_str() );
	}
	if ( valid && saveMap.IsEmpty() ) {
		common->Warning( "Savegame '%s' has an empty map name", savePath.c_str() );
		valid = false;
	}

	fileSystem->CloseFile( file );
	return valid;
}

static bool Session_MenuIsLoadableSaveGameSlot( const idStr &slotName ) {
	return Session_MenuReadSaveGameHeader( slotName, NULL );
}

#ifndef ID_DEDICATED
// What the menus show of a save beyond its description: the difficulty and
// the run's play time its player carried (idPlayer::SavePersistantInfo),
// each -1 for a save from an earlier build, which records neither.
static bool Session_MenuReadSaveDetails( const idStr &slotName, sessionMenuSaveDetails_t &details ) {
	details.skill = -1;
	details.playSeconds = -1;
	idDict player;
	if ( !Session_MenuReadSaveGameHeader( slotName, &player ) ) {
		return false;
	}
	const idKeyValue *skill = player.FindKey( "openq4_skill" );
	const idKeyValue *played = player.FindKey( "openq4_playSeconds" );
	details.skill = skill != NULL ? idMath::ClampInt( 0, 4, atoi( skill->GetValue() ) ) : -1;
	details.playSeconds = played != NULL ? Max( 0, atoi( played->GetValue() ) ) : -1;
	return true;
}
#endif

static bool Session_MenuReadSaveDescription( const idStr &slotName, sessionMenuSaveDescription_t &description ) {
	description.saveName.Clear();
	description.description.Clear();
	description.screenshot.Clear();
	description.noOverwrite = false;
	if ( !sessLocal.FinishPendingSave( true ) ) return false;

	if ( !Session_MenuIsSafeSaveSlotName( slotName ) ) {
		common->Warning( "Ignoring save description for unsafe slot '%s'", slotName.c_str() );
		return false;
	}

	idStr descriptionPath = va( "savegames/%s.txt", slotName.c_str() );
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	idFile *file = fileSystem->OpenFileRead( descriptionPath.c_str(), true, game.Length() ? game.c_str() : NULL );
	if ( file == NULL ) {
		return false;
	}

	const int len = file->Length();
	if ( len < 0 || len > SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES ) {
		common->Warning( "Save description '%s' has invalid size %d (max %d)",
			descriptionPath.c_str(), len, SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES );
		fileSystem->CloseFile( file );
		return false;
	}

	char *buffer = new char[ len + 1 ];
	const int bytesRead = len > 0 ? file->Read( buffer, len ) : 0;
	fileSystem->CloseFile( file );
	if ( bytesRead != len ) {
		common->Warning( "Save description '%s' is truncated (read %d of %d)",
			descriptionPath.c_str(), bytesRead, len );
		delete[] buffer;
		return false;
	}
	buffer[ len ] = '\0';

	bool valid = true;
	{
		idLexer src( buffer, len, descriptionPath.c_str(), LEXFL_NOERRORS | LEXFL_NOSTRINGCONCAT );
		idToken tok;
		if ( src.ReadToken( &tok ) ) {
			description.saveName = tok;
		} else {
			valid = false;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			description.description = tok;
		} else {
			valid = false;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			description.screenshot = tok;
		}
		if ( valid && src.ReadToken( &tok ) ) {
			description.noOverwrite = !idStr::Icmp( tok.c_str(), "nooverwrite" );
		}
	}

	delete[] buffer;
	if ( valid && !Session_MenuSaveDescriptionMatchesSlot( slotName, description.saveName ) ) {
		common->Warning( "Save description '%s' names slot '%s', expected '%s'",
			descriptionPath.c_str(), description.saveName.c_str(), slotName.c_str() );
		valid = false;
	}
	if ( valid && ( description.saveName.Length() >= MAX_STRING_CHARS || description.description.Length() >= MAX_STRING_CHARS ) ) {
		common->Warning( "Save description '%s' has an oversized display token", descriptionPath.c_str() );
		valid = false;
	}
	if ( valid && !Session_MenuIsSafeSaveMaterialPath( description.screenshot ) ) {
		common->Warning( "Save description '%s' has unsafe screenshot material '%s'",
			descriptionPath.c_str(), description.screenshot.c_str() );
		valid = false;
	}
	return valid;
}

// Whether a save already holds `saveName`'s slot, and whether that save is an
// autosave, which the player cannot save over: it names its own picture or
// carries the no-overwrite mark.
enum sessionMenuSaveSlot_t { SESSION_SAVE_SLOT_FREE, SESSION_SAVE_SLOT_TAKEN, SESSION_SAVE_SLOT_AUTOSAVE };
static sessionMenuSaveSlot_t Session_MenuSaveSlotState( const char *saveName ) {
	idStr saveSlotName = saveName;
	sessLocal.ScrubSaveGameFileName( saveSlotName );
	idStr saveFileName = "savegames/" + saveSlotName;
	saveFileName.SetFileExtension( ".save" );
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	idFile *file = game.Length() ? fileSystem->OpenFileRead( saveFileName, true, game ) : fileSystem->OpenFileRead( saveFileName );
	if ( file == NULL ) {
		return SESSION_SAVE_SLOT_FREE;
	}
	fileSystem->CloseFile( file );
	sessionMenuSaveDescription_t description;
	if ( Session_MenuReadSaveDescription( saveSlotName, description ) &&
			( description.screenshot.Length() > 0 || description.noOverwrite ) ) {
		return SESSION_SAVE_SLOT_AUTOSAVE;
	}
	return SESSION_SAVE_SLOT_TAKEN;
}

/*
===============
idSessionLocal::GetSaveGameList
===============
*/
void idSessionLocal::GetSaveGameList( idStrList &fileList, idList<fileTIME_T> &fileTimes ) {
	if ( !FinishPendingSave( true ) ) { fileList.Clear(); fileTimes.Clear(); return; }
	int i;
	idFileList *files;
	idStrList rawFileList;

	// NOTE: no fs_game_base for savegames
	idStr game = cvarSystem->GetCVarString( "fs_game" );
	if( game.Length() ) {
		files = fileSystem->ListFiles( "savegames", ".save", false, false, game );
	} else {
		files = fileSystem->ListFiles( "savegames", ".save" );
	}
	
	rawFileList = files->GetList();
	fileSystem->FreeFileList( files );
	// Alphabetical, so saves written in the same second always list in one
	// order (idListSaveGameCompare keeps it).
	rawFileList.Sort();

	fileList.Clear();
	fileTimes.Clear();

	for ( i = 0; i < rawFileList.Num(); i++ ) {
		ID_TIME_T timeStamp;
		idStr slotName = rawFileList[i];
		slotName.StripLeading( '/' );
		slotName.StripFileExtension();
		if ( slotName.IsEmpty() || !Session_MenuIsLoadableSaveGameSlot( slotName ) ) {
			continue;
		}

		fileSystem->ReadFile( va( "savegames/%s.save", slotName.c_str() ), NULL, &timeStamp );

		fileTIME_T ft;
		ft.index = fileList.Num();
		ft.timeStamp = timeStamp;
		fileList.Append( slotName );
		fileTimes.Append( ft );
	}

	fileTimes.Sort( idListSaveGameCompare );
}

/*
===============
idSessionLocal::SetSaveGameGuiVars
===============
*/
void idSessionLocal::SetSaveGameGuiVars( void ) {
	int i;
	idStr name;
	idStrList fileList;
	idList<fileTIME_T> fileTimes;

	loadGameList.Clear();
	fileList.Clear();
	fileTimes.Clear();

	if ( !guiActive ) {
		return;
	}

	guiActive->SetStateString( "saveGameName", "" );
	guiActive->SetStateString( "saveGameDescription", "" );
	guiActive->SetStateString( "saveGameDate", "" );
	guiActive->SetStateString( "saveGameTime", "" );

	GetSaveGameList( fileList, fileTimes );

	loadGameList.SetNum( fileList.Num() );
	for ( i = 0; i < fileList.Num(); i++ ) {
		loadGameList[i] = fileList[fileTimes[i].index];

		sessionMenuSaveDescription_t description;
		if ( Session_MenuReadSaveDescription( loadGameList[i], description ) && description.description.Length() > 0 ) {
			name = description.description;
		} else {
			name = loadGameList[i];
		}

		guiActive->SetStateString( va("loadgame_item_%i", i), name);
	}
	guiActive->DeleteStateVar( va("loadgame_item_%i", fileList.Num()) );
	guiActive->SetStateInt( "loadgame_listdef_count", fileList.Num() );

	guiActive->SetStateString( "loadgame_sel_0", "-1" );
	guiActive->SetStateString( "loadgame_shot", "gfx/guis/loadscreens/generic" );
	const idMaterial *defaultPreview = declManager->FindMaterial( "gfx/guis/loadscreens/generic" );
	if ( defaultPreview != NULL ) {
		defaultPreview->SetSort( SS_GUI );
	}
	Session_MenuSetSaveGamePreviewRect( guiActive, defaultPreview );

}

/*
===============
idSessionLocal::SetModsMenuGuiVars
===============
*/
void idSessionLocal::SetModsMenuGuiVars( void ) {
	if ( !guiActive ) {
		return;
	}

	idModList *list = fileSystem->ListMods();
	const idStr currentGameDir = cvarSystem->GetCVarString( "fs_game" );
	int selectedIndex = -1;

	modsList.SetNum( list->GetNumMods() );

	// Build the gui list
	for ( int i = 0; i < list->GetNumMods(); i++ ) {
		modsList[ i ] = list->GetInfo( i );
		guiActive->SetStateString( va("modsList_item_%i", i), list->GetDescription( i ) );
		if ( !currentGameDir.Icmp( modsList[ i ].directory ) ) {
			selectedIndex = i;
		}
	}
	guiActive->DeleteStateVar( va("modsList_item_%i", list->GetNumMods()) );
	if ( selectedIndex < 0 && modsList.Num() > 0 ) {
		selectedIndex = 0;
	}
	guiActive->SetStateInt( "modsList_sel_0", selectedIndex );
	UpdateModsMenuGuiVars();

	fileSystem->FreeModList( list );
}

/*
===============
idSessionLocal::UpdateModsMenuGuiVars
===============
*/
void idSessionLocal::UpdateModsMenuGuiVars( void ) {
	if ( !guiActive ) {
		return;
	}

	const int choice = guiActive->State().GetInt( "modsList_sel_0" );
	if ( choice < 0 || choice >= modsList.Num() ) {
		guiActive->SetStateString( "mod_name", "" );
		guiActive->SetStateString( "mod_version", "" );
		guiActive->SetStateString( "mod_release_date", "" );
		guiActive->SetStateString( "mod_author", "" );
		guiActive->SetStateString( "mod_website", "" );
		guiActive->SetStateString( "mod_required_engine_version", "" );
		guiActive->SetStateString( "mod_directory", "" );
		return;
	}

	const idModInfo &modInfo = modsList[ choice ];
	guiActive->SetStateString( "mod_name", modInfo.displayName.c_str() );
	guiActive->SetStateString( "mod_version", modInfo.version.c_str() );
	guiActive->SetStateString( "mod_release_date", modInfo.releaseDate.c_str() );
	guiActive->SetStateString( "mod_author", modInfo.author.c_str() );
	guiActive->SetStateString( "mod_website", modInfo.website.c_str() );
	guiActive->SetStateString( "mod_required_engine_version", modInfo.requiredopenQ4Version.c_str() );
	guiActive->SetStateString( "mod_directory", modInfo.directory.c_str() );
}


/*
===============
idSessionLocal::SetMainMenuSkin
===============
*/
void idSessionLocal::SetMainMenuSkin( void ) {
	// skins
	idStr str = cvarSystem->GetCVarString( "mod_validSkins" );
	idStr uiSkin = cvarSystem->GetCVarString( "ui_skin" );
	idStr skin;
	int skinId = 1;
	int count = 1;
	while ( str.Length() ) {
		int n = str.Find( ";" );
		if ( n >= 0 ) {
			skin = str.Left( n );
			str = str.Right( str.Length() - n - 1 );
		} else {
			skin = str;
			str = "";
		}
		if ( skin.Icmp( uiSkin ) == 0 ) {
			skinId = count;
		}
		count++;
	}

	for ( int i = 0; i < count; i++ ) {
		guiMainMenu->SetStateInt( va( "skin%i", i+1 ), 0 );
	}
	guiMainMenu->SetStateInt( va( "skin%i", skinId ), 1 );
}

/*
===============
idSessionLocal::SetMainMenuGuiVars
===============
*/
void idSessionLocal::SetMainMenuGuiVars( bool refreshCatalogs ) {
	guiMainMenu->SetStateBool( "retainedSystem", RetainedSystemAvailable() );
	guiMainMenu->SetStateBool( "retainedAudio", RetainedAudioAvailable() );
	guiMainMenu->SetStateBool( "retainedGame", RetainedGameAvailable() );
	guiMainMenu->SetStateBool( "retainedControls", RetainedControlsAvailable() );
	guiMainMenu->SetStateBool( "androidSetupAvailable", Session_AndroidSetupAvailable() );

	guiMainMenu->SetStateString( "serverlist_sel_0", "-1" );
	guiMainMenu->SetStateString( "serverlist_selid_0", "-1" ); 

	guiMainMenu->SetStateInt( "com_machineSpec", com_machineSpec.GetInteger() );

	// "inetGame" will hold a hand-typed inet address, which is not archived to a cvar
	guiMainMenu->SetStateString( "inetGame", "" );

	if ( refreshCatalogs ) {
		// These operations may consult drivers, the filesystem, or large decl
		// catalogs. Keep them off the synchronous in-game activation path.
		guiMainMenu->SetKeyBindingNames();
		PopulateMainMenuAudioDeviceChoices( guiMainMenu );
		PopulateMainMenuDisplayChoices( guiMainMenu );
	}

	// flag for in-game menu
	const char *inGameState = mapSpawned ? ( IsMultiplayer() ? "2" : "1" ) : "0";
	guiMainMenu->SetStateString( "inGame", inGameState );
	guiMainMenu->SetStateString( "ingame", inGameState );
	// the in-game Difficulty page opens on the difficulty being played
	guiMainMenu->SetStateInt( "currentSkill", idMath::ClampInt( 0, 4, cvarSystem->GetCVarInteger( "g_skill" ) ) );

#ifdef ID_DEMO_BUILD
	guiMainMenu->SetStateString( "nightmare", "0" );
#else
	guiMainMenu->SetStateString( "nightmare", cvarSystem->GetCVarBool( "g_nightmare" ) ? "1" : "0" );
#endif
	guiMainMenu->SetStateString( "browser_levelshot", "gfx/guis/loadscreens/generic" );
	SetMainMenuQualityGuiVars( guiMainMenu );
	SyncMainMenuAspectVisibility( guiMainMenu );
	guiMainMenu->SetStateInt( "gui_set_sys_scroll", 0 );
	guiMainMenu->SetStateInt( "gui_set_audio_scroll", 0 );
	guiMainMenu->SetStateInt( "gui_set_game_scroll", 0 );
	gui_set_sys_scroll.SetInteger( 0 );
	gui_set_audio_scroll.SetInteger( 0 );
	gui_set_game_scroll.SetInteger( 0 );

	SetMainMenuSkin();
	if ( refreshCatalogs ) {
		SetModsMenuGuiVars();
	}

	guiMsg->SetStateString( "visible_hasxp", fileSystem->HasD3XP() ? "1" : "0" );

#if defined( __linux__ )
	guiMainMenu->SetStateString( "driver_prompt", "1" );
#else
	guiMainMenu->SetStateString( "driver_prompt", "0" );
#endif

	if ( refreshCatalogs ) {
		SetMainMenuMPModelVars( guiMainMenu );
	}
	// the Game Options preview shows the current custom crosshair
	MainMenuPublishCrosshair( guiMainMenu, 0 );
	arenaCampaign.UpdateMainMenuGui( guiMainMenu );
}

/*
==============
idSessionLocal::HandleSaveGameMenuCommands
==============
*/
bool idSessionLocal::HandleSaveGameMenuCommand( idCmdArgs &args, int &icmd ) {

	const char *cmd = args.Argv(icmd-1);

	if ( !idStr::Icmp( cmd, "refreshSaveGameList" ) ) {
		SetSaveGameGuiVars();
		if ( guiActive != NULL ) {
			guiActive->StateChanged( common->GetPresentationTime() );
		}
		return true;
	}

	if ( !idStr::Icmp( cmd, "loadGame" ) ) {
		int choice = guiActive->State().GetInt("loadgame_sel_0");
		if ( choice >= 0 && choice < loadGameList.Num() ) {
			sessLocal.LoadGame( loadGameList[choice] );
		}
		return true;
	}

	if ( !idStr::Icmp( cmd, "saveGame" ) ) {
		const char *saveGameName = guiActive->State().GetString("saveGameName");
		if ( saveGameName && saveGameName[0] ) {

			// First see if the file already exists unless they pass '1' to authorize the overwrite
			if ( icmd == args.Argc() || atoi(args.Argv( icmd++ )) == 0 ) {
				const sessionMenuSaveSlot_t slot = Session_MenuSaveSlotState( saveGameName );
				if ( slot == SESSION_SAVE_SLOT_AUTOSAVE ) {
					// NOTE: base/ gui doesn't handle that one
					guiActive->HandleNamedEvent( "autosaveOverwriteError" );
					return true;
				}
				if ( slot == SESSION_SAVE_SLOT_TAKEN ) {
					guiActive->HandleNamedEvent( "saveGameOverwrite" );
					return true;
				}
			}

			sessLocal.SaveGame( saveGameName, ST_REGULAR );
			SetSaveGameGuiVars( );
			guiActive->StateChanged( common->GetPresentationTime() );
		}
		return true;
	}

	if ( !idStr::Icmp( cmd, "deleteGame" ) ) {
		int choice = guiActive->State().GetInt( "loadgame_sel_0" );
		if ( choice >= 0 && choice < loadGameList.Num() ) {
			DeleteGame( loadGameList[choice] );
			SetSaveGameGuiVars( );
			guiActive->StateChanged( common->GetPresentationTime() );
		}
		return true;
	}

	if ( !idStr::Icmp( cmd, "updateSaveGameInfo" ) ) {
		int choice = guiActive->State().GetInt( "loadgame_sel_0" );
		if ( choice >= 0 && choice < loadGameList.Num() ) {
			const idMaterial *material;

			idStr saveName, description, screenshot;
			sessionMenuSaveDescription_t saveDescription;
			if ( Session_MenuReadSaveDescription( loadGameList[choice], saveDescription ) ) {
				saveName = saveDescription.saveName;
				description = saveDescription.description;
				screenshot = saveDescription.screenshot;
			} else {
				saveName = loadGameList[choice];
				description = loadGameList[choice];
				screenshot = "";
			}
			if ( screenshot.Length() == 0 ) {
				screenshot = va("savegames/%s.tga", loadGameList[choice].c_str());
			}
			material = declManager->FindMaterial( screenshot );
			if ( material ) {
				material->ReloadImages( false );
				material->SetSort( SS_GUI );
			}
			guiActive->SetStateString( "loadgame_shot",  screenshot );
			Session_MenuSetSaveGamePreviewRect( guiActive, material );

			saveName.RemoveColors();
			guiActive->SetStateString( "saveGameName", saveName );
			guiActive->SetStateString( "saveGameDescription", description );

			ID_TIME_T timeStamp;
			fileSystem->ReadFile( va("savegames/%s.save", loadGameList[choice].c_str()), NULL, &timeStamp );
			idStr date = Sys_TimeStampToStr(timeStamp);
			int tab = date.Find( '\t' );
			idStr time = date.Right( date.Length() - tab - 1);
			guiActive->SetStateString( "saveGameDate", date.Left( tab ) );
			guiActive->SetStateString( "saveGameTime", time );
		}
		return true;
	}

	return false;
}

/*
==============
idSessionLocal::HandleRestartMenuCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleRestartMenuCommands( const char *menuCommand ) {
	// execute the command from the menu
	int icmd;
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	for( icmd = 0; icmd < args.Argc(); ) {
		const char *cmd = args.Argv( icmd++ );

		if ( HandleSaveGameMenuCommand( args, icmd ) ) {
			continue;
		}

		if ( !idStr::Icmp( cmd, "restart" ) ) {
			idStr loadName;
			if ( lastCheckPoint != -1 ) {
				loadName = va( "Checkpoint%d", lastCheckPoint );
			} else {
				loadName = GetAutoSaveName( mapSpawnData.serverInfo.GetString("si_map") );
			}

			if ( !LoadGame( loadName ) ) {
				// If we can't load the retail restart slot then just restart the map
				MoveToNewMap( mapSpawnData.serverInfo.GetString("si_map") );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "disconnect" ) ) {
			const char *target = args.Argc() - icmd >= 1 ? args.Argv( icmd ) : "";
			if ( target[ 0 ] != '\0' && idStr::Icmp( target, "mainmenu" ) != 0 ) {
				continue;
			}

			if ( !idStr::Icmp( target, "mainmenu" ) ) {
				icmd++;
			}

			Stop();
			StartMenu();

			if ( guiMainMenu && args.Argc() - icmd >= 1 ) {
				idStr mainMenuEvent = args.Argv( icmd++ );
				if ( mainMenuEvent.Length() > 0 ) {
					guiMainMenu->HandleNamedEvent( mainMenuEvent.c_str() );
				}
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "quit" ) ) {
			ExitMenu();
			common->Quit();
			return;
		}

		if ( !idStr::Icmp ( cmd, "exec" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, args.Argv( icmd++ ) );
			continue;
		}

		if ( !idStr::Icmp( cmd, "play" ) ) {
			if ( args.Argc() - icmd >= 1 ) {
				idStr snd = args.Argv(icmd++);
				sw->PlayShaderDirectly(snd);
			}
			continue;
		}
	}
}

/*
==============
idSessionLocal::HandleIntroMenuCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleIntroMenuCommands( const char *menuCommand ) {
	// execute the command from the menu
	int i;
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	for( i = 0; i < args.Argc(); ) {
		const char *cmd = args.Argv( i++ );

		if ( !idStr::Icmp( cmd, "startGame" ) ) {
			menuSoundWorld->ClearAllSoundEmitters();
			ExitMenu();
			continue;
		}

		if ( !idStr::Icmp( cmd, "play" ) ) {
			if ( args.Argc() - i >= 1 ) {
				idStr snd = args.Argv(i++);
				menuSoundWorld->PlayShaderDirectly(snd);
			}
			continue;
		}
	}
}

/*
==============
idSessionLocal::UpdateMPLevelShot
==============
*/
void idSessionLocal::UpdateMPLevelShot( void ) {
	char screenshot[ MAX_STRING_CHARS ];
	fileSystem->FindMapScreenshot( cvarSystem->GetCVarString( "si_map" ), screenshot, MAX_STRING_CHARS );
	guiMainMenu->SetStateString( "current_levelshot", screenshot );
	declManager->FindMaterial( screenshot )->SetSort( SS_GUI );
}

static int MainMenuGetNewGameOption( idUserInterface *gui, const char *desktopStateName, const char *stateName, int defaultValue ) {
	if ( gui == NULL ) {
		return defaultValue;
	}

	idStr presentation;
	if ( ( desktopStateName != NULL && gui->GetPresentationValue( desktopStateName, presentation ) ) ||
		 ( stateName != NULL && gui->GetPresentationValue( stateName, presentation ) ) ) {
		return atoi( presentation.c_str() );
	}

	int value = defaultValue;
	if ( desktopStateName != NULL && gui->State().GetInt( desktopStateName, va( "%d", defaultValue ), value ) ) {
		return value;
	}
	if ( stateName != NULL && gui->State().GetInt( stateName, va( "%d", defaultValue ), value ) ) {
		return value;
	}

	return defaultValue;
}

static void MainMenuApplyNewGameOptions( idUserInterface *gui ) {
	const int skill = idMath::ClampInt( 0, 4, MainMenuGetNewGameOption( gui, "desktop::skill", "skill", cvarSystem->GetCVarInteger( "g_skill" ) ) );
	const int turboMode = MainMenuGetNewGameOption( gui, "desktop::turboMode", "turboMode", 0 ) != 0 ? 1 : 0;

	cvarSystem->SetCVarInteger( "g_skill", skill );
	cvarSystem->SetCVarInteger( "g_turboMode", turboMode );
	common->DPrintf( "Main menu new game options: g_skill=%d g_turboMode=%d\n", skill, turboMode );
}

/*
==============
Session_BanListRow

A ban list row as the game publishes it on a menu (idGameLocal::PopulateBanList):
"N:  name\tguid". The name follows the row number's ":  "; the GUID is the
text after the tab, as retail's REMOVE BAN read it. False without a GUID.
==============
*/
static bool Session_BanListRow( const char *row, idStr &name, idStr &guid ) {
	const char *tab = row != NULL ? strchr( row, '\t' ) : NULL;
	if ( tab == NULL || tab[1] == '\0' ) {
		return false;
	}
	const char *colon = strstr( row, ":  " );
	const char *start = colon != NULL && colon < tab ? colon + 3 : row;
	name.Clear();
	name.Append( start, static_cast<int>( tab - start ) );
	guid = tab + 1;
	return true;
}

// The stock browser's GET NEW LIST (UpdateServers) or REFRESH LIST
// (RefreshServers) on a source: a new list asks the LAN, or the master
// server and the favorites, and ends a player search, as retail's search was
// a list of its own; a refresh asks the LAN again, or the listed servers.
static void Session_ScanServers( bool lan, bool newList ) {
	idServerScan &servers = idAsyncNetwork::client.serverList;
	if ( newList ) {
		servers.SetPlayerSearch( "" );
		servers.GUIInit();
	}
	if ( lan ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "LANScan" );
	} else if ( newList ) {
		idAsyncNetwork::GetNETServers();
		servers.AddFavoriteServers();
	} else {
		servers.NetScan();
	}
}

/*
==============
Session_ConnectToTypedAddress

JOIN IP ADDRESS: connects to an address the player typed, checked before it
reaches the command system (a hostname or IPv6 literal never becomes console
text) and resolved, the default port added. False, with the message saying
why (#str_06734), when it is not an address it can join.
==============
*/
static bool Session_ConnectToTypedAddress( const char *text, idStr &error ) {
	netadr_t address;
	bool valid = text != NULL && text[0] != '\0' && strlen( text ) < 256;
	for ( const unsigned char *p = reinterpret_cast<const unsigned char *>( text ); valid && *p; ++p ) {
		valid = *p > 32 && *p != 127 && *p != '"' && *p != ';' && *p != '\\';
	}
	if ( !valid || !Sys_StringToNetAdr( text, &address, true ) ||
		( address.type != NA_IP && address.type != NA_IP6 && address.type != NA_LOOPBACK ) ) {
		error = va( common->GetLanguageDict()->GetString( "#str_06734" ), text != NULL ? text : "" );
		return false;
	}
	if ( !address.port ) {
		address.port = PORT_SERVER;
	}
	idCmdArgs connectArgs;
	connectArgs.AppendArg( "connect" );
	connectArgs.AppendArg( Sys_NetAdrToString( address ) );
	cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, connectArgs );
	return true;
}

/*
==============
Session_LocalAddressText

This machine's address as JOIN IP ADDRESS shows the player ("your ip
address:"), as retail's getClientIP published it: its first non-loopback
IPv4 interface's, without a port; empty when it has none.
==============
*/
static idStr Session_LocalAddressText( void ) {
	netadr_t address;
	if ( !Sys_GetLocalIPAddress( address ) ) {
		return "";
	}
	return va( "%i.%i.%i.%i", address.ip[0], address.ip[1], address.ip[2], address.ip[3] );
}

/*
==============
idSessionLocal::HandleMainMenuCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleMainMenuCommands( const char *menuCommand ) {
	// execute the command from the menu
	int icmd;
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	for( icmd = 0; icmd < args.Argc(); ) {
		const char *cmd = args.Argv( icmd++ );

		if ( HandleSaveGameMenuCommand( args, icmd ) ) {
			continue;
		}

		if ( !idStr::Cmp( cmd, ";" ) ) {
			continue;
		}

		if ( !idStr::Icmp( cmd, "openAndroidSetup" ) ) {
			Session_OpenAndroidSetup();
			return;
		}

		if ( !idStr::Icmp( cmd, "openRetainedSystem" ) ) {
			OpenSystemSettingsRoute( false );
			return;
		}

		if ( !idStr::Icmp( cmd, "openRetainedSystemFromMp" ) ) {
			OpenSystemSettingsRoute( true );
			return;
		}

		if ( !idStr::Icmp( cmd, "openRetainedAudio" ) ) {
			OpenRetainedAudioRoute();
			return;
		}

		if ( !idStr::Icmp( cmd, "openRetainedGame" ) ) {
			OpenRetainedGameRoute( false );
			return;
		}

		if ( !idStr::Icmp( cmd, "openRetainedGameFromMp" ) ) {
			OpenRetainedGameRoute( true );
			return;
		}

		if ( !idStr::Icmp( cmd, "openRetainedControls" ) ) {
			OpenRetainedControlsRoute( false );
			return;
		}

		if ( !idStr::Icmp( cmd, "openRetainedControlsFromMp" ) ) {
			OpenRetainedControlsRoute( true );
			return;
		}

		if ( !idStr::Icmp( cmd, "demoOpen" ) ) {
			OpenDemoMenu( true );
			return;
		}

		if ( !idStr::Icmp( cmd, "singlePlayerOpen" ) ) {
			OpenCampaignSelector( false );
			return;
		}

		// Back from a stock Multiplayer page: the retained Multiplayer page
		// where it presents, onto the section that opened that page, else the
		// stock one.
		// Back from the stock settings screen the SETTINGS page opened: that
		// page, onto the section, where it presents, else the home.
		if ( !idStr::Icmp( cmd, "settingsOpen" ) ) {
#ifndef ID_DEDICATED
			const int left = retainedSettingsLeft;
			retainedSettingsLeft = 0;
			const char *section = NULL;
			for ( int i = 0; i < RETAINED_SETTINGS_SECTION_COUNT; i++ ) {
				if ( RETAINED_SETTINGS_SECTIONS[ i ].dest == left ) {
					section = RETAINED_SETTINGS_SECTIONS[ i ].section;
				}
			}
			if ( !OpenRetainedSettingsPage( section ) && guiMainMenu != NULL ) {
				guiMainMenu->SetStateBool( "settingsFromRetained", false );
				guiMainMenu->HandleNamedEvent( "openSettingsHome" );
			}
#endif
			return;
		}

		if ( !idStr::Icmp( cmd, "multiplayerOpen" ) ) {
			const int left = retainedMpPageLeft;
			retainedMpPageLeft = 0;
			const char *section = left == 14 ? "join" : left == 17 ? "create" : left == 18 ? "setup" : NULL;
			if ( !OpenRetainedMultiplayerPage( section ) && guiMainMenu != NULL ) {
				guiMainMenu->HandleNamedEvent( "openMultiplayerStock" );
			}
			return;
		}

		if ( !idStr::Icmp( cmd, "reloadLanguage" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reloadLanguage\n" );
			continue;
		}

		if ( !idStr::Icmp( cmd, "reloadGuis" ) ) {
			// Language changes do not change GUI file timestamps. Reparse all
			// cached labels after this GUI command has finished using its windows.
			cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "reloadGuis all\n" );
			continue;
		}

		if ( !idStr::Icmp( cmd, "CVarStrcmp" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			if ( args.Argc() - icmd >= 2 && gui != NULL ) {
				const char *cvarName = args.Argv( icmd++ );
				const char *stateName = args.Argv( icmd++ );
				const char *value = cvarSystem->GetCVarString( cvarName );
				int matchIndex = -1;
				int optionIndex = 0;
				while ( icmd < args.Argc() ) {
					const char *candidate = args.Argv( icmd++ );
					if ( strstr( candidate, ";" ) != NULL ) {
						break;
					}
					if ( !idStr::Icmp( candidate, value ) ) {
						matchIndex = optionIndex;
					}
					optionIndex++;
				}
				gui->SetStateInt( stateName, matchIndex );
			}
			continue;
		}

		// Retail menu commands that copy a cvar into a gui state and back. The
		// stock and Awakening main menus use the getters to seed their
		// crosshair size and colour controls.
		if ( !idStr::Icmp( cmd, "GetCVarValue" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			if ( args.Argc() - icmd >= 2 ) {
				const char *cvarName = args.Argv( icmd++ );
				const char *stateName = args.Argv( icmd++ );
				if ( gui != NULL ) {
					gui->SetStateString( stateName, cvarSystem->GetCVarString( cvarName ) );
				}
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "SetCVarValue" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			if ( args.Argc() - icmd >= 2 ) {
				const char *stateName = args.Argv( icmd++ );
				const char *cvarName = args.Argv( icmd++ );
				if ( gui != NULL ) {
					cvarSystem->SetCVarString( cvarName, gui->State().GetString( stateName ) );
				}
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "GetVecCVarValue" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			if ( args.Argc() - icmd >= 3 ) {
				const char *cvarName = args.Argv( icmd++ );
				const int component = atoi( args.Argv( icmd++ ) );
				const char *stateName = args.Argv( icmd++ );
				if ( gui != NULL && component >= 0 && component < 4 ) {
					idVec4 value( 0.0f, 0.0f, 0.0f, 0.0f );
					sscanf( cvarSystem->GetCVarString( cvarName ), "%f %f %f %f", &value.x, &value.y, &value.z, &value.w );
					gui->SetStateString( stateName, va( "%f", value[ component ] ) );
				}
			}
			continue;
		}

		// Clicking the crosshair preview sends "chooseCrosshair 1" and
		// right-clicking it "chooseCrosshair -1": one step through the custom
		// set, wrapping at both ends. Retail stepped back for any value below 1.
		// The multiplayer menu's argless button means forward.
		if ( !idStr::Icmp( cmd, "chooseCrosshair" ) ) {
			int step = 1;
			if ( icmd < args.Argc() && idStr::Cmp( args.Argv( icmd ), ";" ) ) {
				step = atoi( args.Argv( icmd++ ) ) >= 1 ? 1 : -1;
			}
			MainMenuPublishCrosshair( guiMainMenu, step );
			continue;
		}

		// always let the game know the command is being run
		if ( game ) {
			game->HandleMainMenuCommands( cmd, guiActive );
		}

		if ( !idStr::Icmp( cmd, "restartLevel" ) ) {
			// The in-game Difficulty page only records its choice in
			// desktop::skill, so backing out of it changes nothing; the choice
			// takes effect here, with the level restarted around it.
			const int skill = idMath::ClampInt( 0, 4, MainMenuGetNewGameOption( guiMainMenu, "desktop::skill", "skill", cvarSystem->GetCVarInteger( "g_skill" ) ) );
			RestartLevelAtSkill( skill );
			return;
		}

		if ( !idStr::Icmp( cmd, "startMap" ) ) {
			MainMenuApplyNewGameOptions( guiMainMenu );
			if ( icmd < args.Argc() ) {
				const char *map = args.Argv( icmd++ );
                if ( !idStr::Icmp( map, "game/airdefense1" ) && !idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) ) map = "game/m01_stranarus_trench1";
                StartNewGame( map );
			} else {
#ifndef ID_DEMO_BUILD
				StartNewGame( !idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) ? "game/m01_stranarus_trench1" : "game/airdefense1" );
#else
				StartNewGame( "game/demo_mars_city1" );
#endif
			}
			if ( guiActive == guiMainMenu ) {
				ExitMenu();
			}
			continue;
		}
		
		if ( !idStr::Icmp( cmd, "startGame" ) ) {
			MainMenuApplyNewGameOptions( guiMainMenu );
			if ( icmd < args.Argc() ) {
				const char *map = args.Argv( icmd++ );
                if ( !idStr::Icmp( map, "game/airdefense1" ) && !idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) ) map = "game/m01_stranarus_trench1";
                StartNewGame( map );
			} else {
#ifndef ID_DEMO_BUILD
				StartNewGame( !idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) ? "game/m01_stranarus_trench1" : "game/airdefense1" );
#else
				StartNewGame( "game/demo_mars_city1" );
#endif
			}
			// need to do this here to make sure presentation time is current or the gui activates with a time that 
			// is "however long map load took" time in the past
			common->GUIFrame( false, false );
			SetGUI( guiIntro, NULL );
			guiIntro->StateChanged( common->GetPresentationTime(), true );
			// stop playing the game sounds
			SetPlayingSoundWorld( menuSoundWorld );

			continue;
		}

		if ( !idStr::Icmp( cmd, "quit" ) ) {
			ExitMenu();
			common->Quit();
			return;
		}

		if ( !idStr::Icmp( cmd, "loadMod" ) ) {
			int choice = guiActive->State().GetInt( "modsList_sel_0" );
			if ( choice >= 0 && choice < modsList.Num() ) {
				cvarSystem->SetCVarString( "fs_game", modsList[ choice ].directory.c_str() );
				cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "reloadEngine menu\n" );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "updateModInfo" ) ) {
			UpdateModsMenuGuiVars();
			guiActive->StateChanged( common->GetPresentationTime() );
			continue;
		}

		if ( !idStr::Icmp( cmd, "InitServerBrowser" ) ) {
			idAsyncNetwork::client.serverList.GUIInit();
			continue;
		}

		if ( !idStr::Icmp( cmd, "updateFilterByMod" ) ||
			!idStr::Icmp( cmd, "filterByPrevMod" ) || !idStr::Icmp( cmd, "filterByNextMod" ) ) {
			const int direction = !idStr::Icmp( cmd, "filterByPrevMod" ) ? -1 : !idStr::Icmp( cmd, "filterByNextMod" ) ? 1 : 0;
			idAsyncNetwork::client.serverList.UpdateFilterByMod( direction );
			continue;
		}

		if ( !idStr::Icmp( cmd, "toggleFavorite" ) ) {
			idAsyncNetwork::client.serverList.ToggleFavorite();
			continue;
		}

		if ( !idStr::Icmp( cmd, "server_clearSort" ) ) {
			idAsyncNetwork::client.serverList.ResetSorting();
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortFavorite" ) || !idStr::Icmp( cmd, "sortLocked" ) ||
			!idStr::Icmp( cmd, "sortDed" ) || !idStr::Icmp( cmd, "sortPB" ) || !idStr::Icmp( cmd, "sortRepeater" ) ) {
			const serverSort_t sort = !idStr::Icmp( cmd, "sortFavorite" ) ? SORT_FAVORITE :
				!idStr::Icmp( cmd, "sortLocked" ) ? SORT_PASSWORD : !idStr::Icmp( cmd, "sortDed" ) ? SORT_DEDICATED :
				!idStr::Icmp( cmd, "sortPB" ) ? SORT_PUNKBUSTER : SORT_REPEATER;
			idAsyncNetwork::client.serverList.SetSorting( sort );
			continue;
		}

		if ( !idStr::Icmp( cmd, "UpdateServers" ) || !idStr::Icmp( cmd, "RefreshServers" ) ) {
			Session_ScanServers( guiActive->State().GetBool( "lanSet" ), !idStr::Icmp( cmd, "UpdateServers" ) );
			continue;
		}

		if ( !idStr::Icmp( cmd, "FilterServers" ) ) {
			idAsyncNetwork::client.serverList.ApplyFilter( );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortServerName" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_SERVERNAME );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortGame" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_GAME );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortPlayers" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_PLAYERS );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortPing" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_PING );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortGameType" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_GAMETYPE );
			continue;
		}

		if ( !idStr::Icmp( cmd, "sortMap" ) ) {
			idAsyncNetwork::client.serverList.SetSorting( SORT_MAP );
			continue;
		}

		if ( !idStr::Icmp( cmd, "serverList" ) || !idStr::Icmp( cmd, "click_serverList" ) ) {
			idAsyncNetwork::client.serverList.GUIUpdateSelected();
			continue;
		}

		if ( !idStr::Icmp( cmd, "LANConnect" ) || !idStr::Icmp( cmd, "connect" ) ) {
			idStr endpoint;
			if ( !idAsyncNetwork::client.serverList.GetSelectedAddress( endpoint ) ) {
				continue;
			}
			idCmdArgs connectArgs;
			connectArgs.AppendArg( "connect" );
			connectArgs.AppendArg( endpoint.c_str() );
			cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, connectArgs );
			return;
		}

		if ( !idStr::Icmp( cmd, "MAPScan" ) || !idStr::Icmp( cmd, "initCreateServerSettings" ) ) {
			const char *gametype = cvarSystem->GetCVarString( "si_gameType" );
			if ( gametype == NULL || *gametype == 0 || idStr::Icmp( gametype, "singleplayer" ) == 0 ) {
				gametype = "DM";
			}

			int i, num;
			idListGUILocal *const mainMenuMapList = static_cast<idListGUILocal *>( guiMainMenu_MapList );
			int numMapsAdded = 0;
			int selectedIndex = -1;
			idStr si_map = cvarSystem->GetCVarString( "si_map" );
			const idDict *dict = NULL;

			mainMenuMapList->SetStateChanges( false );
			mainMenuMapList->Clear();

			num = fileSystem->GetNumMaps();
			for ( i = 0; i < num; i++ ) {
				dict = fileSystem->GetMapDecl( i );
				if ( !MapSupportsStartServerGameType( dict, gametype ) ) {
					continue;
				}

				const char *mapName = dict->GetString( "name" );
				if ( mapName[ 0 ] == '\0' ) {
					mapName = dict->GetString( "path" );
				}
				mapName = common->GetLanguageDict()->GetString( mapName );
				mainMenuMapList->Add( i, mapName );
				if ( !si_map.Icmp( dict->GetString( "path" ) ) ) {
					selectedIndex = numMapsAdded;
				}
				numMapsAdded++;
			}

			// If the requested gametype has no explicit flags in current content, keep the list usable.
			if ( numMapsAdded == 0 ) {
				for ( i = 0; i < num; i++ ) {
					dict = fileSystem->GetMapDecl( i );
					if ( !MapSupportsAnyMPGameType( dict ) ) {
						continue;
					}

					const char *mapName = dict->GetString( "name" );
					if ( mapName[ 0 ] == '\0' ) {
						mapName = dict->GetString( "path" );
					}
					mapName = common->GetLanguageDict()->GetString( mapName );
					mainMenuMapList->Add( i, mapName );
					if ( !si_map.Icmp( dict->GetString( "path" ) ) ) {
						selectedIndex = numMapsAdded;
					}
					numMapsAdded++;
				}
			}

			if ( numMapsAdded > 0 ) {
				if ( selectedIndex < 0 ) {
					selectedIndex = 0;
				}
				guiMainMenu->SetStateInt( "mapList_top", -1 );
				mainMenuMapList->SetSelection( selectedIndex );
				mainMenuMapList->SetStateChanges( true );
				int mapNum = mainMenuMapList->GetSelection( NULL, 0 );
				dict = fileSystem->GetMapDecl( mapNum );
			} else {
				guiMainMenu->SetStateInt( "mapList_sel_0", -1 );
				guiMainMenu->SetStateInt( "mapList_top", 0 );
				mainMenuMapList->SetStateChanges( true );
				dict = NULL;
			}
			cvarSystem->SetCVarString( "si_map", ( dict ? dict->GetString( "path" ) : "" ) );
			guiMainMenu->SetStateInt( "mapList_num", numMapsAdded );

			// set the current level shot
			UpdateMPLevelShot();
			guiMainMenu->StateChanged( common->GetPresentationTime() );
			continue;
		}

		if ( !idStr::Icmp( cmd, "click_mapList" ) ) {
			CommitStartServerMapSelection( guiMainMenu );
			UpdateMPLevelShot();
			guiMainMenu->StateChanged( common->GetPresentationTime() );
			continue;
		}

		if ( !idStr::Icmp( cmd, "inetConnect" ) ) {
			const char	*s = guiMainMenu->State().GetString( "inetGame" );

			if ( !s || s[0] == 0 ) {
				// don't put the menu away if there isn't a valid selection
				continue;
			}

			idStr error;
			if ( !Session_ConnectToTypedAddress( s, error ) ) {
				MessageBox( MSG_OK, error.c_str(), common->GetLanguageDict()->GetString( "#str_06735" ), true );
				continue;
			}
			return;
		}

		// JOIN IP ADDRESS's "your ip address:", as retail's session published it.
		if ( !idStr::Icmp( cmd, "getClientIP" ) ) {
			guiMainMenu->SetStateString( "net_ip", Session_LocalAddressText().c_str() );
			continue;
		}

		// FIND PLAYER/CLAN: retail asked the master server for the servers the
		// player or clan was on, which no master answers now; the list is
		// searched for the name instead (SetPlayerSearch), until a new list.
		if ( !idStr::Icmp( cmd, "playerClanSearch" ) ) {
			idAsyncNetwork::client.serverList.SetPlayerSearch( guiActive->State().GetString( "playerclan_search" ) );
			continue;
		}

		// The stock Create Server page's player-count guards and its advanced
		// popup's ban list, as retail's session handled them: the counts keep
		// their order, the game publishes its ban list on the menu
		// (sa_banList_item_N), and REMOVE BAN removes the chosen row's GUID.
		if ( !idStr::Icmp( cmd, "checkMinPlayers" ) ) {
			const int maxPlayers = cvarSystem->GetCVarInteger( "si_maxPlayers" );
			if ( maxPlayers < cvarSystem->GetCVarInteger( "si_minPlayers" ) ) {
				cvarSystem->SetCVarInteger( "si_minPlayers", maxPlayers );
			}
			continue;
		}
		if ( !idStr::Icmp( cmd, "checkMaxPlayers" ) ) {
			const int minPlayers = cvarSystem->GetCVarInteger( "si_minPlayers" );
			if ( minPlayers > cvarSystem->GetCVarInteger( "si_maxPlayers" ) ) {
				cvarSystem->SetCVarInteger( "si_maxPlayers", minPlayers );
			}
			continue;
		}
		if ( !idStr::Icmp( cmd, "populateBanList" ) ) {
			if ( game != NULL && guiActive != NULL ) {
				game->PopulateBanList( guiActive );
			}
			continue;
		}
		if ( !idStr::Icmp( cmd, "handleServerAdminRemoveBan" ) ) {
			const int selected = guiActive != NULL ? guiActive->State().GetInt( "sa_banList_sel_0", "-1" ) : -1;
			if ( selected >= 0 && game != NULL ) {
				idStr name, guid;
				if ( Session_BanListRow( guiActive->State().GetString( va( "sa_banList_item_%d", selected ) ), name, guid ) ) {
					game->RemoveGuidFromBanList( guid.c_str() );
				}
				game->PopulateBanList( guiActive );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "startMultiplayer" ) ) {
			CommitStartServerMapSelection( guiMainMenu );
			if ( StartMenuServer() ) {
				return;
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "mpSkin")) {
			idStr skin;
			if ( args.Argc() - icmd >= 1 ) {
				skin = args.Argv( icmd++ );
				cvarSystem->SetCVarString( "ui_skin", skin );
				SetMainMenuSkin();
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "close" ) ) {
			// if we aren't in a game, the menu can't be closed
			if ( mapSpawned ) {
				ExitMenu();
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "resetdefaults" ) ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "exec default.cfg" );
			guiMainMenu->SetKeyBindingNames();
			SetMainMenuVideoGuiVars( guiMainMenu );
			SetMainMenuQualityGuiVars( guiMainMenu );
			SyncMainMenuAspectVisibility( guiMainMenu );
			continue;
		}


		if ( !idStr::Icmp( cmd, "bind" ) ) {
			if ( args.Argc() - icmd >= 2 ) {
				int key = atoi( args.Argv( icmd++ ) );
				idStr bind = args.Argv( icmd++ );
				idKeyInput::BindForMenu( key, bind );
				guiMainMenu->SetKeyBindingNames();
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "play" ) ) {
			if ( args.Argc() - icmd >= 1 ) {
				idStr snd = args.Argv( icmd++ );
				int channel = 1;
				if ( snd.Length() == 1 ) {
					channel = atoi( snd );
					snd = args.Argv( icmd++ );
				}
				menuSoundWorld->PlayShaderDirectly( snd, channel );

			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "music" ) ) {
			if ( args.Argc() - icmd >= 1 ) {
				idStr snd = args.Argv( icmd++ );
				menuSoundWorld->PlayShaderDirectly( snd, 2 );
				if ( menuIntroBlackoutActive && menuIntroBlackoutAwaitMenuMusic &&
					( snd.Icmp( "main_menu" ) == 0 || snd.Icmp( "main_menu_gameplay" ) == 0 ) ) {
					menuIntroBlackoutAwaitMenuMusic = false;
					menuIntroBlackoutFadeStart = -1;
				}
			}
			continue;
		}

		// triggered from mainmenu or mpmain
		if ( !idStr::Icmp( cmd, "sound" ) ) {
			idStr vcmd;
			if ( args.Argc() - icmd >= 1 ) {
				vcmd = args.Argv( icmd++ );
			}
			if ( !vcmd.Length() || !vcmd.Icmp( "speakers" ) ) {
				int old = cvarSystem->GetCVarInteger( "s_numberOfSpeakers" );
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );
				if ( old != cvarSystem->GetCVarInteger( "s_numberOfSpeakers" ) ) {
#ifdef _WIN32
					MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_04142" ), common->GetLanguageDict()->GetString( "#str_04141" ), true );
#else
					// a message that doesn't mention the windows control panel
					MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07230" ), common->GetLanguageDict()->GetString( "#str_04141" ), true );
#endif
				}
			}
			if ( !vcmd.Icmp( "eax" ) ) {
				if ( cvarSystem->GetCVarBool( "s_useEAXReverb" ) ) {
					// EAX requires the OpenAL backend; force it on when requested from the menu.
					cvarSystem->SetCVarBool( "s_useOpenAL", true );
					cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );

					const int eax = soundSystem->IsEAXAvailable();
					switch ( eax ) {
					case 2:
						// OpenAL subsystem load failed.
						MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07238" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
						break;
					case 1:
						// Enabled and active on current device.
						MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_04137" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
						break;
					case -1:
						cvarSystem->SetCVarBool( "s_useEAXReverb", false );
						// Explicitly disabled.
						MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07233" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
						break;
					case 0:
					default:
						cvarSystem->SetCVarBool( "s_useEAXReverb", false );
						// Unsupported by OpenAL version/device or EFX unavailable.
						MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_07232" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
						break;
					}
				} else {
					// openQ4 has no legacy (non-OpenAL) mixer; leave
					// s_useOpenAL untouched so IsEAXAvailable() keeps
					// reporting real capability after re-enabling EAX.
					cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );
					// when you restart
					MessageBox( MSG_OK, common->GetLanguageDict()->GetString( "#str_04137" ), common->GetLanguageDict()->GetString( "#str_07231" ), true );
				}
			}
			if ( !vcmd.Icmp( "drivar" ) ) {
				if ( idSoundHardware_OpenAL::IsDefaultDeviceChoiceValue( cvarSystem->GetCVarString( "s_deviceName" ) ) ) {
					cvarSystem->SetCVarString( "s_deviceName", "" );
				}
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "s_restart\n" );				
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyCorpseTimeChoice" ) ) {
			ApplyMainMenuCorpseTimeChoice( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyGunPositionChoice" ) ) {
			ApplyMainMenuGunPositionChoice( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyForceModelChoice" ) ) {
			ApplyMainMenuForceModelChoice( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshKeyBindings" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			if ( targetGui != NULL ) {
				targetGui->SetKeyBindingNames();
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshSystemSettings" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			PopulateMainMenuDisplayChoices( targetGui );
			SetMainMenuQualityGuiVars( targetGui );
			SyncMainMenuAspectVisibility( targetGui );
			if ( targetGui != NULL ) {
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshAudioSettings" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			PopulateMainMenuAudioDeviceChoices( targetGui );
			if ( targetGui != NULL ) {
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshMods" ) ) {
			SetModsMenuGuiVars();
			if ( guiActive != NULL ) {
				guiActive->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyDisplayModeChoice" ) ) {
			ApplyMainMenuDisplayModeChoice( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyCustomDisplaySize" ) ) {
			ApplyMainMenuCustomDisplaySize( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "refreshDisplayChoices" ) ) {
			RefreshMainMenuDisplayChoices( guiActive ? guiActive : guiMainMenu );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applyPerformancePreset" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			idCmdArgs presetArgs;
			presetArgs.AppendArg( "applyPerformancePreset" );
			AppendMainMenuCommandArgsUntilSeparator( args, icmd, presetArgs );
			cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, presetArgs );
			RefreshMainMenuPerformancePresetGuiVars( gui );
			continue;
		}

		if ( !idStr::Icmp( cmd, "autoDetectPerformancePreset" ) ) {
			idUserInterface *gui = guiActive ? guiActive : guiMainMenu;
			idCmdArgs presetArgs;
			presetArgs.AppendArg( "autoDetectPerformancePreset" );
			AppendMainMenuCommandArgsUntilSeparator( args, icmd, presetArgs );
			cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, presetArgs );
			RefreshMainMenuPerformancePresetGuiVars( gui );
			continue;
		}

		if ( !idStr::Icmp( cmd, "applySettingsScroll" ) ) {
			idStr pageName;
			if ( args.Argc() - icmd >= 1 ) {
				pageName = args.Argv( icmd++ );
			}
			ApplyMainMenuSettingsScrollPage( guiActive ? guiActive : guiMainMenu, pageName.c_str(), false );
			continue;
		}

		if ( !idStr::Icmp( cmd, "video" ) ) {
			idStr vcmd;
			if ( args.Argc() - icmd >= 1 ) {
				vcmd = args.Argv( icmd++ );
			}

			const char *presetCommand = NULL;
			if ( idStr::Icmp( vcmd, "low" ) == 0 ) {
				presetCommand = "applyPerformancePreset low\n";
			} else if ( idStr::Icmp( vcmd, "medium" ) == 0 ) {
				presetCommand = "applyPerformancePreset performance\n";
			} else  if ( idStr::Icmp( vcmd, "high" ) == 0 ) {
				presetCommand = "applyPerformancePreset balanced\n";
			} else  if ( idStr::Icmp( vcmd, "ultra" ) == 0 ) {
				presetCommand = "applyPerformancePreset ultra\n";
			} else if ( idStr::Icmp( vcmd, "recommended" ) == 0 ) {
				presetCommand = "autoDetectPerformancePreset\n";
			}

			// Reapply even when the coarse stock spec is unchanged: Balanced and
			// Enhanced share spec 2, and a display change can alter automatic scale.
			if ( presetCommand != NULL ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, presetCommand );
				guiActive->SetStateInt( "com_machineSpec", com_machineSpec.GetInteger() );
				RefreshMainMenuPerformancePresetGuiVars( guiActive );
			}

			if ( idStr::Icmp( vcmd, "restart" )  == 0) {
				guiActive->HandleNamedEvent( "cvar write render" );
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "vid_restart\n" );
			}

			continue;
		}

		if ( !idStr::Icmp( cmd, "clearBind" ) ) {
			if ( args.Argc() - icmd >= 1 ) {
				idKeyInput::UnbindBinding( args.Argv( icmd++ ) );
				guiMainMenu->SetKeyBindingNames();
			}
			continue;
		}

		// FIXME: obsolete
		if ( !idStr::Icmp( cmd, "chatdone" ) ) {
			idStr temp = guiActive->State().GetString( "chattext" );
			temp += "\r";
			guiActive->SetStateString( "chattext", "" );
			continue;
		}

		if ( !idStr::Icmp ( cmd, "exec" ) ) {

			//Backup the language so we can restore it after defaults.
			idStr lang = cvarSystem->GetCVarString("sys_lang");

			cmdSystem->BufferCommandText( CMD_EXEC_NOW, args.Argv( icmd++ ) );
			if ( idStr::Icmp( "cvar_restart", args.Argv( icmd - 1 ) ) == 0 ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "exec default.cfg" );
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "setMachineSpec\n" );

				//Make sure that any r_brightness changes take effect
				float bright = cvarSystem->GetCVarFloat("r_brightness");
				cvarSystem->SetCVarFloat("r_brightness", 0.0f);
				cvarSystem->SetCVarFloat("r_brightness", bright);

				//Force user info modified after a reset to defaults
				cvarSystem->SetModifiedFlags(CVAR_USERINFO);

				guiActive->SetStateInt( "com_machineSpec", com_machineSpec.GetInteger() );

				//Restore the language
				cvarSystem->SetCVarString("sys_lang", lang);

			}
			continue;
		}

		if ( !idStr::Icmp ( cmd, "loadBinds" ) ) {
			guiMainMenu->SetKeyBindingNames();
			continue;
		}
		
		if ( !idStr::Icmp( cmd, "systemCvars" ) ) {
			guiActive->HandleNamedEvent( "cvar read render" );
			guiActive->HandleNamedEvent( "cvar read sound" );
			continue;
		}

		if ( !idStr::Icmp( cmd, "CheckUpdate" ) ) {
			idAsyncNetwork::client.SendVersionCheck();
			continue;
		}

		if ( !idStr::Icmp( cmd, "CheckUpdate2" ) ) {
			idAsyncNetwork::client.SendVersionCheck( true );
			continue;
		}

		if ( !idStr::Icmp( cmd, "update_model" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			SetMainMenuMPModelVars( targetGui );
			if ( targetGui ) {
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "initMPSettings" ) ) {
			idUserInterface *targetGui = guiActive ? guiActive : guiMainMenu;
			SetMainMenuMPModelVars( targetGui );
			if ( targetGui ) {
				targetGui->StateChanged( common->GetPresentationTime() );
			}
			continue;
		}

		if ( !idStr::Icmp( cmd, "exitMPSettings" ) ) {
			CommitMainMenuMPSettings();
			continue;
		}
	}
}

/*
==============
idSessionLocal::StartMenuServer

Starts the server the menu set up, for the stock Create Server page and the
retained one: the upstream rate's client limit and the listen-server warning
ask first, and either answer No leaves the menu as it is (false). The map,
game type and limits are in their CVars already.
==============
*/
bool idSessionLocal::StartMenuServer() {
	// Create Server binds Server Type and Dedicated to CVars, which retail
	// Quake4.exe 1.4.2 reads here; Quake 4's menu never sets Doom 3's
	// "server_type" and "dedicated" GUI keys.
	cvarSystem->SetCVarBool( "net_LANServer", cvarSystem->GetCVarBool( "net_menulanserver" ) );
	// Dedicated turns this client into a dedicated server in SpawnServer:
	// the window closes and the system console runs the server.
	const int dedicated = cvarSystem->GetCVarBool( "net_serverMenuDedicated" ) ? 1 : 0;
	if ( gui_configServerRate.GetInteger() > 0 ) {
		// guess the best rate for upstream, number of internet clients
		if ( gui_configServerRate.GetInteger() == 5 || cvarSystem->GetCVarBool( "net_LANServer" ) ) {
			cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 25600 );
		} else {
			// internet players
			int n_clients = cvarSystem->GetCVarInteger( "si_maxPlayers" );
			if ( !dedicated ) {
				n_clients--;
			}
			int maxclients = 0;
			switch ( gui_configServerRate.GetInteger() ) {
				case 1:
					// 128 kbits
					cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 8000 );
					maxclients = 2;
					break;
				case 2:
					// 256 kbits
					cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 9500 );
					maxclients = 3;
					break;
				case 3:
					// 384 kbits
					cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 10500 );
					maxclients = 4;
					break;
				case 4:
					// highest internet preset: treat as modern high-bandwidth connection
					cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 25600 );
					maxclients = 16;
					break;
				default:
					// unknown preset: fall back to modern defaults
					cvarSystem->SetCVarInteger( "net_serverMaxClientRate", 16000 );
					maxclients = 16;
					break;
			}
			if ( n_clients > maxclients ) {
				const int adjustedMaxClients = dedicated ? maxclients : Min( 16, maxclients + 1 );
				if ( MessageBox( MSG_OKCANCEL, va( common->GetLanguageDict()->GetString( "#str_04315" ), adjustedMaxClients ), common->GetLanguageDict()->GetString( "#str_04316" ), true, "OK" )[ 0 ] == '\0' ) {
					return false;
				}
				cvarSystem->SetCVarInteger( "si_maxPlayers", adjustedMaxClients );
			}
		}
	}

	const int listenWarningLimit = GetListenServerPlayerWarningLimit(
		gui_configServerRate.GetInteger(),
		cvarSystem->GetCVarInteger( "net_serverMaxClientRate" ) );

	if ( !dedicated &&
		!cvarSystem->GetCVarBool( "net_LANServer" ) &&
		listenWarningLimit < 16 &&
		cvarSystem->GetCVarInteger( "si_maxPlayers" ) > listenWarningLimit ) {
		// "Dedicated server mode is recommended for internet servers with more than 4 players. Continue in listen mode?"
		if ( !MessageBox( MSG_YESNO, va( common->GetLanguageDict()->GetString( "#str_100625" ), listenWarningLimit ), common->GetLanguageDict()->GetString ( "#str_100626" ), true, "yes" )[ 0 ] ) {
			return false;
		}
	}

	if ( dedicated ) {
		cvarSystem->SetCVarInteger( "net_serverDedicated", 1 );
	} else {
		cvarSystem->SetCVarInteger( "net_serverDedicated", 0 );
	}

	ExitMenu();
	// may trigger a reloadEngine - APPEND
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "SpawnServer\n" );
	return true;
}

/*
==============
idSessionLocal::HandleChatMenuCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleChatMenuCommands( const char *menuCommand ) {
	// execute the command from the menu
	int i;
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	for ( i = 0; i < args.Argc(); ) {
		const char *cmd = args.Argv( i++ );

		if ( idStr::Icmp( cmd, "chatactive" ) == 0 ) {
			//chat.chatMode = CHAT_GLOBAL;
			continue;
		}
		if ( idStr::Icmp( cmd, "chatabort" ) == 0 ) {
			//chat.chatMode = CHAT_NONE;
			continue;
		}
		if ( idStr::Icmp( cmd, "netready" ) == 0 ) {
			bool b = cvarSystem->GetCVarBool( "ui_ready" );
			cvarSystem->SetCVarBool( "ui_ready", !b );
			continue;
		}
		if ( idStr::Icmp( cmd, "netstart" ) == 0 ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, "netcommand start\n" );
			continue;
		}
	}
}

/*
==============
idSessionLocal::HandleInGameCommands

Executes any commands returned by the gui
==============
*/
void idSessionLocal::HandleInGameCommands( const char *menuCommand ) {
	// execute the command from the menu
	idCmdArgs args;

	args.TokenizeString( menuCommand, false );

	const char *cmd = args.Argv( 0 );
	if ( !idStr::Icmp( cmd, "close" ) ) {
		if ( guiActive ) {
			sysEvent_t  ev;
			ev.evType = SE_NONE;
			const char	*cmd;
			cmd = guiActive->HandleEvent( &ev, common->GetPresentationTime() );
			guiActive->Activate( false, common->GetPresentationTime() );
			openq4::NativeInputBeforeSessionChange();
			guiActive = NULL;
		}
	}
}

#ifndef ID_DEDICATED
// The stock campaign pages: the campaigns (campaign_view 0) and a campaign's
// own page (1), with its difficulty, turbo option, START and chapters.
static const char *const STOCK_CAMPAIGN_GUI = "guis/campaign_menu.gui";

static bool Session_AwakeningPageActive() {
    return !idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" );
}

// A campaign page's difficulty and its description, the stock New Game
// page's. The stock page shows text as published, the retained one resolves
// the keys itself.
static void Session_SetCampaignPageSkill( idUserInterface *gui, int skill, bool resolve ) {
    static const char *descriptions[] = { "#str_200183", "#str_200019", "#str_200184", "#str_200185", "#str_41118" };
    skill = idMath::ClampInt( 0, 4, skill );
    gui->SetStateInt( "skill", skill );
    gui->SetStateString( "skill_description", resolve ? common->GetLanguageDict()->GetString( descriptions[skill] ) : descriptions[skill] );
}

// A campaign page's verbs, the retained pages' requests and the stock page's
// commands alike: a difficulty, turbo, START, a chapter and its start, and
// BACK to the campaigns (onto CHAPTERS from the retained CHAPTERS page).
static void Session_HandleCampaignPageRequest( idSessionLocal &session, idUserInterface *gui, const char *request, bool retained ) {
    const char *campaign = Session_AwakeningPageActive() ? "awakening" : "quake4";
    const bool chaptersBack = !idStr::Icmp( request, "campaignChaptersBack" );
    if ( !idStr::Icmp( request, "campaignSkillBack" ) || chaptersBack ) {
        if ( retained ) session.BeginRetainedCampaignSkill( gui, "", true, chaptersBack ); else session.OpenCampaignSelector( true );
        return;
    }
    if ( !idStr::Icmp( request, "campaignSkillStart" ) || !idStr::Icmp( request, "campaignChapterStart" ) ) {
        // A chapter's autosave brings back the difficulty it was written
        // with; a fresh start takes the page's, where the page has one. The
        // retained CHAPTERS page has none, so the current difficulty stands.
        if ( gui->State().FindKey( "skill" ) != NULL ) {
            cvarSystem->SetCVarInteger( "g_skill", idMath::ClampInt( 0, 4, gui->State().GetInt( "skill", "1" ) ) );
            cvarSystem->SetCVarBool( "g_turboMode", gui->State().GetBool( "turbo" ) );
        }
        if ( !idStr::Icmp( request, "campaignSkillStart" ) ) {
            session.SelectCampaign( campaign, true );
        } else if ( !session.StartCampaignChapter( gui->State().GetInt( retained ? "chapter.selected" : "chapters_sel_0", "-1" ) ) ) {
            session.PublishCampaignChapters( gui );
        }
        return;
    }
    if ( !idStr::Icmp( request, "campaignChapterSelect" ) ) {
        session.PublishCampaignChapters( gui, gui->State().GetInt( retained ? "chapter.pick" : "chapters_sel_0", "-1" ) );
        return;
    }
    if ( !idStr::Icmp( request, "campaignSkillTurbo" ) ) {
        gui->SetStateBool( "turbo", !gui->State().GetBool( "turbo" ) );
    } else {
        for ( int i = 0; i < 5; ++i ) {
            if ( !idStr::Icmp( request, va( "campaignSkill%d", i ) ) ) {
                Session_SetCampaignPageSkill( gui, i, !retained );
                break;
            }
        }
    }
    gui->StateChanged( common->GetPresentationTime() );
}
#endif

/*
==============
idSessionLocal::HandleStockCampaignCommand

The stock campaign pages' commands (STOCK_CAMPAIGN_GUI), each after its
sound; false for any other GUI.
==============
*/
bool idSessionLocal::HandleStockCampaignCommand( idUserInterface *gui, const char *menuCommand ) {
#ifdef ID_DEDICATED
    return false;
#else
    if ( gui == NULL || idStr::Icmp( gui->Name(), STOCK_CAMPAIGN_GUI ) ) return false;
    idCmdArgs args;
    args.TokenizeString( menuCommand, false );
    for ( int i = 0; i < args.Argc(); ) {
        const char *command = args.Argv( i++ );
        if ( !idStr::Icmp( command, "play" ) ) {
            if ( i < args.Argc() ) menuSoundWorld->PlayShaderDirectly( args.Argv( i++ ), 1 );
            continue;
        }
        if ( !idStr::Cmp( command, ";" ) ) continue;
        if ( !idStr::Icmp( command, "campaignQuake4" ) ) SelectCampaign( "quake4" );
        else if ( !idStr::Icmp( command, "campaignAwakening" ) ) SelectCampaign( "awakening" );
        else if ( !idStr::Icmp( command, "campaignBack" ) ) OpenCampaignSelector( false );
        else Session_HandleCampaignPageRequest( *this, gui, command, false );
        // Each verb hands the screen on or republishes the page.
        break;
    }
    return true;
#endif
}

/*
==============
idSessionLocal::DispatchCommand
==============
*/
static void Session_DispatchApplicationCommand( idUserInterface *gui, const char *command, void *context ) {
	static_cast<idSessionLocal*>( context )->DispatchCommand( gui, command );
}

void idSessionLocal::PumpApplicationActions( idUserInterface *only ) {
	UI_PumpApplicationActions( Session_DispatchApplicationCommand, this, only );
}

void idSessionLocal::DispatchCommand( idUserInterface *gui, const char *menuCommand, bool doIngame ) {

	if ( !gui ) {
		gui = guiActive;
	}
	if ( gui == NULL && guiMainMenu == NULL ) {
		// No GUI is open, and this session has no menus (a dedicated server's):
		// nothing takes the command, and below it would match the NULL menus.
		return;
	}
    if ( gui && gui == guiActive && HandleStockCampaignCommand( gui, menuCommand ) ) {
        return;
    }
	bool closeRequested = false;
	if ( UI_DispatchApplicationActions( gui, menuCommand, closeRequested ) ) {
		// A retained screen's accepted verbs, in order. Each names a session
		// operation; nothing here reaches the console command buffer as text.
		idStr sessionRequest;
		while ( UI_TakeSessionRequest( gui, menuCommand, sessionRequest ) ) {
			HandleRetainedSessionRequest( gui, sessionRequest.c_str() );
		}
		if ( closeRequested ) {
			if ( gui == guiSystem ) ReturnSystemSettings();
			else if ( gui == guiTest ) TestGUI( NULL );
			else if ( gui == guiRetainedHome ) HandleRetainedSessionRequest( gui, "resume" );
			else if ( gui == guiRetainedMultiplayer ) HandleRetainedMultiplayerRequest( gui, "mpClose" );
			// The death screen stays: closing it would leave a player with no
			// level and no way back, and the stock screen took no Escape.
			else if ( gui == guiRetainedRestartMenu ) {}
			else if ( gui == guiActive ) ExitMenu();
		}
		return;
	}

	if ( gui == guiMainMenu ) {
		HandleMainMenuCommands( menuCommand );
		return;
	} else if ( arenaCampaign.IsGui( gui ) ) {
		arenaCampaign.HandleGuiCommand( menuCommand );
		return;
	} else if ( gui == guiDemoMenu ) {
		HandleDemoMenuCommand( menuCommand );
		return;
	} else if ( gui == guiIntro) {
		HandleIntroMenuCommands( menuCommand );
	} else if ( gui == guiMsg ) {
		HandleMsgCommands( menuCommand );
	} else if ( gui == guiTakeNotes ) {
		HandleNoteCommands( menuCommand );
	} else if ( gui == guiRestartMenu ) {
		HandleRestartMenuCommands( menuCommand );
	} else if ( game && guiActive && guiActive->State().GetBool( "gameDraw" ) ) {
		HandleGameMenuReturn( game->HandleGuiCommands( menuCommand ) );
	} else if ( guiHandle ) {
		if ( (*guiHandle)( menuCommand ) ) {
			return;
		}
	} else if ( !doIngame ) {
		common->DPrintf( "idSessionLocal::DispatchCommand: no dispatch found for command '%s'\n", menuCommand );
	}

	if ( doIngame ) {
		HandleInGameCommands( menuCommand );
	}
}


/*
==============
idSessionLocal::MenuEvent

Executes any commands returned by the gui
==============
*/
static bool MainMenuWindowStateIsNonZero( idUserInterface *gui, const char *stateName ) {
	idStr value;
	return gui != NULL && gui->GetPresentationValue( stateName, value ) && atoi( value.c_str() ) != 0;
}

static bool MainMenuWindowStateEqualsInt( idUserInterface *gui, const char *stateName, int expectedValue ) {
	idStr value;
	return gui != NULL && gui->GetPresentationValue( stateName, value ) && atoi( value.c_str() ) == expectedValue;
}

static bool MainMenuSettingsPopupIsVisible( idUserInterface *gui ) {
	static const char *settingsPopupStates[] = {
		"pop_p_defaults::visible",
		"pop_p_setAdv::visible",
		"pop_p_auto::visible",
		"pop_p_ultrawarn::visible",
		"pop_p_vidwarn::visible",
		"pop_p_set_sndadv::visible"
	};

	for ( int i = 0; i < static_cast<int>( sizeof( settingsPopupStates ) / sizeof( settingsPopupStates[ 0 ] ) ); ++i ) {
		if ( MainMenuWindowStateIsNonZero( gui, settingsPopupStates[ i ] ) ) {
			return true;
		}
	}

	return false;
}

typedef struct mainMenuSettingsScrollPage_s {
	const char *name;
	const char *pageVisibleState;
	const char *scrollStateName;
	idCVar *scrollCvar;
	const char *contentRectState;
	const char *thumbVisibleState;
	const char *thumbNoEventsState;
	const char *sectionChoiceState;
	int expectedPage;
	int minValue;
	int maxValue;
	int contentX;
	int baseY;
	int contentHeight;
	float stepY;
} mainMenuSettingsScrollPage_t;

static const mainMenuSettingsScrollPage_t MAINMENU_SETTINGS_SCROLL_PAGES[] = {
	{
		"system",
		"p_settings_sys::visible",
		"gui_set_sys_scroll",
		&gui_set_sys_scroll,
		"set_sys_content::rect",
		"set_sys_scroll_thumb::visible",
		"set_sys_scroll_thumb::noevents",
		"sys_section_choice",
		22,
		0,
		28,
		-24,
		-88,
		958,
		22.0f
	},
	{
		"audio",
		"p_settings_audio::visible",
		"gui_set_audio_scroll",
		&gui_set_audio_scroll,
		"set_audio_content::rect",
		"set_audio_scroll_thumb::visible",
		"set_audio_scroll_thumb::noevents",
		NULL,
		36,
		0,
		6,
		-24,
		-84,
		474,
		24.0f
	},
	{
		"game",
		"p_settings_game::visible",
		"gui_set_game_scroll",
		&gui_set_game_scroll,
		"set_game_content::rect",
		"set_game_scroll_thumb::visible",
		"set_game_scroll_thumb::noevents",
		"game_section_choice",
		21,
		0,
		78,
		-24,
		-41,
		2186,
		24.0f
	}
};

static const mainMenuSettingsScrollPage_t *FindMainMenuSettingsScrollPage( const char *name ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return NULL;
	}

	for ( int i = 0; i < static_cast<int>( sizeof( MAINMENU_SETTINGS_SCROLL_PAGES ) / sizeof( MAINMENU_SETTINGS_SCROLL_PAGES[ 0 ] ) ); ++i ) {
		if ( idStr::Icmp( MAINMENU_SETTINGS_SCROLL_PAGES[ i ].name, name ) == 0 ) {
			return &MAINMENU_SETTINGS_SCROLL_PAGES[ i ];
		}
	}

	return NULL;
}

static bool MainMenuSetWindowVar( idUserInterface *gui, const char *stateName, const char *value ) {
	return NativeInput_SessionCurrent() && gui != NULL && gui->SetPresentationValue( stateName, value );
}

static int MainMenuSettingsSectionChoiceForScroll( const mainMenuSettingsScrollPage_t &page, int scrollValue ) {
	if ( idStr::Icmp( page.name, "game" ) == 0 ) {
		if ( scrollValue < 11 ) {
			return 0;
		}
		if ( scrollValue < 21 ) {
			return 1;
		}
		if ( scrollValue < 41 ) {
			return 2;
		}
		if ( scrollValue < 48 ) {
			return 3;
		}
		if ( scrollValue < 60 ) {
			return 4;
		}
		if ( scrollValue < 65 ) {
			return 5;
		}
		if ( scrollValue < 72 ) {
			return 6;
		}
		return 7;
	}

	if ( idStr::Icmp( page.name, "system" ) == 0 ) {
		if ( scrollValue < 5 ) {
			return 0;
		}
		if ( scrollValue < 9 ) {
			return 1;
		}
		if ( scrollValue < 14 ) {
			return 2;
		}
		if ( scrollValue < 21 ) {
			return 3;
		}
		if ( scrollValue < 28 ) {
			return 4;
		}
		return 5;
	}

	return 0;
}

// A page's content height and scroll step: the DISPLAY page is shorter with
// one display, whose rows the multi-monitor section leaves out.
static void MainMenuSettingsScrollGeometry( idUserInterface *gui, const mainMenuSettingsScrollPage_t &page, int &contentHeight, float &stepY ) {
	contentHeight = page.contentHeight;
	stepY = page.stepY;
	if ( idStr::Icmp( page.name, "system" ) == 0 && gui->GetStateInt( "display_count", "0" ) <= 1 ) {
		contentHeight = 902;
		stepY = 19.5f;
	}
}

static bool ApplyMainMenuSettingsScrollPage( idUserInterface *gui, const mainMenuSettingsScrollPage_t &page, int requestedValue, bool requireVisiblePage ) {
    if (!NativeInput_SessionCurrent()) return false;
	if ( gui == NULL ) {
		return false;
	}

	if ( requireVisiblePage && !MainMenuWindowStateEqualsInt( gui, "desktop::curr", page.expectedPage ) ) {
		return false;
	}

	if ( requireVisiblePage && MainMenuSettingsPopupIsVisible( gui ) ) {
		return false;
	}

	if ( requireVisiblePage && !MainMenuWindowStateIsNonZero( gui, page.pageVisibleState ) ) {
		return false;
	}

	const int minValue = page.minValue;
	const int maxValue = page.maxValue;
	int scrollValue = requestedValue;
	if ( scrollValue < minValue || scrollValue > maxValue ) {
		scrollValue = gui->GetStateInt( page.scrollStateName, va( "%d", page.scrollCvar->GetInteger() ) );
	}
	scrollValue = idMath::ClampInt( minValue, maxValue, scrollValue );

	page.scrollCvar->SetInteger( scrollValue );
	gui->SetStateInt( page.scrollStateName, scrollValue );

	const bool canScroll = maxValue > minValue;
	MainMenuSetWindowVar( gui, page.thumbVisibleState, canScroll ? "1" : "0" );
	MainMenuSetWindowVar( gui, page.thumbNoEventsState, canScroll ? "0" : "1" );

	int contentHeight = page.contentHeight;
	float stepY = page.stepY;
	MainMenuSettingsScrollGeometry( gui, page, contentHeight, stepY );

	const int scrollOffset = idMath::Ftoi( stepY * static_cast<float>( scrollValue ) + 0.5f );
	const int contentY = page.baseY - scrollOffset;
	const idStr contentRect = va( "%d,%d,640,%d", page.contentX, contentY, contentHeight );
	const bool applied = MainMenuSetWindowVar( gui, page.contentRectState, contentRect.c_str() );

	// The section selector follows the scroll whenever the scroll moves (wheel,
	// drag, paging, a section jump). An unchanged scroll leaves it alone: a click
	// on the selector steps it on the press and scrolls on the release, and
	// deriving it again in between would undo the click.
	if ( page.sectionChoiceState != NULL ) {
		static const int pageCount = static_cast<int>( sizeof( MAINMENU_SETTINGS_SCROLL_PAGES ) / sizeof( MAINMENU_SETTINGS_SCROLL_PAGES[ 0 ] ) );
		static bool sectionScrollKnown[ pageCount ] = {};
		static int sectionScroll[ pageCount ] = {};
		const int pageIndex = static_cast<int>( &page - MAINMENU_SETTINGS_SCROLL_PAGES );
		if ( pageIndex < 0 || pageIndex >= pageCount || !sectionScrollKnown[ pageIndex ] || sectionScroll[ pageIndex ] != scrollValue ) {
			gui->SetStateInt( page.sectionChoiceState, MainMenuSettingsSectionChoiceForScroll( page, scrollValue ) );
			if ( pageIndex >= 0 && pageIndex < pageCount ) {
				sectionScrollKnown[ pageIndex ] = true;
				sectionScroll[ pageIndex ] = scrollValue;
			}
		}
	}

	gui->StateChanged( common->GetPresentationTime(), true );
	return applied;
}

static bool ApplyMainMenuSettingsScrollPage( idUserInterface *gui, const char *pageName, bool requireVisiblePage ) {
	const mainMenuSettingsScrollPage_t *page = FindMainMenuSettingsScrollPage( pageName );
	if ( page == NULL ) {
		return false;
	}

	const int minValue = page->minValue;
	const int maxValue = page->maxValue;
	const int requestedValue = idMath::ClampInt( minValue, maxValue, gui ? gui->GetStateInt( page->scrollStateName, va( "%d", page->scrollCvar->GetInteger() ) ) : page->scrollCvar->GetInteger() );
	return ApplyMainMenuSettingsScrollPage( gui, *page, requestedValue, requireVisiblePage );
}

static bool AdjustMainMenuPageScroll( idUserInterface *gui, const mainMenuSettingsScrollPage_t &page, int delta, bool toStart, bool toEnd ) {
	if ( gui == NULL ) {
		return false;
	}

	if ( !MainMenuWindowStateEqualsInt( gui, "desktop::curr", page.expectedPage ) ) {
		return false;
	}

	if ( MainMenuSettingsPopupIsVisible( gui ) ) {
		return false;
	}

	if ( !MainMenuWindowStateIsNonZero( gui, page.pageVisibleState ) ) {
		return false;
	}

	const int minValue = page.minValue;
	const int maxValue = page.maxValue;
	if ( maxValue <= minValue ) {
		return false;
	}

	const int current = idMath::ClampInt( minValue, maxValue, gui->GetStateInt( page.scrollStateName, va( "%d", page.scrollCvar->GetInteger() ) ) );
	int next = current;
	if ( toStart ) {
		next = minValue;
	} else if ( toEnd ) {
		next = maxValue;
	} else {
		next = idMath::ClampInt( minValue, maxValue, current + delta );
	}

	if ( next != current || page.scrollCvar->GetInteger() != next ) {
		ApplyMainMenuSettingsScrollPage( gui, page, next, true );
	}

	return true;
}

static bool HandleMainMenuSettingsScrollInput( idUserInterface *gui, int key ) {
	int delta = 0;
	bool toStart = false;
	bool toEnd = false;
	switch ( key ) {
		case K_MWHEELUP:
			delta = -1;
			break;
		case K_MWHEELDOWN:
			delta = 1;
			break;
		case K_PGUP:
		case K_KP_PGUP:
		case K_JOY1:
			delta = -MENU_SETTINGS_PAGE_SCROLL_STEP;
			break;
		case K_PGDN:
		case K_KP_PGDN:
		case K_JOY2:
			delta = MENU_SETTINGS_PAGE_SCROLL_STEP;
			break;
		case K_HOME:
		case K_KP_HOME:
			toStart = true;
			break;
		case K_END:
		case K_KP_END:
			toEnd = true;
			break;
		default:
			return false;
	}

	for ( int i = 0; i < static_cast<int>( sizeof( MAINMENU_SETTINGS_SCROLL_PAGES ) / sizeof( MAINMENU_SETTINGS_SCROLL_PAGES[ 0 ] ) ); ++i ) {
		if ( AdjustMainMenuPageScroll( gui, MAINMENU_SETTINGS_SCROLL_PAGES[ i ], delta, toStart, toEnd ) ) {
			return true;
		}
	}

	return false;
}

/*
===============
ScrollMainMenuSettingsFocusIntoView

A stock settings page scrolls under a fixed view, and the keyboard and the
controller step its focus through every row, shown or not. When the focus
moves to a row outside the view the page scrolls just far enough to show it,
with a little room so the row's plate is whole. Only a change of focus
scrolls: the wheel, the bar and the section list stay free to move the page
away from a row that keeps the focus.
===============
*/
static void ScrollMainMenuSettingsFocusIntoView( idUserInterface *gui ) {
	static idStr lastFocus;
	static const float margin = 6.0f;
	for ( int i = 0; i < static_cast<int>( sizeof( MAINMENU_SETTINGS_SCROLL_PAGES ) / sizeof( MAINMENU_SETTINGS_SCROLL_PAGES[ 0 ] ) ); ++i ) {
		const mainMenuSettingsScrollPage_t &page = MAINMENU_SETTINGS_SCROLL_PAGES[ i ];
		if ( page.maxValue <= page.minValue || !MainMenuWindowStateIsNonZero( gui, page.pageVisibleState ) ) {
			continue;
		}
		idStr content = page.contentRectState;
		const int member = content.Find( "::" );
		if ( member > 0 ) {
			content.CapLength( member );
		}
		idStr focus;
		idRectangle rect;
		float viewHeight = 0.0f;
		if ( !UI_LegacyFocusWithin( gui, content.c_str(), focus, rect, viewHeight ) || viewHeight <= 0.0f ) {
			continue;
		}
		if ( focus == lastFocus ) {
			return;
		}
		lastFocus = focus;
		int contentHeight = 0;
		float stepY = 0.0f;
		MainMenuSettingsScrollGeometry( gui, page, contentHeight, stepY );
		if ( stepY <= 0.0f ) {
			return;
		}
		// The content stands at baseY less the scroll's offset in the view.
		const int current = idMath::ClampInt( page.minValue, page.maxValue, gui->GetStateInt( page.scrollStateName, va( "%d", page.scrollCvar->GetInteger() ) ) );
		const int highest = static_cast<int>( idMath::Floor( ( rect.y - margin + page.baseY ) / stepY ) );
		const int lowest = static_cast<int>( idMath::Ceil( ( rect.y + rect.h + margin + page.baseY - viewHeight ) / stepY ) );
		int next = current;
		if ( next > highest ) {
			next = highest;
		}
		if ( next < lowest ) {
			next = lowest;
		}
		next = idMath::ClampInt( page.minValue, page.maxValue, next );
		if ( next != current ) {
			ApplyMainMenuSettingsScrollPage( gui, page, next, true );
		}
		return;
	}
	lastFocus.Clear();
}

static void SyncMainMenuSettingsScrollPages( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}

	for ( int i = 0; i < static_cast<int>( sizeof( MAINMENU_SETTINGS_SCROLL_PAGES ) / sizeof( MAINMENU_SETTINGS_SCROLL_PAGES[ 0 ] ) ); ++i ) {
		ApplyMainMenuSettingsScrollPage( gui, MAINMENU_SETTINGS_SCROLL_PAGES[ i ], idMath::INT_MIN, true );
	}
	ScrollMainMenuSettingsFocusIntoView( gui );
}

void idSessionLocal::MenuEvent( const sysEvent_t *event ) {
    if (!NativeInput_SessionCurrent()) return;
	const char	*menuCommand;

	if ( guiActive == NULL ) {
		return;
	}

	// A change between the Single Player page and its sub-page: Back reverses
	// a change going deeper, and other input waits for the other level.
	if ( RetainedSubpageEvent( event ) ) {
		return;
	}

	if ( guiRetainedHome != NULL && guiActive == guiMainMenu ) {
		// The retained screen owns input while it covers the legacy home state.
		// During a hand-off the legacy page is not presented yet; like the stock
		// choreography's input lock, nothing reaches either GUI until it is.
		if ( RetainedHomeInputBlocked() ) {
			return;
		}
		menuCommand = guiRetainedHome->HandleEvent( event, common->GetPresentationTime() );
		if ( menuCommand && menuCommand[0] ) {
			DispatchCommand( guiRetainedHome, menuCommand );
		}
		return;
	}

	if ( RetainedMultiplayerCovers() ) {
		// The summary card's chat field takes the keyboard while the player
		// types.
		if ( RetainedSummaryChatEvent( event ) ) {
			return;
		}
		// The card owns input while it covers the game's menu. A function key
		// it leaves alone runs its binding, as over the stock menu (F1 and F2
		// vote).
		idUserInterface *card = guiRetainedMultiplayer;
		menuCommand = card->HandleEvent( event, common->GetPresentationTime() );
		if ( menuCommand && menuCommand[0] ) {
			DispatchCommand( card, menuCommand );
		} else if ( event->evType == SE_KEY && event->evValue2 == 1 && event->evValue >= K_F1 && event->evValue <= K_F12 ) {
			idKeyInput::ExecKeyBinding( event->evValue );
		}
		return;
	}

	// The CONTROLS page waiting for a key takes the next one.
	if ( RetainedControlsKey( event ) ) {
		return;
	}

	if ( event->evType == SE_KEY && event->evValue2 == 1 ) {
		if ( HandleMainMenuSettingsScrollInput( guiActive, event->evValue ) ) {
			return;
		}
	}

    if (!NativeInput_SessionCurrent()) return;
	menuCommand = guiActive->HandleEvent( event, common->GetPresentationTime() );
    if (!NativeInput_SessionCurrent()) return;
    // Keep callback-owned command bytes alive before another callback can replace them.
    const idStr nativeCommand = NativeInput_Inhibited() ? (menuCommand ? menuCommand : "") : "";
    if (NativeInput_Inhibited()) menuCommand = nativeCommand.c_str();
	SyncMainMenuSettingsScrollPages( guiActive );
    if (!NativeInput_SessionCurrent()) return;

	if ( !menuCommand || !menuCommand[0] ) {
		// If the menu didn't handle the event, and it's a key down event for an F key, run the bind
		if ( event->evType == SE_KEY && event->evValue2 == 1 && event->evValue >= K_F1 && event->evValue <= K_F12 ) {
			idKeyInput::ExecKeyBinding( event->evValue );
		}
		return;
	}

	DispatchCommand( guiActive, menuCommand );
    if (!NativeInput_SessionCurrent()) return;
	SyncMainMenuSettingsScrollPages( guiActive );
}

/*
=================
idSessionLocal::GuiFrameEvents
=================
*/
void idSessionLocal::GuiFrameEvents() {
	// Multiplayer frames never run idSessionLocal::Frame, so a return from the
	// Android background is handled here too and its audio resumes. The
	// suspension is consumed once.
	HandleAppResume( false );
	// Programmatic events and deactivation actions are independent of input
	// focus, the console, and which GUI currently receives physical events.
	PumpApplicationActions();
	const char	*cmd;
	sysEvent_t  ev;
	idUserInterface	*gui;

	// A loading screen held over a join gives way to the Welcome card.
	UpdateRetainedLoadingHold();

	// stop generating move and button commands when a local console or menu is active
	// running here so SP, async networking and no game all go through it
	if ( console->Active() || guiActive || guiTest || RetainedUI_IsOpen() || guiLoadingHold != NULL ) {
		usercmdGen->InhibitUsercmd( INHIBIT_SESSION, true );
	} else {
		usercmdGen->InhibitUsercmd( INHIBIT_SESSION, false );
	}
	if ( RetainedUI_IsOpen() ) {
		RetainedUI_FrameInput();
		ClearMenuControllerRepeatState();
		return;
	}

	// Before the no-GUI return: closing the menu must retire the retained
	// home, or the pause screen would outlive a resume.
	UpdateRetainedHome();
	RetainedHomeFrameEvent();
	UpdateRetainedSubpage();
	UpdateRetainedPage();
	UpdateRetainedMultiplayerPage();
	UpdateRetainedJoinGame();
	UpdateRetainedSettingsPage();
	UpdateRetainedNotices();
	UpdateBrowserTooltip();
	PrecacheRetainedLevelshots( false );
	// Likewise the multiplayer card, before its menu's own frame.
	UpdateRetainedMultiplayer();
	RetainedMultiplayerFrameEvent();

	if ( guiTest ) {
		gui = guiTest;
	} else if ( guiActive ) {
		gui = guiActive;
	} else {
		ClearMenuControllerRepeatState();
		return;
	}

	if ( guiActive ) {
		PumpControllerMenuNavigation( this );
		PumpControllerDetailStick( this );
		if ( guiTest ) {
			gui = guiTest;
		} else if ( guiActive ) {
			gui = guiActive;
		} else {
			ClearMenuControllerRepeatState();
			return;
		}
	} else {
		ClearMenuControllerRepeatState();
	}

	memset( &ev, 0, sizeof( ev ) );

	ev.evType = SE_NONE;
	cmd = gui->HandleEvent( &ev, common->GetPresentationTime() );
	if ( cmd && cmd[0] ) {
		DispatchCommand( gui, cmd );
	}
	// Dispatch can close and release a test instance or install another menu.
	SyncMainMenuSettingsScrollPages( guiTest != NULL ? guiTest : guiActive );
}

/*
=================
idSessionLocal::BoxDialogSanityCheck
=================
*/
bool idSessionLocal::BoxDialogSanityCheck( void ) {
	if ( !common->IsInitialized() ) {
		common->DPrintf( "message box sanity check: !common->IsInitialized()\n" );
		return false;
	}
	if ( !guiMsg ) {
		return false;
	}
	if ( guiMsgRestore ) {
		common->DPrintf( "message box sanity check: recursed\n" );
		return false;
	}
	if ( cvarSystem->GetCVarInteger( "net_serverDedicated" ) ) {
		common->DPrintf( "message box sanity check: not compatible with dedicated server\n" );
		return false;
	}
	return true;
}

/*
=================
idSessionLocal::MessageBox
=================
*/
const char* idSessionLocal::MessageBox( msgBoxType_t type, const char *message, const char *title, bool wait, const char *fire_yes, const char *fire_no, bool network ) {
	
	common->DPrintf( "MessageBox: %s - %s\n", title ? title : "", message ? message : "" );
	
	if ( !BoxDialogSanityCheck() ) {
		return NULL;
	}

	guiMsg->SetStateString( "title", title ? title : "" );
	guiMsg->SetStateString( "message", message ? message : "" );
	if ( type == MSG_WAIT ) {
		guiMsg->SetStateString( "visible_msgbox", "0" );
		guiMsg->SetStateString( "visible_waitbox", "1" );
	} else {
		guiMsg->SetStateString( "visible_msgbox", "1" );
		guiMsg->SetStateString( "visible_waitbox", "0" );
	}

	guiMsg->SetStateString( "visible_entry", "0" );
	guiMsg->SetStateString( "visible_cdkey", "0" );
	switch ( type ) {
		case MSG_INFO:
			guiMsg->SetStateString( "mid", "" );
			guiMsg->SetStateString( "visible_mid", "0" );
			guiMsg->SetStateString( "visible_left", "0" );
			guiMsg->SetStateString( "visible_right", "0" );
			break;
		case MSG_OK:
			guiMsg->SetStateString( "mid", common->GetLanguageDict()->GetString( "#str_104339" ) );
			guiMsg->SetStateString( "visible_mid", "1" );
			guiMsg->SetStateString( "visible_left", "0" );
			guiMsg->SetStateString( "visible_right", "0" );
			break;
		case MSG_ABORT:
			guiMsg->SetStateString( "mid", common->GetLanguageDict()->GetString( "#str_104340" ) );
			guiMsg->SetStateString( "visible_mid", "1" );
			guiMsg->SetStateString( "visible_left", "0" );
			guiMsg->SetStateString( "visible_right", "0" );
			break;
		case MSG_OKCANCEL:
			guiMsg->SetStateString( "left", common->GetLanguageDict()->GetString( "#str_104339" ) );
			guiMsg->SetStateString( "right", common->GetLanguageDict()->GetString( "#str_104340" ) );
			guiMsg->SetStateString( "visible_mid", "0" );
			guiMsg->SetStateString( "visible_left", "1" );
			guiMsg->SetStateString( "visible_right", "1" );
			break;
		case MSG_YESNO:
			guiMsg->SetStateString( "left", common->GetLanguageDict()->GetString( "#str_104341" ) );
			guiMsg->SetStateString( "right", common->GetLanguageDict()->GetString( "#str_104342" ) );
			guiMsg->SetStateString( "visible_mid", "0" );
			guiMsg->SetStateString( "visible_left", "1" );
			guiMsg->SetStateString( "visible_right", "1" );
			break;
		case MSG_PROMPT:
			guiMsg->SetStateString( "left", common->GetLanguageDict()->GetString( "#str_104339" ) );
			guiMsg->SetStateString( "right", common->GetLanguageDict()->GetString( "#str_104340" ) );
			guiMsg->SetStateString( "visible_mid", "0" );
			guiMsg->SetStateString( "visible_left", "1" );
			guiMsg->SetStateString( "visible_right", "1" );
			guiMsg->SetStateString( "visible_entry", "1" );			
			guiMsg->HandleNamedEvent( "Prompt" );
			break;
		case MSG_WAIT:
			break;
		default:
			common->Printf( "idSessionLocal::MessageBox: unknown msg box type\n" );
	}
	msgFireBack[ 0 ] = fire_yes ? fire_yes : "";
	msgFireBack[ 1 ] = fire_no ? fire_no : "";
	guiMsgRestore = guiActive;
	openq4::NativeInputBeforeSessionChange();
	guiActive = guiMsg;
	guiMsg->SetCursor( 325, 290 );
	guiActive->Activate( true, common->GetPresentationTime() );
	msgRunning = true;
	msgRetIndex = -1;
	
	if ( wait ) {
		// play one frame ignoring events so we don't get confused by parasite button releases
		msgIgnoreButtons = true;
		common->GUIFrame( true, network );
		msgIgnoreButtons = false;
		while ( msgRunning ) {
			common->GUIFrame( true, network );
		}
		if ( msgRetIndex < 0 ) {
			// MSG_WAIT and other StopBox calls
			return NULL;
		}
		if ( type == MSG_PROMPT ) {
			if ( msgRetIndex == 0 ) {
				guiMsg->State().GetString( "str_entry", "", msgFireBack[ 0 ] );
				return msgFireBack[ 0 ].c_str();
			} else {
				return NULL;
			}
		} else {
			return msgFireBack[ msgRetIndex ].c_str();
		}
	}
	return NULL;
}

void idSessionLocal::RunTimedWaitBoxPacingTest( int durationMsec, bool network, const char *reason ) {
	const int clampedDurationMsec = idMath::ClampInt( 1, 60000, durationMsec );
	const char *snapshotReason = ( reason != NULL && reason[ 0 ] != '\0' ) ? reason : "testWaitBox";

	if ( !BoxDialogSanityCheck() ) {
		common->Warning( "RunTimedWaitBoxPacingTest: message box path is unavailable in the current session state\n" );
		return;
	}

	ResetFramePacingStats();

	MessageBox( MSG_WAIT, "", "", false, NULL, NULL, network );
	if ( !msgRunning || guiActive != guiMsg ) {
		common->Warning( "RunTimedWaitBoxPacingTest: failed to activate wait box\n" );
		return;
	}

	// Keep pumping the modal loop on the normal GUI presentation path, but do
	// not execute queued console commands from inside the automated test itself.
	common->GUIFrame( false, network );
	const int endTime = common->GetPresentationTime() + clampedDurationMsec;

	while ( msgRunning && common->GetPresentationTime() < endTime ) {
		common->GUIFrame( false, network );
	}

	if ( msgRunning ) {
		StopBox();
	}
	while ( msgRunning ) {
		common->GUIFrame( false, network );
	}

	PrintFramePacingSnapshot( snapshotReason );
}

void idSessionLocal::RunTimedMessageBoxPacingTest( int durationMsec, bool network, const char *reason ) {
	const int clampedDurationMsec = idMath::ClampInt( 1, 60000, durationMsec );
	const char *snapshotReason = ( reason != NULL && reason[ 0 ] != '\0' ) ? reason : "testMessageBox";

	if ( !BoxDialogSanityCheck() ) {
		common->Warning( "RunTimedMessageBoxPacingTest: message box path is unavailable in the current session state\n" );
		return;
	}

	ResetFramePacingStats();

	MessageBox( MSG_OK, "", "", false, NULL, NULL, network );
	if ( !msgRunning || guiActive != guiMsg ) {
		common->Warning( "RunTimedMessageBoxPacingTest: failed to activate message box\n" );
		return;
	}

	// Keep pumping the modal loop on the normal GUI presentation path, but do
	// not execute queued console commands from inside the automated test itself.
	common->GUIFrame( false, network );
	const int endTime = common->GetPresentationTime() + clampedDurationMsec;

	while ( msgRunning && common->GetPresentationTime() < endTime ) {
		common->GUIFrame( false, network );
	}

	if ( msgRunning ) {
		HandleMsgCommands( "mid" );
	}
	while ( msgRunning ) {
		common->GUIFrame( false, network );
	}

	PrintFramePacingSnapshot( snapshotReason );
}

/*
=================
idSessionLocal::DownloadProgressBox
=================
*/
void idSessionLocal::DownloadProgressBox( backgroundDownload_t *bgl, const char *title, int progress_start, int progress_end ) {
	int dlnow = 0, dltotal = 0;
	int startTime = Sys_Milliseconds();
	int lapsed;
	idStr sNow, sTotal, sBW, sETA, sMsg;

	if ( !BoxDialogSanityCheck() ) {
		return;
	}

	guiMsg->SetStateString( "visible_msgbox", "1" );
	guiMsg->SetStateString( "visible_waitbox", "0" );

	guiMsg->SetStateString( "visible_entry", "0" );
	guiMsg->SetStateString( "visible_cdkey", "0" );

	guiMsg->SetStateString( "mid", "Cancel" );
	guiMsg->SetStateString( "visible_mid", "1" );
	guiMsg->SetStateString( "visible_left", "0" );
	guiMsg->SetStateString( "visible_right", "0" );

	guiMsg->SetStateString( "title", title );
	guiMsg->SetStateString( "message", "Connecting.." );

	guiMsgRestore = guiActive;
	openq4::NativeInputBeforeSessionChange();
	guiActive = guiMsg;
	msgRunning = true;

	while ( 1 ) {
		while ( msgRunning ) {
			common->GUIFrame( true, false );
			if ( bgl->completed ) {
				openq4::NativeInputBeforeSessionChange();
				guiActive = guiMsgRestore;
				guiMsgRestore = NULL;
				return;
			} else if ( bgl->url.dltotal != dltotal || bgl->url.dlnow != dlnow ) {
				dltotal = bgl->url.dltotal;
				dlnow = bgl->url.dlnow;
				lapsed = Sys_Milliseconds() - startTime;
				sNow.BestUnit( "%.2f", dlnow, MEASURE_SIZE );
				if ( lapsed > 2000 ) {
					sBW.BestUnit( "%.1f", ( 1000.0f * dlnow ) / lapsed, MEASURE_BANDWIDTH );
				} else {
					sBW = "-- KB/s";
				}
				if ( dltotal ) {
					sTotal.BestUnit( "%.2f", dltotal, MEASURE_SIZE );
					if ( lapsed < 2000 ) {
						sMsg = sNow;
						sMsg += " / ";
						sMsg += sTotal;
					} else {
						if ( dlnow > 0 ) {
							sETA = va( "%.0f sec", ( (float)dltotal / (float)dlnow - 1.0f ) * lapsed / 1000 );
						} else {
							sETA = "-- sec";
						}
						sMsg = sNow;
						sMsg += " / ";
						sMsg += sTotal;
						sMsg += " ( ";
						sMsg += sBW;
						sMsg += " - ";
						sMsg += sETA;
						sMsg += " )";
					}
				} else {
					if ( lapsed < 2000 ) {
						sMsg = sNow;
					} else {
						sMsg = sNow;
						sMsg += " - ";
						sMsg += sBW;
					}
				}
				if ( dltotal ) {
					const int64 progressNumerator = static_cast<int64>( dlnow ) * ( progress_end - progress_start );
					guiMsg->SetStateString( "progress", va( "%d", progress_start + static_cast<int>( progressNumerator / dltotal ) ) );
				} else {
					guiMsg->SetStateString( "progress", "0" );
				}
				guiMsg->SetStateString( "message", sMsg.c_str() );
			}
		}
		// abort was used - tell the downloader and wait till final stop
		bgl->url.status = DL_ABORTING;
		guiMsg->SetStateString( "title", "Aborting.." );
		guiMsg->SetStateString( "visible_mid", "0" );
		// continue looping
		guiMsgRestore = guiActive;
		openq4::NativeInputBeforeSessionChange();
		guiActive = guiMsg;
		msgRunning = true;
	}
}

/*
=================
idSessionLocal::StopBox
=================
*/
void idSessionLocal::StopBox() {
	// A session without menus has no box: guiMsg and guiActive are both NULL.
	if ( guiMsg != NULL && guiActive == guiMsg ) {
		HandleMsgCommands( "stop" );
	}
}

/*
=================
idSessionLocal::HandleMsgCommands
=================
*/
void idSessionLocal::HandleMsgCommands( const char *menuCommand ) {
	assert( guiActive == guiMsg );
	idCmdArgs args;
	args.TokenizeString( menuCommand, false );
	if ( args.Argc() == 0 ) {
		return;
	}
	const char *cmd = args.Argv( 0 );
	// "stop" works even on first frame
	if ( idStr::Icmp( cmd, "stop" ) == 0 ) {
		// force hiding the current dialog
		openq4::NativeInputBeforeSessionChange();
		guiActive = guiMsgRestore;
		guiMsgRestore = NULL;
		msgRunning = false;
		msgRetIndex = -1;
	}
	if ( msgIgnoreButtons ) {
		common->DPrintf( "MessageBox HandleMsgCommands 1st frame ignore\n" );
		return;
	}
	if ( idStr::Icmp( cmd, "mid" ) == 0 || idStr::Icmp( cmd, "left" ) == 0 ) {
		openq4::NativeInputBeforeSessionChange();
		guiActive = guiMsgRestore;
		guiMsgRestore = NULL;
		msgRunning = false;
		msgRetIndex = 0;
		DispatchCommand( guiActive, msgFireBack[ 0 ].c_str() );
	} else if ( idStr::Icmp( cmd, "right" ) == 0 ) {
		openq4::NativeInputBeforeSessionChange();
		guiActive = guiMsgRestore;
		guiMsgRestore = NULL;
		msgRunning = false;
		msgRetIndex = 1;
		DispatchCommand( guiActive, msgFireBack[ 1 ].c_str() );
	}
}

/*
=================
idSessionLocal::HandleNoteCommands
=================
*/
#define NOTEDATFILE "C:/notenumber.dat"

void idSessionLocal::HandleNoteCommands( const char *menuCommand ) {
	openq4::NativeInputBeforeSessionChange();
	guiActive = NULL;

	if ( idStr::Icmp( menuCommand,  "note" ) == 0 && mapSpawned ) {

		idFile *file = NULL;
		for ( int tries = 0; tries < 10; tries++ ) {
			file = fileSystem->OpenExplicitFileRead( NOTEDATFILE );
			if ( file != NULL ) {
				break;
			}
			Sys_Sleep( 500 );
		}
		int noteNumber = 1000;
		if ( file ) {
			file->Read( &noteNumber, 4 );
			fileSystem->CloseFile( file );
		}

		int i;
		idStr str, noteNum, shotName, workName, fileName = "viewnotes/";
		idStrList fileList;

		const char *severity = NULL;
		const char *p = guiTakeNotes->State().GetString( "notefile" );
		if ( p == NULL || *p == '\0' ) {
			p = cvarSystem->GetCVarString( "ui_name" );
		}

		bool extended = guiTakeNotes->State().GetBool( "extended" );
		if ( extended ) {
			if ( guiTakeNotes->State().GetInt( "severity" ) == 1 ) {
				severity = "WishList_Viewnotes/";
			} else {
				severity = "MustFix_Viewnotes/";
			}
			fileName += severity;

			const idDecl *mapDecl = declManager->FindType(DECL_ENTITYDEF, mapSpawnData.serverInfo.GetString( "si_map" ), false );
			const idDeclEntityDef *mapInfo = static_cast<const idDeclEntityDef *>(mapDecl);

			if ( mapInfo ) {
				fileName += mapInfo->dict.GetString( "devname" );
			} else {
				fileName += mapSpawnData.serverInfo.GetString( "si_map" );
				fileName.StripFileExtension();
			}

			int count = guiTakeNotes->State().GetInt( "person_numsel" );
			if ( count == 0 ) {
				fileList.Append( fileName + "/Nobody" );
			} else {
				for ( i = 0; i < count; i++ ) {
					int person = guiTakeNotes->State().GetInt( va( "person_sel_%i", i ) );
					workName = fileName + "/";
					workName += guiTakeNotes->State().GetString( va( "person_item_%i", person ), "Nobody" );
					fileList.Append( workName );
				}
			}
		} else {
			fileName += "maps/";
			fileName += mapSpawnData.serverInfo.GetString( "si_map" );
			fileName.StripFileExtension();
			fileList.Append( fileName );
		}

		bool bCon = cvarSystem->GetCVarBool( "con_noPrint" );
		cvarSystem->SetCVarBool( "con_noPrint", true );
		for ( i = 0; i < fileList.Num(); i++ ) {
			workName = fileList[i];
			workName += "/";
			workName += p;
		//	R_ScreenshotFilename( workNote, workName, shotName );

			noteNum = shotName;
			noteNum.StripPath();
			noteNum.StripFileExtension();

			if ( severity && *severity ) {
				workName = severity;
				workName += "viewNotes";
			}

			idStr note = guiTakeNotes->State().GetString( "note" );
			workName.Replace( "\\", "\\\\" );
			workName.Replace( "\"", "\\\"" );
			noteNum.Replace( "\\", "\\\\" );
			noteNum.Replace( "\"", "\\\"" );
			note.Replace( "\\", "\\\\" );
			note.Replace( "\"", "\\\"" );

			str = "recordViewNotes \"";
			str += workName;
			str += "\" \"";
			str += noteNum;
			str += "\" \"";
			str += note;
			str += "\"\n";
			
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, str );
			cmdSystem->ExecuteCommandBuffer();

			UpdateScreen();
			//renderSystem->TakeScreenshot( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight(), shotName, 1, NULL );
		}
		noteNumber++;

		for ( int tries = 0; tries < 10; tries++ ) {
			file = fileSystem->OpenExplicitFileWrite( "p:/viewnotes/notenumber.dat" );
			if ( file != NULL ) {
				break;
			}
			Sys_Sleep( 500 );
		}
		if ( file ) {
			file->Write( &noteNumber, 4 );
			fileSystem->CloseFile( file );
		}

		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "closeViewNotes\n" );
		cvarSystem->SetCVarBool( "con_noPrint", bCon );
	}
}

/*
===============================================================================

	Retained screens (ui_retained)

	The title and single-player pause screens are retained documents that
	cover the legacy main menu while it rests at its home state. The legacy
	menu stays active underneath and keeps every page: choosing a page runs
	the legacy home button's own action, and the retained screen covers the
	legacy departure until the page appears. Back on a legacy page returns
	to the retained screen as soon as the page starts to leave.

===============================================================================
*/

#ifndef ID_DEDICATED
static const char *const RETAINED_TITLE_GUI = "guis/menu/title.q4ui";
static const char *const RETAINED_PAUSE_GUI = "guis/menu/pause.q4ui";
static const char *const RETAINED_PAUSE_STROGG_GUI = "guis/menu/pause_strogg.q4ui";
static const char *const RETAINED_LOADING_GUI = "guis/loading/loading.q4ui";
static const char *const RETAINED_SINGLEPLAYER_GUI = "guis/menu/singleplayer.q4ui";
static const char *const RETAINED_CAMPAIGNS_GUI = "guis/menu/campaigns.q4ui";
static const char *const RETAINED_SKILL_GUI = "guis/menu/campaign_skill.q4ui";
static const char *const RETAINED_CHAPTERS_GUI = "guis/menu/chapters.q4ui";
// The Load Game page, which LOAD GAME on the title and the Marine pause hands
// off to as to a stock page. Its list holds this many saves at a time and
// pages through more (ROWS in tools/ui/retained_saves.py); a save without a
// picture shows the stock page's generic one.
static const char *const RETAINED_LOADGAME_GUI = "guis/menu/loadgame.q4ui";
static const char *const RETAINED_SAVEGAME_GUI = "guis/menu/savegame.q4ui";
static const char *const RETAINED_RESTART_GUI = "guis/menu/restartlevel.q4ui";
// The same three pages in the Strogg family, which the Strogg pause hands off
// to; they load once a level's player is Strogg.
static const char *const RETAINED_LOADGAME_STROGG_GUI = "guis/menu/loadgame_strogg.q4ui";
static const char *const RETAINED_SAVEGAME_STROGG_GUI = "guis/menu/savegame_strogg.q4ui";
static const char *const RETAINED_RESTART_STROGG_GUI = "guis/menu/restartlevel_strogg.q4ui";
// The Demos page, which the title's DEMOS hands off to as LOAD GAME does to
// its page. Its list holds this many demos at a time (ROWS in
// tools/ui/retained_demos.py); demos.filter picks one of the legacy
// browser's filters, 0 showing them all.
static const char *const RETAINED_DEMOS_GUI = "guis/menu/demos.q4ui";
// The restart screen single player shows after the player dies (the stock
// guis/restart.gui).
static const char *const RETAINED_RESTARTMENU_GUI = "guis/menu/restartmenu.q4ui";
// The Credits page, which the title's CREDITS hands off to.
static const char *const RETAINED_CREDITS_GUI = "guis/menu/credits.q4ui";
// The Multiplayer page, which the title's MULTIPLAYER hands off to. Its
// sections open the stock Multiplayer pages (desktop::dest) once it has left
// as the stock Multiplayer page leaves, 500 ms after the choice.
static const char *const RETAINED_MPPAGE_GUI = "guis/menu/multiplayer.q4ui";
static const int RETAINED_MPPAGE_LEAVE_MSEC = 500;
// The retained SETTINGS page (tools/ui/retained_settings_page.py); its
// sections are RETAINED_SETTINGS_SECTIONS.
static const char *const RETAINED_SETTINGSPAGE_GUI = "guis/menu/settings.q4ui";
static const int RETAINED_SETTINGSPAGE_LEAVE_MSEC = 500;
// Multiplayer's Create Server sub-page. Its map list holds this many maps at a
// time (MAP_ROWS in tools/ui/retained_create_server.py), and its game types
// are the stock choice's, in its order (mp_create_val4's values).
static const char *const RETAINED_CREATE_SERVER_GUI = "guis/menu/createserver.q4ui";
static const int RETAINED_CREATE_MAP_ROWS = 64;
static const char *const RETAINED_CREATE_GAMETYPES[] = {
	"DM", "Tourney", "Team DM", "CTF", "Arena CTF", "DeadZone", "Duel", "Clan Arena", "Freeze Tag", "Red Rover", "One Flag CTF",
	"Arena One Flag CTF"
};
static const int RETAINED_CREATE_GAMETYPE_COUNT = static_cast<int>( sizeof( RETAINED_CREATE_GAMETYPES ) / sizeof( RETAINED_CREATE_GAMETYPES[0] ) );
// Each setting the page offers: the verb its control asks with
// ("<verb> <value>"), the page state the session publishes it in
// (create.<state>), its CVar and the range the session takes; a Boolean
// setting publishes a Boolean. tools/ui/retained_create_server.py offers
// the same.
typedef struct retainedCreateSetting_s {
	const char *	verb;
	const char *	state;
	const char *	cvar;
	int				minimum;
	int				maximum;
	bool			boolean;
} retainedCreateSetting_t;
static const retainedCreateSetting_t RETAINED_CREATE_SETTINGS[] = {
	{ "createLan",			"lan",			"net_menulanserver",		0, 1,	false },
	{ "createDedicated",	"dedicated",	"net_serverMenuDedicated",	0, 1,	true },
	{ "createAutoBalance",	"autobalance",	"si_autobalance",			0, 1,	true },
	{ "createFragLimit",	"fraglimit",	"si_fragLimit",				0, 999,	false },
	{ "createCaptureLimit",	"capturelimit",	"si_captureLimit",			1, 999,	false },
	{ "createControlTime",	"controltime",	"si_controlTime",			1, 999,	false },
	{ "createTimeLimit",	"timelimit",	"si_timeLimit",				0, 60,	false },
	{ "createMaxPlayers",	"maxplayers",	"si_maxPlayers",			1, 16,	false },
	{ "createMinPlayers",	"minplayers",	"si_minPlayers",			1, 16,	false },
	{ "createVoting",		"voting",		"si_allowVoting",			0, 1,	true },
	{ "createTeamDamage",	"teamdamage",	"si_teamDamage",			0, 1,	true },
	{ "createTourneyLimit",	"tourneylimit",	"si_tourneyLimit",			1, 999,	false },
	{ "createUsePass",		"usepass",		"si_usePass",				0, 1,	true },
	{ "createServerMods",	"servermods",	"net_serverAllowServerMod",	0, 1,	true },
	{ "createWeaponStay",	"weaponstay",	"si_weaponStay",			0, 1,	true },
	{ "createPure",			"pure",			"si_pure",					0, 1,	true },
	{ "createRate",			"rate",			"gui_configServerRate",		0, 5,	false },
	{ "createReady",		"ready",		"si_useReady",				0, 1,	true },
	{ "createSpectators",	"spectators",	"si_spectators",			0, 1,	true },
	{ "createWarmup",		"warmup",		"si_warmup",				0, 1,	true },
	{ "createVoice",		"voice",		"si_voiceChat",				0, 1,	true },
	{ "createBuying",		"buying",		"si_isBuyingEnabled",		0, 1,	true },
	{ "createBots",			"bots",			"bot_minPlayers",			0, 16,	false },
	{ "createBotSkill",		"botskill",		"bot_skill",				1, 5,	false },
};
static const int RETAINED_CREATE_SETTING_COUNT = static_cast<int>( sizeof( RETAINED_CREATE_SETTINGS ) / sizeof( RETAINED_CREATE_SETTINGS[0] ) );
// The page's text fields: the verb a field asks with once its text is in its
// page state, that state, the CVar the session writes and the most bytes it
// takes (the stock fields' maxchars). The passwords are masked fields.
typedef struct retainedCreateText_s {
	const char *	verb;
	const char *	state;
	const char *	cvar;
	int				limit;
} retainedCreateText_t;
static const retainedCreateText_t RETAINED_CREATE_TEXTS[] = {
	{ "createName",			"create.name",			"si_name",							25 },
	{ "createMapCycle",		"create.mapcycle",		"g_mapCycle",						200 },
	{ "createPassword",		"create.password",		"g_password",						16 },
	{ "createRconPassword",	"create.rconpassword",	"net_serverRemoteConsolePassword",	16 },
};
static const int RETAINED_CREATE_TEXT_COUNT = static_cast<int>( sizeof( RETAINED_CREATE_TEXTS ) / sizeof( RETAINED_CREATE_TEXTS[0] ) );
// PLAYER BAN LIST's rows (BAN_ROWS in tools/ui/retained_create_server.py):
// the page lists the first this many bans; removing one brings up the next.
static const int RETAINED_CREATE_BAN_ROWS = 32;
// Multiplayer's Join Game sub-page, the server browser. Its list holds this
// many servers at a time (JOIN_ROWS in tools/ui/retained_join_game.py), and
// each column heading asks to sort by its column (the sort the page's
// join.sort names, COLUMNS in the generator).
static const char *const RETAINED_JOIN_GAME_GUI = "guis/menu/joingame.q4ui";
static const int RETAINED_JOIN_ROWS = 64;
// The details card's lines: the stock tooltip's player list holds 11 of its
// 12 u items, and the scan describes 7 settings (DETAIL_PLAYERS and
// DETAIL_SETTINGS in the generator).
static const int RETAINED_JOIN_DETAIL_PLAYERS = 11;
static const int RETAINED_JOIN_DETAIL_SETTINGS = 7;
typedef struct retainedJoinSort_s {
	const char *	verb;
	serverSort_t	sort;
} retainedJoinSort_t;
static const retainedJoinSort_t RETAINED_JOIN_SORTS[] = {
	{ "joinSortFavorite",	SORT_FAVORITE },
	{ "joinSortLocked",		SORT_PASSWORD },
	{ "joinSortDedicated",	SORT_DEDICATED },
	{ "joinSortName",		SORT_SERVERNAME },
	{ "joinSortPing",		SORT_PING },
	{ "joinSortRepeater",	SORT_REPEATER },
	{ "joinSortPlayers",	SORT_PLAYERS },
	{ "joinSortGameType",	SORT_GAMETYPE },
	{ "joinSortMap",		SORT_MAP },
};
static const int RETAINED_JOIN_SORT_COUNT = static_cast<int>( sizeof( RETAINED_JOIN_SORTS ) / sizeof( RETAINED_JOIN_SORTS[0] ) );
// Single Player's Arena sub-page, the Arena campaign's browser, and the
// campaign command each of its verbs runs (idArenaCampaign::HandleGuiCommand).
static const char *const RETAINED_ARENA_GUI = "guis/menu/arena.q4ui";
static const char *const RETAINED_ARENA_VERBS[][2] = {
	{ "arenaTier0", "arenaSelectTier 0" },
	{ "arenaTier1", "arenaSelectTier 1" },
	{ "arenaTier2", "arenaSelectTier 2" },
	{ "arenaTier3", "arenaSelectTier 3" },
	{ "arenaTier4", "arenaSelectTier 4" },
	{ "arenaMatch0", "arenaSelectMatch 0" },
	{ "arenaMatch1", "arenaSelectMatch 1" },
	{ "arenaMatch2", "arenaSelectMatch 2" },
	{ "arenaMatch3", "arenaSelectMatch 3" },
	{ "arenaTierPrevious", "arenaStepTier -1" },
	{ "arenaTierNext", "arenaStepTier 1" },
	{ "arenaEasier", "arenaDifficulty -1" },
	{ "arenaHarder", "arenaDifficulty 1" },
	{ "arenaStart", "arenaStart" },
	{ "arenaReset", "arenaReset" },
	{ "arenaResetConfirm", "arenaResetConfirm" },
	{ "arenaResetCancel", "arenaResetCancel" },
	{ "arenaRetry", "arenaRetry" },
	{ "arenaDismiss", "arenaDismissResult" },
};
static const int RETAINED_ARENA_VERB_COUNT = sizeof( RETAINED_ARENA_VERBS ) / sizeof( RETAINED_ARENA_VERBS[0] );
static const int RETAINED_DEMO_ROWS = 64;
// The title's MODS dialog lists this many mods (MOD_ROWS in
// tools/ui/build_retained_screens.py).
static const int RETAINED_MOD_ROWS = 32;
static const char *const RETAINED_DEMO_FILTERS[] = { "all", "mvd", "render", "legacy", "incomplete" };
static const int RETAINED_DEMO_FILTER_COUNT = sizeof( RETAINED_DEMO_FILTERS ) / sizeof( RETAINED_DEMO_FILTERS[0] );
static const int RETAINED_LOAD_ROWS = 64;
static const char *const RETAINED_LOAD_GENERIC_SHOT = "gfx/guis/loadscreens/generic";
// Multiplayer levelshots precache on idle front-end frames: none until the
// title has presented for this long, then as many a frame as fit this long
// (at least one).
static const int RETAINED_LEVELSHOT_DELAY_MSEC = 1000;
static const int RETAINED_LEVELSHOT_FRAME_MSEC = 4;
// The stock choreography shows a page 550 ms after its activation and a
// home-state pop-up at once (specification section 8); the retained screen
// covers the legacy departure for that long before the page takes over.
static const int RETAINED_PAGE_HANDOFF_MSEC = 550;
static const int RETAINED_POPUP_HANDOFF_MSEC = 200;
// Returning to the game releases the paused view's soft focus over 250 ms,
// as the modal backdrop does (section 4).
static const int RETAINED_RELEASE_MSEC = 250;
// Single Player and its Campaign sub-page: going deeper the page plays its
// half and the sub-page presents at 350 ms; Back plays the sub-page's half and
// the page returns at 300 ms (section 8). With reduced motion the hand-over
// is at once, and the arrival's 80 ms fade is the change. Back while going
// deeper reverses the change from where it stands, over 150-300 ms in 50 ms
// steps (subpageReverse<N>), by the distance the band has stepped since it
// started at 50 ms.
static const int RETAINED_SUBPAGE_DEEPER_MSEC = 350;
static const int RETAINED_SUBPAGE_BACK_MSEC = 300;
static const int RETAINED_SUBPAGE_STEP_START_MSEC = 50;
static const int RETAINED_SUBPAGE_STEP_MSEC = 300;

typedef enum {
	RETAINED_HANDOFF_PAGE,		// a page: the bands dock and the label carries into its title slot
	RETAINED_HANDOFF_POPUP,		// a pop-up over the home state
	RETAINED_HANDOFF_IN_PLACE	// an action the home stays over, whose answer is a message box
} retainedHandoffKind_t;

typedef struct retainedHandoff_s {
	const char *			request;
	const char *			window;		// the legacy home button whose action opens the page
	retainedHandoffKind_t	kind;
} retainedHandoff_t;

static const retainedHandoff_t RETAINED_HANDOFFS[] = {
	{ "singlePlayer",	"main_b_newgame",		RETAINED_HANDOFF_PAGE },
	{ "loadGame",		"main_b_loadgame",		RETAINED_HANDOFF_PAGE },
	{ "saveGame",		"main_b_savegame",		RETAINED_HANDOFF_PAGE },
	{ "multiplayer",	"main_b_multiplayer",	RETAINED_HANDOFF_PAGE },
	{ "settings",		"main_b_settings",		RETAINED_HANDOFF_PAGE },
	{ "credits",		"main_b_credits",		RETAINED_HANDOFF_PAGE },
	{ "restartLevel",	"main_b_difficulty",	RETAINED_HANDOFF_PAGE },
	{ "mods",			"main_b_mods",			RETAINED_HANDOFF_POPUP },
	// The update check opens no pop-up: its answer comes as a message box
	// over the home, up to 2 s later (idAsyncClient::HandleDownloads).
	{ "updates",		"main_b_updates",		RETAINED_HANDOFF_IN_PLACE },
};

// Legacy pop-ups drawn over the home state keep the framing bands at home.
static bool Session_RetainedHomePopup( idUserInterface *gui ) {
	static const int popups[] = { 5, 6, 8, 27 };
	for ( int i = 0; i < static_cast<int>( sizeof( popups ) / sizeof( popups[0] ) ); ++i ) {
		if ( MainMenuWindowStateEqualsInt( gui, "desktop::curr", popups[i] ) ) {
			return true;
		}
	}
	return false;
}

// The retained title and pause screens cover the legacy main menu and hand
// its pages off to it, so they present only over a menu with every page they
// offer. Returns the first page this menu lacks: a mod's own main menu
// without them presents as the mod made it.
static const char *Session_RetainedHomeMissingPage( idUserInterface *menu ) {
	for ( int i = 0; i < static_cast<int>( sizeof( RETAINED_HANDOFFS ) / sizeof( RETAINED_HANDOFFS[0] ) ); ++i ) {
		if ( !UI_LegacyWindowExists( menu, RETAINED_HANDOFFS[i].window ) ) {
			return RETAINED_HANDOFFS[i].window;
		}
	}
	return NULL;
}

// The retained Multiplayer page opens the legacy menu's own Multiplayer pages
// and their Back returns it, through these events of the legacy menu: a main
// menu without them (a mod's copy of an older one) keeps its own page.
static bool Session_RetainedMultiplayerRoutes( idUserInterface *menu ) {
	static const char *const events[] = { "openMultiplayerPage", "leaveMultiplayerPage", "openMultiplayerStock" };
	for ( int i = 0; i < static_cast<int>( sizeof( events ) / sizeof( events[0] ) ); ++i ) {
		if ( !UI_LegacyNamedEventExists( menu, events[i] ) ) {
			return false;
		}
	}
	return true;
}

// A save as the retained screens name it. The menu label a save writes is
// "^:name\t^0level" (idSessionLocal::SaveGame): its name and level, colors
// removed; a save without one shows its slot.
static void Session_RetainedSaveLabel( const idStr &slot, const sessionMenuSaveDescription_t &description, bool described,
		idStr &name, idStr &location ) {
	const idStr text = described && description.description.Length() > 0 ? description.description : slot;
	const int tab = text.Find( '\t' );
	name = tab >= 0 ? text.Left( tab ) : text;
	location = tab >= 0 ? text.Right( text.Length() - tab - 1 ) : idStr();
	name.RemoveColors();
	location.RemoveColors();
	name.StripTrailingWhitespace();
	location.StripTrailingWhitespace();
}

// A save's picture: an autosave names its level's loadscreen; any other save
// keeps its own screenshot beside it, as the stock Load Game page shows it,
// reread since a newer save may have replaced it. Without either, `fallback`.
static idStr Session_RetainedSaveShot( const idStr &slot, const sessionMenuSaveDescription_t &description, bool described,
		const char *fallback ) {
	if ( described && description.screenshot.Length() > 0 ) {
		return UI_RetainedImageSource( description.screenshot.c_str() ) ? description.screenshot : idStr( fallback );
	}
	if ( !sessLocal.FinishPendingSave( true ) ) return fallback;
	const idStr preview = va( "savegames/%s.tga", slot.c_str() );
	if ( UI_RetainedImageSource( preview.c_str() ) && fileSystem->ReadFile( preview.c_str(), NULL, NULL ) > 0 ) {
		UI_RetainedReloadImage( preview.c_str() );
		return preview;
	}
	return fallback;
}

// When a save was written: the stamp's date and time, split at its tab.
static void Session_RetainedSaveStamp( ID_TIME_T timeStamp, idStr &date, idStr &time ) {
	const idStr stamp = Sys_TimeStampToStr( timeStamp );
	const int tab = stamp.Find( '\t' );
	date = tab >= 0 ? stamp.Left( tab ) : stamp;
	time = tab >= 0 ? stamp.Right( stamp.Length() - tab - 1 ) : idStr();
}

// A save's difficulty and the run's play time as the retained screens name
// them, each empty when the save records none.
static void Session_RetainedSaveDetails( const idStr &slot, idStr &skill, idStr &played ) {
	sessionMenuSaveDetails_t details;
	Session_MenuReadSaveDetails( slot, details );
	skill = details.skill >= 0 ? Session_SkillName( details.skill ) : "";
	played = details.playSeconds >= 0 ?
		va( common->GetLanguageDict()->GetString( "#str_231525" ), Session_RetainedPlayTime( details.playSeconds ).c_str() ) : "";
}

// The title screen's CONTINUE resumes the newest save and describes it: its
// name, then its level, the difficulty and the run's play time (section
// 13.7). A save from an earlier build records neither and says when it was
// written instead. Without a picture, none shows.
static void Session_PublishRetainedTitleState( idUserInterface *gui, idSessionLocal *session ) {
	idStrList fileList;
	idList<fileTIME_T> fileTimes;
	session->GetSaveGameList( fileList, fileTimes );
	const bool hasSave = fileTimes.Num() > 0;
	idStr name, location, date, time, shot, detail;
	if ( hasSave ) {
		const idStr &slot = fileList[ fileTimes[0].index ];
		sessionMenuSaveDescription_t description;
		const bool described = Session_MenuReadSaveDescription( slot, description );
		Session_RetainedSaveLabel( slot, description, described, name, location );
		Session_RetainedSaveStamp( fileTimes[0].timeStamp, date, time );
		shot = Session_RetainedSaveShot( slot, description, described, "" );
		idStr skill, played;
		Session_RetainedSaveDetails( slot, skill, played );
		idStrList parts;
		parts.Append( location );
		parts.Append( skill );
		parts.Append( played );
		if ( skill.IsEmpty() && played.IsEmpty() ) {
			parts.Append( va( common->GetLanguageDict()->GetString( "#str_231535" ), date.c_str(), time.c_str() ) );
		}
		for ( int i = 0; i < parts.Num(); i++ ) {
			detail = Session_RetainedList( detail.c_str(), parts[i].c_str() );
		}
	}
	gui->SetStateBool( "menu_continue", hasSave );
	gui->SetStateString( "menu_continue_title", name.c_str() );
	gui->SetStateString( "menu_continue_detail", detail.c_str() );
	gui->SetStateString( "menu_continue_shot", shot.c_str() );
	gui->StateChanged( common->GetPresentationTime() );
}

// A page section's summary picture (section 13.7), read now as a precache
// rather than a late load; an empty name shows none. A levelshot still
// waiting for the idle frames (map's, or the picture itself) is read here
// instead, so each is read once.
static void Session_PublishSummaryShot( idSessionLocal *session, idUserInterface *page, const char *state, const idStr &shot,
	const char *map = NULL ) {
	if ( shot.Length() > 0 ) {
		const bool insideLoad = declManager->GetInsideLoad();
		declManager->SetInsideLoad( true );
		for ( int i = session->retainedLevelshots.Num() - 1; i >= 0; i-- ) {
			const idSessionLocal::retainedLevelshot_t &queued = session->retainedLevelshots[ i ];
			if ( queued.map ? map != NULL && !idStr::Icmp( queued.name.c_str(), map ) : !idStr::Icmp( queued.name.c_str(), shot.c_str() ) ) {
				if ( queued.material ) {
					declManager->FindMaterial( queued.name.c_str() );
				}
				session->retainedLevelshots.RemoveIndex( i );
			}
		}
		UI_RetainedPrecacheImage( shot.c_str() );
		declManager->SetInsideLoad( insideLoad );
	}
	page->SetStateString( state, shot.c_str() );
}

// Single Player's summaries: CAMPAIGN over the newest save's levelshot (the
// one CONTINUE shows), ARENA over the match its browser would start.
static void Session_PublishSinglePlayerSummaries( idUserInterface *page, idSessionLocal *session ) {
	idStrList fileList;
	idList<fileTIME_T> fileTimes;
	session->GetSaveGameList( fileList, fileTimes );
	idStr campaign;
	if ( fileTimes.Num() > 0 ) {
		const idStr &slot = fileList[ fileTimes[0].index ];
		sessionMenuSaveDescription_t description;
		const bool described = Session_MenuReadSaveDescription( slot, description );
		campaign = Session_RetainedSaveShot( slot, description, described, "" );
	}
	Session_PublishSummaryShot( session, page, "summary.campaign.shot", campaign );
	Session_PublishSummaryShot( session, page, "summary.arena.shot", arenaCampaign.SelectedLevelshot() );
}

// Campaign's summaries: NEW GAME over the active campaign's first level,
// CHAPTERS over the furthest it has reached. The page shows them while the
// campaign chosen there is the active one; the other's are not mounted.
static void Session_PublishCampaignSummaries( idUserInterface *page, idSessionLocal *session ) {
	idStr first, furthest;
	session->CampaignSummaryShots( first, furthest );
	Session_PublishSummaryShot( session, page, "summary.newgame.shot", first );
	Session_PublishSummaryShot( session, page, "summary.chapters.shot", furthest );
}

// Multiplayer's summaries: CREATE SERVER over the map it would host.
static void Session_PublishMultiplayerSummaries( idUserInterface *page, idSessionLocal *session ) {
	const char *map = cvarSystem->GetCVarString( "si_map" );
	Session_PublishSummaryShot( session, page, "summary.create.shot", map[0] != '\0' ? session->RetainedPauseShot( map ) : idStr(), map );
}

/*
===============
idSessionLocal::FindRetainedGui

A screen's retained document, or NULL while its stock GUI presents instead.
A document that no search path provides falls back quietly, and so does a
home screen over a legacy main menu that lacks a page it hands off to. A
document that is installed but cannot load is reported. A fallback holds
for the rest of the session, and ui_retainedStatus lists it.
===============
*/
idUserInterface *idSessionLocal::FindRetainedGui( const char *path, bool shared, bool home ) {
	if ( retainedStock.FindIndex( path ) >= 0 ) {
		return NULL;
	}
	idUserInterface *gui = NULL;
	const char *missingPage = home ? Session_RetainedHomeMissingPage( guiMainMenu ) : NULL;
	if ( missingPage != NULL ) {
		common->DPrintf( "retained UI: the main menu has no '%s' page, so it presents its own screen instead of '%s'\n", missingPage, path );
	} else if ( fileSystem->ReadFile( path, NULL, NULL ) <= 0 ) {
		common->DPrintf( "retained UI: '%s' is not installed; the stock screen presents instead\n", path );
	} else if ( ( gui = uiManager->FindGui( path, true, !shared, shared ) ) == NULL ) {
		common->Warning( "retained UI: '%s' could not be loaded; the stock screen stays in use", path );
	} else if ( UI_RetainedViewFailed( gui ) ) {
		// A shared view that failed to come back after a renderer or language
		// change draws nothing: the pages found here each time (Single Player,
		// Campaign, the campaign difficulty page) hand back to the stock screen as
		// the others do.
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", path );
		gui = NULL;
	}
	if ( gui == NULL ) {
		retainedStock.Append( path );
	}
	return gui;
}
#endif


void idSessionLocal::OpenCampaignSelector( bool campaigns, bool fromArena, bool fromChapters ) {
#ifndef ID_DEDICATED
    // The stock selectors stand in when the retained one is off, missing or
    // cannot load.
    idUserInterface *gui = Session_RetainedScreensEnabled() ?
        FindRetainedGui( campaigns ? RETAINED_CAMPAIGNS_GUI : RETAINED_SINGLEPLAYER_GUI, true, false ) : NULL;
    if ( gui == NULL && !campaigns ) {
        arenaCampaign.OpenSelector();
        return;
    }
    const bool stock = gui == NULL;
    if ( gui == NULL ) gui = uiManager->FindGui( STOCK_CAMPAIGN_GUI, true, false, true );
    if ( gui == NULL ) return;
    if ( campaigns ) {
        if ( stock ) gui->SetStateInt( "campaign_view", 0 );
        const idCampaignContentInfo info = fileSystem->GetAwakeningContentInfo();
        gui->SetStateBool( "awakening_ready", info.ready );
        gui->SetStateBool( "awakening_present", info.present );
        // The campaign being played is the chosen one; the retained page's
        // activation focuses NEW GAME, or CHAPTERS coming back from it.
        const bool awakening = !idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" );
        gui->SetStateBool( "awakening_chosen", awakening );
        if ( !stock ) {
            gui->SetStateBool( "campaign.awakening", awakening && info.ready );
            gui->SetStateBool( "from_chapters", fromChapters );
            Session_PublishCampaignSummaries( gui, this );
        }
        gui->StateChanged( common->GetPresentationTime() );
    } else if ( gui != NULL ) {
        // Back from Arena returns onto ARENA; the activation reads it.
        gui->SetStateBool( "from_arena", fromArena );
        if ( !stock ) Session_PublishSinglePlayerSummaries( gui, this );
        gui->StateChanged( common->GetPresentationTime() );
    }
    SetGUI( gui, NULL );
    gui->StateChanged( common->GetPresentationTime() );
#endif
}

#ifndef ID_DEDICATED
// Whether entering a campaign's game directory reloads the engine first.
static bool Session_CampaignNeedsReload( const char *gameDir ) {
    return idStr::Icmp( fileSystem->GetActiveGameDir(), gameDir ) ||
           idStr::Icmp( cvarSystem->GetCVarString( "fs_game" ), gameDir ) ||
           cvarSystem->GetCVarString( "fs_game_base" )[0] ||
           idStr::Icmp( cvarSystem->GetCVarString( "com_activeGameModule" ), "game_sp" );
}
#endif

void idSessionLocal::SelectCampaign( const char *campaign, bool start, bool chapters ) {
#ifndef ID_DEDICATED
    const bool awakening = !idStr::Icmp( campaign, "awakening" );
    const bool arena = !idStr::Icmp( campaign, "arena" );
    if ( !awakening && !arena && idStr::Icmp( campaign, "quake4" ) ) return;
    if ( awakening ) {
        const idCampaignContentInfo info = fileSystem->GetAwakeningContentInfo();
        if ( !info.ready ) {
            common->Warning( "Awakening campaign is unavailable; missing %s", info.missing.c_str() );
            OpenCampaignSelector( true );
            return;
        }
    }
    const char *gameDir = awakening ? "q4xbase" : OPENQ4_GAMEDIR;
    // Consume one-shot command-line overrides before a full engine restart.
    common->StartupVariable( "si_gameType", true );
    if ( Session_CampaignNeedsReload( gameDir ) ) {
        cvarSystem->SetCVarString( "fs_game", gameDir );
        cvarSystem->SetCVarString( "fs_game_base", "" );
        cvarSystem->SetCVarString( "si_gameType", "singleplayer" );
        cvarSystem->SetCVarString( "com_nextGameModule", "game_sp" );
        idCmdArgs continuation;
        continuation.AppendArg( "campaignSelect" );
        continuation.AppendArg( campaign );
        if ( start ) continuation.AppendArg( "start" );
        else if ( chapters ) continuation.AppendArg( "chapters" );
        cmdSystem->SetupReloadEngineMenu( continuation );
        return;
    }
    common->Printf( "CAMPAIGN_SELECTED id=%s gameDir=%s module=%s\n", campaign, gameDir,
        cvarSystem->GetCVarString( "com_activeGameModule" ) );
    if ( start && !arena ) {
        StartNewGame( awakening ? "game/m01_stranarus_trench1" : "game/airdefense1" );
        return;
    }
    if ( !arena && ( chapters ? OpenRetainedCampaignChapters() : OpenRetainedCampaignSkill() ) ) return;
    // The stock page holds its chapters too, a CHAPTERS press away.
    idUserInterface *page = !arena ? uiManager->FindGui( STOCK_CAMPAIGN_GUI, true, false, true ) : NULL;
    if ( page != NULL ) {
        // The stock campaign page, as the retained one: the campaign's
        // difficulty, turbo, START and its chapters.
        page->SetStateInt( "campaign_view", 1 );
        Session_SetCampaignPageSkill( page, cvarSystem->GetCVarInteger( "g_skill" ), true );
        page->SetStateBool( "turbo", false );
        page->SetStateString( "campaign_title", common->GetLanguageDict()->GetString( awakening ? "#str_230040" : "#str_230039" ) );
        page->SetStateString( "campaign_crumb", va( "%s  /  %s", common->GetLanguageDict()->GetString( "#str_42000" ),
            common->GetLanguageDict()->GetString( "#str_230038" ) ) );
        PublishCampaignChapters( page );
        SetGUI( page, NULL );
        page->StateChanged( common->GetPresentationTime() );
        return;
    }
    StartMenu();
    if ( arena ) {
        arenaCampaign.OpenBrowser();
    } else {
        // Continue through the existing difficulty/options screen. Its start
        // action chooses the active campaign's fixed initial map below.
        arenaCampaign.HandleGuiCommand( "arenaMissionOpen" );
    }
#endif
}

// The CHAPTERS page: the active campaign's levels, the chosen one focused.
bool idSessionLocal::OpenRetainedCampaignChapters() {
#ifdef ID_DEDICATED
    return false;
#else
    if ( !Session_RetainedScreensEnabled() ) return false;
    idUserInterface *gui = FindRetainedGui( RETAINED_CHAPTERS_GUI, true, false );
    if ( !gui ) return false;
    PublishCampaignChapters( gui );
    SetGUI( gui, NULL );
    gui->StateChanged( common->GetPresentationTime() );
    return true;
#endif
}

bool idSessionLocal::OpenRetainedCampaignSkill() {
#ifdef ID_DEDICATED
    return false;
#else
    if ( !Session_RetainedScreensEnabled() ) return false;
    idUserInterface *gui = FindRetainedGui( RETAINED_SKILL_GUI, true, false );
    if ( !gui ) return false;
    Session_SetCampaignPageSkill( gui, cvarSystem->GetCVarInteger( "g_skill" ), false );
    gui->SetStateBool( "turbo", false );
    gui->SetStateString( "campaign_title", Session_AwakeningPageActive() ? "#str_230040" : "#str_230039" );
    SetGUI( gui, NULL );
    gui->StateChanged( common->GetPresentationTime() );
    return true;
#endif
}

void idSessionLocal::BeginRetainedCampaignSkill( idUserInterface *from, const char *campaign, bool back, bool chapters ) {
#ifndef ID_DEDICATED
    if ( !from || retainedSubpageFrom ) return;
    if ( !back && idStr::Icmp( campaign, "quake4" ) && idStr::Icmp( campaign, "awakening" ) ) return;
    if ( FindRetainedGui( back ? RETAINED_CAMPAIGNS_GUI : chapters ? RETAINED_CHAPTERS_GUI : RETAINED_SKILL_GUI, true, false ) == NULL ||
         cvarSystem->GetCVarBool( "ui_retainedReducedMotion" ) ) {
        if ( back ) OpenCampaignSelector( true, false, chapters ); else SelectCampaign( campaign, false, chapters );
        return;
    }
    retainedDetailTarget = back ? ( chapters ? 4 : 2 ) : ( chapters ? 3 : 1 );
    retainedDetailCampaign = campaign;
    from->HandleNamedEvent( back ? "detailLeave" : chapters ? "detailEnterChapters" :
        !idStr::Icmp( campaign, "awakening" ) ? "detailEnterAwakening" : "detailEnterQuake4" );
    PumpApplicationActions( from );
    retainedSubpageFrom = from;
    retainedSubpageDeeper = !back;
    retainedSubpageBegan = common->GetPresentationTime();
    retainedSubpageUntil = retainedSubpageBegan + ( back ? RETAINED_SUBPAGE_BACK_MSEC : RETAINED_SUBPAGE_DEEPER_MSEC );
    if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) common->Printf( "RETAINED_DETAIL begin level=%d campaign=%s\n", back ? 3 : 4, campaign );
#endif
}

/*
===============
idSessionLocal::MenuInputGUI

The GUI that HandleMenuEvent delivers input to. While the retained home screen
covers the main menu, or the retained multiplayer card covers the game's menu,
that screen owns input and keeps its own cursor, so platform cursor routing has
to follow it rather than guiActive.
===============
*/
idUserInterface *idSessionLocal::MenuInputGUI() const {
	if ( guiTest != NULL ) {
		return guiTest;
	}
	if ( guiRetainedHome != NULL && guiActive != NULL && guiActive == guiMainMenu ) {
		return guiRetainedHome;
	}
	if ( RetainedMultiplayerCovers() && guiRetainedMultiplayer != NULL ) {
		return guiRetainedMultiplayer;
	}
	return guiActive;
}

idUserInterface *Session_MenuInputGUI( void ) {
	return sessLocal.MenuInputGUI();
}

bool idSessionLocal::RetainedHomeInputBlocked() const {
	return guiRetainedHome != NULL && common->GetPresentationTime() < retainedHandoffUntil;
}

void idSessionLocal::UpdateRetainedHome() {
#ifndef ID_DEDICATED
	idUserInterface *want = NULL;
	bool returning = false;
	bool fromPopup = false;
	const int now = common->GetPresentationTime();
	// A release ends after its 250 ms, or as soon as anything but the game
	// takes the screen again.
	if ( guiRetainedReleasing != NULL && ( now >= retainedReleaseUntil || guiActive != NULL || !mapSpawned ) ) {
		guiRetainedReleasing = NULL;
		retainedReleaseUntil = 0;
	}
	// A stock message box over the legacy home (the update check's answer, a
	// failed connection): the retained home stays behind it, still and
	// without input, where the stock menu stood, and takes the screen back as
	// it was once the box closes.
	if ( guiRetainedHome != NULL && guiActive != NULL && guiActive == guiMsg && guiMsgRestore == guiMainMenu ) {
		if ( !retainedHomeUnderMessage ) {
			retainedHomeUnderMessage = true;
			guiRetainedHome->Activate( false, now );
		}
		return;
	}
	const bool context = Session_RetainedScreensEnabled() && guiMainMenu != NULL && guiActive == guiMainMenu &&
		guiTest == NULL && !RetainedUI_IsOpen() && ( !mapSpawned || !IsMultiplayer() );
	if ( context && !MainMenuWindowStateIsNonZero( guiMainMenu, "desktop::video_check" ) ) {
		const bool atHome = MainMenuWindowStateEqualsInt( guiMainMenu, "desktop::curr", 0 ) &&
			MainMenuWindowStateEqualsInt( guiMainMenu, "desktop::active", 0 );
		// Back on a legacy page: the page is leaving for the home state.
		returning = !MainMenuWindowStateEqualsInt( guiMainMenu, "desktop::curr", 0 ) &&
			MainMenuWindowStateIsNonZero( guiMainMenu, "desktop::active" ) &&
			MainMenuWindowStateEqualsInt( guiMainMenu, "desktop::dest", 0 );
		fromPopup = returning && Session_RetainedHomePopup( guiMainMenu );
		const bool handoff = guiRetainedHome != NULL && now < retainedHandoffUntil;
		if ( atHome || returning || handoff ) {
			// The paused game's screen or the title; each loads on first use.
			// After Kane's stroggification the pause takes the Strogg family,
			// and the Marine pause stands in when that cannot present.
			if ( mapSpawned && RetainedPauseIsStrogg() ) {
				want = RetainedHomeDocument( guiRetainedPauseStrogg, RETAINED_PAUSE_STROGG_GUI );
			}
			if ( want == NULL ) {
				want = mapSpawned ? RetainedHomeDocument( guiRetainedPause, RETAINED_PAUSE_GUI ) :
					RetainedHomeDocument( guiRetainedTitle, RETAINED_TITLE_GUI );
			}
		}
	}
	if ( mapSpawned ) {
		retainedJoinReturn = false;
	} else if ( retainedJoinReturn && want != NULL && want == guiRetainedTitle && guiRetainedHome == NULL ) {
		// Back at the menu after a join from Join Game that failed or was
		// aborted: the list it was joined from, as the stock browser kept it,
		// rather than the title.
		retainedJoinReturn = false;
		OpenRetainedJoinGame();
		if ( guiActive != guiMainMenu ) {
			retainedHomeReturning = false;
			return;
		}
	}
	if ( want == guiRetainedHome ) {
		if ( retainedHomeUnderMessage && want != NULL ) {
			// The message box closed over the home it covered.
			want->Activate( true, now );
		}
		retainedHomeUnderMessage = false;
		retainedHomeReturning = returning;
		return;
	}
	retainedHomeUnderMessage = false;
	idUserInterface *previous = guiRetainedHome;
	guiRetainedHome = NULL;
	retainedHandoffUntil = 0;
	if ( previous != NULL ) {
		previous->Activate( false, now );
		// Returning to the game closes the pause screen at once (section 8),
		// but the softened view releases over 250 ms as the modal backdrop
		// does (section 4): the closed screen keeps drawing only that, over
		// the running game, without input or cursor.
		if ( want == NULL && guiActive == NULL && mapSpawned &&
				( previous == guiRetainedPause || previous == guiRetainedPauseStrogg ) ) {
			previous->HandleNamedEvent( "release" );
			guiRetainedReleasing = previous;
			retainedReleaseUntil = now + RETAINED_RELEASE_MSEC;
		}
		PumpApplicationActions( previous );
	}
	if ( want != NULL ) {
		if ( want == guiRetainedPause || want == guiRetainedPauseStrogg ) {
			PublishRetainedPauseState( want );
		} else {
			Session_PublishRetainedTitleState( want, this );
		}
		guiRetainedHome = want;
		want->Activate( true, now );
		// A page leaving for home, stock or retained, brings the bands back
		// from their page dock; opening the menu, or closing a home pop-up,
		// only reveals the content.
		want->HandleNamedEvent( ( returning && !fromPopup ) || retainedPageReturn ? "returnHome" : "open" );
		PumpApplicationActions( want );
	}
	retainedHomeReturning = returning;
#endif
}

idUserInterface *idSessionLocal::RetainedHomeDocument( idUserInterface *&home, const char *path ) {
#ifndef ID_DEDICATED
	if ( home == NULL ) {
		home = FindRetainedGui( path, false, true );
	} else if ( UI_RetainedViewFailed( home ) ) {
		// A view that failed to come back after a renderer or language
		// change draws nothing; the stock home screen takes over.
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", path );
		retainedStock.AddUnique( path );
		home = NULL;
	}
	return home;
#else
	return NULL;
#endif
}

// The game answers once a level, through the main-menu command channel, on
// the legacy menu's state; a game module that does not answer keeps the
// Marine pause. The first Strogg answer starts parsing the Strogg pause's
// pages on the job workers, so a page that pause hands off to rarely waits.
bool idSessionLocal::RetainedPauseIsStrogg() {
	if ( retainedPauseStrogg < 0 && game != NULL && mapSpawned && guiMainMenu != NULL ) {
		guiMainMenu->SetStateBool( "pause_strogg", false );
		game->HandleMainMenuCommands( "retainedPauseFamily", guiMainMenu );
		retainedPauseStrogg = guiMainMenu->State().GetBool( "pause_strogg" ) ? 1 : 0;
#ifndef ID_DEDICATED
		if ( retainedPauseStrogg > 0 ) {
			const char *const pages[] = {
				guiRetainedLoadStrogg == NULL ? RETAINED_LOADGAME_STROGG_GUI : NULL,
				guiRetainedSaveStrogg == NULL ? RETAINED_SAVEGAME_STROGG_GUI : NULL,
				guiRetainedRestartStrogg == NULL ? RETAINED_RESTART_STROGG_GUI : NULL
			};
			UI_RetainedPrefetchDocuments( pages, static_cast<int>( sizeof( pages ) / sizeof( pages[0] ) ) );
		}
#endif
	}
	return retainedPauseStrogg > 0;
}

void idSessionLocal::RetainedHomeFrameEvent() {
	if ( guiRetainedHome == NULL || guiActive != guiMainMenu ) {
		return;
	}
	// The VR pointer moves the cursor by its offset from the active GUI's
	// cursor, but this screen takes the input and keeps its own: hold the
	// covered menu's cursor on it.
	if ( VR_HasInputFocus() ) {
		guiMainMenu->SetCursor( guiRetainedHome->CursorX(), guiRetainedHome->CursorY() );
	}
	sysEvent_t ev;
	memset( &ev, 0, sizeof( ev ) );
	ev.evType = SE_NONE;
	idUserInterface *gui = guiRetainedHome;
	// Depth (section 13.8): the pointer's place in the view, -1..1 on each
	// axis, for the title's backdrop layers to lean away from. A deflected
	// look stick leads while it is held; menu focus moves with the other.
	float pointerX = idMath::ClampFloat( -1.0f, 1.0f, ( gui->CursorX() - 320.0f ) / 320.0f );
	float pointerY = idMath::ClampFloat( -1.0f, 1.0f, ( gui->CursorY() - 240.0f ) / 240.0f );
	int lookX = 0;
	int lookY = 0;
	if ( Sys_GetJoystickAxisState( AXIS_SIDE, lookX ) && Sys_GetJoystickAxisState( AXIS_FORWARD, lookY ) &&
			( MenuControllerAbs( lookX ) > 16 || MenuControllerAbs( lookY ) > 16 ) ) {
		pointerX = idMath::ClampFloat( -1.0f, 1.0f, lookX / 127.0f );
		pointerY = idMath::ClampFloat( -1.0f, 1.0f, lookY / 127.0f );
	}
	if ( idMath::Fabs( pointerX - retainedPointerX ) > 0.005f || idMath::Fabs( pointerY - retainedPointerY ) > 0.005f ) {
		retainedPointerX = pointerX;
		retainedPointerY = pointerY;
		gui->SetStateFloat( "pointer_x", pointerX );
		gui->SetStateFloat( "pointer_y", pointerY );
		gui->StateChanged( common->GetPresentationTime() );
	}
	const char *cmd = gui->HandleEvent( &ev, common->GetPresentationTime() );
	if ( cmd && cmd[0] ) {
		DispatchCommand( gui, cmd );
	}
}

void idSessionLocal::DrawRetainedHome( int presentationTime ) {
	// The legacy menu keeps its timelines, music and hand-off choreography
	// running unseen; only the retained screen reaches the output.
	UI_RunTimeEvents( guiMainMenu, presentationTime );
	if ( ( guiRetainedHome == guiRetainedPause || guiRetainedHome == guiRetainedPauseStrogg ) && mapSpawned &&
		!com_skipGameDraw.GetBool() && GetLocalClientNum() >= 0 ) {
		// The paused view stays behind the pause screen, under its scrim.
		if ( !game->Draw( GetLocalClientNum(), true ) ) {
			renderSystem->SetColor( colorBlack );
			renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
		}
	}
	guiRetainedHome->Redraw( presentationTime );
}

// The Single Player page and its Campaign and Arena sub-pages, the
// Multiplayer page and its Create Server, Join Game and Demos sub-pages, and the
// SETTINGS page and its CONTROLS, GAME OPTIONS, DISPLAY and AUDIO sub-pages, are
// documents of their own (see RETAINED_SUBPAGE_DEEPER_MSEC). When the other
// document cannot present, its stock screen opens at once.
void idSessionLocal::BeginRetainedSubpage( idUserInterface *gui, bool deeper, retainedSubpage_t subpage ) {
#ifndef ID_DEDICATED
	retainedDetailTarget = 0;
	HandleMainMenuCommands( "play main_menu_selection" );
	const bool create = subpage == RETAINED_SUBPAGE_CREATE_SERVER, join = subpage == RETAINED_SUBPAGE_JOIN_GAME;
	const bool demos = subpage == RETAINED_SUBPAGE_DEMOS;
	const bool arena = subpage == RETAINED_SUBPAGE_ARENA;
	const retainedSettingsSubpage_t *settings = Session_RetainedSettingsSubpage( subpage );
	const char *other = settings != NULL ? ( deeper ? settings->gui : RETAINED_SETTINGSPAGE_GUI ) :
		create || join || demos ? ( !deeper ? RETAINED_MPPAGE_GUI : create ? RETAINED_CREATE_SERVER_GUI :
			join ? RETAINED_JOIN_GAME_GUI : RETAINED_DEMOS_GUI ) :
		!deeper ? RETAINED_SINGLEPLAYER_GUI : arena ? RETAINED_ARENA_GUI : RETAINED_CAMPAIGNS_GUI;
	const bool system = subpage == RETAINED_SUBPAGE_SETTINGS_SYSTEM && deeper;
	if ( ( system ? !RetainedSystemAvailable() : FindRetainedGui( other, true, false ) == NULL ) ||
			cvarSystem->GetCVarBool( "ui_retainedReducedMotion" ) ) {
		// The stock screen, or reduced motion: the other level at once.
		PresentRetainedSubpage( deeper, subpage );
		return;
	}
	gui->HandleNamedEvent( !deeper ? "subpageLeave" : settings != NULL ? settings->enter : create ? "subpageEnterCreate" :
		join ? "subpageEnterJoin" : demos ? "subpageEnterDemos" : arena ? "subpageEnterArena" : "subpageEnter" );
	PumpApplicationActions( gui );
	retainedSubpageFrom = gui;
	retainedSubpageDeeper = deeper;
	retainedSubpageKind = subpage;
	retainedSubpageBegan = common->GetPresentationTime();
	retainedSubpageUntil = retainedSubpageBegan + ( deeper ? RETAINED_SUBPAGE_DEEPER_MSEC : RETAINED_SUBPAGE_BACK_MSEC );
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_SUBPAGE begin deeper=%d handover=%d\n", deeper ? 1 : 0, retainedSubpageUntil - common->GetPresentationTime() );
	}
#endif
}

void idSessionLocal::UpdateRetainedSubpage() {
#ifndef ID_DEDICATED
	if ( retainedSubpageFrom == NULL ) {
		return;
	}
	// Anything else taking the screen abandons the change.
	if ( guiActive != retainedSubpageFrom ) {
		retainedSubpageFrom = NULL;
		retainedDetailTarget = 0;
		return;
	}
	if ( common->GetPresentationTime() < retainedSubpageUntil ) {
		return;
	}
	const bool deeper = retainedSubpageDeeper;
	retainedSubpageFrom = NULL;
	if ( retainedDetailTarget != 0 ) {
		const int target = retainedDetailTarget;
		const idStr campaign = retainedDetailCampaign;
		retainedDetailTarget = 0; retainedDetailCampaign.Clear();
		const bool parent = target == 2 || target == 4;
		if ( parent ) OpenCampaignSelector( true, false, target == 4 ); else SelectCampaign( campaign, false, target == 3 );
		if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) common->Printf( "RETAINED_DETAIL handover level=%d gui=%s\n", parent ? 3 : 4, guiActive ? guiActive->Name() : "-" );
		return;
	}
	// The arriving document's activation fades its backing in; the page
	// coming back also fades its plates in.
	PresentRetainedSubpage( deeper, retainedSubpageKind );
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_SUBPAGE handover deeper=%d gui=%s\n", deeper ? 1 : 0, guiActive != NULL ? guiActive->Name() : "-" );
	}
#endif
}

// The other level presents: Campaign (or the stock selector), the Arena
// campaign's browser (the Arena page where it can), Create Server, Join Game,
// Demos, a settings sub-page, or the page coming back: Single Player, onto
// ARENA from Arena, Multiplayer onto CREATE SERVER, JOIN GAME or DEMOS, or
// SETTINGS onto the sub-page's section.
void idSessionLocal::PresentRetainedSubpage( bool deeper, retainedSubpage_t subpage ) {
#ifndef ID_DEDICATED
	const bool arena = subpage == RETAINED_SUBPAGE_ARENA;
	retainedSubpageKind = RETAINED_SUBPAGE_CAMPAIGN;
	if ( const retainedSettingsSubpage_t *settings = Session_RetainedSettingsSubpage( subpage ) ) {
		const bool controls = subpage == RETAINED_SUBPAGE_SETTINGS_CONTROLS, game = subpage == RETAINED_SUBPAGE_SETTINGS_GAME;
		const bool system = subpage == RETAINED_SUBPAGE_SETTINGS_SYSTEM;
		if ( deeper ) {
			// The sub-page over the SETTINGS page, which its Back climbs to. One
			// that cannot open now leaves the stock screen to show its panel.
			if ( !( controls ? OpenRetainedControls() : game ? OpenRetainedGame() : system ? OpenSystemSettings() : OpenRetainedAudio() ) &&
					guiRetainedSettingsPage != NULL && guiActive == guiRetainedSettingsPage ) {
				for ( int i = 0; i < RETAINED_SETTINGS_SECTION_COUNT; i++ ) {
					if ( !idStr::Icmp( RETAINED_SETTINGS_SECTIONS[ i ].section, settings->section ) ) {
						retainedSettingsDest = RETAINED_SETTINGS_SECTIONS[ i ].dest;
						retainedSettingsAt = common->GetPresentationTime();
					}
				}
			}
			return;
		}
		// Back: the SETTINGS page returns onto the section, or where it cannot,
		// the screen that opened the sub-page returns as it was.
		if ( system ) {
			// Presenting another screen closes the SYSTEM page (SetGUI), its
			// draft already settled (UI_RetainedSettingsCanReturn).
			idUserInterface *screen = guiSystemParent;
			const HandleGuiCommand_t handler = guiSystemParentHandle;
			if ( OpenRetainedSettingsPage( settings->section ) ) {
				guiActive->HandleNamedEvent( "subpageReturn" );
				PumpApplicationActions( guiActive );
			} else if ( screen != NULL ) {
				SetGUI( screen, handler );
			}
			return;
		}
		idUserInterface *&parent = controls ? retainedControlsParent : game ? retainedGameParent : retainedAudioParent;
		HandleGuiCommand_t &handle = controls ? retainedControlsParentHandle : game ? retainedGameParentHandle : retainedAudioParentHandle;
		idUserInterface *screen = parent;
		const HandleGuiCommand_t handler = handle;
		parent = NULL;
		handle = NULL;
		if ( controls ) {
			retainedControlsCapture = -1;
		}
		if ( OpenRetainedSettingsPage( settings->section ) ) {
			guiActive->HandleNamedEvent( "subpageReturn" );
			PumpApplicationActions( guiActive );
		} else if ( screen != NULL ) {
			SetGUI( screen, handler );
		}
		return;
	}
	if ( subpage == RETAINED_SUBPAGE_CREATE_SERVER || subpage == RETAINED_SUBPAGE_JOIN_GAME || subpage == RETAINED_SUBPAGE_DEMOS ) {
		const bool create = subpage == RETAINED_SUBPAGE_CREATE_SERVER, demos = subpage == RETAINED_SUBPAGE_DEMOS;
		if ( deeper && create ) {
			OpenRetainedCreateServer();
		} else if ( deeper && demos ) {
			OpenRetainedDemos();
		} else if ( deeper ) {
			OpenRetainedJoinGame();
		} else if ( OpenRetainedMultiplayerPage( create ? "create" : demos ? "demos" : "join" ) ) {
			guiActive->HandleNamedEvent( "subpageReturn" );
			PumpApplicationActions( guiActive );
		} else if ( guiMainMenu != NULL ) {
			// The Multiplayer page cannot present (its view stopped drawing,
			// or the gate went off): Back finds the stock Multiplayer page,
			// as Back from a stock page does, rather than staying put.
			SetGUI( guiMainMenu, NULL );
			guiMainMenu->HandleNamedEvent( "openMultiplayerStock" );
		}
		return;
	}
	if ( deeper && arena ) {
		common->Printf( "CAMPAIGN_SELECTED id=arena gameDir=%s module=%s\n", OPENQ4_GAMEDIR,
			cvarSystem->GetCVarString( "com_activeGameModule" ) );
		arenaCampaign.OpenBrowser();
		return;
	}
	OpenCampaignSelector( deeper, !deeper && arena );
	if ( !deeper && guiActive != NULL && !idStr::Icmp( guiActive->Name(), RETAINED_SINGLEPLAYER_GUI ) ) {
		guiActive->HandleNamedEvent( "subpageReturn" );
		PumpApplicationActions( guiActive );
	}
#endif
}

// Input while a change is under way. A key's release still reaches the
// leaving document, so a key held into the change retires normally, but no
// command it returns runs mid-change. Back while going deeper reverses the
// change from where it stands (section 8, MOT-016) and gives the page its
// input back; anything else waits for the hand-over.
bool idSessionLocal::RetainedSubpageEvent( const sysEvent_t *event ) {
#ifndef ID_DEDICATED
	if ( retainedSubpageFrom == NULL || guiActive != retainedSubpageFrom ) {
		return false;
	}
	if ( event->evType == SE_KEY && event->evValue2 == 0 ) {
		retainedSubpageFrom->HandleEvent( event, common->GetPresentationTime() );
		return true;
	}
	const bool back = event->evType == SE_KEY && ( event->evValue == K_ESCAPE || idKeyInput::MenuControllerKey( event->evValue ) == K_JOY4 ||
		event->evValue == K_JOY7 || event->evValue == K_JOY8 );
	if ( back && retainedSubpageDeeper ) {
		const int stepped = idMath::ClampInt( 0, RETAINED_SUBPAGE_STEP_MSEC,
			common->GetPresentationTime() - retainedSubpageBegan - RETAINED_SUBPAGE_STEP_START_MSEC );
		const int duration = idMath::ClampInt( 150, 300, ( stepped + 49 ) / 50 * 50 );
		idUserInterface *page = retainedSubpageFrom;
		retainedSubpageFrom = NULL;
		page->HandleNamedEvent( va( retainedDetailTarget ? "detailReverse%d" : "subpageReverse%d", duration ) );
		retainedDetailTarget = 0; retainedDetailCampaign.Clear();
		PumpApplicationActions( page );
		if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
			common->Printf( "RETAINED_SUBPAGE reverse duration=%d\n", duration );
		}
	}
	return true;
#else
	return false;
#endif
}

#ifndef ID_DEDICATED
// Why a save could not be loaded, including restore failure after map teardown.
static const char *Session_LoadFailureText( int failure ) {
	switch ( failure ) {
		case idSessionLocal::LOADGAME_FAILURE_MAP:		return "#str_231102";
		case idSessionLocal::LOADGAME_FAILURE_DAMAGED:	return "#str_231103";
		case idSessionLocal::LOADGAME_FAILURE_PLATFORM:	return "#str_231192";
		case idSessionLocal::LOADGAME_FAILURE_RESTORE:	return "#str_231190";
		default:										return "#str_231104";
	}
}
#endif

void idSessionLocal::ShowSaveRestoreFailure( const char *saveName ) {
#ifndef ID_DEDICATED
	StartMenu();
	// StartMenu activates the stock host. Refresh the retained home now, before
	// choosing the load-page route, rather than waiting for the next frame.
	UpdateRetainedHome();
	const char *notice = Session_LoadFailureText( LOADGAME_FAILURE_RESTORE );
	if ( guiRetainedHome != NULL && BeginRetainedPage( "loadGame" ) && retainedPagePending != NULL ) {
		idStr slot = saveName;
		ScrubSaveGameFileName( slot );
		slot.StripFileExtension();
		const int selected = retainedLoadSlots.FindIndex( slot );
		if ( selected >= 0 ) {
			retainedLoadSelected = selected;
			retainedLoadFirst = selected / RETAINED_LOAD_ROWS * RETAINED_LOAD_ROWS;
			PublishRetainedLoadRows( retainedPagePending );
		}
		PublishRetainedLoadSave( retainedPagePending, notice );
	} else {
		MessageBox( MSG_OK, common->GetLocalizedString( notice ), common->GetLocalizedString( "#str_231191" ), false );
	}
#endif
}

void idSessionLocal::HandleRetainedSessionRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	// The retained AUDIO and GAME OPTIONS pages' verbs, while they present.
	if ( request != NULL && gui != NULL && gui == guiRetainedAudio ) {
		if ( gui == guiActive ) {
			HandleRetainedAudioRequest( gui, request );
		}
		return;
	}
	if ( request != NULL && gui != NULL && gui == guiRetainedGame ) {
		if ( gui == guiActive ) {
			HandleRetainedGameRequest( gui, request );
		}
		return;
	}
	if ( request != NULL && gui != NULL && gui == guiRetainedControls ) {
		if ( gui == guiActive ) {
			HandleRetainedControlsRequest( gui, request );
		}
		return;
	}
	// A notice's action (section 13.13), on whichever menu shows the stack.
	const int noticeSlot = request == NULL ? -1 : !idStr::Icmp( request, "noticeAction0" ) ? 0 :
		!idStr::Icmp( request, "noticeAction1" ) ? 1 : !idStr::Icmp( request, "noticeAction2" ) ? 2 : -1;
	if ( noticeSlot >= 0 ) {
		HandleRetainedNoticeAction( gui, noticeSlot );
		return;
	}
	if ( request != NULL && !idStr::Icmp( request, "menuResume" ) ) {
		// The Menu button on one of the pause's pages closes the menus and
		// resumes a single-player game (section 13.5): the page returns the
		// pause, whose RESUME closes it.
		if ( gui != NULL && gui == guiActive && RetainedPageActive() && mapSpawned && !IsMultiplayer() ) {
			ReturnRetainedPage();
			if ( guiRetainedHome != NULL ) {
				HandleRetainedSessionRequest( guiRetainedHome, "resume" );
			}
		}
		return;
	}
	if ( gui && request && gui == guiActive &&
		 ( !idStr::Icmp( gui->Name(), RETAINED_SKILL_GUI ) || !idStr::Icmp( gui->Name(), RETAINED_CHAPTERS_GUI ) ) ) {
		if ( retainedSubpageFrom ) return;
		Session_HandleCampaignPageRequest( *this, gui, request, true );
		return;
	}
	if ( gui != NULL && gui == guiRetainedMultiplayer ) {
		// The multiplayer card's verbs, and only its verbs, reach the game.
		HandleRetainedMultiplayerRequest( gui, request );
		return;
	}
	if ( request != NULL && IsRetainedLoadPage( gui ) ) {
		// The Load Game page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedLoadRequest( gui, request );
		}
		return;
	}
	if ( request != NULL && IsRetainedSavePage( gui ) ) {
		// The Save Game page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedSaveRequest( gui, request );
		}
		return;
	}
	if ( request != NULL && IsRetainedRestartPage( gui ) ) {
		// The Restart Level page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedRestartRequest( gui, request );
		}
		return;
	}
	if ( gui != NULL && request != NULL && gui == guiRetainedCredits ) {
		// The Credits page's one verb, Back, while it presents.
		if ( gui == guiActive && !idStr::Icmp( request, "creditsBack" ) ) {
			ReturnRetainedPage();
		}
		return;
	}
	if ( gui != NULL && request != NULL && gui == guiRetainedDemos ) {
		// The Demos page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedDemosRequest( gui, request );
		}
		return;
	}
	if ( gui != NULL && request != NULL && gui == guiRetainedMpPage ) {
		// The Multiplayer page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedMultiplayerPageRequest( gui, request );
		}
		return;
	}
	if ( gui != NULL && request != NULL && gui == guiRetainedSettingsPage ) {
		// The SETTINGS page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedSettingsPageRequest( gui, request );
		}
		return;
	}
	if ( gui != NULL && request != NULL && gui == guiRetainedCreate ) {
		// The Create Server sub-page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedCreateServerRequest( gui, request );
		}
		return;
	}
	if ( gui != NULL && request != NULL && gui == guiRetainedJoin ) {
		// The Join Game sub-page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedJoinGameRequest( gui, request );
		}
		return;
	}
	if ( gui != NULL && request != NULL && gui == guiRetainedArena ) {
		// The Arena page's verbs, while it presents.
		if ( gui == guiActive ) {
			HandleRetainedArenaRequest( gui, request );
		}
		return;
	}
    if ( gui && request && gui == guiActive && gui == guiRetainedRestartMenu ) {
        // The restart screen's plates run the stock screen's own commands.
        HandleMainMenuCommands( "play main_menu_selection" );
        if ( !idStr::Icmp( request, "deathRestart" ) ) HandleRestartMenuCommands( "restart" );
        else if ( !idStr::Icmp( request, "deathLoad" ) ) {
            // LOAD GAME leaves for the title and opens the retained Load Game
            // page there, as the title's own LOAD GAME does; the stock page
            // where that cannot present.
            HandleRestartMenuCommands( "disconnect mainmenu" );
            UpdateRetainedHome();
            if ( !BeginRetainedPage( "loadGame" ) && guiMainMenu != NULL ) {
                guiMainMenu->HandleNamedEvent( "fromRestart_toLoad" );
            }
        }
        else if ( !idStr::Icmp( request, "deathMenu" ) ) HandleRestartMenuCommands( "disconnect mainmenu" );
        else if ( !idStr::Icmp( request, "quit" ) ) common->Quit();
        return;
    }
    if ( gui && request && gui == guiActive &&
         ( !idStr::Icmp( gui->Name(), RETAINED_SINGLEPLAYER_GUI ) || !idStr::Icmp( gui->Name(), RETAINED_CAMPAIGNS_GUI ) ) ) {
        // A change of level in progress takes no further request.
        if ( retainedSubpageFrom != NULL ) return;
        if ( !idStr::Icmp( request, "campaigns" ) ) BeginRetainedSubpage( gui, true );
        else if ( !idStr::Icmp( request, "campaignNew" ) || !idStr::Icmp( request, "campaignChapters" ) ) {
            // The chosen campaign's difficulty (NEW GAME) or its levels (CHAPTERS).
            BeginRetainedCampaignSkill( gui, gui->State().GetBool( "campaign.awakening" ) ? "awakening" : "quake4", false,
                !idStr::Icmp( request, "campaignChapters" ) );
        }
        else if ( !idStr::Icmp( request, "campaignArena" ) ) {
            // ARENA leads deeper to the Arena sub-page when the campaign needs
            // no engine reload into its game directory first; else it is
            // selected as before, and the browser presents after the reload.
            if ( !idStr::Icmp( gui->Name(), RETAINED_SINGLEPLAYER_GUI ) && !Session_CampaignNeedsReload( OPENQ4_GAMEDIR ) &&
                 RetainedArenaPage() != NULL ) {
                BeginRetainedSubpage( gui, true, RETAINED_SUBPAGE_ARENA );
            } else {
                SelectCampaign( "arena" );
            }
        }
        else if ( !idStr::Icmp( request, "campaignBack" ) ) BeginRetainedSubpage( gui, false );
        else if ( !idStr::Icmp( request, "campaignHome" ) ) ReturnRetainedSinglePlayerHome();
        return;
    }
	if ( gui == NULL || request == NULL || gui != guiRetainedHome || guiActive != guiMainMenu ) {
		return;
	}
	const int now = common->GetPresentationTime();
	if ( !idStr::Icmp( request, "resume" ) ) {
		// Single-player RETURN TO GAME closes at once (section 8).
		if ( mapSpawned ) {
			ExitMenu();
		}
		return;
	}
	if ( !idStr::Icmp( request, "quit" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		ExitMenu();
		common->Quit();
		return;
	}
	if ( !idStr::Icmp( request, "quitToMenu" ) ) {
		if ( mapSpawned ) {
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
		}
		return;
	}
	if ( !idStr::Icmp( request, "continue" ) ) {
		idStrList fileList;
		idList<fileTIME_T> fileTimes;
		GetSaveGameList( fileList, fileTimes );
		if ( fileTimes.Num() > 0 ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			const idStr slot = fileList[ fileTimes[0].index ];
			// Preflight refusals keep the title; deeper failures rebuild it.
			// Both routes open the Load Game page with the reason.
			if ( !LoadGame( slot.c_str() ) && guiRetainedHome != NULL && guiActive == guiMainMenu && BeginRetainedPage( "loadGame" ) &&
					retainedPagePending != NULL ) {
				PublishRetainedLoadSave( retainedPagePending, Session_LoadFailureText( loadGameFailure ) );
			}
		}
		return;
	}
	// MODS opens the title's own dialog with the installed mods; the pause has
	// no MODS. A row selects its mod, and LOAD MOD restarts the engine into it
	// as the stock LOAD MOD does.
	if ( !idStr::Icmp( request, "mods" ) && guiRetainedHome == guiRetainedTitle ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		PublishRetainedMods( guiRetainedHome );
		guiRetainedHome->HandleNamedEvent( "modsModalShow" );
		return;
	}
	if ( !idStr::Icmp( request, "modsSelect" ) ) {
		SelectRetainedMod( guiRetainedHome, guiRetainedHome->State().GetInt( "mods.pick", "-1" ) );
		return;
	}
	if ( !idStr::Icmp( request, "modsLoad" ) ) {
		if ( retainedModSelected >= 0 && retainedModSelected < modsList.Num() ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			cvarSystem->SetCVarString( "fs_game", modsList[ retainedModSelected ].directory.c_str() );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "reloadEngine menu\n" );
		}
		return;
	}
	// LOAD GAME, SAVE GAME, RESTART LEVEL, CREDITS, MULTIPLAYER and SETTINGS
	// open their retained pages where those can present; else the stock pages
	// below.
	if ( ( !idStr::Icmp( request, "loadGame" ) || !idStr::Icmp( request, "saveGame" ) || !idStr::Icmp( request, "restartLevel" ) ||
			!idStr::Icmp( request, "credits" ) || !idStr::Icmp( request, "multiplayer" ) ||
			!idStr::Icmp( request, "settings" ) ) && BeginRetainedPage( request ) ) {
		return;
	}
	for ( int i = 0; i < static_cast<int>( sizeof( RETAINED_HANDOFFS ) / sizeof( RETAINED_HANDOFFS[0] ) ); ++i ) {
		const retainedHandoff_t &handoff = RETAINED_HANDOFFS[i];
		if ( idStr::Icmp( request, handoff.request ) ) {
			continue;
		}
		idStr command;
		if ( !UI_RunLegacyWindowAction( guiMainMenu, handoff.window, false, command ) ) {
			common->Warning( "retained UI: '%s' is unavailable from the current menu", request );
			return;
		}
		const bool popup = handoff.kind == RETAINED_HANDOFF_POPUP;
		if ( handoff.kind != RETAINED_HANDOFF_IN_PLACE ) {
			retainedHandoffUntil = now + ( popup ? RETAINED_POPUP_HANDOFF_MSEC : RETAINED_PAGE_HANDOFF_MSEC );
			if ( !popup ) {
				// The chosen label travels into the page's title slot (section 13.8).
				guiRetainedHome->HandleNamedEvent( va( "carry_%s", handoff.request ) );
			}
			guiRetainedHome->HandleNamedEvent( popup ? "departPopup" : "depart" );
		}
		if ( command.Length() > 0 ) {
			// The legacy action's own command, such as its selection sound.
			DispatchCommand( guiMainMenu, command.c_str() );
		}
		return;
	}
	common->Warning( "retained UI: unhandled session request '%s'", request );
#endif
}

/*
===============
idSessionLocal::BeginRetainedPage

LOAD GAME on the title or the pause, and SAVE GAME and RESTART LEVEL on the
pause: the home plays a page hand-off (the bands dock and the label carries
into the title slot) and the page presents at 550 ms, when the stock page
would (UpdateRetainedPage). The Strogg pause hands off to the Strogg family's
pages; where one cannot present, the stock page opens rather than the Marine
one, whose family would break. False when the page cannot present: the stock
page then opens as before.
===============
*/
bool idSessionLocal::BeginRetainedPage( const char *request ) {
#ifndef ID_DEDICATED
	const bool saving = !idStr::Icmp( request, "saveGame" ), restarting = !idStr::Icmp( request, "restartLevel" );
	const bool credits = !idStr::Icmp( request, "credits" );
	const bool multiplayer = !idStr::Icmp( request, "multiplayer" ), settings = !idStr::Icmp( request, "settings" );
	const bool strogg = !settings && guiRetainedHome != NULL && guiRetainedHome == guiRetainedPauseStrogg;
	if ( ( !saving && !restarting && !credits && !multiplayer && !settings && idStr::Icmp( request, "loadGame" ) ) ||
			guiRetainedHome == NULL ||
			guiActive != guiMainMenu || ( ( credits || multiplayer ) && mapSpawned ) ||
			( ( saving || restarting ) && ( !mapSpawned || IsMultiplayer() ) ) ) {
		return false;
	}
	if ( retainedPagePending != NULL ) {
		return true;	// already handing off
	}
	// Loaded with the other screens (PreloadRetainedScreens), or now when the
	// gate came on after startup. A view that stopped drawing after a renderer
	// or language change hands the page back to the stock one for good.
	idUserInterface *&loaded = strogg ? ( saving ? guiRetainedSaveStrogg : restarting ? guiRetainedRestartStrogg : guiRetainedLoadStrogg ) :
		saving ? guiRetainedSave : restarting ? guiRetainedRestart :
		credits ? guiRetainedCredits : multiplayer ? guiRetainedMpPage : settings ? guiRetainedSettingsPage : guiRetainedLoad;
	const char *path = strogg ? ( saving ? RETAINED_SAVEGAME_STROGG_GUI : restarting ? RETAINED_RESTART_STROGG_GUI : RETAINED_LOADGAME_STROGG_GUI ) :
		saving ? RETAINED_SAVEGAME_GUI : restarting ? RETAINED_RESTART_GUI :
		credits ? RETAINED_CREDITS_GUI : multiplayer ? RETAINED_MPPAGE_GUI : settings ? RETAINED_SETTINGSPAGE_GUI : RETAINED_LOADGAME_GUI;
	if ( loaded != NULL && UI_RetainedViewFailed( loaded ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", path );
		retainedStock.AddUnique( path );
		loaded = NULL;
		return false;
	}
	// The Multiplayer and SETTINGS pages also need the legacy menu's routes to
	// their stock pages.
	idUserInterface *page = multiplayer ? RetainedMultiplayerPage() : settings ? RetainedSettingsPage() :
		loaded != NULL ? loaded : FindRetainedGui( path, true, false );
	if ( page == NULL ) {
		return false;
	}
	loaded = page;
	if ( saving || ( !restarting && !credits && !multiplayer && !settings ) ) {
		page->SetStateInt( "browser.sort", 0 );
		page->SetStateInt( "browser.filter", 0 );
	}
	// The saves, read while the home hands off: the newest selected to load,
	// none to save over. Each arrival to save starts with no name, as the
	// stock page's field did; a restart starts at the level's difficulty.
	if ( restarting ) {
		page->SetStateInt( "restart.skill", idMath::ClampInt( 0, 4, cvarSystem->GetCVarInteger( "g_skill" ) ) );
		page->StateChanged( common->GetPresentationTime() );
	} else if ( saving ) {
		retainedSaveTarget.Clear();
		page->SetStateString( "save.name", "" );
		page->SetStateString( "save.target", "" );
		page->SetStateString( "save.notice", "" );
		PublishRetainedSavePage( page, -1 );
	} else if ( credits ) {
		PublishRetainedCredits( page );
	} else if ( multiplayer ) {
		retainedMpPageDest = 0;
		// From the title the page starts on its primary action.
		page->SetStateBool( "from_create", false );
		page->SetStateBool( "from_join", false );
		page->SetStateBool( "from_demos", false );
		page->SetStateBool( "from_setup", false );
		Session_PublishMultiplayerSummaries( page, this );
		page->StateChanged( common->GetPresentationTime() );
	} else if ( settings ) {
		// From the home the page starts on CONTROLS.
		retainedSettingsDest = 0;
		for ( int i = 0; i < RETAINED_SETTINGS_SECTION_COUNT; i++ ) {
			page->SetStateBool( va( "from_%s", RETAINED_SETTINGS_SECTIONS[ i ].section ), false );
		}
		page->StateChanged( common->GetPresentationTime() );
	} else {
		PublishRetainedLoadPage( page, 0, NULL );
	}
	retainedHandoffUntil = common->GetPresentationTime() + RETAINED_PAGE_HANDOFF_MSEC;
	retainedPagePending = page;
	retainedPageAt = retainedHandoffUntil;
	guiRetainedHome->HandleNamedEvent( va( "carry_%s", request ) );
	guiRetainedHome->HandleNamedEvent( "depart" );
	HandleMainMenuCommands( "play main_menu_selection" );
	return true;
#else
	return false;
#endif
}

bool idSessionLocal::RetainedPageActive() const {
	return guiActive != NULL && ( IsRetainedLoadPage( guiActive ) || IsRetainedSavePage( guiActive ) || IsRetainedRestartPage( guiActive ) ||
		guiActive == guiRetainedCredits || guiActive == guiRetainedMpPage ||
		( guiRetainedSettingsPage != NULL && guiActive == guiRetainedSettingsPage ) );
}

// The hand-off's end: the page presents over the home that handed off, which
// stops taking input on the same frame. Anything else taking the screen first
// abandons it.
void idSessionLocal::UpdateRetainedPage() {
#ifndef ID_DEDICATED
	if ( retainedPagePending == NULL ) {
		return;
	}
	if ( guiActive != guiMainMenu || guiRetainedHome == NULL ) {
		retainedPagePending = NULL;
		return;
	}
	if ( common->GetPresentationTime() < retainedPageAt ) {
		return;
	}
	idUserInterface *page = retainedPagePending;
	retainedPagePending = NULL;
	if ( IsRetainedLoadPage( page ) ) {
		page->SetStateBool( "load.ingame", mapSpawned );
	}
	// The home keeps drawing behind the page (DrawRetainedPageHome).
	retainedPageHome = guiRetainedHome;
	SetGUI( page, NULL );
	UpdateRetainedHome();
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_PAGE present gui=%s\n", page->Name() );
	}
#endif
}

// Back on the page: the home presents at once and brings the bands back from
// the page dock, as it does when a stock page leaves.
void idSessionLocal::ReturnRetainedPage() {
#ifndef ID_DEDICATED
	if ( !RetainedPageActive() || guiMainMenu == NULL ) {
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	SetGUI( guiMainMenu, NULL );
	retainedPageReturn = true;
	UpdateRetainedHome();
	retainedPageReturn = false;
	retainedPageHome = NULL;
#endif
}

/*
===============
idSessionLocal::ReturnRetainedSinglePlayerHome

Back on the Single Player page. The title's SINGLE PLAYER ran the legacy home
button's own action, whose choreography took the legacy menu off its home
(Single Player's hand-off, desktop::dest 37, the home panel hidden). Its own
return, which the stock selector runs too (returnFromSinglePlayer), puts the
panel and its rails back unseen under the retained title, and the menu rests
at home at once, so the title takes the screen back with the bands coming home
from the page dock, as from any page. Without a legacy main menu, the menu
starts as before.
===============
*/
void idSessionLocal::ReturnRetainedSinglePlayerHome() {
#ifndef ID_DEDICATED
	if ( guiMainMenu == NULL ) {
		StartMenu();
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	// The legacy menu's timelines stood still while the page presented; they
	// catch up first, so none of their remaining steps lands on its home.
	UI_RunTimeEvents( guiMainMenu, common->GetPresentationTime() );
	// Off its home, the menu's activation starts no intro, as the selector's
	// own return keeps it (ArenaCampaign's return to the main menu).
	guiMainMenu->SetPresentationValue( "desktop::curr", "1" );
	guiMainMenu->SetPresentationValue( "desktop::active", "1" );
	SetGUI( guiMainMenu, NULL );
	guiMainMenu->HandleNamedEvent( "returnFromSinglePlayer" );
	guiMainMenu->SetPresentationValue( "desktop::curr", "0" );
	guiMainMenu->SetPresentationValue( "desktop::active", "0" );
	guiMainMenu->SetPresentationValue( "desktop::dest", "0" );
	retainedPageReturn = true;
	UpdateRetainedHome();
	retainedPageReturn = false;
#endif
}

/*
===============
idSessionLocal::DrawRetainedPageHome

A stock page lives in the main menu, over its home's backdrop and docked
bands; the retained Load Game page is that page's content alone. The home
that handed off keeps drawing behind it as the departure left it: the title's
montage, or the pause over the softened game. Its carried label is the page's
title.
===============
*/
void idSessionLocal::DrawRetainedPageHome( int presentationTime ) {
#ifndef ID_DEDICATED
	if ( mapSpawned && !com_skipGameDraw.GetBool() && GetLocalClientNum() >= 0 ) {
		if ( !game->Draw( GetLocalClientNum() ) ) {
			renderSystem->SetColor( colorBlack );
			renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
		}
	} else if ( retainedPageHome == NULL ) {
		renderSystem->SetColor( colorBlack );
		renderSystem->DrawStretchPic( 0, 0, 640, 480, 0, 0, 1, 1, declManager->FindMaterial( "_white" ) );
	}
	if ( retainedPageHome != NULL ) {
		retainedPageHome->Redraw( presentationTime );
	}
#endif
}

/*
===============
idSessionLocal::PublishRetainedLoadPage

The Load Game page's saves, newest first. `select` is the save to select,
clamped to the list (-1: none), and `error` the reason to show over its
picture (NULL: none). The rows hold the RETAINED_LOAD_ROWS saves around the
selection.
===============
*/
// Six orders: date descending/ascending, name ascending/descending,
// location ascending/descending. Equal keys use the slot name in either direction.
void idSessionLocal::BuildRetainedSaveList( idUserInterface *gui, int select ) {
#ifndef ID_DEDICATED
	const int sort = idMath::ClampInt( 0, 5, gui->State().GetInt( "browser.sort" ) );
	const int filter = idMath::ClampInt( 0, 2, gui->State().GetInt( "browser.filter" ) );
	idStrList files;
	idList<fileTIME_T> times;
	GetSaveGameList( files, times );
	struct entry_t { idStr slot, name, location; ID_TIME_T stamp; };
	std::vector<entry_t> entries;
	for ( int i = 0; i < times.Num(); ++i ) {
		entry_t entry;
		entry.slot = files[ times[i].index ];
		entry.stamp = times[i].timeStamp;
		sessionMenuSaveDescription_t description;
		const bool described = Session_MenuReadSaveDescription( entry.slot, description );
		const bool automatic = described && ( description.noOverwrite || description.screenshot.Length() > 0 );
		if ( ( filter == 1 && automatic ) || ( filter == 2 && !automatic ) ) continue;
		Session_RetainedSaveLabel( entry.slot, description, described, entry.name, entry.location );
		entries.push_back( entry );
	}
	std::stable_sort( entries.begin(), entries.end(), [sort]( const entry_t &a, const entry_t &b ) {
		int order = sort < 2 ? ( a.stamp > b.stamp ? -1 : a.stamp < b.stamp ? 1 : 0 ) :
			sort < 4 ? a.name.Icmp( b.name ) : a.location.Icmp( b.location );
		if ( order != 0 ) return sort % 2 ? order > 0 : order < 0;
		order = a.slot.Icmp( b.slot );
		return order != 0 ? order < 0 : a.slot.Cmp( b.slot ) < 0;
	} );
	retainedLoadSlots.Clear();
	retainedLoadTimes.Clear();
	retainedLoadNames.Clear();
	retainedLoadLocations.Clear();
	for ( const auto &entry : entries ) {
		retainedLoadSlots.Append( entry.slot );
		retainedLoadTimes.Append( entry.stamp );
		retainedLoadNames.Append( entry.name );
		retainedLoadLocations.Append( entry.location );
	}
	const int count = retainedLoadSlots.Num();
	retainedLoadSelected = count > 0 && select >= 0 ? idMath::ClampInt( 0, count - 1, select ) : -1;
	retainedLoadFirst = retainedLoadSelected > 0 ? retainedLoadSelected / RETAINED_LOAD_ROWS * RETAINED_LOAD_ROWS : 0;
	retainedSaveGeneration = retainedSaveGeneration == 2147483647 ? 1 : retainedSaveGeneration + 1;
	gui->SetStateInt( "browser.generation", retainedSaveGeneration );
	gui->SetStateString( "browser.context", va( "%d", retainedSaveGeneration ) );
	gui->SetStateInt( "browser.total", times.Num() );
	gui->SetStateInt( "browser.sort", sort );
	gui->SetStateInt( "browser.filter", filter );
#endif
}

// Refresh explicitly, never scan the filesystem every presented frame. Both
// selection and row focus follow filenames, including across a 64-row boundary.
bool idSessionLocal::RefreshRetainedSaveList( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	const bool refresh = !idStr::Icmp( request, "saveListRefresh" );
	const bool sort = !idStr::Icmp( request, "saveListSort" );
	const bool manual = !idStr::Icmp( request, "saveListFilterManual" );
	const bool automatic = !idStr::Icmp( request, "saveListFilterAuto" );
	const bool clear = !idStr::Icmp( request, "saveListClear" );
	if ( !refresh && !sort && !manual && !automatic && !clear ) return false;
	const idStr selected = retainedLoadSelected >= 0 && retainedLoadSelected < retainedLoadSlots.Num() ?
		retainedLoadSlots[ retainedLoadSelected ] : idStr();
	const std::string focus = UI_RetainedFocusedControl( gui );
	idStr focused;
	for ( int row = 0; row < RETAINED_LOAD_ROWS; ++row ) {
		if ( focus == va( "row-%d", row ) && retainedLoadFirst + row < retainedLoadSlots.Num() ) {
			focused = retainedLoadSlots[ retainedLoadFirst + row ];
			break;
		}
	}
	if ( sort ) gui->SetStateInt( "browser.sort", ( idMath::ClampInt( 0, 5, gui->State().GetInt( "browser.sort" ) ) + 1 ) % 6 );
	if ( manual || automatic || clear ) {
		const int filter = manual ? 1 : automatic ? 2 : 0;
		gui->SetStateInt( "browser.filter", gui->State().GetInt( "browser.filter" ) == filter ? 0 : filter );
	}
	BuildRetainedSaveList( gui, -1 );
	retainedLoadSelected = retainedLoadSlots.FindIndex( selected );
	const int focusIndex = retainedLoadSlots.FindIndex( focused );
	const int anchor = focusIndex >= 0 ? focusIndex : retainedLoadSelected;
	retainedLoadFirst = anchor >= 0 ? anchor / RETAINED_LOAD_ROWS * RETAINED_LOAD_ROWS : 0;
	PublishRetainedLoadRows( gui );
	if ( IsRetainedSavePage( gui ) ) PublishRetainedSaveSelection( gui, false );
	else PublishRetainedLoadSave( gui, NULL );
	if ( !focused.IsEmpty() ) {
		gui->HandleNamedEvent( focusIndex >= 0 ? va( "browserFocus%d", focusIndex - retainedLoadFirst ) : "browserFocusRefresh" );
	}
	return true;
#else
	return false;
#endif
}

#ifndef ID_DEDICATED
// Reject stale catalog operands and malformed integers, including overflow.
static bool Session_RetainedSaveOperand( const char *request, const char *verb, int generation, int count, int &index ) {
	idCmdArgs args( request, false );
	if ( args.Argc() != 3 || idStr::Icmp( args.Argv( 0 ), verb ) ) return false;
	int values[2];
	for ( int i = 0; i < 2; ++i ) {
		const char *text = args.Argv( i + 1 );
		if ( !*text || strlen( text ) > 10 ) return false;
		unsigned long long value = 0;
		for ( const char *c = text; *c; ++c ) {
			if ( *c < '0' || *c > '9' ) return false;
			value = value * 10 + ( *c - '0' );
		}
		if ( value > 2147483647 ) return false;
		values[i] = static_cast<int>( value );
	}
	if ( values[1] != generation || generation < 1 || values[0] >= count ) return false;
	index = values[0];
	return true;
}
#endif

void idSessionLocal::PublishRetainedLoadPage( idUserInterface *gui, int select, const char *error ) {
#ifndef ID_DEDICATED
	BuildRetainedSaveList( gui, select );
	PublishRetainedLoadRows( gui );
	PublishRetainedLoadSave( gui, error );
#endif
}

// Each row's save: its name and level, on the Load or the Save Game page.
void idSessionLocal::PublishRetainedLoadRows( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	const char *list = IsRetainedSavePage( gui ) ? "save" : "load";
	for ( int row = 0; row < RETAINED_LOAD_ROWS; ++row ) {
		const int index = retainedLoadFirst + row;
		idStr name, location;
		if ( index < retainedLoadSlots.Num() ) {
			name = retainedLoadNames[ index ];
			location = retainedLoadLocations[ index ];
		}
		const idStr context = index < retainedLoadSlots.Num() ? va( "%d:%s", retainedSaveGeneration, retainedLoadSlots[ index ].c_str() ) : "";
		gui->SetStateString( va( "%s.row%d.context", list, row ), context.c_str() );
		gui->SetStateString( va( "%s.row%d.name", list, row ), name.c_str() );
		gui->SetStateString( va( "%s.row%d.location", list, row ), location.c_str() );
	}
	gui->SetStateInt( va( "%s.count", list ), retainedLoadSlots.Num() );
	gui->SetStateInt( va( "%s.first", list ), retainedLoadFirst );
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_SAVE_CATALOG page=%s generation=%d sort=%d filter=%d count=%d first=%d selected=%d slot=%s\n",
			list, retainedSaveGeneration, gui->State().GetInt( "browser.sort" ), gui->State().GetInt( "browser.filter" ),
			retainedLoadSlots.Num(), retainedLoadFirst, retainedLoadSelected,
			retainedLoadSelected >= 0 ? retainedLoadSlots[ retainedLoadSelected ].c_str() : "<none>" );
	}
#endif
}

// The selected save: its picture, its date and time, the difficulty and the
// run's play time, and why it failed.
void idSessionLocal::PublishRetainedLoadSave( idUserInterface *gui, const char *error ) {
#ifndef ID_DEDICATED
	idStr shot = RETAINED_LOAD_GENERIC_SHOT, date, time, context, skill, played;
	if ( retainedLoadSelected >= 0 && retainedLoadSelected < retainedLoadSlots.Num() ) {
		const idStr &slot = retainedLoadSlots[ retainedLoadSelected ];
		context = va( "%d:%s", retainedSaveGeneration, slot.c_str() );
		sessionMenuSaveDescription_t description;
		const bool described = Session_MenuReadSaveDescription( slot, description );
		shot = Session_RetainedSaveShot( slot, description, described, RETAINED_LOAD_GENERIC_SHOT );
		Session_RetainedSaveStamp( retainedLoadTimes[ retainedLoadSelected ], date, time );
		Session_RetainedSaveDetails( slot, skill, played );
	}
	gui->SetStateInt( "load.selected", retainedLoadSelected );
	gui->SetStateString( "load.context", context.c_str() );
	gui->SetStateString( "load.shot", shot.c_str() );
	gui->SetStateString( "load.date", date.c_str() );
	gui->SetStateString( "load.time", time.c_str() );
	gui->SetStateString( "load.skill", skill.c_str() );
	gui->SetStateString( "load.played", played.c_str() );
	gui->SetStateString( "load.error", error != NULL ? error : "" );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::HandleRetainedLoadRequest

The Load Game page's verbs. A row reports itself in load.pick and the session
checks it against the saves it shows; every other verb acts on the selected
save. A save that cannot be loaded or deleted stays selected and says why.
===============
*/
void idSessionLocal::HandleRetainedLoadRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( RefreshRetainedSaveList( gui, request ) ) return;
	const int count = retainedLoadSlots.Num();
	if ( !idStr::Icmp( request, "loadBack" ) ) {
		ReturnRetainedPage();
		return;
	}
	if ( !idStr::Icmpn( request, "loadSelect", 10 ) ) {
		int index = -1;
		if ( Session_RetainedSaveOperand( request, "loadSelect", retainedSaveGeneration, count, index ) ) {
			retainedLoadSelected = index;
			PublishRetainedLoadSave( gui, NULL );
		}
		return;
	}
	if ( !idStr::Icmp( request, "loadNewer" ) || !idStr::Icmp( request, "loadOlder" ) ) {
		// A page further: its first save going to older ones, its last coming
		// back to newer ones, is selected and focused.
		const bool older = !idStr::Icmp( request, "loadOlder" );
		const int first = retainedLoadFirst + ( older ? RETAINED_LOAD_ROWS : -RETAINED_LOAD_ROWS );
		if ( first < 0 || first >= count ) {
			return;
		}
		retainedLoadFirst = first;
		retainedLoadSelected = older ? first : first + RETAINED_LOAD_ROWS - 1;
		PublishRetainedLoadRows( gui );
		PublishRetainedLoadSave( gui, NULL );
		gui->HandleNamedEvent( older ? "pagedOlder" : "pagedNewer" );
		return;
	}
	if ( !idStr::Icmpn( request, "loadSave", 8 ) ) {
		int target = -1;
		if ( !Session_RetainedSaveOperand( request, "loadSave", retainedSaveGeneration, count, target ) ) return;
		retainedLoadSelected = target;
		const idStr slot = retainedLoadSlots[ retainedLoadSelected ];
		HandleMainMenuCommands( "play main_menu_selection" );
		// A preflight refusal preserves this page. A deeper restore failure
		// builds a fresh menu and publishes its notice there instead.
		if ( !LoadGame( slot.c_str() ) && guiActive == gui ) {
			PublishRetainedLoadSave( gui, Session_LoadFailureText( loadGameFailure ) );
		}
		return;
	}
	if ( !idStr::Icmpn( request, "loadDelete", 10 ) ) {
		int target = -1;
		if ( !Session_RetainedSaveOperand( request, "loadDelete", retainedSaveGeneration, count, target ) ) return;
		retainedLoadSelected = target;
		// The page asked first. The save that takes the deleted one's place is
		// selected; one that is still there stays selected and says so.
		const idStr slot = retainedLoadSlots[ retainedLoadSelected ];
		const int at = retainedLoadSelected;
		DeleteGame( slot.c_str() );
		PublishRetainedLoadPage( gui, at, NULL );
		const int remaining = retainedLoadSlots.FindIndex( slot );
		if ( remaining >= 0 ) {
			PublishRetainedLoadPage( gui, remaining, "#str_231105" );
		}
		return;
	}
	if ( !idStr::Icmp( request, "loadNewGame" ) ) {
		// No saves at the title: the Single Player page starts a game.
		if ( !mapSpawned ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			OpenCampaignSelector( false );
		}
		return;
	}
	common->Warning( "retained UI: unhandled Load Game request '%s'", request );
#endif
}

/*
===============
idSessionLocal::PublishRetainedSavePage

The Save Game page's saves, newest first, as the Load Game page lists them,
with `select` selected (-1: none).
===============
*/
void idSessionLocal::PublishRetainedSavePage( idUserInterface *gui, int select ) {
#ifndef ID_DEDICATED
	BuildRetainedSaveList( gui, select );
	PublishRetainedLoadRows( gui );
	PublishRetainedSaveSelection( gui, false );
#endif
}

// The selected save and, with `name`, its name in the name field, as the
// stock page's updateSaveGameInfo filled it, unless a name is being typed
// there: the field never loses typed text to a selection.
void idSessionLocal::PublishRetainedSaveSelection( idUserInterface *gui, bool name ) {
#ifndef ID_DEDICATED
	gui->SetStateInt( "save.selected", retainedLoadSelected );
	if ( name && retainedLoadSelected >= 0 && retainedLoadSelected < retainedLoadSlots.Num() &&
			!gui->State().GetBool( "ui.numberDraftsPending" ) ) {
		const idStr &slot = retainedLoadSlots[ retainedLoadSelected ];
		sessionMenuSaveDescription_t description;
		idStr saveName = Session_MenuReadSaveDescription( slot, description ) ? description.saveName : slot;
		saveName.RemoveColors();
		gui->SetStateString( "save.name", saveName.c_str() );
	}
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::RetainedSaveGame

Saves the game under `name` from the Save Game page. A save the session
refuses says why in the page's notice instead of the stock message box; a
saved game takes the list's top row, selected.
===============
*/
bool idSessionLocal::RetainedSaveGame( idUserInterface *gui, const char *name ) {
#ifndef ID_DEDICATED
	const char *refusal = NULL;
	if ( !mapSpawned || IsMultiplayer() || game->InCinematic() ) {
		refusal = "#str_231110";
	} else if ( game->GetPersistentPlayerInfo( 0 ).GetInt( "health" ) <= 0 ) {
		refusal = "#str_231111";
	} else if ( Sys_GetDriveFreeSpace( cvarSystem->GetCVarString( "fs_savepath" ) ) < 25 ) {
		refusal = "#str_231112";
	} else if ( objectiveFailed ) {
		refusal = "#str_231113";
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	if ( refusal == NULL && !SaveGame( name, ST_REGULAR ) ) {
		refusal = "#str_231110";
	}
	if ( refusal != NULL ) {
		gui->SetStateString( "save.notice", refusal );
		gui->StateChanged( common->GetPresentationTime() );
		gui->HandleNamedEvent( "noticeModalShow" );
		return false;
	}
	// Reveal the saved file even while viewing autosaves. Keep the sort and
	// typed name; the new file need not occupy the first row in this order.
	if ( gui->State().GetInt( "browser.filter" ) == 2 ) gui->SetStateInt( "browser.filter", 0 );
	idStr slot = name;
	ScrubSaveGameFileName( slot );
	slot.StripFileExtension();
	BuildRetainedSaveList( gui, -1 );
	retainedLoadSelected = retainedLoadSlots.FindIndex( slot );
	retainedLoadFirst = retainedLoadSelected >= 0 ? retainedLoadSelected / RETAINED_LOAD_ROWS * RETAINED_LOAD_ROWS : 0;
	PublishRetainedLoadRows( gui );
	PublishRetainedSaveSelection( gui, false );
	return true;
#else
	return false;
#endif
}

/*
===============
idSessionLocal::HandleRetainedSaveRequest

The Save Game page's verbs. A row reports itself in save.pick, as on the
Load Game page; SAVE GAME saves under the name the page holds in save.name,
asking first over an existing save and refusing to save over an autosave.
===============
*/
void idSessionLocal::HandleRetainedSaveRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( RefreshRetainedSaveList( gui, request ) ) return;
	const int count = retainedLoadSlots.Num();
	if ( !idStr::Icmp( request, "saveBack" ) ) {
		ReturnRetainedPage();
		return;
	}
	if ( !idStr::Icmpn( request, "saveSelect", 10 ) ) {
		int index = -1;
		if ( Session_RetainedSaveOperand( request, "saveSelect", retainedSaveGeneration, count, index ) ) {
			retainedLoadSelected = index;
			PublishRetainedSaveSelection( gui, true );
		}
		return;
	}
	if ( !idStr::Icmp( request, "saveNewer" ) || !idStr::Icmp( request, "saveOlder" ) ) {
		const bool older = !idStr::Icmp( request, "saveOlder" );
		const int first = retainedLoadFirst + ( older ? RETAINED_LOAD_ROWS : -RETAINED_LOAD_ROWS );
		if ( first < 0 || first >= count ) {
			return;
		}
		retainedLoadFirst = first;
		retainedLoadSelected = older ? first : first + RETAINED_LOAD_ROWS - 1;
		PublishRetainedLoadRows( gui );
		PublishRetainedSaveSelection( gui, true );
		gui->HandleNamedEvent( older ? "pagedOlder" : "pagedNewer" );
		return;
	}
	if ( !idStr::Icmp( request, "saveSave" ) ) {
		idStr name = gui->State().GetString( "save.name" );
		name.StripTrailingWhitespace();
		if ( name.Length() == 0 ) {
			return;
		}
		const sessionMenuSaveSlot_t slot = Session_MenuSaveSlotState( name.c_str() );
		if ( slot == SESSION_SAVE_SLOT_AUTOSAVE ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			gui->SetStateString( "save.notice", "#str_200982" );
			gui->StateChanged( common->GetPresentationTime() );
			gui->HandleNamedEvent( "noticeModalShow" );
			return;
		}
		if ( slot == SESSION_SAVE_SLOT_TAKEN ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			retainedSaveTarget = name;
			gui->SetStateString( "save.target", name.c_str() );
			gui->StateChanged( common->GetPresentationTime() );
			gui->HandleNamedEvent( "overwriteModalShow" );
			return;
		}
		RetainedSaveGame( gui, name.c_str() );
		return;
	}
	if ( !idStr::Icmp( request, "saveOverwrite" ) ) {
		// The page asked first; the save it asked about, whatever the field
		// has come to hold since.
		if ( retainedSaveTarget.Length() > 0 ) {
			const idStr target = retainedSaveTarget;
			retainedSaveTarget.Clear();
			RetainedSaveGame( gui, target.c_str() );
		}
		return;
	}
	common->Warning( "retained UI: unhandled Save Game request '%s'", request );
#endif
}

/*
===============
idSessionLocal::HandleRetainedRestartRequest

The Restart Level page's verbs. The page holds the chosen difficulty in
restart.skill; RESTART LEVEL restarts the level at it, and Back leaves the
level's difficulty as it was.
===============
*/
void idSessionLocal::HandleRetainedRestartRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( !idStr::Icmp( request, "restartBack" ) ) {
		ReturnRetainedPage();
		return;
	}
	if ( !idStr::Icmp( request, "restartGo" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		RestartLevelAtSkill( idMath::ClampInt( 0, 4, gui->State().GetInt( "restart.skill", "1" ) ) );
		return;
	}
	common->Warning( "retained UI: unhandled Restart Level request '%s'", request );
#endif
}

// The Credits page's words: every line's text, read from the legacy main
// menu's credits window of the same name (credits.<window>), so the generated
// page carries none of them.
void idSessionLocal::PublishRetainedCredits( idUserInterface *page ) {
#ifndef ID_DEDICATED
	std::vector<std::string> names;
	UI_RetainedStateNames( page, "credits.", names );
	for ( const std::string &name : names ) {
		idStr text;
		if ( guiMainMenu == NULL || !guiMainMenu->GetPresentationValue( va( "%s::text", name.c_str() + 8 ), text ) ) {
			text.Clear();
		}
		page->SetStateString( name.c_str(), text.c_str() );
	}
	page->StateChanged( common->GetPresentationTime() );
#endif
}

// After the player dies the level unloads and the restart screen offers its
// choices: the retained one where it can present, else the stock one.
void idSessionLocal::OpenRestartMenu() {
#ifndef ID_DEDICATED
	// Loaded with the pause, or now when the gate came on after the level
	// loaded. A view that stopped drawing hands the screen back for good.
	if ( guiRetainedRestartMenu != NULL && UI_RetainedViewFailed( guiRetainedRestartMenu ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_RESTARTMENU_GUI );
		retainedStock.AddUnique( RETAINED_RESTARTMENU_GUI );
		guiRetainedRestartMenu = NULL;
	}
	if ( Session_RetainedScreensEnabled() && guiRetainedRestartMenu == NULL ) {
		guiRetainedRestartMenu = FindRetainedGui( RETAINED_RESTARTMENU_GUI, true, false );
	}
	if ( Session_RetainedScreensEnabled() && guiRetainedRestartMenu != NULL ) {
		SetGUI( guiRetainedRestartMenu, NULL );
		return;
	}
#endif
	SetGUI( guiRestartMenu, NULL );
}

/*
===============
idSessionLocal::RetainedArenaPage

Single Player's Arena sub-page (section 9 "Arena Campaign", FLOW-011): the
Arena campaign's browser, which idArenaCampaign::OpenBrowser presents where
this returns it and publishes into while it is loaded. NULL with the gate off
or when the page is not installed, cannot load or stopped drawing: the stock
browser presents instead.
===============
*/
idUserInterface *idSessionLocal::RetainedArenaPage() {
#ifndef ID_DEDICATED
	// A view that stopped drawing hands the browser back for good.
	if ( guiRetainedArena != NULL && UI_RetainedViewFailed( guiRetainedArena ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_ARENA_GUI );
		retainedStock.AddUnique( RETAINED_ARENA_GUI );
		guiRetainedArena = NULL;
	}
	if ( !Session_RetainedScreensEnabled() ) {
		return NULL;
	}
	if ( guiRetainedArena == NULL ) {
		guiRetainedArena = FindRetainedGui( RETAINED_ARENA_GUI, true, false );
	}
	return guiRetainedArena;
#else
	return NULL;
#endif
}

/*
===============
idSessionLocal::RetainedMultiplayerPage

The retained Multiplayer page where it can present: NULL with the gate off,
in a game, over a main menu without the routes to its Multiplayer pages, or
when the page is not installed, cannot load or stopped drawing.
===============
*/
idUserInterface *idSessionLocal::RetainedMultiplayerPage() {
#ifndef ID_DEDICATED
	// A view that stopped drawing hands the page back for good.
	if ( guiRetainedMpPage != NULL && UI_RetainedViewFailed( guiRetainedMpPage ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_MPPAGE_GUI );
		retainedStock.AddUnique( RETAINED_MPPAGE_GUI );
		guiRetainedMpPage = NULL;
	}
	if ( !Session_RetainedScreensEnabled() || mapSpawned ) {
		return NULL;
	}
	// Quietly, as a home screen over a main menu without its pages.
	if ( !Session_RetainedMultiplayerRoutes( guiMainMenu ) ) {
		if ( retainedStock.FindIndex( RETAINED_MPPAGE_GUI ) < 0 ) {
			common->DPrintf( "retained UI: the main menu has no routes to its Multiplayer pages, so it presents its own instead of '%s'\n",
				RETAINED_MPPAGE_GUI );
			retainedStock.Append( RETAINED_MPPAGE_GUI );
		}
		return NULL;
	}
	if ( guiRetainedMpPage == NULL ) {
		guiRetainedMpPage = FindRetainedGui( RETAINED_MPPAGE_GUI, true, false );
	}
	return guiRetainedMpPage;
#else
	return NULL;
#endif
}

/*
===============
idSessionLocal::OpenRetainedMultiplayerPage

Back from a stock Multiplayer page, or from a sub-page onto its section
(`section`: "create" from Create Server, "join" from Join Game, "demos" from
Demos): the
retained page presents at once, its arrival bringing its plates and
description back. False when it cannot present: the stock Multiplayer page
opens instead.
===============
*/
bool idSessionLocal::OpenRetainedMultiplayerPage( const char *section ) {
#ifndef ID_DEDICATED
	idUserInterface *page = RetainedMultiplayerPage();
	if ( page == NULL || guiMainMenu == NULL ) {
		return false;
	}
	retainedMpPageDest = 0;
	retainedPageHome = NULL;
	// The activation reads them.
	page->SetStateBool( "from_create", section != NULL && !idStr::Icmp( section, "create" ) );
	page->SetStateBool( "from_join", section != NULL && !idStr::Icmp( section, "join" ) );
	page->SetStateBool( "from_demos", section != NULL && !idStr::Icmp( section, "demos" ) );
	page->SetStateBool( "from_setup", section != NULL && !idStr::Icmp( section, "setup" ) );
	Session_PublishMultiplayerSummaries( page, this );
	page->StateChanged( common->GetPresentationTime() );
	SetGUI( page, NULL );
	return true;
#else
	return false;
#endif
}

/*
===============
idSessionLocal::HandleRetainedMultiplayerPageRequest

The Multiplayer page's verbs. PLAYER SETUP edits in place, and opens the
stock Settings page once the page has left where its editor cannot present
(UpdateRetainedMultiplayerPage); JOIN GAME and CREATE SERVER go deeper to the
retained Join Game and Create Server sub-pages where they can present, else
they open the stock browser and Create Server pages the same way; DEMOS goes
deeper to the retained Demos sub-page, else the legacy demo browser opens over
the page and returns to it; BACK returns the title, from the page dock as from
a stock page. A page or a change of level under way takes no further request.
===============
*/
void idSessionLocal::HandleRetainedMultiplayerPageRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( retainedMpPageDest != 0 || retainedSubpageFrom != NULL || guiMainMenu == NULL ) {
		return;
	}
	if ( !idStr::Icmpn(request,openq4::ui::PlayerSetupRequestPrefix,sizeof(openq4::ui::PlayerSetupRequestPrefix)-1) ) {
		HandleRetainedPlayerSetupRequest(gui,request+sizeof(openq4::ui::PlayerSetupRequestPrefix)-1);
		return;
	}
	if ( !idStr::Icmp(request,"playerSetupClose") ) {
		if ( gui->State().GetBool("setup.open") ) {
			gui->HandleNamedEvent("setupClose");
			PumpApplicationActions(gui);
		}
		return;
	}
	if ( !idStr::Icmp(request,"multiplayerSetup") && OpenRetainedPlayerSetup(gui) ) {
		HandleMainMenuCommands("play main_menu_selection");
		return;
	}
	if ( !idStr::Icmp( request, "multiplayerCreate" ) && RetainedCreateServerPage() != NULL ) {
		BeginRetainedSubpage( gui, true, RETAINED_SUBPAGE_CREATE_SERVER );
		return;
	}
	if ( !idStr::Icmp( request, "multiplayerJoin" ) && RetainedJoinGamePage() != NULL ) {
		BeginRetainedSubpage( gui, true, RETAINED_SUBPAGE_JOIN_GAME );
		return;
	}
	if ( !idStr::Icmp( request, "multiplayerDemos" ) ) {
		if ( idUserInterface *demos = RetainedDemosPage() ) {
			// Each arrival shows every demo, the newest selected, published
			// while the page leaves.
			retainedDemoFilter = 0;
			PublishRetainedDemosPage( demos, NULL, 0, NULL );
			BeginRetainedSubpage( gui, true, RETAINED_SUBPAGE_DEMOS );
		} else {
			HandleMainMenuCommands( "play main_menu_selection" );
			OpenDemoMenu( true );
		}
		return;
	}
	if ( !idStr::Icmp( request, "multiplayerBack" ) ) {
		if ( MainMenuWindowStateEqualsInt( guiMainMenu, "desktop::curr", 0 ) &&
				MainMenuWindowStateEqualsInt( guiMainMenu, "desktop::active", 0 ) ) {
			// The legacy menu never left home: the title comes back as from
			// any retained page.
			ReturnRetainedPage();
			return;
		}
		// After a stock Multiplayer page the legacy menu goes home as its own
		// Multiplayer page does, and the retained home follows it.
		HandleMainMenuCommands( "play main_menu_selection" );
		UI_RunTimeEvents( guiMainMenu, common->GetPresentationTime() );
		guiMainMenu->SetPresentationValue( "desktop::curr", "3" );
		guiMainMenu->SetPresentationValue( "desktop::active", "1" );
		retainedPageHome = NULL;
		SetGUI( guiMainMenu, NULL );
		guiMainMenu->HandleNamedEvent( "leaveMultiplayerPage" );
		UpdateRetainedHome();
		return;
	}
	int dest = 0;
	if ( !idStr::Icmp( request, "multiplayerJoin" ) ) {
		dest = 14;
	} else if ( !idStr::Icmp( request, "multiplayerCreate" ) ) {
		dest = 17;
	} else if ( !idStr::Icmp( request, "multiplayerSetup" ) ) {
		dest = 18;
	} else {
		return;
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	gui->HandleNamedEvent( "depart" );
	PumpApplicationActions( gui );
	retainedMpPageDest = dest;
	retainedMpPageAt = common->GetPresentationTime() + RETAINED_MPPAGE_LEAVE_MSEC;
#endif
}

/*
===============
idSessionLocal::RetainedSettingsParent

The settings pages open over the main menu's settings screen or the retained
SETTINGS page, and return to it.
===============
*/
bool idSessionLocal::RetainedSettingsParent() const {
	return guiActive != NULL && ( guiActive == guiMainMenu || ( guiRetainedSettingsPage != NULL && guiActive == guiRetainedSettingsPage ) );
}

/*
===============
idSessionLocal::RetainedSettingsPage

The retained SETTINGS page, loaded when first wanted; NULL with the retained
screens off, over a main menu without the routes to its settings screen (a
mod's own), or when the page is not installed, cannot load or stopped
drawing.
===============
*/
idUserInterface *idSessionLocal::RetainedSettingsPage() {
#ifndef ID_DEDICATED
	// A view that stopped drawing hands the page back for good.
	if ( guiRetainedSettingsPage != NULL && UI_RetainedViewFailed( guiRetainedSettingsPage ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_SETTINGSPAGE_GUI );
		retainedStock.AddUnique( RETAINED_SETTINGSPAGE_GUI );
		guiRetainedSettingsPage = NULL;
	}
	if ( !Session_RetainedScreensEnabled() || guiMainMenu == NULL ) {
		return NULL;
	}
	// Quietly, as a home screen over a main menu without its routes.
	if ( !UI_LegacyNamedEventExists( guiMainMenu, "openSettingsPage" ) ) {
		if ( retainedStock.FindIndex( RETAINED_SETTINGSPAGE_GUI ) < 0 ) {
			common->DPrintf( "retained UI: the main menu has no route to its settings screen, so it presents its own instead of '%s'\n",
				RETAINED_SETTINGSPAGE_GUI );
			retainedStock.Append( RETAINED_SETTINGSPAGE_GUI );
		}
		return NULL;
	}
	if ( guiRetainedSettingsPage == NULL ) {
		guiRetainedSettingsPage = FindRetainedGui( RETAINED_SETTINGSPAGE_GUI, true, false );
	}
	return guiRetainedSettingsPage;
#else
	return NULL;
#endif
}

/*
===============
idSessionLocal::OpenRetainedSettingsPage

Back from the stock settings screen the SETTINGS page presents again, onto
the section that opened it. False when it cannot present.
===============
*/
bool idSessionLocal::OpenRetainedSettingsPage( const char *section ) {
#ifndef ID_DEDICATED
	idUserInterface *page = RetainedSettingsPage();
	if ( page == NULL ) {
		return false;
	}
	retainedSettingsDest = 0;
	retainedPageHome = NULL;
	guiMainMenu->SetStateBool( "settingsFromRetained", false );
	// The activation reads them.
	for ( int i = 0; i < RETAINED_SETTINGS_SECTION_COUNT; i++ ) {
		page->SetStateBool( va( "from_%s", RETAINED_SETTINGS_SECTIONS[ i ].section ),
			section != NULL && !idStr::Icmp( section, RETAINED_SETTINGS_SECTIONS[ i ].section ) );
	}
	page->StateChanged( common->GetPresentationTime() );
	SetGUI( page, NULL );
	return true;
#else
	return false;
#endif
}

/*
===============
idSessionLocal::HandleRetainedSettingsPageRequest

The SETTINGS page's verbs. CONTROLS, GAME OPTIONS and AUDIO go one level
deeper to their retained sub-pages (BeginRetainedSubpage), and so does
DISPLAY to the retained SYSTEM page where it is on, each returning here onto
its section; where one cannot, the stock settings screen opens on that panel once
the page has left (UpdateRetainedSettingsPage). LOAD DEFAULTS' YES restores
the defaults as the stock dialog's did. BACK returns the home, from the page
dock as from the stock screen. A panel or a change of level under way takes
no further request.
===============
*/
void idSessionLocal::HandleRetainedSettingsPageRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( retainedSettingsDest != 0 || retainedSubpageFrom != NULL || guiMainMenu == NULL ) {
		return;
	}
	if ( !idStr::Icmp( request, "settingsBack" ) ) {
		if ( MainMenuWindowStateEqualsInt( guiMainMenu, "desktop::curr", 0 ) &&
				MainMenuWindowStateEqualsInt( guiMainMenu, "desktop::active", 0 ) ) {
			// The legacy menu never left home: the home comes back as from any
			// retained page.
			ReturnRetainedPage();
			return;
		}
		// After the stock settings screen the legacy menu goes home as that
		// screen's own Back does, and the retained home follows it.
		HandleMainMenuCommands( "play main_menu_selection" );
		UI_RunTimeEvents( guiMainMenu, common->GetPresentationTime() );
		guiMainMenu->SetPresentationValue( "desktop::curr", "9" );
		guiMainMenu->SetPresentationValue( "desktop::active", "1" );
		guiMainMenu->SetStateBool( "settingsFromRetained", false );
		retainedPageHome = NULL;
		SetGUI( guiMainMenu, NULL );
		guiMainMenu->HandleNamedEvent( "leaveSettingsPage" );
		UpdateRetainedHome();
		return;
	}
	if ( !idStr::Icmp( request, "settingsDefaults" ) ) {
		// The stock dialog's YES: every setting and binding to its default,
		// and the sound system restarted.
		HandleMainMenuCommands( "exec cvar_restart ; play main_menu_selection ; resetdefaults ; exec s_restart" );
		return;
	}
	const retainedSettingsSection_t *chosen = NULL;
	for ( int i = 0; i < RETAINED_SETTINGS_SECTION_COUNT; i++ ) {
		if ( !idStr::Icmp( request, RETAINED_SETTINGS_SECTIONS[ i ].verb ) ) {
			chosen = &RETAINED_SETTINGS_SECTIONS[ i ];
		}
	}
	if ( chosen == NULL ) {
		return;
	}
	// The section the page returns onto.
	for ( int i = 0; i < RETAINED_SETTINGS_SECTION_COUNT; i++ ) {
		gui->SetStateBool( va( "from_%s", RETAINED_SETTINGS_SECTIONS[ i ].section ), &RETAINED_SETTINGS_SECTIONS[ i ] == chosen );
	}
	gui->StateChanged( common->GetPresentationTime() );
	// CONTROLS, GAME OPTIONS, DISPLAY and AUDIO go one level deeper to their pages.
	for ( const retainedSettingsSubpage_t &subpage : RETAINED_SETTINGS_SUBPAGES ) {
		const bool available = subpage.kind == RETAINED_SUBPAGE_SETTINGS_CONTROLS ? RetainedControlsAvailable() :
			subpage.kind == RETAINED_SUBPAGE_SETTINGS_GAME ? RetainedGameAvailable() :
			subpage.kind == RETAINED_SUBPAGE_SETTINGS_SYSTEM ? RetainedSystemAvailable() : RetainedAudioAvailable();
		if ( available && !idStr::Icmp( subpage.section, chosen->section ) ) {
			BeginRetainedSubpage( gui, true, subpage.kind );
			return;
		}
	}
	HandleMainMenuCommands( "play main_menu_selection" );
	const bool opened = chosen->dest == 4 ? OpenRetainedControls() : chosen->dest == 21 ? OpenRetainedGame() :
		chosen->dest == 36 ? OpenRetainedAudio() : RetainedSystemAvailable() && OpenSystemSettings();
	if ( opened ) {
		return;
	}
	gui->HandleNamedEvent( "depart" );
	PumpApplicationActions( gui );
	retainedSettingsDest = chosen->dest;
	retainedSettingsAt = common->GetPresentationTime() + RETAINED_SETTINGSPAGE_LEAVE_MSEC;
#endif
}

/*
===============
idSessionLocal::UpdateRetainedSettingsPage

The stock settings screen takes over when the SETTINGS page has left: the
legacy menu presents in the page state with the screen arriving on the panel
(openSettingsPage), and its Back returns here (settingsOpen). Anything else
taking the screen first abandons it.
===============
*/
void idSessionLocal::UpdateRetainedSettingsPage() {
#ifndef ID_DEDICATED
	if ( retainedSettingsDest == 0 ) {
		return;
	}
	if ( guiRetainedSettingsPage == NULL || guiActive != guiRetainedSettingsPage || guiMainMenu == NULL ) {
		retainedSettingsDest = 0;
		return;
	}
	if ( common->GetPresentationTime() < retainedSettingsAt ) {
		return;
	}
	const int dest = retainedSettingsDest;
	retainedSettingsDest = 0;
	retainedSettingsLeft = dest;
	// The legacy menu's timelines stood still while the page presented; they
	// catch up first. It activates away from home (desktop::curr 9), which
	// keeps SetGUI from starting its intro; the screen's arrival takes the
	// panel from desktop::dest.
	UI_RunTimeEvents( guiMainMenu, common->GetPresentationTime() );
	guiMainMenu->SetPresentationValue( "desktop::curr", "9" );
	guiMainMenu->SetPresentationValue( "desktop::active", "1" );
	guiMainMenu->SetPresentationValue( "desktop::dest", va( "%d", dest ) );
	guiMainMenu->SetStateBool( "settingsFromRetained", true );
	retainedPageHome = NULL;
	SetGUI( guiMainMenu, NULL );
	guiMainMenu->HandleNamedEvent( "openSettingsPage" );
	UpdateRetainedHome();
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_SETTINGSPAGE stock dest=%d\n", dest );
	}
#endif
}

/*
===============
idSessionLocal::UpdateRetainedMultiplayerPage

The stock page takes over when the Multiplayer page has left: the legacy menu
presents in the page state with that page arriving (openMultiplayerPage).
Anything else taking the screen first abandons it.
===============
*/
void idSessionLocal::UpdateRetainedMultiplayerPage() {
#ifndef ID_DEDICATED
	if ( guiRetainedMpPage && guiActive == guiRetainedMpPage && guiRetainedMpPage->State().GetBool("setup.open") ) {
		if ( UI_RetainedViewFailed(guiRetainedMpPage) || !PublishRetainedPlayerSetup(guiRetainedMpPage) ) {
			// A native preview that cannot present hands this exact edit flow
			// to its stock counterpart immediately, rather than leaving a blank.
			retainedMpPageDest = 18;
			retainedMpPageAt = common->GetPresentationTime();
		}
	}
	if ( retainedMpPageDest == 0 ) {
		return;
	}
	if ( guiActive != guiRetainedMpPage || guiMainMenu == NULL ) {
		retainedMpPageDest = 0;
		return;
	}
	if ( common->GetPresentationTime() < retainedMpPageAt ) {
		return;
	}
	const int dest = retainedMpPageDest;
	retainedMpPageDest = 0;
	retainedMpPageLeft = dest;
	// The legacy menu's timelines stood still while the page presented; they
	// catch up first, so none of their remaining steps lands on this state.
	UI_RunTimeEvents( guiMainMenu, common->GetPresentationTime() );
	// The legacy menu activates on its Multiplayer page (desktop::curr 3), which
	// keeps SetGUI from starting its intro; Create Server's own state would
	// reset it home. The stock page's arrival takes the state from there. The
	// values are written as the menu's own `set` writes them: a variable that
	// kept its declared expression would get it back from the next EvalRegs.
	guiMainMenu->SetPresentationValue( "desktop::curr", "3" );
	guiMainMenu->SetPresentationValue( "desktop::active", "1" );
	guiMainMenu->SetPresentationValue( "desktop::dest", va( "%d", dest ) );
	retainedPageHome = NULL;
	SetGUI( guiMainMenu, NULL );
	guiMainMenu->HandleNamedEvent( "openMultiplayerPage" );
	UpdateRetainedHome();
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_MPPAGE stock dest=%d\n", dest );
	}
#endif
}

#ifndef ID_DEDICATED
// The chosen game type's index among the page's, from si_gameType; a value
// the page does not offer (singleplayer, say) reads as DM, as the stock
// MAPScan reads it.
static int Session_RetainedCreateGameType( void ) {
	const char *gametype = cvarSystem->GetCVarString( "si_gameType" );
	for ( int i = 0; i < RETAINED_CREATE_GAMETYPE_COUNT; ++i ) {
		if ( !idStr::Icmp( gametype, RETAINED_CREATE_GAMETYPES[i] ) ) {
			return i;
		}
	}
	return 0;
}

// A setting's request, "<verb> <value>": its index in RETAINED_CREATE_SETTINGS
// and the value, a whole number from -1 to 999.
static bool Session_RetainedCreateSettingRequest( const char *request, int &setting, int &value ) {
	const char *verbs[ RETAINED_CREATE_SETTING_COUNT ];
	for ( int i = 0; i < RETAINED_CREATE_SETTING_COUNT; ++i ) {
		verbs[i] = RETAINED_CREATE_SETTINGS[i].verb;
	}
	return Session_RetainedValueRequest( request, verbs, RETAINED_CREATE_SETTING_COUNT, setting, value );
}

// The text a field handed over, at most `limit` bytes and never cut inside a
// UTF-8 sequence.
static idStr Session_RetainedCreateText( idUserInterface *gui, const char *state, int limit ) {
	idStr text = gui->State().GetString( state );
	if ( text.Length() > limit ) {
		int length = limit;
		while ( length > 0 && ( static_cast<unsigned char>( text[ length ] ) & 0xC0 ) == 0x80 ) {
			--length;
		}
		text.CapLength( length );
	}
	return text;
}
#endif

/*
===============
idSessionLocal::RetainedCreateServerPage

Multiplayer's retained Create Server sub-page where it can present: NULL while
the session does not offer it (RETAINED_CREATE_SERVER_MISSING without
ui_retainedMultiplayer), in a game, or when the page is not installed, cannot
load or stopped drawing.
===============
*/
idUserInterface *idSessionLocal::RetainedCreateServerPage() {
#ifndef ID_DEDICATED
	// A view that stopped drawing hands the page back for good.
	if ( guiRetainedCreate != NULL && UI_RetainedViewFailed( guiRetainedCreate ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_CREATE_SERVER_GUI );
		retainedStock.AddUnique( RETAINED_CREATE_SERVER_GUI );
		guiRetainedCreate = NULL;
	}
	if ( !Session_RetainedCreateServerEnabled() || mapSpawned ) {
		return NULL;
	}
	if ( guiRetainedCreate == NULL ) {
		guiRetainedCreate = FindRetainedGui( RETAINED_CREATE_SERVER_GUI, true, false );
	}
	return guiRetainedCreate;
#else
	return NULL;
#endif
}

/*
===============
idSessionLocal::OpenRetainedCreateServer

The Create Server sub-page presents with its settings and the chosen game
type's maps published. Where it cannot, CREATE SERVER opens the stock page
from the Multiplayer page, as its other sections do.
===============
*/
void idSessionLocal::OpenRetainedCreateServer() {
#ifndef ID_DEDICATED
	idUserInterface *page = RetainedCreateServerPage();
	if ( page == NULL ) {
		if ( guiRetainedMpPage != NULL && guiActive == guiRetainedMpPage ) {
			HandleRetainedMultiplayerPageRequest( guiRetainedMpPage, "multiplayerCreate" );
		}
		return;
	}
	// Any levelshot the idle frames have not read yet, so browsing the maps
	// never loads one.
	PrecacheRetainedLevelshots( true );
	PublishRetainedCreateServer( page );
	PublishRetainedCreateMaps( page, true );
	SetGUI( page, NULL );
#endif
}

/*
===============
idSessionLocal::PublishRetainedCreateServer

The page's settings as their CVars hold them, within the ranges it offers,
its text fields' (RETAINED_CREATE_TEXTS), and the chosen game type, which
picks the limit row and the slot under Allow Voting.
===============
*/
void idSessionLocal::PublishRetainedCreateServer( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	for ( int i = 0; i < RETAINED_CREATE_SETTING_COUNT; ++i ) {
		const retainedCreateSetting_t &setting = RETAINED_CREATE_SETTINGS[i];
		const int value = idMath::ClampInt( setting.minimum, setting.maximum, cvarSystem->GetCVarInteger( setting.cvar ) );
		if ( setting.boolean ) {
			gui->SetStateBool( va( "create.%s", setting.state ), value != 0 );
		} else {
			gui->SetStateInt( va( "create.%s", setting.state ), value );
		}
	}
	for ( int i = 0; i < RETAINED_CREATE_TEXT_COUNT; ++i ) {
		gui->SetStateString( RETAINED_CREATE_TEXTS[i].state, cvarSystem->GetCVarString( RETAINED_CREATE_TEXTS[i].cvar ) );
	}
	gui->SetStateInt( "create.gametype", Session_RetainedCreateGameType() );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::PublishRetainedCreateMaps

The chosen game type's maps as the stock page's MAPScan lists them: the maps
whose decls play it, else every multiplayer map, si_map's own chosen where
the list has it, else the first. A map's name loses its color codes, as the
retained save labels do: the stock list drew them, the page draws plain text.
`rescan` makes the list again; either way the page's rows, the choice and its
levelshot follow, and si_map takes the choice, as the stock list's selection
did.
===============
*/
void idSessionLocal::PublishRetainedCreateMaps( idUserInterface *gui, bool rescan ) {
#ifndef ID_DEDICATED
	if ( rescan ) {
		const char *gametype = RETAINED_CREATE_GAMETYPES[ Session_RetainedCreateGameType() ];
		const int count = fileSystem->GetNumMaps();
		retainedCreateMaps.Clear();
		for ( int pass = 0; pass < 2 && retainedCreateMaps.Num() == 0; ++pass ) {
			for ( int i = 0; i < count; ++i ) {
				const idDict *dict = fileSystem->GetMapDecl( i );
				if ( pass == 0 ? MapSupportsStartServerGameType( dict, gametype ) : MapSupportsAnyMPGameType( dict ) ) {
					retainedCreateMaps.Append( i );
				}
			}
		}
		retainedCreateMapSelected = retainedCreateMaps.Num() > 0 ? 0 : -1;
		const idStr current = cvarSystem->GetCVarString( "si_map" );
		for ( int i = 0; i < retainedCreateMaps.Num(); ++i ) {
			if ( !current.Icmp( fileSystem->GetMapDecl( retainedCreateMaps[i] )->GetString( "path" ) ) ) {
				retainedCreateMapSelected = i;
				break;
			}
		}
		retainedCreateMapFirst = retainedCreateMapSelected > 0 ?
			retainedCreateMapSelected / RETAINED_CREATE_MAP_ROWS * RETAINED_CREATE_MAP_ROWS : 0;
	}
	for ( int row = 0; row < RETAINED_CREATE_MAP_ROWS; ++row ) {
		const int index = retainedCreateMapFirst + row;
		idStr name;
		if ( index < retainedCreateMaps.Num() ) {
			const idDict *dict = fileSystem->GetMapDecl( retainedCreateMaps[ index ] );
			const char *key = dict->GetString( "name" );
			name = common->GetLanguageDict()->GetString( key[0] != '\0' ? key : dict->GetString( "path" ) );
			name.RemoveColors();
		}
		gui->SetStateString( va( "create.map%d", row ), name.c_str() );
	}
	idStr shot = RETAINED_LOAD_GENERIC_SHOT;
	if ( retainedCreateMapSelected >= 0 ) {
		const char *path = fileSystem->GetMapDecl( retainedCreateMaps[ retainedCreateMapSelected ] )->GetString( "path" );
		cvarSystem->SetCVarString( "si_map", path );
		shot = RetainedPauseShot( path );
	} else {
		cvarSystem->SetCVarString( "si_map", "" );
	}
	gui->SetStateInt( "create.map_count", retainedCreateMaps.Num() );
	gui->SetStateInt( "create.map_first", retainedCreateMapFirst );
	gui->SetStateInt( "create.map_selected", retainedCreateMapSelected );
	gui->SetStateString( "create.shot", shot.c_str() );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::PublishRetainedCreateBans

The local server's ban list on the page's PLAYER BAN LIST: the game publishes
it on the stock menu as the stock popup reads it, and each row reaches the
page as its name (colors removed) and GUID, at most RETAINED_CREATE_BAN_ROWS;
the session keeps the GUIDs, and no row is chosen.
===============
*/
void idSessionLocal::PublishRetainedCreateBans( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	retainedCreateBanGuids.Clear();
	retainedCreateBanSelected = -1;
	if ( game != NULL && guiMainMenu != NULL ) {
		game->PopulateBanList( guiMainMenu );
		for ( int row = 0; row < RETAINED_CREATE_BAN_ROWS; ++row ) {
			const idKeyValue *item = guiMainMenu->State().FindKey( va( "sa_banList_item_%d", row ) );
			idStr name, guid;
			if ( item == NULL || !Session_BanListRow( item->GetValue().c_str(), name, guid ) ) {
				break;
			}
			name.RemoveColors();
			gui->SetStateString( va( "create.ban%d.name", row ), name.c_str() );
			gui->SetStateString( va( "create.ban%d.guid", row ), guid.c_str() );
			retainedCreateBanGuids.Append( guid );
		}
	}
	gui->SetStateInt( "create.ban_count", retainedCreateBanGuids.Num() );
	gui->SetStateInt( "create.ban_selected", -1 );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::HandleRetainedCreateServerRequest

The Create Server sub-page's verbs. A setting's verb carries its value, which
the session checks against the setting's range before it writes the CVar;
the player counts keep their order, a lower maximum lowering the minimum and
a higher minimum raising the maximum. The game type lists its maps again. A
map row chooses its map (create.pick) and the pagers page the list; a text
field hands over its text, which the session caps at the field's stock
length; PLAYER BAN LIST lists the bans, a row chooses its ban
(create.ban_pick) and REMOVE BAN removes it; CREATE SERVER starts the server
as the stock page's does; BACK climbs to the Multiplayer page. A change of
level under way takes none.
===============
*/
void idSessionLocal::HandleRetainedCreateServerRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( retainedSubpageFrom != NULL ) {
		return;
	}
	if ( !idStr::Icmp( request, "createBack" ) ) {
		BeginRetainedSubpage( gui, false, RETAINED_SUBPAGE_CREATE_SERVER );
		return;
	}
	if ( !idStr::Icmp( request, "createStart" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		StartMenuServer();
		return;
	}
	if ( !idStr::Icmp( request, "createMap" ) ) {
		const int row = gui->State().GetInt( "create.pick", "-1" );
		const int index = retainedCreateMapFirst + row;
		if ( row >= 0 && row < RETAINED_CREATE_MAP_ROWS && index < retainedCreateMaps.Num() ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			retainedCreateMapSelected = index;
			PublishRetainedCreateMaps( gui, false );
		}
		return;
	}
	if ( !idStr::Icmp( request, "createMapsNewer" ) || !idStr::Icmp( request, "createMapsOlder" ) ) {
		// A page further, its first map chosen going on, its last coming back.
		const bool older = !idStr::Icmp( request, "createMapsOlder" );
		const int first = retainedCreateMapFirst + ( older ? RETAINED_CREATE_MAP_ROWS : -RETAINED_CREATE_MAP_ROWS );
		if ( first < 0 || first >= retainedCreateMaps.Num() ) {
			return;
		}
		retainedCreateMapFirst = first;
		retainedCreateMapSelected = older ? first : first + RETAINED_CREATE_MAP_ROWS - 1;
		PublishRetainedCreateMaps( gui, false );
		return;
	}
	if ( !idStr::Icmp( request, "createBanList" ) ) {
		PublishRetainedCreateBans( gui );
		return;
	}
	if ( !idStr::Icmp( request, "createBanPick" ) ) {
		const int row = gui->State().GetInt( "create.ban_pick", "-1" );
		if ( row >= 0 && row < retainedCreateBanGuids.Num() ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			retainedCreateBanSelected = row;
			gui->SetStateInt( "create.ban_selected", row );
			gui->StateChanged( common->GetPresentationTime() );
		}
		return;
	}
	if ( !idStr::Icmp( request, "createBanRemove" ) ) {
		// The chosen row's GUID as the session published it, never the page's.
		if ( retainedCreateBanSelected >= 0 && retainedCreateBanSelected < retainedCreateBanGuids.Num() && game != NULL ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			game->RemoveGuidFromBanList( retainedCreateBanGuids[ retainedCreateBanSelected ].c_str() );
			PublishRetainedCreateBans( gui );
		}
		return;
	}
	for ( int i = 0; i < RETAINED_CREATE_TEXT_COUNT; ++i ) {
		const retainedCreateText_t &field = RETAINED_CREATE_TEXTS[i];
		if ( !idStr::Icmp( request, field.verb ) ) {
			const idStr text = Session_RetainedCreateText( gui, field.state, field.limit );
			cvarSystem->SetCVarString( field.cvar, text.c_str() );
			PublishRetainedCreateServer( gui );
			return;
		}
	}
	static const char *const gametypeVerb[] = { "createGameType" };
	int field = -1, value = 0;
	if ( Session_RetainedValueRequest( request, gametypeVerb, 1, field, value ) ) {
		if ( value < 0 || value >= RETAINED_CREATE_GAMETYPE_COUNT ) {
			common->Warning( "retained UI: Create Server asked for game type %d", value );
			return;
		}
		cvarSystem->SetCVarString( "si_gameType", RETAINED_CREATE_GAMETYPES[ value ] );
		PublishRetainedCreateServer( gui );
		PublishRetainedCreateMaps( gui, true );
		return;
	}
	if ( Session_RetainedCreateSettingRequest( request, field, value ) ) {
		const retainedCreateSetting_t &setting = RETAINED_CREATE_SETTINGS[ field ];
		if ( value < setting.minimum || value > setting.maximum ) {
			common->Warning( "retained UI: Create Server asked for %s %d", setting.cvar, value );
			return;
		}
		cvarSystem->SetCVarInteger( setting.cvar, value );
		if ( !idStr::Icmp( setting.cvar, "si_maxPlayers" ) && cvarSystem->GetCVarInteger( "si_minPlayers" ) > value ) {
			cvarSystem->SetCVarInteger( "si_minPlayers", value );
		} else if ( !idStr::Icmp( setting.cvar, "si_minPlayers" ) && cvarSystem->GetCVarInteger( "si_maxPlayers" ) < value ) {
			cvarSystem->SetCVarInteger( "si_maxPlayers", value );
		}
		PublishRetainedCreateServer( gui );
		return;
	}
	common->Warning( "retained UI: unhandled Create Server request '%s'", request );
#endif
}


/*
===============
idSessionLocal::RetainedJoinGamePage

Multiplayer's retained Join Game sub-page where it can present: NULL while the
session does not offer it (RETAINED_JOIN_GAME_MISSING without
ui_retainedMultiplayer), in a game, or when the page is not installed, cannot
load or stopped drawing.
===============
*/
idUserInterface *idSessionLocal::RetainedJoinGamePage() {
#ifndef ID_DEDICATED
	// A view that stopped drawing hands the page back for good.
	if ( guiRetainedJoin != NULL && UI_RetainedViewFailed( guiRetainedJoin ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_JOIN_GAME_GUI );
		retainedStock.AddUnique( RETAINED_JOIN_GAME_GUI );
		guiRetainedJoin = NULL;
	}
	if ( !Session_RetainedJoinGameEnabled() || mapSpawned ) {
		return NULL;
	}
	if ( guiRetainedJoin == NULL ) {
		guiRetainedJoin = FindRetainedGui( RETAINED_JOIN_GAME_GUI, true, false );
	}
	return guiRetainedJoin;
#else
	return NULL;
#endif
}

/*
===============
idSessionLocal::OpenRetainedJoinGame

The Join Game sub-page presents as the stock browser opens
(InitServerBrowser: the favorites read, the mod filter checked), with the
servers the last scan found listed from the page holding the chosen one. Where
it cannot, JOIN GAME opens the stock browser from the Multiplayer page, as
its other sections do.
===============
*/
void idSessionLocal::OpenRetainedJoinGame() {
#ifndef ID_DEDICATED
	idUserInterface *page = RetainedJoinGamePage();
	if ( page == NULL ) {
		if ( guiRetainedMpPage != NULL && guiActive == guiRetainedMpPage ) {
			HandleRetainedMultiplayerPageRequest( guiRetainedMpPage, "multiplayerJoin" );
		}
		return;
	}
	idAsyncNetwork::client.serverList.GUIInit();
	retainedJoinFirst = -1;
	page->SetStateString( "join.clientip", Session_LocalAddressText().c_str() );
	PublishRetainedJoinGame( page );
	SetGUI( page, NULL );
#endif
}

/*
===============
idSessionLocal::UpdateBrowserTooltip

The stock browser's server tooltip, as retail's session kept it: each frame
the classic main menu is the active GUI, the scan shows the hovered row's
server (its address, players and settings) or hides the tooltip.
===============
*/
void idSessionLocal::UpdateBrowserTooltip() {
#ifndef ID_DEDICATED
	if ( guiActive != NULL && guiActive == guiMainMenu ) {
		idAsyncNetwork::client.serverList.GUIUpdateHover();
	}
#endif
}

/*
===============
idSessionLocal::PublishRetainedJoinGame

The servers the scan found and the filter passes, in the sort's order: the
list's page (join.row<i>.*: favorite, password, dedicated server, name,
ping, repeater, players, game type and map, the server's own text read as a
player's), their count, how many the scan holds (join.listed, which tells a
list the filters empty apart), the scan's status line (the stock page's
sortNotice) and whether it runs, its progress (join.progress and
join.notice, the stock page's loadPct and loadNotice), the source the legacy
menu keeps
(gui::lanSet, which the scans read), the mod filter (filterMod, which the
scan writes there), the sort and its direction, and which server is chosen
(join.selected), by the address the session kept, with its levelshot and
whether it is a favorite. A chosen server the list stops holding stays
chosen while a scan may list it again, or while the scan holds it and a
filter or search hides it. A first page of -1 pages to the chosen server.
===============
*/
/*
===============
Session_NetworkQualityBars

Section 13.13's network quality: four bars to 60 ms, three to 120 ms, two to
200 ms and one above, each 2% of packet loss taking one away, never below
one.
===============
*/
#ifndef ID_DEDICATED
static int Session_NetworkQualityBars( int ping, float lossPercent ) {
	int bars = ping <= 60 ? 4 : ping <= 120 ? 3 : ping <= 200 ? 2 : 1;
	if ( lossPercent > 0.0f ) {
		bars -= static_cast<int>( lossPercent / 2.0f );
	}
	return Max( 1, bars );
}
#endif

void idSessionLocal::PublishRetainedJoinGame( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	idServerScan &servers = idAsyncNetwork::client.serverList;
	// A focused row's server, by the address its row last showed: the focus
	// follows it through a new order, a refresh or a filter, onto its page.
	const std::string focus = UI_RetainedFocusedControl( gui );
	idStr focusedAddress;
	if ( focus.rfind( "server-", 0 ) == 0 ) {
		const int row = atoi( focus.c_str() + 7 );
		if ( row >= 0 && row < retainedJoinRowAddresses.Num() ) {
			focusedAddress = retainedJoinRowAddresses[ row ];
		}
	}
	// The list's loading rows show while a scan has listed nothing yet
	// (section 13.13); the first servers replace them with row.reveal.
	const bool wasLoading = gui->State().GetBool( "join.scanning" ) && gui->State().GetInt( "join.count" ) == 0;
	retainedJoinRevision = servers.GetRevision();
	servers.VisibleServers( retainedJoinServers );
	const int count = retainedJoinServers.Num();
	const bool scanning = servers.GetState() != idServerScan::IDLE;
	int selected = -1;
	for ( int i = 0; i < count && retainedJoinAddress.Length() > 0 && selected < 0; ++i ) {
		if ( !retainedJoinAddress.Icmp( Sys_NetAdrToString( servers[ retainedJoinServers[ i ] ].adr ) ) ) {
			selected = i;
		}
	}
	if ( selected < 0 && !scanning ) {
		// A filter or a search may hide the chosen server; it stays chosen
		// while the scan still holds it.
		bool held = false;
		for ( int i = 0; i < servers.Num() && !held && retainedJoinAddress.Length() > 0; ++i ) {
			held = !retainedJoinAddress.Icmp( Sys_NetAdrToString( servers[ i ].adr ) );
		}
		if ( !held ) {
			retainedJoinAddress.Clear();
		}
	}
	if ( retainedJoinFirst < 0 ) {
		retainedJoinFirst = selected > 0 ? selected / RETAINED_JOIN_ROWS * RETAINED_JOIN_ROWS : 0;
	}
	int focusIndex = -1;
	for ( int i = 0; i < count && focusedAddress.Length() > 0 && focusIndex < 0; ++i ) {
		if ( !focusedAddress.Icmp( Sys_NetAdrToString( servers[ retainedJoinServers[ i ] ].adr ) ) ) {
			focusIndex = i;
		}
	}
	if ( focusIndex >= 0 ) {
		retainedJoinFirst = focusIndex / RETAINED_JOIN_ROWS * RETAINED_JOIN_ROWS;
	}
	retainedJoinFirst = idMath::ClampInt( 0, Max( 0, count - 1 ) / RETAINED_JOIN_ROWS * RETAINED_JOIN_ROWS, retainedJoinFirst );
	retainedJoinRowAddresses.SetNum( RETAINED_JOIN_ROWS );
	for ( int row = 0; row < RETAINED_JOIN_ROWS; ++row ) {
		const int index = retainedJoinFirst + row;
		const networkServer_t *server = index < count ? &servers[ retainedJoinServers[ index ] ] : NULL;
		retainedJoinRowAddresses[ row ] = server != NULL ? Sys_NetAdrToString( server->adr ) : "";
		idStr name, ping, players, type, map;
		if ( server != NULL ) {
			name = Session_RetainedPlayerText( server->serverInfo.GetString( "si_name" ), 96, 1 );
			if ( name.Length() == 0 ) {
				name = Sys_NetAdrToString( server->adr );
			}
			ping = va( "%d", Max( 0, server->ping ) );
			players = va( "%d/%d", idMath::ClampInt( 0, MAX_ASYNC_CLIENTS, server->clients ),
				idMath::ClampInt( 0, MAX_ASYNC_CLIENTS, server->serverInfo.GetInt( "si_maxPlayers" ) ) );
			type = Session_RetainedPlayerText( idServerScan::GameTypeName( server->serverInfo.GetString( "si_gameType" ) ), 64, 1 );
			map = Session_RetainedPlayerText( server->serverInfo.GetString( "si_mapName" ), 64, 1 );
		}
		gui->SetStateBool( va( "join.row%d.fav", row ), server != NULL && servers.IsFavoriteServer( retainedJoinServers[ index ] ) );
		gui->SetStateBool( va( "join.row%d.lock", row ), server != NULL && server->serverInfo.GetBool( "si_usePass" ) );
		gui->SetStateBool( va( "join.row%d.ded", row ), server != NULL && server->serverInfo.GetInt( "net_serverDedicated" ) != 0 );
		gui->SetStateBool( va( "join.row%d.rep", row ), server != NULL && server->serverInfo.GetBool( "si_repeater" ) );
		gui->SetStateString( va( "join.row%d.name", row ), name.c_str() );
		gui->SetStateString( va( "join.row%d.ping", row ), ping.c_str() );
		// The browser hears no packet loss, so the ping alone sets the bars.
		gui->SetStateInt( va( "join.row%d.quality", row ), server != NULL ? Session_NetworkQualityBars( Max( 0, server->ping ), 0.0f ) : 0 );
		gui->SetStateString( va( "join.row%d.players", row ), players.c_str() );
		gui->SetStateString( va( "join.row%d.type", row ), type.c_str() );
		gui->SetStateString( va( "join.row%d.map", row ), map.c_str() );
	}
	// The chosen server's levelshot, looked up again only when its map
	// changes; a map path the browser may not look up shows the generic art,
	// as does the frame while no server is chosen (Demos' empty choice too).
	const int chosen = selected >= 0 ? retainedJoinServers[ selected ] : -1;
	const char *chosenMap = chosen >= 0 ? servers[ chosen ].serverInfo.GetString( "si_map" ) : "";
	if ( chosen < 0 ) {
		retainedJoinShotMap.Clear();
		retainedJoinShot.Clear();
	} else if ( retainedJoinShot.Length() == 0 || retainedJoinShotMap.Cmp( chosenMap ) != 0 ) {
		retainedJoinShotMap = chosenMap;
		retainedJoinShot = idServerScan::ValidMapPath( chosenMap ) ? RetainedPauseShot( chosenMap ) : idStr( "gfx/guis/loadscreens/generic" );
		UI_RetainedPrecacheImage( retainedJoinShot.c_str() );
	}
	gui->SetStateString( "join.shot", chosen >= 0 ? retainedJoinShot.c_str() : RETAINED_LOAD_GENERIC_SHOT );
	gui->SetStateBool( "join.favorite", chosen >= 0 && servers.IsFavoriteServer( chosen ) );
	gui->SetStateString( "join.mod", guiMainMenu != NULL ?
		Session_RetainedPlayerText( guiMainMenu->State().GetString( "filterMod" ), 64, 1 ).c_str() : "" );
	gui->SetStateString( "join.search", Session_RetainedPlayerText( servers.GetPlayerSearch(), 64, 1 ).c_str() );
	gui->SetStateBool( "join.filterFull", servers.IsFilterSet( idServerScan::BROWSER_FILTER_FULL ) );
	gui->SetStateBool( "join.filterEmpty", servers.IsFilterSet( idServerScan::BROWSER_FILTER_EMPTY ) );
	gui->SetStateBool( "join.filterPassword", servers.IsFilterSet( idServerScan::BROWSER_FILTER_PASSWORD ) );
	gui->SetStateInt( "join.gameType", servers.GetGameTypeFilter() );
	gui->SetStateString( "join.gameTypeName", idServerScan::GameTypeFilterName( servers.GetGameTypeFilter() ) );
	// The notice can name a server, whose text is read as a player's.
	gui->SetStateFloat( "join.progress", servers.GetLoadProgress() );
	gui->SetStateString( "join.notice", Session_RetainedPlayerText( servers.GetLoadNotice(), 160, 1 ).c_str() );
	bool ascending = true;
	const serverSort_t sort = servers.GetSorting( ascending );
	gui->SetStateInt( "join.count", count );
	gui->SetStateInt( "join.listed", servers.Num() );
	gui->SetStateInt( "join.first", retainedJoinFirst );
	gui->SetStateInt( "join.selected", selected );
	gui->SetStateInt( "join.sort", static_cast<int>( sort ) );
	gui->SetStateBool( "join.ascending", ascending );
	gui->SetStateBool( "join.lan", guiMainMenu != NULL && guiMainMenu->State().GetBool( "lanSet" ) );
	gui->SetStateBool( "join.scanning", scanning );
	gui->SetStateString( "join.status", guiMainMenu != NULL ? guiMainMenu->State().GetString( "sortNotice" ) : "" );
	// The rows may now hold other servers: the details card's again.
	PublishRetainedJoinDetail( gui );
	gui->StateChanged( common->GetPresentationTime() );
	if ( wasLoading && count > 0 ) {
		gui->HandleNamedEvent( "joinRowsReveal" );
	}
	if ( focusIndex >= 0 && focus != va( "server-%d", focusIndex - retainedJoinFirst ) ) {
		gui->HandleNamedEvent( va( "joinFocus%d", focusIndex - retainedJoinFirst ) );
	}
#endif
}

/*
===============
idSessionLocal::UpdateRetainedJoinGame

Each frame the Join Game sub-page presents: the list again once the scan
has moved on (a server answered, a sort or filter changed, the status
changed), and the details card's server once the row under the pointer or
the focus changes.
===============
*/
void idSessionLocal::UpdateRetainedJoinGame() {
#ifndef ID_DEDICATED
	if ( guiRetainedJoin == NULL || guiActive != guiRetainedJoin ) {
		return;
	}
	if ( idAsyncNetwork::client.serverList.GetRevision() != retainedJoinRevision ) {
		PublishRetainedJoinGame( guiRetainedJoin );
	} else if ( guiRetainedJoin->State().GetInt( "join.detail", "-1" ) != retainedJoinDetailRow ) {
		PublishRetainedJoinDetail( guiRetainedJoin );
		guiRetainedJoin->StateChanged( common->GetPresentationTime() );
	}
#endif
}

/*
===============
idSessionLocal::PushRetainedNotice

A notice for the retained menus' stack (section 13.13): it waits until the
stack has room and no notice is leaving, then enters at the top. A
persistent one stays until ResolveRetainedNotices resolves its kind.
===============
*/
void idSessionLocal::PushRetainedNotice( const char *text, int kind, const char *action, const char *subject, int undo,
	bool persistent ) {
	retainedNotice_t notice;
	notice.text = text;
	notice.action = action;
	notice.kind = kind;
	notice.subject = subject;
	notice.undo = undo;
	notice.persistent = persistent;
	notice.resolved = false;
	notice.shownAt = -1;
	notice.leavingAt = -1;
	retainedNotices.Append( notice );
	retainedNoticesDirty = true;
}

/*
===============
idSessionLocal::ResolveRetainedNotices

The state behind a kind of notice is resolved: a shown one leaves, a waiting
one goes.
===============
*/
void idSessionLocal::ResolveRetainedNotices( int kind ) {
	for ( int i = retainedNotices.Num() - 1; i >= 0; --i ) {
		if ( retainedNotices[ i ].kind != kind ) {
			continue;
		}
		if ( retainedNotices[ i ].shownAt < 0 ) {
			retainedNotices.RemoveIndex( i );
			retainedNoticesDirty = true;
		} else {
			retainedNotices[ i ].resolved = true;
		}
	}
}

/*
===============
idSessionLocal::UpdateRetainedNotices

The notices each frame (section 13.13), over whichever retained menu shows:
a page, or the retained home over the legacy menu; never the multiplayer
card. A notice past its hold (4 s, or 8 s when it offers an action; a
persistent one until resolved) leaves over 200 ms (notice.leave, the
menu's noticeLeave<slot>) and then goes; the slot it empties rests
(noticeRest<slot>) and the older notices under it close up. While no notice
leaves and fewer than three show, the oldest waiting one enters at the top
(noticeEnter0) and the others move down. The stack publishes newest first as
notice.<slot>.*, and again in full to a menu that comes to show; the home,
which stays drawn behind a page, drops it while the page shows it. A
controller's loss is a persistent notice (section 13.7) until one connects
again.
===============
*/
void idSessionLocal::UpdateRetainedNotices() {
#ifndef ID_DEDICATED
	const int slots = 3, hold = 4000, actionHold = 8000, leave = 200;
	const int now = common->GetPresentationTime();
	const idStr gamepad = cvarSystem->GetCVarString( "in_gamepadFamily" );
	const idStr family = gamepad.Length() > 0 ? gamepad : idStr( "none" );
	if ( retainedNoticeGamepad.Length() > 0 && family.Icmp( retainedNoticeGamepad ) != 0 ) {
		if ( !family.Icmp( "none" ) ) {
			PushRetainedNotice( "#str_231541", RETAINED_NOTICE_CONTROLLER, "", "", RETAINED_NOTICE_UNDO_NONE, true );
		} else {
			ResolveRetainedNotices( RETAINED_NOTICE_CONTROLLER );
		}
	}
	retainedNoticeGamepad = family;
	idUserInterface *gui = NULL;
	if ( guiActive != NULL && guiActive == guiMainMenu && guiRetainedHome != NULL ) {
		gui = guiRetainedHome;
	} else if ( guiActive != NULL && guiActive != guiRetainedMultiplayer && UI_IsRetainedPath( guiActive->Name() ) ) {
		gui = guiActive;
	}
	idStrList events;
	if ( gui != retainedNoticesGui ) {
		// A home screen stays drawn behind a page, as its frame: it drops the
		// stack the page now shows.
		if ( retainedNoticesGui != NULL && ( retainedNoticesGui == guiRetainedTitle || retainedNoticesGui == guiRetainedPause ||
				retainedNoticesGui == guiRetainedPauseStrogg ) ) {
			for ( int slot = 0; slot < slots; ++slot ) {
				retainedNoticesGui->SetStateBool( va( "notice.%d.shown", slot ), false );
			}
			retainedNoticesGui->StateChanged( now );
		}
		retainedNoticesGui = gui;
		retainedNoticesDirty = true;
		for ( int slot = 0; slot < slots; ++slot ) {
			events.Append( va( "noticeRest%d", slot ) );
		}
	}
	int shown = 0;
	while ( shown < retainedNotices.Num() && retainedNotices[ shown ].shownAt >= 0 ) {
		++shown;
	}
	for ( int i = shown - 1; i >= 0; --i ) {
		retainedNotice_t &notice = retainedNotices[ i ];
		const int slot = shown - 1 - i;
		const bool expired = notice.resolved ||
			( !notice.persistent && now - notice.shownAt >= ( notice.action.Length() > 0 ? actionHold : hold ) );
		if ( notice.leavingAt < 0 && expired ) {
			notice.leavingAt = now;
			events.Append( va( "noticeLeave%d", slot ) );
		} else if ( notice.leavingAt >= 0 && now - notice.leavingAt >= leave ) {
			retainedNotices.RemoveIndex( i );
			--shown;
			events.Append( va( "noticeRest%d", slot ) );
			retainedNoticesDirty = true;
		}
	}
	bool leaving = false;
	for ( int i = 0; i < shown; ++i ) {
		leaving |= retainedNotices[ i ].leavingAt >= 0;
	}
	if ( !leaving && shown < slots && shown < retainedNotices.Num() ) {
		retainedNotices[ shown ].shownAt = now;
		++shown;
		events.Append( "noticeEnter0" );
		retainedNoticesDirty = true;
	}
	if ( gui == NULL ) {
		return;
	}
	if ( retainedNoticesDirty ) {
		retainedNoticesDirty = false;
		for ( int slot = 0; slot < slots; ++slot ) {
			const retainedNotice_t *notice = slot < shown ? &retainedNotices[ shown - 1 - slot ] : NULL;
			gui->SetStateBool( va( "notice.%d.shown", slot ), notice != NULL );
			gui->SetStateString( va( "notice.%d.text", slot ), notice != NULL ? notice->text.c_str() : "" );
			gui->SetStateString( va( "notice.%d.action", slot ), notice != NULL ? notice->action.c_str() : "" );
			gui->SetStateInt( va( "notice.%d.kind", slot ), notice != NULL ? notice->kind : 0 );
		}
		gui->StateChanged( now );
	}
	for ( int i = 0; i < events.Num(); ++i ) {
		gui->HandleNamedEvent( events[ i ].c_str() );
	}
#endif
}

/*
===============
idSessionLocal::HandleRetainedNoticeAction

A notice's action on the menu that shows the stack (section 13.13): Undo
puts a removed favorite back, if the scan still holds its server, and the
notice leaves.
===============
*/
void idSessionLocal::HandleRetainedNoticeAction( idUserInterface *gui, int slot ) {
#ifndef ID_DEDICATED
	if ( gui == NULL || gui != retainedNoticesGui ) {
		return;
	}
	int shown = 0;
	while ( shown < retainedNotices.Num() && retainedNotices[ shown ].shownAt >= 0 ) {
		++shown;
	}
	const int index = shown - 1 - slot;
	if ( index < 0 || retainedNotices[ index ].leavingAt >= 0 || retainedNotices[ index ].action.Length() == 0 ) {
		return;
	}
	retainedNotice_t &notice = retainedNotices[ index ];
	HandleMainMenuCommands( "play main_menu_selection" );
	if ( notice.undo == RETAINED_NOTICE_UNDO_FAVORITE ) {
		idServerScan &servers = idAsyncNetwork::client.serverList;
		for ( int i = 0; i < servers.Num(); ++i ) {
			if ( !notice.subject.Icmp( Sys_NetAdrToString( servers[ i ].adr ) ) ) {
				if ( !servers.IsFavoriteServer( i ) ) {
					servers.ToggleFavoriteServer( i );
				}
				break;
			}
		}
		if ( guiRetainedJoin != NULL && guiActive == guiRetainedJoin ) {
			PublishRetainedJoinGame( guiRetainedJoin );
		}
	}
	notice.leavingAt = common->GetPresentationTime();
	gui->HandleNamedEvent( va( "noticeLeave%d", slot ) );
#endif
}

/*
===============
idSessionLocal::PublishRetainedJoinDetail

The details card's server: the page row under the pointer or the focus
(join.detail, which the runtime writes as they move) names one of the
listed servers, whose address, players and settings the session publishes
as the stock browser's tooltip shows them (idServerScan::GetServerDetails),
under its name in full, read as players' text, with the row they describe (join.detail.row), which
shows the card once it matches the row. A row without a server publishes
none.
===============
*/
void idSessionLocal::PublishRetainedJoinDetail( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	const int row = gui->State().GetInt( "join.detail", "-1" );
	const int at = retainedJoinFirst + row;
	const int index = row >= 0 && at >= 0 && at < retainedJoinServers.Num() ? retainedJoinServers[ at ] : -1;
	idStr address;
	idStrList players, settings;
	const bool described = idAsyncNetwork::client.serverList.GetServerDetails( index, address, players, settings );
	retainedJoinDetailRow = row;
	gui->SetStateInt( "join.detail.row", described ? row : -1 );
	gui->SetStateString( "join.detail.address", Session_RetainedPlayerText( address.c_str(), 64, 1 ).c_str() );
	// The server's name in full, which its row cuts (section 13.13).
	gui->SetStateString( "join.detail.name", described ?
		Session_RetainedPlayerText( idAsyncNetwork::client.serverList[ index ].serverInfo.GetString( "si_name" ), 96, 1 ).c_str() : "" );
	for ( int i = 0; i < RETAINED_JOIN_DETAIL_PLAYERS; ++i ) {
		const idStr player = i < players.Num() ? Session_RetainedPlayerText( players[ i ].c_str(), 64, 1 ) : idStr();
		gui->SetStateString( va( "join.detail.player%d", i ), player.c_str() );
	}
	for ( int i = 0; i < RETAINED_JOIN_DETAIL_SETTINGS; ++i ) {
		const idStr setting = i < settings.Num() ? Session_RetainedPlayerText( settings[ i ].c_str(), 128, 1 ) : idStr();
		gui->SetStateString( va( "join.detail.setting%d", i ), setting.c_str() );
	}
#endif
}

/*
===============
idSessionLocal::HandleRetainedJoinGameRequest

The Join Game sub-page's verbs, the stock browser's commands on the source
the legacy menu keeps (gui::lanSet): INTERNET and LAN choose the source and
get a new list from it, as the stock tags do; GET NEW LIST gets a new list
and REFRESH LIST asks again; a column heading sorts by its column, and again
the other way, and CLEAR SORTING lists the servers as they answered; MOD's
spinner shows the previous or next mod the list holds; a row chooses its
server (join.pick) and the pagers page the list; the favorite toggle adds
the chosen server to the favorites or takes it off; JOIN SERVER connects to
the chosen server by the address the session kept, never the page's; BACK
climbs to the Multiplayer page. JOIN IP ADDRESS connects to the address
the player entered (join.address), as the stock popup's does, or says why it
cannot (join.addressError); FIND PLAYER/CLAN searches the list for the name
the player entered (join.find), and the search's tag ends it. The filter
row's tags hide full servers, empty ones or those with a password, and its
spinner shows the previous or next game type, or every type. A change of
level under way takes none.
===============
*/
void idSessionLocal::HandleRetainedJoinGameRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( retainedSubpageFrom != NULL ) {
		return;
	}
	idServerScan &servers = idAsyncNetwork::client.serverList;
	if ( !idStr::Icmp( request, "joinBack" ) ) {
		BeginRetainedSubpage( gui, false, RETAINED_SUBPAGE_JOIN_GAME );
		return;
	}
	const bool spectate = !idStr::Icmp( request, "joinSpectate" );
	if ( spectate || !idStr::Icmp( request, "joinServer" ) ) {
		if ( gui->State().GetInt( "join.selected", "-1" ) < 0 || retainedJoinAddress.Length() == 0 ) {
			return;
		}
		HandleMainMenuCommands( "play main_menu_selection" );
		// SPECTATE joins the chosen server watching (the prompt bar's list
		// pattern), and JOIN plays, whichever the last join chose.
		cvarSystem->SetCVarString( "ui_spectate", spectate ? "Spectate" : "Play" );
		// A join that fails or is aborted comes back to this list.
		retainedJoinReturn = true;
		idCmdArgs connectArgs;
		connectArgs.AppendArg( "connect" );
		connectArgs.AppendArg( retainedJoinAddress.c_str() );
		cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, connectArgs );
		return;
	}
	if ( !idStr::Icmp( request, "joinPick" ) ) {
		const int row = gui->State().GetInt( "join.pick", "-1" );
		const int index = retainedJoinFirst + row;
		if ( row >= 0 && row < RETAINED_JOIN_ROWS && index < retainedJoinServers.Num() && retainedJoinServers[ index ] < servers.Num() ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			retainedJoinAddress = Sys_NetAdrToString( servers[ retainedJoinServers[ index ] ].adr );
			PublishRetainedJoinGame( gui );
		}
		return;
	}
	if ( !idStr::Icmp( request, "joinConnect" ) ) {
		const idStr address = Session_RetainedCreateText( gui, "join.address", 99 );
		if ( address.Length() == 0 ) {
			return;
		}
		HandleMainMenuCommands( "play main_menu_selection" );
		idStr error;
		retainedJoinReturn = true;
		if ( !Session_ConnectToTypedAddress( address.c_str(), error ) ) {
			retainedJoinReturn = false;
			gui->SetStateString( "join.addressError", Session_RetainedPlayerText( error.c_str(), 160, 2 ).c_str() );
			gui->StateChanged( common->GetPresentationTime() );
			return;
		}
		// Connecting: the dialog closes, so a return to the menu finds the list.
		gui->HandleNamedEvent( "joinIpModalHide" );
		return;
	}
	if ( !idStr::Icmp( request, "joinSearch" ) || !idStr::Icmp( request, "joinSearchClear" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		servers.SetPlayerSearch( !idStr::Icmp( request, "joinSearch" ) ? Session_RetainedCreateText( gui, "join.find", 99 ).c_str() : "" );
		PublishRetainedJoinGame( gui );
		return;
	}
	if ( !idStr::Icmp( request, "joinFavorite" ) ) {
		for ( int i = 0; i < retainedJoinServers.Num() && retainedJoinAddress.Length() > 0; ++i ) {
			if ( !retainedJoinAddress.Icmp( Sys_NetAdrToString( servers[ retainedJoinServers[ i ] ].adr ) ) ) {
				HandleMainMenuCommands( "play main_menu_selection" );
				if ( servers.ToggleFavoriteServer( retainedJoinServers[ i ] ) ) {
					// A notice says so (section 13.13); a removal offers Undo.
					const bool favorite = servers.IsFavoriteServer( retainedJoinServers[ i ] );
					PushRetainedNotice( favorite ? "#str_231539" : "#str_231540", RETAINED_NOTICE_FAVORITE, favorite ? "" : "#str_42904",
						retainedJoinAddress.c_str(), favorite ? RETAINED_NOTICE_UNDO_NONE : RETAINED_NOTICE_UNDO_FAVORITE, false );
					UpdateRetainedNotices();
				}
				PublishRetainedJoinGame( gui );
				break;
			}
		}
		return;
	}
	if ( !idStr::Icmp( request, "joinModPrevious" ) || !idStr::Icmp( request, "joinModNext" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		servers.UpdateFilterByMod( !idStr::Icmp( request, "joinModNext" ) ? 1 : -1 );
		PublishRetainedJoinGame( gui );
		return;
	}
	if ( !idStr::Icmp( request, "joinFilterFull" ) || !idStr::Icmp( request, "joinFilterEmpty" ) ||
		!idStr::Icmp( request, "joinFilterPassword" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		servers.ToggleFilter( !idStr::Icmp( request, "joinFilterFull" ) ? idServerScan::BROWSER_FILTER_FULL :
			!idStr::Icmp( request, "joinFilterEmpty" ) ? idServerScan::BROWSER_FILTER_EMPTY : idServerScan::BROWSER_FILTER_PASSWORD );
		PublishRetainedJoinGame( gui );
		return;
	}
	// The empty list's CLEAR FILTERS (section 13.13): every filter the row,
	// the mod and the search set, at once.
	if ( !idStr::Icmp( request, "joinClearFilters" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		servers.ClearFilters();
		PublishRetainedJoinGame( gui );
		return;
	}
	if ( !idStr::Icmp( request, "joinGameTypePrevious" ) || !idStr::Icmp( request, "joinGameTypeNext" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		servers.StepGameTypeFilter( !idStr::Icmp( request, "joinGameTypeNext" ) ? 1 : -1 );
		PublishRetainedJoinGame( gui );
		return;
	}
	if ( !idStr::Icmp( request, "joinNewer" ) || !idStr::Icmp( request, "joinOlder" ) ) {
		const int first = retainedJoinFirst + ( !idStr::Icmp( request, "joinOlder" ) ? RETAINED_JOIN_ROWS : -RETAINED_JOIN_ROWS );
		if ( first >= 0 && first < retainedJoinServers.Num() ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			retainedJoinFirst = first;
			PublishRetainedJoinGame( gui );
		}
		return;
	}
	if ( guiMainMenu == NULL ) {
		return;
	}
	if ( !idStr::Icmp( request, "joinSourceInternet" ) || !idStr::Icmp( request, "joinSourceLan" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		guiMainMenu->SetStateBool( "lanSet", !idStr::Icmp( request, "joinSourceLan" ) );
		guiMainMenu->StateChanged( common->GetPresentationTime() );
		Session_ScanServers( guiMainMenu->State().GetBool( "lanSet" ), true );
		PublishRetainedJoinGame( gui );
		return;
	}
	if ( !idStr::Icmp( request, "joinNewList" ) || !idStr::Icmp( request, "joinRefresh" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		Session_ScanServers( guiMainMenu->State().GetBool( "lanSet" ), !idStr::Icmp( request, "joinNewList" ) );
		PublishRetainedJoinGame( gui );
		return;
	}
	if ( !idStr::Icmp( request, "joinClearSort" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		servers.ResetSorting();
		PublishRetainedJoinGame( gui );
		return;
	}
	for ( int i = 0; i < RETAINED_JOIN_SORT_COUNT; ++i ) {
		if ( !idStr::Icmp( request, RETAINED_JOIN_SORTS[i].verb ) ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			servers.SetSorting( RETAINED_JOIN_SORTS[i].sort );
			PublishRetainedJoinGame( gui );
			return;
		}
	}
	// The controller's north (section 13.5): the list sorted by the column
	// after the sort's among the text columns, ascending, from PING when the
	// list is unsorted or sorted by a symbol.
	if ( !idStr::Icmp( request, "joinSortNext" ) ) {
		static const serverSort_t order[] = { SORT_PING, SORT_SERVERNAME, SORT_PLAYERS, SORT_GAMETYPE, SORT_MAP };
		const int count = static_cast<int>( sizeof( order ) / sizeof( order[0] ) );
		bool ascending = true;
		const serverSort_t current = servers.GetSorting( ascending );
		int next = 0;
		for ( int i = 0; i < count; ++i ) {
			if ( order[i] == current ) {
				next = ( i + 1 ) % count;
			}
		}
		HandleMainMenuCommands( "play main_menu_selection" );
		servers.SetSorting( order[ next ] );
		PublishRetainedJoinGame( gui );
		return;
	}
	common->Warning( "retained UI: unhandled Join Game request '%s'", request );
#endif
}

/*
===============
idSessionLocal::HandleRetainedArenaRequest

The Arena page's verbs. Each runs the Arena campaign's own command, which
checks it again (a locked tier or match, a match it cannot start, a reset
without progress); BACK climbs to Single Player. A change of level in
progress, or the campaign's entrance, takes none.
===============
*/
void idSessionLocal::HandleRetainedArenaRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( retainedSubpageFrom != NULL || arenaCampaign.IsEntering() ) {
		return;
	}
	if ( !idStr::Icmp( request, "arenaBack" ) ) {
		BeginRetainedSubpage( gui, false, RETAINED_SUBPAGE_ARENA );
		return;
	}
	for ( int i = 0; i < RETAINED_ARENA_VERB_COUNT; ++i ) {
		if ( !idStr::Icmp( request, RETAINED_ARENA_VERBS[i][0] ) ) {
			HandleMainMenuCommands( "play main_menu_selection" );
			arenaCampaign.HandleGuiCommand( RETAINED_ARENA_VERBS[i][1] );
			return;
		}
	}
#endif
}

/*
===============
idSessionLocal::PublishRetainedMods

The title's MODS dialog: every installed mod, as the stock list lists them,
the current game's selected, and the selected mod's details. The dialog holds
RETAINED_MOD_ROWS of them.
===============
*/
void idSessionLocal::PublishRetainedMods( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	idModList *list = fileSystem->ListMods();
	const idStr current = cvarSystem->GetCVarString( "fs_game" );
	modsList.Clear();
	int select = -1;
	for ( int i = 0; i < list->GetNumMods() && modsList.Num() < RETAINED_MOD_ROWS; ++i ) {
		modsList.Append( list->GetInfo( i ) );
		if ( select < 0 && !current.Icmp( modsList[ modsList.Num() - 1 ].directory.c_str() ) ) {
			select = modsList.Num() - 1;
		}
	}
	fileSystem->FreeModList( list );
	for ( int row = 0; row < RETAINED_MOD_ROWS; ++row ) {
		const idModInfo *info = row < modsList.Num() ? &modsList[ row ] : NULL;
		gui->SetStateString( va( "mods.row%d.name", row ), info == NULL ? "" :
			( info->displayName.Length() > 0 ? info->displayName.c_str() : info->directory.c_str() ) );
		gui->SetStateString( va( "mods.row%d.version", row ), info == NULL ? "" : info->version.c_str() );
	}
	gui->SetStateInt( "mods.count", modsList.Num() );
	SelectRetainedMod( gui, select >= 0 ? select : 0 );
#endif
}

// A row's first Accept: its mod's author, release date, required engine and
// directory.
void idSessionLocal::SelectRetainedMod( idUserInterface *gui, int row ) {
#ifndef ID_DEDICATED
	if ( modsList.Num() == 0 ) {
		retainedModSelected = -1;
	} else if ( row >= 0 && row < modsList.Num() ) {
		retainedModSelected = row;
	}
	const idModInfo *info = retainedModSelected >= 0 ? &modsList[ retainedModSelected ] : NULL;
	gui->SetStateInt( "mods.selected", retainedModSelected );
	gui->SetStateString( "mods.author", info != NULL ? info->author.c_str() : "" );
	gui->SetStateString( "mods.date", info != NULL ? info->releaseDate.c_str() : "" );
	gui->SetStateString( "mods.engine", info != NULL ? info->requiredopenQ4Version.c_str() : "" );
	gui->SetStateString( "mods.directory", info != NULL ? info->directory.c_str() : "" );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::RetainedDemosPage

Multiplayer's retained Demos sub-page where it can present: NULL in a game,
during playback, or when the page is not installed, cannot load or stopped
drawing.
===============
*/
idUserInterface *idSessionLocal::RetainedDemosPage() {
#ifndef ID_DEDICATED
	// A view that stopped drawing hands the page back for good.
	if ( guiRetainedDemos != NULL && UI_RetainedViewFailed( guiRetainedDemos ) ) {
		common->Warning( "retained UI: '%s' stopped drawing; the stock screen presents instead", RETAINED_DEMOS_GUI );
		retainedStock.AddUnique( RETAINED_DEMOS_GUI );
		guiRetainedDemos = NULL;
	}
	if ( !Session_RetainedScreensEnabled() || mapSpawned || IsDemoPlaybackActive() ) {
		return NULL;
	}
	if ( guiRetainedDemos == NULL ) {
		guiRetainedDemos = FindRetainedGui( RETAINED_DEMOS_GUI, true, false );
	}
	return guiRetainedDemos;
#else
	return NULL;
#endif
}

/*
===============
idSessionLocal::OpenRetainedDemos

The Demos sub-page presents with the library DEMOS published as the
Multiplayer page left. Where it cannot, the legacy demo browser opens over
the Multiplayer page instead.
===============
*/
void idSessionLocal::OpenRetainedDemos() {
#ifndef ID_DEDICATED
	idUserInterface *page = RetainedDemosPage();
	if ( page == NULL ) {
		if ( guiRetainedMpPage != NULL && guiActive == guiRetainedMpPage ) {
			OpenDemoMenu( true );
		}
		return;
	}
	SetGUI( page, NULL );
#endif
}

/*
===============
idSessionLocal::PublishRetainedDemosPage

The Demos page's library: every recording, rescanned, that its filter shows,
newest first. The demo at `selectPath` is selected where the filter still
shows it, else the one at `selectRow` (the next after a deletion), else the
first.
===============
*/
void idSessionLocal::PublishRetainedDemosPage( idUserInterface *gui, const char *selectPath, int selectRow, const char *error ) {
#ifndef ID_DEDICATED
	ScanDemoLibrary();
	retainedDemoFilter = idMath::ClampInt( 0, RETAINED_DEMO_FILTER_COUNT - 1, retainedDemoFilter );
	retainedDemoRows.Clear();
	for ( int i = 0; i < demoLibrary.Num(); ++i ) {
		if ( Session_DemoEntryMatchesFilter( demoLibrary[i], RETAINED_DEMO_FILTERS[ retainedDemoFilter ] ) ) {
			retainedDemoRows.Append( i );
		}
	}
	const int count = retainedDemoRows.Num();
	int select = count > 0 ? idMath::ClampInt( 0, count - 1, selectRow ) : -1;
	for ( int row = 0; selectPath != NULL && row < count; ++row ) {
		if ( !demoLibrary[ retainedDemoRows[row] ].path.Icmp( selectPath ) ) {
			select = row;
			break;
		}
	}
	retainedDemoSelected = select;
	retainedDemoFirst = select > 0 ? select / RETAINED_DEMO_ROWS * RETAINED_DEMO_ROWS : 0;
	gui->SetStateInt( "demos.filter", retainedDemoFilter );
	PublishRetainedDemoRows( gui );
	PublishRetainedDemoSelection( gui, error );
#endif
}

// Each row's demo: its name, map, date and duration.
void idSessionLocal::PublishRetainedDemoRows( idUserInterface *gui ) {
#ifndef ID_DEDICATED
	for ( int row = 0; row < RETAINED_DEMO_ROWS; ++row ) {
		const int index = retainedDemoFirst + row;
		sessionDemoText_t text;
		if ( index < retainedDemoRows.Num() ) {
			Session_DemoEntryText( demoLibrary[ retainedDemoRows[ index ] ], text );
		}
		gui->SetStateString( va( "demos.row%d.name", row ), text.name.c_str() );
		gui->SetStateString( va( "demos.row%d.map", row ), text.map.c_str() );
		gui->SetStateString( va( "demos.row%d.date", row ), text.date.c_str() );
		gui->SetStateString( va( "demos.row%d.length", row ), text.length.c_str() );
	}
	gui->SetStateInt( "demos.count", retainedDemoRows.Num() );
	gui->SetStateInt( "demos.first", retainedDemoFirst );
#endif
}

// The selected demo: its map's levelshot (the generic picture without one),
// its kind, status and details, what it can do, the delete confirmation's
// question naming it, and why an operation failed.
void idSessionLocal::PublishRetainedDemoSelection( idUserInterface *gui, const char *error ) {
#ifndef ID_DEDICATED
	idStr shot = RETAINED_LOAD_GENERIC_SHOT, confirm;
	sessionDemoText_t text;
	bool playable = false, canPlay = false, canDelete = false;
	if ( retainedDemoSelected >= 0 && retainedDemoSelected < retainedDemoRows.Num() ) {
		const demoLibraryEntry_t &entry = demoLibrary[ retainedDemoRows[ retainedDemoSelected ] ];
		Session_DemoEntryText( entry, text );
		if ( !entry.mapName.IsEmpty() ) {
			shot = RetainedPauseShot( entry.mapName.c_str() );
		}
		playable = entry.playable;
		canPlay = ( entry.capabilities & DEMO_CAP_PLAY ) != 0;
		canDelete = ( entry.capabilities & DEMO_CAP_DELETE ) != 0;
		confirm = va( common->GetLanguageDict()->GetString( "#str_41555" ), text.name.c_str() );
	}
	gui->SetStateInt( "demos.selected", retainedDemoSelected );
	gui->SetStateString( "demos.shot", shot.c_str() );
	gui->SetStateString( "demos.kind", text.kind.c_str() );
	gui->SetStateString( "demos.status", text.status.c_str() );
	gui->SetStateString( "demos.detail", text.detail.c_str() );
	gui->SetStateBool( "demos.playable", playable );
	gui->SetStateBool( "demos.canPlay", canPlay );
	gui->SetStateBool( "demos.canDelete", canDelete );
	gui->SetStateString( "demos.confirm", confirm.c_str() );
	gui->SetStateString( "demos.error", error != NULL ? error : "" );
	gui->StateChanged( common->GetPresentationTime() );
#endif
}

/*
===============
idSessionLocal::HandleRetainedDemosRequest

The Demos page's verbs. A row's first Accept selects its demo; PLAY, or
Accept on the selected demo, plays it; DELETE deletes it once the page asked;
REFRESH rescans; a filter tag sets or clears demos.filter; Back returns the
title.
===============
*/
void idSessionLocal::HandleRetainedDemosRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	const int count = retainedDemoRows.Num();
	const bool chosen = retainedDemoSelected >= 0 && retainedDemoSelected < count;
	const idStr selectedPath = chosen ? demoLibrary[ retainedDemoRows[ retainedDemoSelected ] ].path : idStr();
	if ( !idStr::Icmp( request, "demosBack" ) ) {
		BeginRetainedSubpage( gui, false, RETAINED_SUBPAGE_DEMOS );
		return;
	}
	if ( !idStr::Icmp( request, "demosSelect" ) ) {
		const int row = gui->State().GetInt( "demos.pick", "-1" );
		const int index = retainedDemoFirst + row;
		if ( row >= 0 && row < RETAINED_DEMO_ROWS && index < count ) {
			retainedDemoSelected = index;
			PublishRetainedDemoSelection( gui, NULL );
		}
		return;
	}
	if ( !idStr::Icmp( request, "demosNewer" ) || !idStr::Icmp( request, "demosOlder" ) ) {
		// A page further: its first demo going to older ones, its last coming
		// back to newer ones, is selected and focused.
		const bool older = !idStr::Icmp( request, "demosOlder" );
		const int first = retainedDemoFirst + ( older ? RETAINED_DEMO_ROWS : -RETAINED_DEMO_ROWS );
		if ( first < 0 || first >= count ) {
			return;
		}
		retainedDemoFirst = first;
		retainedDemoSelected = older ? first : first + RETAINED_DEMO_ROWS - 1;
		PublishRetainedDemoRows( gui );
		PublishRetainedDemoSelection( gui, NULL );
		gui->HandleNamedEvent( older ? "pagedOlder" : "pagedNewer" );
		return;
	}
	if ( !idStr::Icmp( request, "demosRefresh" ) ) {
		HandleMainMenuCommands( "play main_menu_selection" );
		PublishRetainedDemosPage( gui, chosen ? selectedPath.c_str() : NULL, retainedDemoSelected > 0 ? retainedDemoSelected : 0, NULL );
		return;
	}
	if ( !idStr::Icmp( request, "demosFilter" ) ) {
		retainedDemoFilter = gui->State().GetInt( "demos.filter", "0" );
		PublishRetainedDemosPage( gui, chosen ? selectedPath.c_str() : NULL, 0, NULL );
		return;
	}
	if ( !chosen ) {
		return;
	}
	const int index = retainedDemoRows[ retainedDemoSelected ];
	if ( !idStr::Icmp( request, "demosPlay" ) ) {
		if ( ( demoLibrary[ index ].capabilities & DEMO_CAP_PLAY ) == 0 ) {
			return;
		}
		HandleMainMenuCommands( "play main_menu_selection" );
		// Playback takes the screen; a demo that could not start leaves the
		// page here to say so.
		if ( !PlayDemoLibraryEntry( index ) ) {
			openq4::NativeInputBeforeSessionChange();
			guiActive = gui;
			PublishRetainedDemoSelection( gui, "#str_231120" );
		}
		return;
	}
	if ( !idStr::Icmp( request, "demosDelete" ) ) {
		// The page asked first. The demo that takes the deleted one's place
		// is selected; one that could not be deleted stays selected and says so.
		const int at = retainedDemoSelected;
		if ( !DeleteDemoLibraryFile( demoLibrary[ index ] ) ) {
			PublishRetainedDemoSelection( gui, "#str_231119" );
			return;
		}
		PublishRetainedDemosPage( gui, NULL, at, NULL );
		return;
	}
	common->Warning( "retained UI: unhandled Demos request '%s'", request );
#endif
}

#ifndef ID_DEDICATED
// A mod that ships its own loading screen under a stock name keeps it: the
// file comes from the mod's own game directory (fs_game or fs_game_base),
// not from openQ4, the stock game or the Awakening, whose stock screens the
// retained one replaces.
static bool Session_ModSuppliesFile( const char *path ) {
	const char *const mods[] = { cvarSystem->GetCVarString( "fs_game" ), cvarSystem->GetCVarString( "fs_game_base" ) };
	for ( int i = 0; i < static_cast<int>( sizeof( mods ) / sizeof( mods[0] ) ); ++i ) {
		const char *mod = mods[i];
		if ( mod == NULL || mod[0] == '\0' || !idStr::Icmp( mod, OPENQ4_GAMEDIR ) || !idStr::Icmp( mod, BASE_GAMEDIR ) ||
				!idStr::Icmp( mod, "q4xbase" ) ) {
			continue;
		}
		idFile *file = fileSystem->OpenFileRead( path, false, mod );
		if ( file != NULL ) {
			fileSystem->CloseFile( file );
			return true;
		}
	}
	return false;
}
#endif

idUserInterface *idSessionLocal::SelectRetainedLoadingGui( idUserInterface *legacy, bool multiplayer ) {
#ifdef ID_DEDICATED
	return legacy;
#else
	if ( !Session_RetainedScreensEnabled() || legacy == NULL ) {
		return legacy;
	}
	// Only the stock loading screens are replaced; a map's own GUI stays, and
	// so does a mod's own copy of a stock one.
	static const char *stockScreens[] = {
		"guis/loading/generic.gui", "guis/loading/splevel.gui", "guis/loading/mplevel.gui", "guis/loading/intro.gui"
	};
	bool stock = false;
	for ( int i = 0; i < static_cast<int>( sizeof( stockScreens ) / sizeof( stockScreens[0] ) ); ++i ) {
		stock |= idStr::Icmp( legacy->Name(), stockScreens[i] ) == 0;
	}
	if ( !stock ) {
		return legacy;
	}
	if ( Session_ModSuppliesFile( legacy->Name() ) ) {
		common->DPrintf( "retained UI: the mod supplies its own '%s', so it presents instead of the retained loading screen\n", legacy->Name() );
		return legacy;
	}
	idUserInterface *retained = FindRetainedGui( RETAINED_LOADING_GUI, true, false );
	if ( retained == NULL ) {
		return legacy;
	}
	retained->SetStateBool( "loading_mp", multiplayer );
	retained->SetStateBool( "loading_intro", idStr::Icmp( legacy->Name(), "guis/loading/intro.gui" ) == 0 );
	retained->SetStateBool( "loading_ready", false );
	// Whole again after the last join's hand-off faded it out.
	retained->HandleNamedEvent( "present" );
	return retained;
#endif
}

void idSessionLocal::PrefetchRetainedScreens() {
#ifndef ID_DEDICATED
	// PreloadRetainedScreens' documents, in its order, the title first. The
	// pages a gate or a missing page will refuse are parsed for nothing.
	if ( !Session_RetainedScreensEnabled() ) {
		return;
	}
	const char *const documents[] = {
		RETAINED_TITLE_GUI, RETAINED_LOADGAME_GUI, RETAINED_CREDITS_GUI, RETAINED_DEMOS_GUI, RETAINED_ARENA_GUI,
		RETAINED_MPPAGE_GUI, RETAINED_SETTINGSPAGE_GUI, Session_RetainedCreateServerEnabled() ? RETAINED_CREATE_SERVER_GUI : NULL,
		Session_RetainedJoinGameEnabled() ? RETAINED_JOIN_GAME_GUI : NULL
	};
	UI_RetainedPrefetchDocuments( documents, static_cast<int>( sizeof( documents ) / sizeof( documents[0] ) ) );
#endif
}

void idSessionLocal::PreloadRetainedScreens() {
#ifndef ID_DEDICATED
	// Loaded with the stock menus, so a document's own pictures resolve
	// before the first menu opens. Only the screens the title reaches: the
	// pause screens and the pages only they reach load with a level
	// (PrepareRetainedLevel), and the title waits for none of them.
	if ( !Session_RetainedScreensEnabled() ) {
		return;
	}
	if ( guiRetainedTitle == NULL ) {
		guiRetainedTitle = FindRetainedGui( RETAINED_TITLE_GUI, false, true );
	}
	// The Load Game page too, which only a retained home reaches, so LOAD
	// GAME never stalls its hand-off on a load (about 0.2 s), and the picture
	// a save without one shows.
	if ( guiRetainedLoad == NULL && guiRetainedTitle != NULL ) {
		guiRetainedLoad = FindRetainedGui( RETAINED_LOADGAME_GUI, true, false );
		if ( guiRetainedLoad != NULL ) {
			UI_RetainedPrecacheImage( RETAINED_LOAD_GENERIC_SHOT );
		}
	}
	// The Credits page, which only the title reaches.
	if ( guiRetainedCredits == NULL && guiRetainedTitle != NULL ) {
		guiRetainedCredits = FindRetainedGui( RETAINED_CREDITS_GUI, true, false );
	}
	// The Demos page, which only the title reaches.
	if ( guiRetainedDemos == NULL && guiRetainedTitle != NULL ) {
		guiRetainedDemos = FindRetainedGui( RETAINED_DEMOS_GUI, true, false );
	}
	// The Arena page, which only the title reaches, through Single Player.
	if ( guiRetainedArena == NULL && guiRetainedTitle != NULL ) {
		guiRetainedArena = FindRetainedGui( RETAINED_ARENA_GUI, true, false );
	}
	// The Multiplayer page, which only the title reaches, over a main menu
	// with the routes to its Multiplayer pages.
	if ( guiRetainedMpPage == NULL && guiRetainedTitle != NULL && Session_RetainedMultiplayerRoutes( guiMainMenu ) ) {
		guiRetainedMpPage = FindRetainedGui( RETAINED_MPPAGE_GUI, true, false );
	}
	// Its Create Server sub-page, where the session offers it. Every
	// multiplayer map's levelshot precaches too, so choosing a map never loads
	// one late, but on the title's idle frames (PrecacheRetainedLevelshots):
	// read here, they held the title back by over a second.
	if ( guiRetainedCreate == NULL && guiRetainedMpPage != NULL && Session_RetainedCreateServerEnabled() ) {
		guiRetainedCreate = FindRetainedGui( RETAINED_CREATE_SERVER_GUI, true, false );
		retainedLevelshots.Clear();
		retainedLevelshotsFrom = 0;
		for ( int i = 0; guiRetainedCreate != NULL && i < fileSystem->GetNumMaps(); ++i ) {
			const idDict *dict = fileSystem->GetMapDecl( i );
			if ( MapSupportsAnyMPGameType( dict ) ) {
				retainedLevelshot_t shot;
				shot.name = dict->GetString( "path" );
				shot.map = true;
				shot.material = false;
				retainedLevelshots.Append( shot );
			}
		}
	}
	// Its Join Game sub-page, where the session offers it.
	if ( guiRetainedJoin == NULL && guiRetainedMpPage != NULL && Session_RetainedJoinGameEnabled() ) {
		guiRetainedJoin = FindRetainedGui( RETAINED_JOIN_GAME_GUI, true, false );
	}
#endif
}

void idSessionLocal::PrepareRetainedLevel( const char *mapPath, bool multiplayer ) {
#ifndef ID_DEDICATED
	// Inside the level load: opening the pause menu later then reads no file
	// mid-frame, even when the gate was switched on after startup. Whether
	// the player is Strogg is asked again of each level.
	retainedPauseStrogg = -1;
	guiRetainedReleasing = NULL;
	retainedReleaseUntil = 0;
	// A map change ends the multiplayer card's cover and any stock page's
	// takeover, without a release: the next view is a new map.
	RetireRetainedMultiplayer( false );
	retainedMultiplayerUncovered = false;
	retainedMultiplayerHandoff = -1;
	if ( multiplayer ) {
		// The multiplayer cards load here too, and each map opens them on
		// their first pages.
		if ( Session_RetainedMultiplayerEnabled( false ) && guiRetainedEscape == NULL ) {
			guiRetainedEscape = FindRetainedGui( RETAINED_MP_ESCAPE_GUI, false, false );
		}
		if ( Session_RetainedMultiplayerEnabled( true ) && guiRetainedWelcome == NULL ) {
			guiRetainedWelcome = FindRetainedGui( RETAINED_MP_WELCOME_GUI, false, false );
		}
		if ( Session_RetainedSummaryEnabled() && guiRetainedSummary == NULL ) {
			guiRetainedSummary = FindRetainedGui( RETAINED_MP_SUMMARY_GUI, false, false );
		}
		if ( Session_RetainedArenaPauseEnabled() && guiRetainedArenaPause == NULL &&
				cvarSystem->GetCVarBool( "ui_arenaCampaignActive" ) ) {
			guiRetainedArenaPause = FindRetainedGui( RETAINED_ARENA_PAUSE_GUI, false, false );
		}
		if ( guiRetainedEscape != NULL ) {
			guiRetainedEscape->SetStateInt( "card.tab", 0 );
		}
		if ( guiRetainedWelcome != NULL ) {
			guiRetainedWelcome->SetStateInt( "card.tab", 0 );
		}
		if ( guiRetainedSummary != NULL ) {
			guiRetainedSummary->SetStateInt( "card.tab", 0 );
		}
		return;
	}
	if ( !Session_RetainedScreensEnabled() ) {
		return;
	}
	if ( guiRetainedPause == NULL ) {
		guiRetainedPause = FindRetainedGui( RETAINED_PAUSE_GUI, false, true );
	}
	if ( guiRetainedPauseStrogg == NULL ) {
		guiRetainedPauseStrogg = FindRetainedGui( RETAINED_PAUSE_STROGG_GUI, false, true );
	}
	// The pages only the Marine pause reaches, so its SAVE GAME and RESTART
	// LEVEL never stall their hand-offs on a load, and the restart screen,
	// which only a level reaches; each loads with the first level that needs
	// it. The Load Game page loaded with the title unless that could not.
	if ( guiRetainedPause != NULL ) {
		if ( guiRetainedLoad == NULL ) {
			guiRetainedLoad = FindRetainedGui( RETAINED_LOADGAME_GUI, true, false );
			if ( guiRetainedLoad != NULL ) {
				UI_RetainedPrecacheImage( RETAINED_LOAD_GENERIC_SHOT );
			}
		}
		if ( guiRetainedSave == NULL ) {
			guiRetainedSave = FindRetainedGui( RETAINED_SAVEGAME_GUI, true, false );
		}
		if ( guiRetainedRestart == NULL ) {
			guiRetainedRestart = FindRetainedGui( RETAINED_RESTART_GUI, true, false );
		}
		if ( guiRetainedRestartMenu == NULL ) {
			guiRetainedRestartMenu = FindRetainedGui( RETAINED_RESTARTMENU_GUI, true, false );
		}
	}
	// The pause screen's time line reports the newest save's age; read it
	// here, inside the load, and keep it as saves are written.
	idStrList saves;
	idList<fileTIME_T> times;
	GetSaveGameList( saves, times );
	retainedNewestSave = times.Num() > 0 ? times[0].timeStamp : 0;
	UI_RetainedPrecacheImage( RetainedPauseShot( mapPath ).c_str() );
#endif
}

#ifndef ID_DEDICATED
// Whether the map change under way loads a multiplayer level, as
// LoadLoadingGui decides it from the same spawn data.
static bool Session_MultiplayerMapLoad( const idDict &serverInfo ) {
	const char *gameType = serverInfo.GetString( "si_gameType", cvarSystem->GetCVarString( "si_gameType" ) );
	const char *mapPath = serverInfo.GetString( "si_map", "" );
	return !idStr::Icmpn( mapPath, "mp/", 3 ) || ( gameType[ 0 ] != '\0' && idStr::Icmp( gameType, "singleplayer" ) != 0 );
}
#endif

void idSessionLocal::PrefetchRetainedLevel() {
#ifndef ID_DEDICATED
	if ( IsDedicatedServer() || !cvarSystem->GetCVarBool( "ui_retained" ) ) {
		return;
	}
	const bool multiplayer = Session_MultiplayerMapLoad( mapSpawnData.serverInfo );
	idList<const char *> documents;
	if ( multiplayer ) {
		if ( Session_RetainedMultiplayerEnabled( false ) && guiRetainedEscape == NULL ) {
			documents.Append( RETAINED_MP_ESCAPE_GUI );
		}
		if ( Session_RetainedMultiplayerEnabled( true ) && guiRetainedWelcome == NULL ) {
			documents.Append( RETAINED_MP_WELCOME_GUI );
		}
		if ( Session_RetainedSummaryEnabled() && guiRetainedSummary == NULL ) {
			documents.Append( RETAINED_MP_SUMMARY_GUI );
		}
		if ( Session_RetainedArenaPauseEnabled() && guiRetainedArenaPause == NULL &&
				cvarSystem->GetCVarBool( "ui_arenaCampaignActive" ) ) {
			documents.Append( RETAINED_ARENA_PAUSE_GUI );
		}
	} else if ( Session_RetainedScreensEnabled() ) {
		if ( guiRetainedPause == NULL ) {
			documents.Append( RETAINED_PAUSE_GUI );
		}
		if ( guiRetainedPauseStrogg == NULL ) {
			documents.Append( RETAINED_PAUSE_STROGG_GUI );
		}
		if ( guiRetainedLoad == NULL ) {
			documents.Append( RETAINED_LOADGAME_GUI );
		}
		if ( guiRetainedSave == NULL ) {
			documents.Append( RETAINED_SAVEGAME_GUI );
		}
		if ( guiRetainedRestart == NULL ) {
			documents.Append( RETAINED_RESTART_GUI );
		}
		if ( guiRetainedRestartMenu == NULL ) {
			documents.Append( RETAINED_RESTARTMENU_GUI );
		}
	}
	UI_RetainedPrefetchDocuments( documents.Ptr(), documents.Num() );
	// The HUD, weapon wheel and quick chat documents the game loads as it spawns.
	UI_RetainedPrefetchGameplayDocuments( multiplayer );
#endif
}

void idSessionLocal::SettleRetainedLevelPrefetch() {
#ifndef ID_DEDICATED
	UI_RetainedSettleGameplayDocuments( Session_MultiplayerMapLoad( mapSpawnData.serverInfo ) );
#endif
}

/*
===============
idSessionLocal::PrecacheRetainedLevelshots

Create Server shows its chosen map's levelshot, so every multiplayer map's
precaches before the page can browse them (PreloadRetainedScreens lists
them), and so do the Arena campaign's levelshots for its page
(QueueRetainedLevelshot). The front end's idle frames read them, a few each
frame once a menu has presented for a second, so the title's opening plays
undisturbed; each page reads its own still waiting when it opens, with
`all`. A level in play waits for the front end again.
===============
*/
void idSessionLocal::PrecacheRetainedLevelshots( bool all, bool maps ) {
#ifndef ID_DEDICATED
	if ( retainedLevelshots.Num() == 0 ) {
		return;
	}
	const int now = Sys_Milliseconds();
	if ( !all ) {
		if ( mapSpawned || guiActive == NULL ) {
			return;
		}
		if ( retainedLevelshotsFrom == 0 ) {
			retainedLevelshotsFrom = now + RETAINED_LEVELSHOT_DELAY_MSEC;
		}
		if ( now < retainedLevelshotsFrom ) {
			return;
		}
	}
	// Each is a precache, as the startup's were, not a late load.
	const bool insideLoad = declManager->GetInsideLoad();
	declManager->SetInsideLoad( true );
	int read = 0;
	for ( int i = 0; i < retainedLevelshots.Num(); ) {
		if ( all && retainedLevelshots[ i ].map != maps ) {
			++i;	// the other page's
			continue;
		}
		if ( !all && read > 0 && Sys_Milliseconds() - now >= RETAINED_LEVELSHOT_FRAME_MSEC ) {
			break;	// the frame's share is spent
		}
		const retainedLevelshot_t shot = retainedLevelshots[ i ];
		retainedLevelshots.RemoveIndex( i );
		++read;
		if ( shot.map ) {
			UI_RetainedPrecacheImage( RetainedPauseShot( shot.name.c_str() ).c_str() );
			continue;
		}
		if ( shot.material ) {
			declManager->FindMaterial( shot.name.c_str() );
		}
		UI_RetainedPrecacheImage( shot.name.c_str() );
	}
	declManager->SetInsideLoad( insideLoad );
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_LEVELSHOTS %s read=%d left=%d ms=%d\n", !all ? "idle" : maps ? "maps" : "pictures", read,
			retainedLevelshots.Num(), Sys_Milliseconds() - now );
	}
#endif
}

void idSessionLocal::QueueRetainedLevelshot( const char *picture, bool material ) {
#ifndef ID_DEDICATED
	retainedLevelshot_t shot;
	shot.name = picture;
	shot.map = false;
	shot.material = material;
	retainedLevelshots.Append( shot );
#endif
}

// The game's in-match menu: its multiplayer menu, drawn over the game. The
// chat and buy screens are other GUIs and are never covered.
static bool Session_IsGameMenu( idUserInterface *gui ) {
	return gui != NULL && gui->State().GetBool( "gameDraw" ) && !idStr::Icmp( gui->Name(), "guis/mpmain.gui" );
}

// The game's match summary, which the summary card covers (section 14.14).
static bool Session_IsGameSummary( idUserInterface *gui ) {
	return gui != NULL && gui->State().GetBool( "gameDraw" ) && !idStr::Icmp( gui->Name(), "guis/summary.gui" );
}

// The single-player Arena's pause menu, which the game opens in its menu's
// place during an Arena match, under its own card.
static bool Session_IsArenaPause( idUserInterface *gui ) {
	return gui != NULL && gui->State().GetBool( "gameDraw" ) && !idStr::Icmp( gui->Name(), "guis/arena_pause.gui" );
}

// A game GUI a multiplayer card covers.
static bool Session_IsCoveredGameGui( idUserInterface *gui ) {
	return Session_IsGameMenu( gui ) || Session_IsGameSummary( gui ) || Session_IsArenaPause( gui );
}

bool idSessionLocal::RetainedMultiplayerCovers() const {
	return guiRetainedMultiplayer != NULL && Session_IsCoveredGameGui( guiActive );
}

bool idSessionLocal::RetainedSummaryCovers() const {
	return retainedMultiplayerSummary && RetainedMultiplayerCovers() && Session_IsGameSummary( guiActive );
}

/*
===============
idSessionLocal::BeginRetainedLoadingHold

At the end of a multiplayer load the retained loading screen presented: a
player who joins by the Welcome card (no ui_autoJoin, the card on) keeps the
screen, reading JOINING, until the card presents (section 14.17), so the
join never shows the bare match or the stock menu between them. The hold
gives up after 5 s: a remote client's card took 2 to 3 s to come, the
menu's first opening among it. False when the load ends as before, with
the wipe.
===============
*/
static const int RETAINED_LOADING_HOLD_MSEC = 5000;
static const int RETAINED_LOADING_HANDOFF_MSEC = 250;

bool idSessionLocal::BeginRetainedLoadingHold() {
#ifdef ID_DEDICATED
	return false;
#else
	ClearRetainedLoadingHold();
	// ExecuteMapChange asks just before it marks the map spawned.
	if ( !retainedLoadingActive || guiLoading == NULL || !IsMultiplayer() || readDemo != NULL ||
			idAsyncNetwork::multiViewDemo.IsPlaying() || cvarSystem->GetCVarBool( "ui_autoJoin" ) ||
			!Session_RetainedMultiplayerEnabled( true ) || retainedStock.FindIndex( RETAINED_MP_WELCOME_GUI ) >= 0 ||
			Session_ModSuppliesFile( "guis/mpmain.gui" ) ) {
		return false;
	}
	// The 5 s count from the first frame the presentation clock moves.
	guiLoadingHold = guiLoading;
	retainedLoadingHoldBegan = common->GetPresentationTime();
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_LOADING_HOLD begin\n" );
	}
	return true;
#endif
}

/*
===============
idSessionLocal::FadeRetainedLoadingHold

The held screen fades out over 250 ms (80 ms under reduced motion), over the
card that takes over or over the match; the session draws it until then.
===============
*/
void idSessionLocal::FadeRetainedLoadingHold( const char *reason ) {
#ifndef ID_DEDICATED
	// Not before the clock moves after the load (UpdateRetainedLoadingHold).
	if ( guiLoadingHold == NULL || retainedLoadingHoldFading || retainedLoadingHoldLive == 0 ) {
		return;
	}
	const int now = common->GetPresentationTime();
	guiLoadingHold->HandleNamedEvent( "handoff" );
	retainedLoadingHoldFading = true;
	retainedLoadingHoldUntil = now + RETAINED_LOADING_HANDOFF_MSEC;
	if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
		common->Printf( "RETAINED_LOADING_HOLD fade=%s waited=%d\n", reason, now - retainedLoadingHoldLive );
	}
#endif
}

/*
===============
idSessionLocal::UpdateRetainedLoadingHold

Each frame, before the card's own update: the hold fades once a card has
presented, when anything else takes the screen, or after 5 s, and ends with
its fade. A card that presents this frame starts the fade on the next, so its
first frame, the slowest (the menu opening, the softening's first draw), draws
under the screen and the fade plays on the quick frames after it. A new load
or a stop drops the hold at once.
===============
*/
void idSessionLocal::UpdateRetainedLoadingHold() {
#ifndef ID_DEDICATED
	if ( guiLoadingHold == NULL ) {
		return;
	}
	if ( !mapSpawned || insideExecuteMapChange ) {
		ClearRetainedLoadingHold();
		return;
	}
	const int now = common->GetPresentationTime();
	if ( retainedLoadingHoldFading ) {
		if ( now >= retainedLoadingHoldUntil ) {
			if ( cvarSystem->GetCVarBool( "ui_retainedTrace" ) ) {
				common->Printf( "RETAINED_LOADING_HOLD end faded=%d\n", now - ( retainedLoadingHoldUntil - RETAINED_LOADING_HANDOFF_MSEC ) );
			}
			ClearRetainedLoadingHold();
		}
		return;
	}
	// The presentation clock stands still through the load and the rest of
	// its frame, then catches up at once: the hold counts its 5 s, and may
	// fade, only from the first frame the clock moves.
	if ( retainedLoadingHoldLive == 0 ) {
		if ( now == retainedLoadingHoldBegan ) {
			return;
		}
		retainedLoadingHoldLive = now;
		retainedLoadingHoldUntil = now + RETAINED_LOADING_HOLD_MSEC;
	}
	if ( guiRetainedMultiplayer != NULL ) {
		FadeRetainedLoadingHold( "card" );
	} else if ( RetainedUI_IsOpen() || ( guiActive != NULL && ( !Session_IsGameMenu( guiActive ) || retainedMultiplayerUncovered ) ) ) {
		FadeRetainedLoadingHold( "screen" );
	} else if ( now >= retainedLoadingHoldUntil ) {
		FadeRetainedLoadingHold( "expired" );
	}
#endif
}

void idSessionLocal::ClearRetainedLoadingHold() {
	guiLoadingHold = NULL;
	retainedLoadingHoldBegan = 0;
	retainedLoadingHoldLive = 0;
	retainedLoadingHoldUntil = 0;
	retainedLoadingHoldFading = false;
}

/*
===============
idSessionLocal::UpdateRetainedMultiplayer

The multiplayer menu card (section 14.18) covers the game's menu while it is
open over a match. The game keeps the menu: its state, its commands, its
legacy pages and the Match Control adapter all stay on mpmain.gui, which only
stops drawing. The game names the card for each opening: Welcome while the
player has not answered the join offer, Escape otherwise. The card presents
when that variant's gate allows it, the game answers the cover protocol and
allows the cover now, and neither a test GUI, a retained preview nor a mod's
own mpmain.gui is in the way; otherwise the stock menu presents, and a
document that cannot present falls back for the session. A stock page the
card hands off to keeps the menu until it closes.
===============
*/
void idSessionLocal::UpdateRetainedMultiplayer() {
#ifndef ID_DEDICATED
	const int now = common->GetPresentationTime();
	// A card covers one game GUI, the menu or the summary: another taking the
	// screen (the summary closing into the menu) ends that cover and asks
	// again.
	if ( guiActive != retainedMultiplayerTarget ) {
		if ( guiRetainedMultiplayer != NULL ) {
			RetireRetainedMultiplayer( guiActive == NULL && mapSpawned );
		}
		retainedMultiplayerUncovered = false;
		retainedMultiplayerTarget = guiActive;
	}
	if ( !Session_IsCoveredGameGui( guiActive ) ) {
		// The menu closing ends a stock page's takeover.
		retainedMultiplayerUncovered = false;
		retainedMultiplayerHandoff = -1;
	} else if ( retainedMultiplayerHandoff >= 0 ) {
		// The stock page's button answers once the menu's opening shows its
		// column (UI_RunLegacyWindowAction acts only on visible windows).
		idStr command;
		const char *window = Session_RetainedMultiplayerStockPage( retainedMultiplayerWelcome, retainedMultiplayerHandoff,
			guiActive->State().GetBool( "initial_join" ) );
		if ( window != NULL && UI_RunLegacyWindowAction( guiActive, window, false, command ) ) {
			retainedMultiplayerHandoff = -1;
			if ( command.Length() > 0 ) {
				DispatchCommand( guiActive, command.c_str() );
			}
		} else if ( now >= retainedMultiplayerHandoffUntil ) {
			common->Warning( "retained UI: the multiplayer menu's '%s' page did not become available", window != NULL ? window : "-" );
			retainedMultiplayerHandoff = -1;
		}
	}
	const bool context = ( Session_RetainedMultiplayerEnabled( false ) || Session_RetainedMultiplayerEnabled( true ) ||
		Session_RetainedSummaryEnabled() ) && game != NULL && mapSpawned && IsMultiplayer() && Session_IsCoveredGameGui( guiActive ) &&
		guiTest == NULL && !RetainedUI_IsOpen() && !retainedMultiplayerUncovered;
	if ( !context ) {
		if ( guiRetainedMultiplayer != NULL ) {
			// Closing the menu releases the softened view over the running game;
			// anything that takes the screen instead drops it at once.
			RetireRetainedMultiplayer( guiActive == NULL && mapSpawned );
		}
		return;
	}
	if ( guiRetainedMultiplayer == NULL ) {
		// The summary takes its own card. For the menu the game names the
		// card; a game module that does not answer gets Escape, which its join
		// offer refuses.
		const bool summary = Session_IsGameSummary( guiActive );
		// The Arena's pause menu takes its own card.
		const bool arena = Session_IsArenaPause( guiActive );
		bool welcome = false;
		if ( !summary && !arena ) {
			guiActive->SetStateBool( "mp.welcome", false );
			game->HandleMainMenuCommands( "retainedMultiplayerVariant", guiActive );
			welcome = guiActive->State().GetBool( "mp.welcome" );
		}
		const char *path = arena ? RETAINED_ARENA_PAUSE_GUI : summary ? RETAINED_MP_SUMMARY_GUI :
			welcome ? RETAINED_MP_WELCOME_GUI : RETAINED_MP_ESCAPE_GUI;
		idUserInterface *&document = arena ? guiRetainedArenaPause : summary ? guiRetainedSummary :
			welcome ? guiRetainedWelcome : guiRetainedEscape;
		// Whatever keeps the stock menu below keeps it for this opening (a
		// card still waiting for its pages, a fallback, an Arena Campaign
		// match); the next opening asks again.
		retainedMultiplayerUncovered = true;
		const bool enabled = arena ? Session_RetainedArenaPauseEnabled() :
			summary ? Session_RetainedSummaryEnabled() : Session_RetainedMultiplayerEnabled( welcome );
		if ( !enabled || retainedStock.FindIndex( path ) >= 0 ) {
			return;
		}
		const char *stockGui = arena ? "guis/arena_pause.gui" : summary ? "guis/summary.gui" : "guis/mpmain.gui";
		if ( Session_ModSuppliesFile( stockGui ) ) {
			common->DPrintf( "retained UI: the mod supplies its own '%s', so it presents instead of '%s'\n", stockGui, path );
			retainedStock.Append( path );
			return;
		}
		if ( document == NULL ) {
			document = FindRetainedGui( path, false, false );
		} else if ( UI_RetainedViewFailed( document ) ) {
			common->Warning( "retained UI: '%s' stopped drawing; the stock menu presents instead", path );
			retainedStock.AddUnique( path );
			document = NULL;
		}
		idUserInterface *card = document;
		if ( card == NULL ) {
			return;
		}
		card->SetStateInt( "mp.protocol", 0 );
		card->SetStateBool( "mp.cover_allowed", false );
		game->HandleMainMenuCommands( "retainedMultiplayerCover", card );
		if ( card->State().GetInt( "mp.protocol" ) != RETAINED_MP_PROTOCOL ) {
			common->DPrintf( "retained UI: the game module does not answer the multiplayer card's protocol %d; the stock menu presents\n",
				RETAINED_MP_PROTOCOL );
			retainedStock.Append( path );
			return;
		}
		if ( !card->State().GetBool( "mp.cover_allowed" ) ) {
			return;
		}
		retainedMultiplayerUncovered = false;
		guiRetainedMultiplayer = card;
		retainedMultiplayerWelcome = welcome;
		retainedMultiplayerSummary = summary;
		retainedMultiplayerRevision = card->State().GetInt( "mp.revision" );
		if ( summary ) {
			// Each summary opens on its SUMMARY page with the chat field at rest.
			card->SetStateInt( "card.tab", 0 );
			EndRetainedSummaryChat( true );
		}
		if ( arena ) {
			// The match block's levelshot, found as the single-player pause
			// finds its level's.
			card->SetStateString( "pause_shot", RetainedPauseShot( mapSpawnData.serverInfo.GetString( "si_map" ) ).c_str() );
		}
		// The keys bound to the Vote page's ballots (F1 and F2 by default), by
		// name, for the card to show beside Yes and No.
		const idStr yesKey = Session_RetainedBoundKey( "_impulse28" ), noKey = Session_RetainedBoundKey( "_impulse29" );
		card->SetStateString( "mp.keys.vote_yes", yesKey.c_str() );
		card->SetStateBool( "mp.keys.vote_yes_bound", yesKey.Length() > 0 );
		card->SetStateString( "mp.keys.vote_no", noKey.c_str() );
		card->SetStateBool( "mp.keys.vote_no_bound", noKey.Length() > 0 );
		// And the push-to-talk key, for the Voice page.
		const idStr talkKey = Session_RetainedBoundKey( "_voiceChat" );
		card->SetStateString( "mp.keys.voice_chat", talkKey.c_str() );
		card->SetStateBool( "mp.keys.voice_chat_bound", talkKey.Length() > 0 );
		card->StateChanged( now );
		card->Activate( true, now );
		card->HandleNamedEvent( "open" );
		PumpApplicationActions( card );
		return;
	}
	// The game writes only what changed, under a new revision.
	game->HandleMainMenuCommands( "retainedMultiplayerState", guiRetainedMultiplayer );
	const int revision = guiRetainedMultiplayer->State().GetInt( "mp.revision" );
	if ( revision != retainedMultiplayerRevision ) {
		retainedMultiplayerRevision = revision;
		guiRetainedMultiplayer->StateChanged( now );
	}
#endif
}

/*
===============
idSessionLocal::RetireRetainedMultiplayer

The card stops covering. With `release`, the menu closed over the running
game: the closed card keeps drawing only its softened view while it releases
over 250 ms, as the pause screen does.
===============
*/
void idSessionLocal::RetireRetainedMultiplayer( bool release ) {
	cvarSystem->SetCVarBool( "s_voiceChatTest", false );
#ifndef ID_DEDICATED
	idUserInterface *card = guiRetainedMultiplayer;
	if ( card == NULL ) {
		return;
	}
	const int now = common->GetPresentationTime();
	guiRetainedMultiplayer = NULL;
	// The summary's chat field stops typing with its card.
	retainedSummaryTyping = false;
	retainedMultiplayerSummary = false;
	if ( game != NULL ) {
		game->HandleMainMenuCommands( "retainedMultiplayerUncover", card );
	}
	card->Activate( false, now );
	if ( release && mapSpawned && guiActive == NULL ) {
		card->HandleNamedEvent( "release" );
		guiRetainedReleasing = card;
		retainedReleaseUntil = now + RETAINED_RELEASE_MSEC;
	}
	PumpApplicationActions( card );
#endif
}

void idSessionLocal::RetainedMultiplayerFrameEvent() {
	if ( !RetainedMultiplayerCovers() ) {
		return;
	}
	sysEvent_t ev;
	memset( &ev, 0, sizeof( ev ) );
	ev.evType = SE_NONE;
	idUserInterface *card = guiRetainedMultiplayer;
	const char *cmd = card->HandleEvent( &ev, common->GetPresentationTime() );
	if ( cmd && cmd[0] ) {
		DispatchCommand( card, cmd );
	}
}

void idSessionLocal::HandleGameMenuReturn( const char *cmd ) {
	if ( !cmd ) {
		if ( Session_IsArenaPause( guiActive ) ) {
			ExitMenu();
		} else {
			openq4::NativeInputBeforeSessionChange();
			guiActive = NULL;
		}
	} else if ( idStr::Icmp( cmd, "main" ) == 0 ) {
		StartMenu();
	} else if ( idStr::Icmpn( cmd, "main ", 5 ) == 0 ) {
		StartMenu();
		idStr mainMenuEvent = cmd + 5;
		mainMenuEvent.StripLeading( ' ' );
		if ( mainMenuEvent.Length() > 0 && guiMainMenu != NULL ) {
			guiMainMenu->HandleNamedEvent( mainMenuEvent.c_str() );
		}
	} else if ( strstr( cmd, "sound " ) == cmd ) {
		// pipe the GUI sound commands not handled by the game to the main menu code
		HandleMainMenuCommands( cmd );
	}
}

/*
===============
idSessionLocal::HandleRetainedMultiplayerRequest

The card's verbs, each the stock menu's own command with its selection
sound: Resume closes the menu, Main Menu leaves for the main menu with the
match running, Disconnect (after the card's own confirmation) leaves the
server, the Team page's and the Welcome card's Join page actions, the
Players page's selection, Mute and Friend, the Vote page's ballots, call
and drafted fields, the Match page's actions and list rows, and the
Settings pages' models, rail color and crosshair go to the game (the Match
page's choices and rule value wait on the game's menu, as the stock
controls' values do); Controls, Game Options and System leave for the
main menu's pages; and a page still in development, or one the card cannot
edit yet (name and clan), hands off to its stock page. Every other request
is refused, as the card's own verbs are anywhere else.
===============
*/
void idSessionLocal::HandleRetainedMultiplayerRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	if ( gui == NULL || request == NULL || gui != guiRetainedMultiplayer || !RetainedMultiplayerCovers() || game == NULL ) {
		return;
	}
	if ( !idStr::Icmp( request, "mpSummaryChat" ) ) {
		// The summary's chat field starts typing, which the session owns.
		if ( RetainedSummaryCovers() ) {
			BeginRetainedSummaryChat( false );
		}
		return;
	}
	if ( Session_IsArenaPause( guiActive ) ) {
		// The Arena pause card's verbs, each the stock Arena pause menu's own
		// action: Resume (and Back) closes it, Settings leaves for the main
		// menu's settings page, and Restart match and Leave match, after the
		// card's own confirmations, go to the campaign.
		const char *arenaCommand = NULL;
		if ( !idStr::Icmp( request, "resume" ) || !idStr::Icmp( request, "mpClose" ) ) {
			arenaCommand = "play main_menu_selection ; arenaPause resume";
		} else if ( !idStr::Icmp( request, "settings" ) ) {
			arenaCommand = "play main_menu_selection ; arenaPause settings";
		} else if ( !idStr::Icmp( request, "arenaRestart" ) ) {
			arenaCommand = "play main_menu_selection ; arenaPause restart";
		} else if ( !idStr::Icmp( request, "arenaLeave" ) ) {
			arenaCommand = "play main_menu_selection ; arenaPause leave";
		} else {
			common->Warning( "retained UI: unhandled Arena pause request '%s'", request );
			return;
		}
		HandleGameMenuReturn( game->HandleGuiCommands( arenaCommand ) );
		if ( !Session_IsCoveredGameGui( guiActive ) ) {
			RetireRetainedMultiplayer( guiActive == NULL && mapSpawned );
		}
		return;
	}
	const char *command = NULL;
	idStr gameCommand;
	int voteField = -1, voteValue = 0;
	const bool select = !idStr::Icmp( request, "mpSelectPlayer" ) || !idStr::Icmpn( request, "mpSelectPlayer ", 15 ),
		mute = !idStr::Icmp( request, "mpMute" ) || !idStr::Icmpn( request, "mpMute ", 7 ),
		befriend = !idStr::Icmp( request, "mpFriend" ) || !idStr::Icmpn( request, "mpFriend ", 9 ),
		adminSelect = !idStr::Icmp( request, "mpAdminSelect" ) || !idStr::Icmpn( request, "mpAdminSelect ", 14 ),
		adminKick = !idStr::Icmp( request, "mpAdminKick" ) || !idStr::Icmpn( request, "mpAdminKick ", 12 ),
		adminBan = !idStr::Icmp( request, "mpAdminBan" ) || !idStr::Icmpn( request, "mpAdminBan ", 11 ),
		adminSwitch = !idStr::Icmp( request, "mpAdminSwitch" ) || !idStr::Icmpn( request, "mpAdminSwitch ", 14 );
	if ( select || mute || befriend || adminSelect || adminKick || adminBan || adminSwitch ) {
		const char *payload = strchr( request, ' ' );
		const idStr operands = payload != NULL ? payload + 1 : "";
		const int separator = operands.Find( ' ' );
		const idStr clientText = separator >= 0 ? operands.Left( separator ) : idStr();
		const idStr identity = separator >= 0 ? operands.Right( operands.Length() - separator - 1 ) : idStr();
		const auto digits = []( const idStr &text, int maximum ) {
			if ( text.Length() < 1 || text.Length() > maximum ) return false;
			for ( int i = 0; i < text.Length(); ++i ) if ( text[i] < '0' || text[i] > '9' ) return false;
			return true;
		};
		const int client = digits( clientText, 2 ) ? atoi( clientText.c_str() ) : -1;
		if ( client < 0 || client >= RETAINED_MP_CLIENTS || !digits( identity, 10 ) ) {
			common->Warning( "retained UI: invalid multiplayer player identity" );
			return;
		}
		// None of them closes the menu.
		gameCommand = va( "play main_menu_selection ; retained %s %d %s", select ? "select" : mute ? "mute" : befriend ? "friend" :
			adminSelect ? "adminSelect" : adminKick ? "adminKick" : adminBan ? "adminBan" : "adminSwitch", client, identity.c_str() );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpWelcomeAction" ) ) {
		const int slot = gui->State().GetInt( "card.welcome_action", "-1" );
		if ( slot < 0 || slot >= RETAINED_MP_WELCOME_SLOTS ) {
			common->Warning( "retained UI: the multiplayer card asked for join action %d", slot );
			return;
		}
		// An action that is unavailable now keeps the menu open.
		gameCommand = va( "play main_menu_selection ; retained welcome %d", slot );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpMatch" ) ) {
		const int action = gui->State().GetInt( "card.match_op", "-1" );
		if ( action < 0 || action >= RETAINED_MP_MATCH_ACTIONS ) {
			common->Warning( "retained UI: the multiplayer card asked for match action %d", action );
			return;
		}
		// Match Control's own fixed token; the game checks the action again
		// against its accepted view and keeps the menu open. A refresh, which
		// the page asks as it opens, makes no sound.
		gameCommand = va( "%smatchControl %s", action == 0 ? "" : "play main_menu_selection ; ", RETAINED_MP_MATCH_TOKENS[ action ] );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpMatchSelect" ) ) {
		const int list = gui->State().GetInt( "card.match_list", "-1" ), row = gui->State().GetInt( "card.match_row", "-1" );
		if ( list < 0 || list >= RETAINED_MP_MATCH_LIST_COUNT || row < 0 || row >= RETAINED_MP_MATCH_ROWS ) {
			common->Warning( "retained UI: the multiplayer card asked for row %d of match list %d", row, list );
			return;
		}
		// The stock list's own selection on the game's menu, then its token;
		// the menu stays open.
		guiActive->SetStateInt( va( "%s_sel_0", RETAINED_MP_MATCH_LISTS[ list ][ 0 ] ), row );
		gameCommand = va( "play main_menu_selection ; matchControl %s", RETAINED_MP_MATCH_LISTS[ list ][ 1 ] );
		command = gameCommand.c_str();
	} else if ( Session_RetainedValueRequest( request, RETAINED_MP_MATCH_CHOICES,
		static_cast<int>( sizeof( RETAINED_MP_MATCH_CHOICES ) / sizeof( RETAINED_MP_MATCH_CHOICES[0] ) ), voteField, voteValue ) ) {
		// Each choice's values: the roles 1 to 4, the ballot targets 0 and 1,
		// the series formats 0 to 2.
		static const int lowest[] = { 1, 0, 0 };
		static const int highest[] = { RETAINED_MP_MATCH_ROLES, 1, 2 };
		static const char *const choices[] = { "match role", "ballot target", "series format" };
		static_assert( sizeof( lowest ) / sizeof( lowest[0] ) == sizeof( RETAINED_MP_MATCH_CHOICES ) / sizeof( RETAINED_MP_MATCH_CHOICES[0] ),
			"one range for each Match page choice" );
		if ( voteValue < lowest[ voteField ] || voteValue > highest[ voteField ] ) {
			common->Warning( "retained UI: the multiplayer card chose %s %d", choices[ voteField ], voteValue );
			return;
		}
		// The stock choice's own value on the game's menu, quietly; the game
		// reads it as it acts.
		if ( voteField == 0 ) {
			guiActive->SetStateInt( "match_role_choice", voteValue );
		} else if ( voteField == 1 ) {
			guiActive->SetStateString( "match_proposal_scope_choice", RETAINED_MP_MATCH_SCOPES[ voteValue ] );
		} else {
			guiActive->SetStateString( "match_series_profile_choice", RETAINED_MP_MATCH_SERIES_PROFILES[ voteValue ] );
		}
		return;
	} else if ( Session_RetainedRuleValueRequest( request, voteValue ) ) {
		// The stock field's own value on the game's menu, quietly; the game
		// checks it against the rule chosen as it stages it.
		guiActive->SetStateInt( "match_rule_value", voteValue );
		return;
	} else if ( !idStr::Icmp( request, "mpTeamAction" ) ) {
		const int slot = gui->State().GetInt( "card.team_action", "-1" );
		if ( slot < 0 || slot >= RETAINED_MP_TEAM_SLOTS ) {
			common->Warning( "retained UI: the multiplayer card asked for team action %d", slot );
			return;
		}
		// An action that is unavailable now keeps the menu open.
		gameCommand = va( "play main_menu_selection ; retained team %d", slot );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpVoteYes" ) || !idStr::Icmp( request, "mpVoteNo" ) ) {
		// A ballot closes the menu, as the stock buttons do; one the player
		// cannot cast keeps it open.
		command = !idStr::Icmp( request, "mpVoteYes" ) ? "play main_menu_selection ; retained vote yes" :
			"play main_menu_selection ; retained vote no";
	} else if ( !idStr::Icmpn( request, "mpVoteKick ", 11 ) ) {
		const idStr operands = request + 11;
		const int separator = operands.Find( ' ' );
		const idStr row = separator >= 0 ? operands.Left( separator ) : idStr();
		const idStr catalog = separator >= 0 ? operands.Right( operands.Length() - separator - 1 ) : idStr();
		bool valid = row == "-1" || ( row.Length() >= 1 && row.Length() <= 2 );
		for ( int i = 0; valid && row != "-1" && i < row.Length(); ++i ) valid = row[i] >= '0' && row[i] <= '9';
		valid = valid && atoi( row.c_str() ) < RETAINED_MP_CLIENTS && catalog.Length() >= 1 && catalog.Length() <= 512;
		for ( int i = 0; valid && i < catalog.Length(); ++i ) valid = ( catalog[i] >= '0' && catalog[i] <= '9' ) || catalog[i] == ':' || catalog[i] == ',';
		if ( !valid ) return;
		// The game command lexer splits commas and colons outside quotes.
		gameCommand = va( "retained voteKick %s \"%s\"", row.c_str(), catalog.c_str() );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpCallVote" ) ) {
		command = "play main_menu_selection ; retained callVote";
	} else if ( Session_RetainedValueRequest( request, RETAINED_MP_VOTE_FIELDS,
		static_cast<int>( sizeof( RETAINED_MP_VOTE_FIELDS ) / sizeof( RETAINED_MP_VOTE_FIELDS[0] ) ), voteField, voteValue ) ) {
		// A drafted field changes quietly, and the menu stays open.
		gameCommand = va( "retained voteSet %d %d", voteField, voteValue );
		command = gameCommand.c_str();
	} else if ( Session_RetainedValueRequest( request, RETAINED_MP_APPEARANCE_VALUES,
		static_cast<int>( sizeof( RETAINED_MP_APPEARANCE_VALUES ) / sizeof( RETAINED_MP_APPEARANCE_VALUES[0] ) ), voteField, voteValue ) &&
		voteValue >= 0 ) {
		// A model or the crosshair changes quietly, and the menu stays open.
		gameCommand = voteField < 3 ? va( "retained appearance %d %d", voteField, voteValue ) : va( "retained crosshair %d", voteValue );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpRail" ) ) {
		const int rail = gui->State().GetInt( "card.rail", "-1" );
		if ( rail < 0 || rail >= RETAINED_MP_RAIL_COLORS ) {
			common->Warning( "retained UI: the multiplayer card asked for rail color %d", rail );
			return;
		}
		gameCommand = va( "play main_menu_selection ; retained rail %d", rail );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpAdminPass" ) || !idStr::Icmp( request, "mpRefereeLogin" ) ) {
		// A credential the card's masked field entered: it moves straight to
		// where the stock page's field kept it (the remote console password's
		// CVar, the referee credential on the game's menu, which the game
		// clears as it reads it) and leaves the card at once.
		const bool admin = !idStr::Icmp( request, "mpAdminPass" );
		const char *key = admin ? "card.admin.password" : "card.referee_credential";
		const idStr credential = gui->State().GetString( key );
		gui->SetStateString( key, "" );
		gui->StateChanged( common->GetPresentationTime() );
		if ( credential.Length() == 0 || credential.Length() > ( admin ? RETAINED_MP_PASSWORD_BYTES : RETAINED_MP_CREDENTIAL_BYTES ) ) {
			return;
		}
		if ( admin ) {
			cvarSystem->SetCVarString( "net_clientRemoteConsolePassword", credential.c_str() );
			command = "play main_menu_selection ; retained adminPass";
		} else {
			guiActive->SetStateString( "match_referee_credential", credential.c_str() );
			command = "play main_menu_selection ; matchControl referee_login";
		}
	} else if ( !idStr::Icmp( request, "mpSettingsName" ) || !idStr::Icmp( request, "mpSettingsClan" ) ) {
		// The player's name or clan from the Settings pages' fields, as the
		// stock fields set them; the game publishes the result.
		const bool name = !idStr::Icmp( request, "mpSettingsName" );
		const idStr text = gui->State().GetString( name ? "mp.settings.name_edit" : "mp.settings.clan_edit" );
		if ( ( name && text.Length() == 0 ) || text.Length() > ( name ? RETAINED_MP_NAME_BYTES : RETAINED_MP_CLAN_BYTES ) ) {
			return;
		}
		cvarSystem->SetCVarString( name ? "ui_name" : "ui_clan", text.c_str() );
		cvarSystem->SetCVarString( name ? "gui_ui_name" : "gui_ui_clan", text.c_str() );
		return;
	} else if ( !idStr::Icmp( request, "mpAdminConsole" ) ) {
		// The console's command, through the stock console's own input.
		const idStr text = gui->State().GetString( "card.admin.command" );
		gui->SetStateString( "card.admin.command", "" );
		gui->StateChanged( common->GetPresentationTime() );
		if ( text.Length() == 0 || text.Length() > RETAINED_MP_COMMAND_BYTES ) {
			return;
		}
		guiActive->SetStateString( "admin_console_input", text.c_str() );
		command = "retained adminConsole";
	} else if ( !idStr::Icmp( request, "mpAdminBanPick" ) ) {
		const int row = gui->State().GetInt( "card.admin_ban_pick", "-1" );
		if ( row < 0 || row >= RETAINED_MP_ADMIN_BANS ) {
			common->Warning( "retained UI: the multiplayer card asked for ban row %d", row );
			return;
		}
		gameCommand = va( "play main_menu_selection ; retained adminBanSelect %d", row );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpAdminBans" ) ) {
		command = "play main_menu_selection ; retained adminBans";
	} else if ( !idStr::Icmp( request, "mpAdminUnban" ) ) {
		command = "play main_menu_selection ; retained adminUnban";
	} else if ( !idStr::Icmp( request, "mpAdminApply" ) ) {
		// Apply closes the menu where it changes the server, as the stock
		// page's did.
		command = "play main_menu_selection ; retained adminApply";
	} else if ( Session_RetainedValueRequest( request, RETAINED_MP_ADMIN_FIELDS,
		static_cast<int>( sizeof( RETAINED_MP_ADMIN_FIELDS ) / sizeof( RETAINED_MP_ADMIN_FIELDS[0] ) ), voteField, voteValue ) ) {
		// A drafted server setting changes quietly, and the menu stays open.
		gameCommand = va( "retained adminSet %d %d", voteField, voteValue );
		command = gameCommand.c_str();
	} else if ( !idStr::Icmp( request, "mpSettingsControls" ) ) {
		command = "play main_menu_selection ; mainMenu fromMp_toControls";
	} else if ( !idStr::Icmp( request, "mpSettingsGame" ) ) {
		command = "play main_menu_selection ; mainMenu fromMp_toGameoptions";
	} else if ( !idStr::Icmp( request, "mpSettingsSystem" ) ) {
		command = "play main_menu_selection ; mainMenu fromMp_toSystem";
	} else if ( !idStr::Icmp( request, "mpClose" ) ) {
		command = "play main_menu_selection ; close";
	} else if ( !idStr::Icmp( request, "mpMainMenu" ) ) {
		command = "play main_menu_selection ; mainMenu";
	} else if ( !idStr::Icmp( request, "mpDisconnect" ) ) {
		command = "play main_menu_selection ; disconnect";
	} else if ( !idStr::Icmp( request, "mpStockPage" ) ) {
		const int page = gui->State().GetInt( "card.stock_page", "-1" );
		if ( Session_RetainedMultiplayerStockPage( retainedMultiplayerWelcome, page, guiActive->State().GetBool( "initial_join" ) ) == NULL ) {
			common->Warning( "retained UI: the multiplayer card asked for stock page %d", page );
			return;
		}
		// The stock menu presents at once, and presses the page's button as
		// soon as its column shows (UpdateRetainedMultiplayer).
		retainedMultiplayerUncovered = true;
		retainedMultiplayerHandoff = page;
		retainedMultiplayerHandoffUntil = common->GetPresentationTime() + RETAINED_MP_HANDOFF_MSEC;
		RetireRetainedMultiplayer( false );
		UpdateRetainedMultiplayer();
		return;
	} else {
		common->Warning( "retained UI: unhandled multiplayer request '%s'", request );
		return;
	}
	HandleGameMenuReturn( game->HandleGuiCommands( command ) );
	// A closed menu releases the softened view in this frame, not the next.
	if ( !Session_IsCoveredGameGui( guiActive ) ) {
		RetireRetainedMultiplayer( guiActive == NULL && mapSpawned );
	}
#endif
}

/*
===============
idSessionLocal::BeginRetainedSummaryChat

The summary card's chat field (section 14.14) types into the stock summary's
own entry, its chattext, which the game's chatmessage sends: the session owns
the typing while the card covers the summary. The stock entry takes focus, so
the platform's text input runs only while the player types. `team` opens
team chat where it exists.
===============
*/
void idSessionLocal::BeginRetainedSummaryChat( bool team ) {
#ifndef ID_DEDICATED
	if ( !RetainedSummaryCovers() ) {
		return;
	}
	if ( !retainedSummaryTyping ) {
		retainedSummaryTyping = true;
		retainedSummaryCaret = idStr::Length( guiActive->State().GetString( "chattext" ) );
		guiActive->SetStateInt( "messagemode", team && guiRetainedMultiplayer->State().GetBool( "mp.summary.channels" ) ? 1 : 0 );
		guiActive->HandleNamedEvent( "oq4_chatFocus" );
	}
	PublishRetainedSummaryChat();
#endif
}

// Typing stops, the entry kept unless `clear`; the stock entry gives its
// focus back.
void idSessionLocal::EndRetainedSummaryChat( bool clear ) {
#ifndef ID_DEDICATED
	const bool typing = retainedSummaryTyping;
	retainedSummaryTyping = false;
	if ( !RetainedSummaryCovers() ) {
		return;
	}
	if ( clear ) {
		guiActive->SetStateString( "chattext", "" );
		guiActive->SetStateInt( "messagemode", 0 );
		retainedSummaryCaret = 0;
	}
	if ( typing ) {
		guiActive->HandleNamedEvent( "oq4_chatBlur" );
	}
	PublishRetainedSummaryChat();
#endif
}

// The entry for the card: the text before and after the caret, the
// characters left, the channel and whether the player types.
void idSessionLocal::PublishRetainedSummaryChat() {
#ifndef ID_DEDICATED
	idUserInterface *card = guiRetainedMultiplayer;
	if ( card == NULL || !retainedMultiplayerSummary || guiActive == NULL ) {
		return;
	}
	const idStr text = guiActive->State().GetString( "chattext" );
	const int caret = idMath::ClampInt( 0, text.Length(), retainedSummaryCaret );
	retainedSummaryCaret = caret;
	card->SetStateBool( "mp.summary.typing", retainedSummaryTyping );
	card->SetStateString( "mp.summary.entry_before", text.Left( caret ).c_str() );
	card->SetStateString( "mp.summary.entry_after", text.Right( text.Length() - caret ).c_str() );
	card->SetStateInt( "mp.summary.entry_left", RETAINED_SUMMARY_CHAT_LIMIT - text.Length() );
	card->SetStateBool( "mp.summary.entry_team", guiActive->State().GetInt( "messagemode" ) != 0 );
	card->StateChanged( common->GetPresentationTime() );
	if ( retainedSummaryTyping ) {
		// Typing restarts the caret's blink on.
		card->HandleNamedEvent( "chatType" );
	}
#endif
}

#ifndef ID_DEDICATED
// The byte length of the UTF-8 sequence that starts at `text[ at ]`, or
// ends just before `text[ at ]` with `backward`.
static int Session_Utf8Step( const idStr &text, int at, bool backward ) {
	if ( backward ) {
		int start = at - 1;
		while ( start > 0 && ( static_cast<unsigned char>( text[ start ] ) & 0xc0 ) == 0x80 ) {
			start--;
		}
		return at - start;
	}
	int end = at + 1;
	while ( end < text.Length() && ( static_cast<unsigned char>( text[ end ] ) & 0xc0 ) == 0x80 ) {
		end++;
	}
	return end - at;
}
#endif

/*
===============
idSessionLocal::RetainedSummaryChatEvent

While the player types in the summary card's chat field: typed characters
enter as UTF-8 at the caret, up to the chat input's 128 bytes; Backspace,
Delete, the arrows (by words with Ctrl), Home and End edit and move; Ctrl+V
pastes; Tab switches the channel where team chat exists; Enter sends through
the game's chatmessage and keeps typing, or stops typing on an empty entry;
Esc stops and clears. A pointer press stops typing, keeping the entry, and
reaches the card, as the pointer's movement and controller input always do.
True when the event was the field's.
===============
*/
bool idSessionLocal::RetainedSummaryChatEvent( const sysEvent_t *event ) {
#ifndef ID_DEDICATED
	if ( !retainedSummaryTyping || !RetainedSummaryCovers() ) {
		return false;
	}
	idStr text = guiActive->State().GetString( "chattext" );
	int caret = idMath::ClampInt( 0, text.Length(), retainedSummaryCaret );
	const auto insert = [&]( const char *bytes ) {
		const int length = idStr::Length( bytes );
		if ( length <= 0 || text.Length() + length > RETAINED_SUMMARY_CHAT_LIMIT ) {
			return;
		}
		text.Insert( bytes, caret );
		caret += length;
	};
	if ( event->evType == SE_CHAR ) {
		const int code = event->evValue;
		if ( code < 0x20 || code == 0x7f || code > 0xff || code == Sys_GetConsoleKey( false ) || code == Sys_GetConsoleKey( true ) ) {
			// Control characters arrive as keys too.
			return true;
		}
		char utf8[ 3 ] = { 0, 0, 0 };
		if ( code < 0x80 ) {
			utf8[ 0 ] = static_cast<char>( code );
		} else {
			utf8[ 0 ] = static_cast<char>( 0xc0 | ( code >> 6 ) );
			utf8[ 1 ] = static_cast<char>( 0x80 | ( code & 0x3f ) );
		}
		insert( utf8 );
	} else if ( event->evType == SE_KEY ) {
		const int key = event->evValue;
		if ( key >= K_MOUSE1 && key <= K_MOUSE8 ) {
			if ( event->evValue2 ) {
				EndRetainedSummaryChat( false );
			}
			return false;
		}
		if ( ( key >= K_JOY1 && key <= K_JOY32 ) || ( key >= K_F1 && key <= K_F12 ) ) {
			return false;
		}
		if ( !event->evValue2 ) {
			return true;
		}
		const bool ctrl = idKeyInput::IsDown( K_CTRL );
		if ( key == K_ESCAPE ) {
			EndRetainedSummaryChat( true );
			return true;
		}
		if ( key == K_ENTER || key == K_KP_ENTER ) {
			if ( text.IsEmpty() ) {
				EndRetainedSummaryChat( false );
				return true;
			}
			guiActive->SetStateString( "chattext", text.c_str() );
			DispatchCommand( guiActive, "chatmessage" );
			retainedSummaryCaret = 0;
			PublishRetainedSummaryChat();
			return true;
		}
		if ( key == K_TAB ) {
			if ( guiRetainedMultiplayer->State().GetBool( "mp.summary.channels" ) ) {
				guiActive->SetStateInt( "messagemode", guiActive->State().GetInt( "messagemode" ) ? 0 : 1 );
			}
		} else if ( key == K_BACKSPACE ) {
			if ( caret > 0 ) {
				const int step = Session_Utf8Step( text, caret, true );
				text = text.Left( caret - step ) + text.Right( text.Length() - caret );
				caret -= step;
			}
		} else if ( key == K_DEL ) {
			if ( caret < text.Length() ) {
				const int step = Session_Utf8Step( text, caret, false );
				text = text.Left( caret ) + text.Right( text.Length() - caret - step );
			}
		} else if ( key == K_LEFTARROW ) {
			do {
				caret -= caret > 0 ? Session_Utf8Step( text, caret, true ) : 0;
			} while ( ctrl && caret > 0 && !( text[ caret - 1 ] == ' ' && text[ caret ] != ' ' ) );
		} else if ( key == K_RIGHTARROW ) {
			do {
				caret += caret < text.Length() ? Session_Utf8Step( text, caret, false ) : 0;
			} while ( ctrl && caret < text.Length() && !( text[ caret - 1 ] == ' ' && text[ caret ] != ' ' ) );
		} else if ( key == K_HOME ) {
			caret = 0;
		} else if ( key == K_END ) {
			caret = text.Length();
		} else if ( ctrl && ( key == 'v' || key == 'V' ) ) {
			char *clipboard = Sys_GetClipboardData();
			if ( clipboard != NULL ) {
				// One line of the clipboard's UTF-8 text.
				idStr paste;
				for ( const char *c = clipboard; *c != '\0'; c++ ) {
					const unsigned char byte = static_cast<unsigned char>( *c );
					if ( byte == '\n' || byte == '\r' || byte == '\t' ) {
						paste += ' ';
					} else if ( byte >= 0x20 && byte != 0x7f ) {
						paste += static_cast<char>( byte );
					}
				}
				Mem_Free( clipboard );
				while ( paste.Length() > 0 && text.Length() + paste.Length() > RETAINED_SUMMARY_CHAT_LIMIT ) {
					paste = paste.Left( paste.Length() - Session_Utf8Step( paste, paste.Length(), true ) );
				}
				insert( paste.c_str() );
			}
		} else {
			// Every other key belongs to the field while the player types.
			return true;
		}
	} else {
		return false;
	}
	guiActive->SetStateString( "chattext", text.c_str() );
	retainedSummaryCaret = caret;
	PublishRetainedSummaryChat();
	return true;
#else
	return false;
#endif
}

void idSessionLocal::ReportRetainedScreens() {
	// stock= lists the retained documents that fell back to their stock
	// screens this session.
	idStr stock;
	for ( int i = 0; i < retainedStock.Num(); ++i ) {
		stock += i > 0 ? "," : "";
		stock += retainedStock[i];
	}
	// multiplayer= is the Escape card's gate, welcome= the Welcome card's; mp=
	// the card covering the game's menu or summary.
	common->Printf( "OPENQ4_RETAINED enabled=%d system=%d multiplayer=%d welcome=%d home=%s title=%d pause=%d strogg=%d handoff=%d release=%d mp=%s views=%d stock=%s\n",
		Session_RetainedScreensEnabled() ? 1 : 0, Session_RetainedSystemEnabled() ? 1 : 0, Session_RetainedMultiplayerEnabled( false ) ? 1 : 0,
		Session_RetainedMultiplayerEnabled( true ) ? 1 : 0,
		guiRetainedHome != NULL ? guiRetainedHome->Name() : "-",
		guiRetainedTitle != NULL ? 1 : 0, guiRetainedPause != NULL ? 1 : 0, guiRetainedPauseStrogg != NULL ? 1 : 0,
		RetainedHomeInputBlocked() ? 1 : 0, guiRetainedReleasing != NULL ? 1 : 0,
		RetainedMultiplayerCovers() ? ( retainedMultiplayerSummary ? "summary" : retainedMultiplayerWelcome ? "welcome" : "escape" ) : "-",
		RetainedUI_ViewCount(),
		stock.Length() > 0 ? stock.c_str() : "-" );
}
