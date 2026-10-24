#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "../../sys/AutoVersion.h"

#if defined( _DEBUG )
	#define	BUILD_DEBUG	"-debug"
#else
	#define	BUILD_DEBUG "-release"
#endif

/*

All game cvars should be defined here.

*/

// RAVEN BEGIN
// ddynerman: our gameplay modes
// RITUAL BEGIN
// squirrel: added DeadZone multiplayer mode
// RITUAL END
// RAVEN END
// openQ4: si_gameTypeArgs now lives beside the gametype descriptor table in
// mp/GameTypes.cpp so the token list and the table cannot drift apart.
const char *si_readyArgs[]			= { "Not Ready", "Ready", NULL };
const char *si_spectateArgs[]		= { "Play", "Spectate", NULL };

// RAVEN BEGIN
// ddynerman: our teams
const char *ui_teamArgs[]			= { "Marine", "Strogg", NULL }; 
// RAVEN END

struct gameVersion_s {
	gameVersion_s( void ) { sprintf( string, "%s %s V%s %s %s", GAME_NAME, GAME_BUILD_TYPE, VERSION_STRING_DOTTED, BUILD_STRING, __DATE__ ); }
	char	string[256];
} gameVersion;

idCVar g_version(					"g_version",				gameVersion.string,	CVAR_GAME | CVAR_ROM, "game version" );

// noset vars
idCVar gamedate(					"gamedate",					__DATE__,		CVAR_GAME | CVAR_ROM, "" );

// server info
idCVar si_name(						"si_name",					"Quake 4 Server",	CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_CASE_SENSITIVE | CVAR_SPECIAL_CONCAT, "name of the server" );
// openQ4: the server's message on the loading screen's server card, wrapped to three lines (256 bytes)
idCVar si_motd(						"si_motd",					"",					CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_CASE_SENSITIVE | CVAR_SPECIAL_CONCAT, "server message shown to players while they load the map" );
// RITUAL BEGIN
// squirrel: added DeadZone multiplayer mode
//idCVar sq_numRoundsPerMatch(		"dz_numRoundsPerMatch",		"5",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_INTEGER, "number of rounds per match in DeadZone", 1, 999999 );
//idCVar sq_buyFreezeSeconds(			"dz_buyFreezeSeconds",		"3",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_INTEGER, "number of seconds players are frozen at the start of each round in DeadZone", 0, 30 );
//idCVar sq_buyTimeSeconds(			"dz_buyTimeSeconds",		"20",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_INTEGER, "number of additional seconds after buy freeze that buy zones are active", 0, 999999 );
// squirrel: Mode-agnostic buymenus
idCVar si_isBuyingEnabled(			"si_isBuyingEnabled",			"0",		CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_BOOL, "enable buying in current mode" );
idCVar si_dropWeaponsInBuyingModes(	"si_dropWeaponsInBuyingModes",	"0",		CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_BOOL, "dead players drop weapons, even in Buying game modes" );
// RITUAL END
// RAVEN BEGIN
// ddynerman: new gametype strings
idCVar si_gameType(					"si_gameType",				si_gameTypeArgs[ 0 ],	CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE, "validated game type token; completion lists only implemented public modes", si_gameTypeArgs, idCmdSystem::ArgCompletion_String<si_gameTypeArgs> );
idCVar si_map(						"si_map",					"mp/q4dm1",				CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE, "map to be played next on server", idCmdSystem::ArgCompletion_MapName );
idCVar si_mapCycle(					"si_mapCycle",				"",						CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE, "map cycle list semicolon delimited" );
idCVar si_arenaCampaign(				"si_arenaCampaign",			"0",					CVAR_GAME | CVAR_SERVERINFO | CVAR_INTEGER, "single-player arena campaign match token, 0 for a normal multiplayer match", 0, 20 );
// openQ4's competitive framework intentionally exposes one profile selector
// and one read-only canonical identity instead of a second forest of live
// match cvars.  Legacy si_* values are imported/mirrored only at boundaries.
idCVar g_matchProfile(				"g_matchProfile",			"casual",				CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE, "typed match-rules profile token" );
idCVar si_matchRules(				"si_matchRules",			"",					CVAR_GAME | CVAR_SERVERINFO | CVAR_ROM, "committed match-rules profile, schema and digest" );
idCVar si_managedMatch(			"si_managedMatch",		"0",					CVAR_GAME | CVAR_SERVERINFO | CVAR_ROM | CVAR_BOOL, "committed managed-match state for client participation" );
idCVar g_refPassword(				"g_refPassword",			"",					CVAR_GAME, "one-shot server-only referee credential; converted to a verifier and cleared at session start" );
idCVar g_matchEvidence(				"g_matchEvidence",			"2",					CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_INTEGER, "competitive match evidence: 0 off, 1 final reports, 2 journal and reports", 0, 2 );
idCVar g_matchSeriesRecoveryId(		"g_matchSeriesRecoveryId",	"0",					CVAR_GAME | PC_CVAR_ARCHIVE, "server-owned active competition-series recovery identity" );
// bdube: raise player limit
idCVar si_maxPlayers(				"si_maxPlayers",			"12",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "max number of players allowed on the server", 1, 16 );
// ddynerman: min players to start
idCVar si_minPlayers(				"si_minPlayers",			"1",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "min number of players to start a game (only when warmup is enabled)", 1, 16 );
// ddynerman: CTF
idCVar si_captureLimit(				"si_captureLimit",			"5",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "score limit for CTF", 1, MP_PLAYER_MAXFRAGS );
// shouchard:  for tourney
idCVar si_tourneyLimit(				"si_tourneyLimit",			"3",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_INTEGER, "number of times a tourney will be run before cycling maps", 1, MP_PLAYER_MAXFRAGS );
// openQ4: defaults to on.  Quake Live's warmup is a ready-up phase, and with
// this off idPlayer::IsReady is unconditionally true so warmup ends the moment
// two players connect - which is not what anyone wants from a match.
idCVar si_useReady(					"si_useReady",				"1",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_BOOL, "require players to ready before starting a match" );
idCVar si_allowVoting(				"si_allowVoting",			"0",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_BOOL, "enable or disable server option voting" );
// ddynerman: disable hitscan tint
idCVar si_allowHitscanTint(			"si_allowHitscanTint",		"2",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "use hitscan tint (e.g. rail color) 0 - no tinting allowed, 1 - player hitscan tinting allowed in DM and NO hitscan tinting in team games, 2 - player hitscan tinting allowed in DM and use team-color hitscan tints in team games" );
idCVar si_privatePlayers(			"si_privatePlayers",		"0",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "number of private player slots reserved on the server.  subtracts from si_maxPlayers, so a server with si_maxPlayers 16 and 4 private player slots will only allow 12 public players to connect - see g_privatePassword, privatePassword", 0, 16 );
idCVar g_privatePassword(			"g_privatePassword",		"",				CVAR_GAME | PC_CVAR_ARCHIVE, "server-side password to access reserved client slots, clients set privatePassword" );
idCVar privatePassword(				"privatePassword",			"",				CVAR_GAME | CVAR_NOCHEAT, "client password used to access a servers private player slots" );
idCVar si_numPrivatePlayers(		"si_numPrivatePlayers",		"0",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ROM, "number of private slots currently in use" );
idCVar si_suddenDeathRestart(		"si_suddenDeathRestart",	"1",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE, "toggles whether or not to respawn players/items when team games enter sudden death" );
// RAVEN END

// openQ4 BEGIN
// Match progression carried over from Quake Live.  Units are stated in every
// description because Quake Live mixed minutes, seconds and milliseconds
// across this same set of rules.
idCVar si_scoreLimit(				"si_scoreLimit",			"150",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "score limit for objective modes scored by accumulation (Domination, Attack Defend), 0 for no limit", 0, 9999 );
idCVar si_roundLimit(				"si_roundLimit",			"8",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "rounds a team must win to take a round based match, 0 for no limit", 0, 99 );
idCVar si_roundTimeLimit(			"si_roundTimeLimit",		"180",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "length of a single round in seconds, 0 for no limit", 0, 3600 );
idCVar si_roundWarmupDelay(			"si_roundWarmupDelay",		"10",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "countdown before each round starts, in seconds", 0, 60 );
idCVar si_roundEndDelay(			"si_roundEndDelay",			"4",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "pause after a round is decided before the next one is set up, in seconds", 1, 30 );
idCVar si_mercyLimit(				"si_mercyLimit",			"0",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "end a team match early once one team leads by this many points, 0 to disable", 0, 999 );
idCVar si_overtime(					"si_overtime",				"120",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "length of each overtime period in seconds; 0 falls back to Quake 4 sudden death", 0, 3600 );
idCVar si_suddenDeathRespawnDelay(	"si_suddenDeathRespawnDelay","3",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "respawn delay in seconds while a match is in overtime, 0 to respawn normally", 0, 60 );
idCVar si_suddenDeathRespawnIncrease("si_suddenDeathRespawnIncrease","1",		CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "seconds added to the overtime respawn delay for every minute of overtime", 0, 60 );
idCVar si_suddenDeathRespawnMax(	"si_suddenDeathRespawnMax",	"10",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "upper bound on the overtime respawn delay, in seconds", 0, 60 );
idCVar si_forfeit(					"si_forfeit",				"1",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "end a team match early when one team is left with no players" );

// Warmup and ready-up
idCVar si_warmupReadyPercentage(	"si_warmupReadyPercentage",	"0.51",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_FLOAT, "fraction of eligible players that must ready up before the countdown starts", 0.0f, 1.0f );
idCVar si_teamSizeMin(				"si_teamSizeMin",			"1",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "players required on a team before a team match may leave warmup", 1, 16 );
idCVar si_teamForcePresent(			"si_teamForcePresent",		"1",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "require both teams to meet si_teamSizeMin, rather than just one" );
idCVar si_warmupWeapons(			"si_warmupWeapons",			"1",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "hand out every weapon on the map during warmup" );
idCVar si_warmupScoring(			"si_warmupScoring",			"0",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "count frags scored during warmup" );
idCVar si_autoShuffle(				"si_autoShuffle",			"0",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "shuffle teams automatically during warmup when they are lopsided" );
idCVar si_shuffleDelay(				"si_shuffleDelay",			"5",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "countdown before an automatic team shuffle runs, in seconds", 1, 60 );

// Freeze Tag
idCVar si_freezeThawTime(			"si_freezeThawTime",		"2",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "seconds a team mate must stand by a frozen player to thaw them", 1, 30 );
idCVar si_freezeThawRadius(			"si_freezeThawRadius",		"96",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "how close a team mate must be to thaw a frozen player, in world units", 16, 512 );
idCVar si_freezeThawThroughSurface(	"si_freezeThawThroughSurface","0",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "allow thawing a team mate through world geometry, rather than requiring line of sight" );
idCVar si_freezeAutoThawTime(		"si_freezeAutoThawTime",	"120",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "seconds after which a frozen player thaws with no help, 0 to disable", 0, 600 );
idCVar si_freezeWorldDeathDelay(	"si_freezeWorldDeathDelay",	"5",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "seconds before a player frozen by the world rather than an enemy comes back on their own, 0 to disable", 0, 60 );

// Overload and Harvester
idCVar si_obeliskHealth(			"si_obeliskHealth",			"2500",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "hit points of a team obelisk", 100, 10000 );
idCVar si_obeliskRegen(				"si_obeliskRegen",			"15",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "hit points an obelisk regenerates each second, 0 to disable", 0, 500 );
idCVar si_obeliskRespawn(			"si_obeliskRespawn",		"10",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "seconds before a destroyed obelisk comes back", 1, 120 );
idCVar si_skullTimeout(				"si_skullTimeout",			"30",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "seconds a dropped Harvester skull survives before despawning", 5, 300 );

// Domination
idCVar si_domCaptureTime(			"si_domCaptureTime",		"7",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "seconds of standing on a control point needed to take it", 1, 60 );
idCVar si_domScoreRate(				"si_domScoreRate",			"5",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "seconds between score ticks for each control point held", 1, 60 );

// Hit feedback.  Server side permission only - what an attacker actually sees
// is theirs to choose through the hud_damageNumber* settings below.
idCVar g_hitFeedback(				"g_hitFeedback",			"2",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "attacker hit feedback permitted by this server: 0 none, 1 announce the hit without the amount, 2 include the damage amount", 0, 2 );
// openQ4 END
idCVar si_fragLimit(				"si_fragLimit",				"10",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "frag limit", 0, MP_PLAYER_MAXFRAGS );
idCVar si_timeLimit(				"si_timeLimit",				"10",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_INTEGER, "time limit in minutes", 0, 60 );
idCVar si_teamDamage(				"si_teamDamage",			"0",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "enable team damage" );
idCVar si_warmup(					"si_warmup",				"1",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "do pre-game warmup" );
idCVar si_usePass(					"si_usePass",				"0",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "enable client password checking" );
#ifdef _MPBETA
	idCVar si_pure(					"si_pure",					"1",			CVAR_GAME | CVAR_SERVERINFO | CVAR_BOOL | CVAR_ROM, "server is pure and does not allow modified data" );
#else
	idCVar si_pure(					"si_pure",					"1",			CVAR_GAME | CVAR_SERVERINFO | CVAR_BOOL, "server is pure and does not allow modified data" );
#endif // _MPBETA
idCVar si_spectators(				"si_spectators",			"1",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "allow spectators or require all clients to play" );
idCVar si_shuffle(					"si_shuffle",				"0",			CVAR_GAME | CVAR_SERVERINFO | PC_CVAR_ARCHIVE | CVAR_BOOL, "shuffle teams after each round" );
// shouchard:  g_balanceTDM->si_autobalance so we can also use it for CTF
// asalmon: Changed to archive only on PC
idCVar si_autobalance(				"si_autobalance",			"1",			CVAR_GAME | CVAR_SERVERINFO | CVAR_BOOL | PC_CVAR_ARCHIVE, "maintain even teams" );

// RAVEN BEGIN
// jscott: added entity filtering
idCVar si_entityFilter(				"si_entityFilter",			"",				CVAR_GAME | CVAR_SERVERINFO, "filter to use when spawning entities" );
idCVar si_countDown(				"si_countDown",				"10",			CVAR_GAME | CVAR_SERVERINFO | CVAR_INTEGER, "pregame countdown in seconds", 4, 3600 );
// MCG: added "weapon stay" option
idCVar si_weaponStay(				"si_weaponStay",			"0",			CVAR_GAME | CVAR_SERVERINFO | CVAR_BOOL, "cannot pick up weapons you already have (get no ammo from them)" );
// RAVEN END

// RITUAL BEGIN
// DeadZone Mode and Buying related CVARS
idCVar si_deadZonePowerupTime(		"si_deadZonePowerupTime",		"45",			CVAR_GAME | CVAR_SERVERINFO | CVAR_INTEGER, "Amount of time the dead zone powerup lasts" );
idCVar si_buyModeStartingCredits(	"si_buyModeStartingCredits",	"1000",			CVAR_GAME | CVAR_SERVERINFO | CVAR_INTEGER, "Amount of credits players start with in buying enable games" );
idCVar si_buyModeMaxCredits(		"si_buyModeMaxCredits",			"25000",		CVAR_GAME | CVAR_SERVERINFO | CVAR_INTEGER, "Maximum amount of credits in buying enable games" );
idCVar si_buyModeMinCredits(		"si_buyModeMinCredits",			"0",			CVAR_GAME | CVAR_SERVERINFO | CVAR_INTEGER, "Minimum amount of credits in buying enable games" );
idCVar si_controlTime(				"si_controlTime",				"120",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_INTEGER, "Time required to hold the dead zone", 1, 999 );
// RITUAL END

idCVar si_fps(						"si_fps",						"60",			CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE | CVAR_INTEGER, "Server framerate/game tick rate", 30, 90 );

idCVar ri_useViewerPass(			"ri_useViewerPass",				"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "use g_viewerPassword for viewers/repeaters" );
idCVar g_viewerPassword(			"g_viewerPassword",				"",				CVAR_GAME | CVAR_ARCHIVE, "password for viewers/repeaters" );

idCVar ri_privateViewers(			"ri_privateViewers",			"0",			CVAR_GAME | CVAR_REPEATERINFO | CVAR_ARCHIVE | CVAR_INTEGER, "number of private viewer slots" );
idCVar g_privateViewerPassword(		"g_privateViewerPassword",		"",				CVAR_GAME | CVAR_ARCHIVE, "privatePassword for private viewer slots" );
idCVar g_repeaterPassword(			"g_repeaterPassword",			"",				CVAR_GAME | CVAR_ARCHIVE, "privatePassword for repeaters" );

idCVar ri_numViewers(				"ri_numViewers",				"0",			CVAR_GAME | CVAR_REPEATERINFO | CVAR_INTEGER | CVAR_ROM, "number of viewer slots in use" );
idCVar ri_numPrivateViewers(		"ri_numPrivateViewers",			"0",			CVAR_GAME | CVAR_REPEATERINFO | CVAR_INTEGER | CVAR_ROM, "number of private viewer slots in use" );

idCVar ri_name(						"ri_name",						"",				CVAR_GAME | CVAR_ARCHIVE, "override the server's si_name with this for relays" );

idCVar g_noTVChat(					"g_noTVChat",					"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "Server enable/disable flag for viewer chat on Q4TV" );

// user info
idCVar ui_name(						"ui_name",					"Player",		CVAR_GAME | CVAR_USERINFO | PC_CVAR_ARCHIVE | CVAR_CASE_SENSITIVE | CVAR_SPECIAL_CONCAT, "player name" );
idCVar ui_team(						"ui_team",				ui_teamArgs[ 0 ],	CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE, "player team", ui_teamArgs, idCmdSystem::ArgCompletion_String<ui_teamArgs> ); 
// RAVEN BEGIN
// ddynerman: new UI cvars
idCVar ui_model(					"ui_model",					"",	CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE, "player model, blank uses default model" );
idCVar ui_model_backup(				"ui_model_backup",			"",	CVAR_GAME | CVAR_USERINFO, "player model backup" );
idCVar ui_model_marine(				"ui_model_marine",			"",	CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE, "player model used on marine team in team games, blank uses default model" );
idCVar ui_model_strogg(				"ui_model_strogg",			"",	CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE, "player model used on strogg team in team games, blank uses default model" );
idCVar ui_clan(						"ui_clan",					"",	CVAR_GAME | CVAR_USERINFO | PC_CVAR_ARCHIVE | CVAR_CASE_SENSITIVE | CVAR_SPECIAL_CONCAT, "player clan" );
idCVar ui_hitscanTint(				"ui_hitscanTint",			"120.0 0.6 1.0",	CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE, "a tint applied to select hitscan effects.  Specified as a value in HSV color space. Hue [0.0-360.0] Saturation [0.0-1.0] Value [0.75-1.0]" );
// RAVEN END
idCVar ui_autoSwitch(				"ui_autoSwitch",			"1",			CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE | CVAR_BOOL, "auto switch weapon" );
idCVar ui_autoReload(				"ui_autoReload",			"1",			CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE | CVAR_BOOL, "auto reload weapon" );
idCVar ui_showGun(					"ui_showGun",				"1",			CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE | CVAR_BOOL, "show gun" );
idCVar ui_ready(					"ui_ready",				si_readyArgs[ 0 ],	CVAR_GAME | CVAR_USERINFO, "player is ready to start playing", idCmdSystem::ArgCompletion_String<si_readyArgs> );
idCVar ui_spectate(					"ui_spectate",		si_spectateArgs[ 0 ],	CVAR_GAME | CVAR_USERINFO, "play or spectate", idCmdSystem::ArgCompletion_String<si_spectateArgs> );
idCVar ui_autoJoin(					"ui_autoJoin",				"0",			CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE | CVAR_BOOL, "automatically join multiplayer servers instead of opening the initial join screen" );
idCVar ui_joined(					"ui_joined",				"0",			CVAR_GAME | CVAR_USERINFO | CVAR_BOOL, "internal multiplayer initial join choice state" );
idCVar ui_chat(						"ui_chat",					"0",			CVAR_GAME | CVAR_USERINFO | CVAR_BOOL | CVAR_ROM | CVAR_CHEAT, "player is chatting" );
idCVar ui_handicap(					"ui_handicap",				"100",			CVAR_GAME | CVAR_USERINFO | CVAR_INTEGER | CVAR_ARCHIVE, "player damage output handicap" );

// Voice-chat user settings are bound directly by the stock multiplayer GUI.
// Keep these declarations identical to game-sp so game-mp also owns them when
// it is the first (or only) module loaded by the unified client.
idCVar s_voiceChatSend(				"s_voiceChatSend",			"1",			CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE | CVAR_BOOL, "allow sending voice chat" );
idCVar s_voiceChatReceive(			"s_voiceChatReceive",		"1",			CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE | CVAR_BOOL, "allow receiving voice chat" );
idCVar s_voiceChatEcho(				"s_voiceChatEcho",			"0",			CVAR_GAME | CVAR_USERINFO | CVAR_ARCHIVE | CVAR_BOOL, "echo microphone back to speakers" );
idCVar s_voiceVolume(				"s_voiceVolume",			"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "voice chat receive volume", 0.0f, 1.0f );
idCVar s_micInputLevel(				"s_micInputLevel",			"5",			CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "microphone input level", 0.0f, 10.0f );

idCVar hud_showSpeed(				"hud_showSpeed",			"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "Show player's movement speed, measured in Units Per Second." );
idCVar hud_showInput(				"hud_showInput",			"0",			CVAR_GAME | CVAR_BOOL | CVAR_ARCHIVE, "When set to 1, shows the players movement controls on the HUD");
idCVar hud_inputPosition(			"hud_inputPosition",		"580 90",		CVAR_GAME | CVAR_ARCHIVE, "Input display position (x y)");
idCVar hud_inputColor(				"hud_inputColor",			"1 0 0",		CVAR_GAME | CVAR_ARCHIVE, "Input display color (r g b, range 0 to 1)");

// openQ4 BEGIN
// Floating damage numbers, ported from Quake Live's damage plums.  Purely a
// display choice: the server decides only whether it is allowed at all, through
// g_hitFeedback.
//
// This is a MULTIPLAYER-ONLY feature and it is declared here, in the mpgame
// tree, precisely so that it does not exist in single player: game-sp links no
// part of mp/HitFeedback.cpp and registers no hud_damageNumber* cvar at all.
// Do not mirror any of this into src/game/.
//
// Ships at 1 - the plum only ever describes damage the local player themselves
// dealt to an opponent, so it tells you nothing you did not earn, and reading a
// fight without it is strictly harder.  2 additionally shows team mates and
// self damage, which is diagnostic rather than competitive.
idCVar hud_damageNumbers(			"hud_damageNumbers",		"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_INTEGER, "floating damage numbers over the players you hit: 0 off, 1 opponents only, 2 all damage you deal including team mates and yourself", 0, 2 );
idCVar hud_damageNumberStyle(		"hud_damageNumberStyle",	"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_INTEGER, "damage number colouring: 1 white through red across the damage range, 2 one colour per damage band, 3 one colour per weapon", 1, 3 );
idCVar hud_damageNumberScale(		"hud_damageNumberScale",	"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "damage number size multiplier", 0.25f, 4.0f );

// Crosshair hit marker.  Four angled marks bloom out of the crosshair and fade
// on every hit the player lands, sized by how much the hit was worth.  It
// replaces stock Quake 4's crosshair recolour, which hud_crosshairHitFlash can
// bring back; turning the marker off restores that flash on its own so a player
// is never left with no hit cue at all.
// Fresh profiles keep stock hit feedback; existing archived choices are kept.
idCVar hud_hitMarker(				"hud_hitMarker",			"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "crosshair hit marker on hits you land" );
idCVar hud_hitMarkerScale(			"hud_hitMarkerScale",		"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "hit marker size multiplier, on top of g_crosshairSize", 0.25f, 4.0f );
idCVar hud_crosshairHitFlash(		"hud_crosshairHitFlash",	"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "recolour the crosshair on a hit, as stock Quake 4 does; implied when hud_hitMarker is off" );
// openQ4 END

// change anytime vars
idCVar developer(					"developer",				"0",			CVAR_GAME | CVAR_BOOL, "" );

idCVar g_forceModel(				"g_forceModel",				"",			CVAR_GAME | CVAR_ARCHIVE, "Locally forces all players to this model in non-team gameplay modes.  See g_forceStroggModel, g_forceMarineModel.  listModels to list available models", idCmdSystem::ArgCompletion_ForceModel );
idCVar g_forceStroggModel(			"g_forceStroggModel",		"",			CVAR_GAME | CVAR_ARCHIVE, "Locally forces Strogg team players to this model in team gameplay modes.  See g_forceModel.  listModels to list available models", idCmdSystem::ArgCompletion_ForceModelStrogg );
idCVar g_forceMarineModel(			"g_forceMarineModel",		"",			CVAR_GAME | CVAR_ARCHIVE, "Locally forces Marine team players to this model in team gameplay modes.  See g_forceModel.  listModels to list available models", idCmdSystem::ArgCompletion_ForceModelMarine );

// RAVEN BEGIN
// jnewquist: vertical stretch for letterboxed cinematics authored for 4:3 aspect
idCVar g_fixedHorizFOV(				"r_fixedHorizFOV",			"0",			CVAR_RENDERER | CVAR_BOOL, "vertical stretch for letterboxed cinematics authored for 4:3 aspect" );
idCVar g_cinematic(					"g_cinematic",				"1",			CVAR_GAME | CVAR_BOOL, "skips updating entities that aren't marked 'cinematic' '1' during cinematics" );
idCVar g_cinematicMaxSkipTime(		"g_cinematicMaxSkipTime",	"600",			CVAR_GAME | CVAR_FLOAT, "# of seconds to allow game to run when skipping cinematic.  prevents lock-up when cinematic doesn't end.", 0, 3600 );
idCVar g_autoSkipCinematics(		"g_autoSkipCinematics",		"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "automatically skips cinematics as soon as they start" );
idCVar g_objectivesToggle(			"g_objectivesToggle",		"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "the objectives display: 0 shows it while the scores button is held, 1 opens and closes it with each press" );
idCVar g_allowAssetlessStartup(		"g_allowAssetlessStartup",	"0",			CVAR_GAME | CVAR_BOOL, "allow renderer validation startup without shipped game decls" );
idCVar g_corpseRemoveDelayMP(		"g_corpseRemoveDelayMP",	"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "seconds before multiplayer corpses begin disappearing (0 = stock timing, -1 = never)", -1, 6000 );
idCVar g_corpseSink(				"g_corpseSink",				"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_INTEGER, "corpse sink mode (0 = off, 1 = ragdoll sink, 2 = no-ragdoll sink)", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );

idCVar g_muzzleFlash(				"g_muzzleFlash",			"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "show muzzle flashes" );
idCVar g_projectileLights(			"g_projectileLights",		"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "show dynamic lights on projectiles" );
idCVar g_classicDynamicLights(	"g_classicDynamicLights",	"1",		CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "Quake II/III style dynamic lights on muzzle flashes, bright projectiles and explosions" );
idCVar g_classicDynamicLightScale(	"g_classicDynamicLightScale",	"1",	CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "radius multiplier for the classic dynamic lights", 0.25f, 4.0f );
idCVar g_doubleVision(				"g_doubleVision",			"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "show double vision when taking damage" );
idCVar g_monsters(					"g_monsters",				"1",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_decals(					"g_decals",					"1",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "show decals such as bullet holes" );
idCVar g_knockback(					"g_knockback",				"1000",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar g_skill(						"g_skill",					"1",			CVAR_GAME | CVAR_INTEGER, "difficulty level", 0, MAX_SKILL_LEVELS - 1 );
idCVar g_nightmare(					"g_nightmare",				"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "if nightmare mode is allowed" );
idCVar g_gravity(					"g_gravity",		DEFAULT_GRAVITY_STRING, CVAR_GAME | CVAR_FLOAT, "singleplayer gravity" );
idCVar g_mp_gravity(				"g_mp_gravity",		DEFAULT_MP_GRAVITY_STRING, CVAR_GAME | CVAR_FLOAT, "multiplayer gravity" );
idCVar g_skipFX(					"g_skipFX",					"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_skipParticles(				"g_skipParticles",			"0",			CVAR_GAME | CVAR_BOOL, "" );

idCVar g_nailTrail(					"g_nailTrail",				"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "show smoke trails on nail projectiles" );
idCVar g_grenadeTrail(				"g_grenadeTrail",			"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "show smoke trails on grenade projectiles" );
idCVar g_rocketTrail(				"g_rocketTrail",			"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "show smoke trails on rocket projectiles" );
idCVar g_railTrail(					"g_railTrail",				"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "show spiral fx on rail trail" );
idCVar g_napalmTrail(				"g_napalmTrail",			"1",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "show fiery trails on napalm projectiles" );

idCVar g_predictedProjectiles(		"g_predictedProjectiles",	"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "predict projectiles on the client" );
idCVar g_projectilePredictionDebug(	"g_projectilePredictionDebug",	"0",		CVAR_GAME | CVAR_INTEGER, "report a projectile's dead-reckon offset against its authoritative trajectory, in milliseconds" );

idCVar g_disasm(					"g_disasm",					"0",			CVAR_GAME | CVAR_BOOL, "disassemble script into base/script/disasm.txt on the local drive when script is compiled" );
idCVar g_debugBounds(				"g_debugBounds",			"0",			CVAR_GAME | CVAR_BOOL, "checks for models with bounds > 2048" );
idCVar g_debugAnim(					"g_debugAnim",				"-1",			CVAR_GAME | CVAR_INTEGER, "displays information on which animations are playing on the specified entity number.  set to -1 to disable." );
idCVar g_debugMove(					"g_debugMove",				"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_debugDamage(				"g_debugDamage",			"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_debugWeapon(				"g_debugWeapon",			"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_debugScript(				"g_debugScript",			"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_debugMover(				"g_debugMover",				"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_debugTriggers(				"g_debugTriggers",			"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_debugCinematic(			"g_debugCinematic",			"0",			CVAR_GAME, "set to the name of the state you want to debug or * for all" );
// RAVEN BEGIN
// bdube: added
idCVar g_debugState(				"g_debugState",				"0",			CVAR_GAME, "" );
idCVar g_stopTime(					"g_stopTime",				"0",			CVAR_GAME | CVAR_BOOL, "" );
//idCVar g_damageScale(				"g_damageScale",			"1",			CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "scale final damage on player by this factor" );
// RAVEN END									   
idCVar g_armorProtection(			"g_armorProtection",		"0.66667",			CVAR_GAME | CVAR_FLOAT | PC_CVAR_ARCHIVE, "armor takes this percentage of damage" );
idCVar g_armorProtectionMP(			"g_armorProtectionMP",		"0.66667",		CVAR_GAME | CVAR_FLOAT | PC_CVAR_ARCHIVE, "armor takes this percentage of damage in mp" );
idCVar g_useDynamicProtection(		"g_useDynamicProtection",	"1",			CVAR_GAME | CVAR_BOOL | PC_CVAR_ARCHIVE, "scale damage and armor dynamically to keep the player alive more often" );
idCVar g_healthTakeTime(			"g_healthTakeTime",			"5",			CVAR_GAME | CVAR_INTEGER | PC_CVAR_ARCHIVE, "how often to take health in nightmare mode" );
idCVar g_healthTakeAmt(				"g_healthTakeAmt",			"5",			CVAR_GAME | CVAR_INTEGER | PC_CVAR_ARCHIVE, "how much health to take in nightmare mode" );
idCVar g_healthTakeLimit(			"g_healthTakeLimit",		"25",			CVAR_GAME | CVAR_INTEGER | PC_CVAR_ARCHIVE, "how low can health get taken in nightmare mode" );



idCVar g_showPVS(					"g_showPVS",				"0",			CVAR_GAME | CVAR_INTEGER, "", 0, 2 );
idCVar g_showTargets(				"g_showTargets",			"0",			CVAR_GAME | CVAR_BOOL, "draws entities and thier targets.  hidden entities are drawn grey." );
idCVar g_showTriggers(				"g_showTriggers",			"0",			CVAR_GAME | CVAR_BOOL, "draws trigger entities (orange) and thier targets (green).  disabled triggers are drawn grey." );
idCVar g_showCollisionWorld(		"g_showCollisionWorld",		"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_showCollisionModels(		"g_showCollisionModels",	"0",			CVAR_GAME | CVAR_INTEGER, "0 = off, 1 = draw collision models, 2 = only draw player collision models.  g_maxShowDistance controls distance." );
// RAVEN BEGIN
// rjohnson: added debug line drawing for traces
idCVar g_showCollisionTraces(		"g_showCollisionTraces",	"0",			CVAR_GAME | CVAR_INTEGER, "", 0, 2 );
// ddynerman: SD's clip sector code
idCVar g_showClipSectors(			"g_showClipSectors",		"0",			CVAR_GAME | CVAR_BOOL,	"" );
idCVar g_showClipSectorFilter(		"g_showClipSectorFilter",	"0",			CVAR_GAME,				"" );
idCVar g_showAreaClipSectors(		"g_showAreaClipSectors",	"0",			CVAR_GAME | CVAR_FLOAT, "" );
// RAVEN END
idCVar g_maxShowDistance(			"g_maxShowDistance",		"128",			CVAR_GAME | CVAR_FLOAT, "Distance at which to draw clipmodels and clipworld - Will significantly hurt performance at values above 512" );
idCVar g_showEntityInfo(			"g_showEntityInfo",			"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_showviewpos(				"g_showviewpos",			"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_debugYawHud(				"g_debugYawHud",			"0",			CVAR_GAME | CVAR_BOOL | CVAR_NOCHEAT, "shows per-frame yaw diagnostics in the HUD viewcomments channel" );
idCVar g_showcamerainfo(			"g_showcamerainfo",			"0",			CVAR_GAME | PC_CVAR_ARCHIVE, "displays the current frame # for the camera when playing cinematics" );
idCVar g_showTestModelFrame(		"g_showTestModelFrame",		"0",			CVAR_GAME | CVAR_BOOL, "displays the current animation and frame # for testmodels" );
idCVar g_showActiveEntities(		"g_showActiveEntities",		"0",			CVAR_GAME | CVAR_BOOL, "draws boxes around thinking entities.  dormant entities (outside of pvs) are drawn yellow.  non-dormant are green." );
idCVar g_showEnemies(				"g_showEnemies",			"0",			CVAR_GAME | CVAR_BOOL, "draws boxes around monsters that have targeted the the player" );

idCVar g_presentationInterpolation(	"g_presentationInterpolation",	"1",		CVAR_GAME | CVAR_BOOL | CVAR_ARCHIVE, "draw movers and everything riding them at the same interpolated presentation time as the first-person camera; 0 puts them back on the 60 Hz simulation clock" );

idCVar g_frametime(					"g_frametime",				"0",			CVAR_GAME | CVAR_BOOL, "displays timing information for each game frame" );
idCVar g_timeentities(				"g_timeEntities",			"0",			CVAR_GAME | CVAR_FLOAT, "when non-zero, shows entities whose think functions exceeded the # of milliseconds specified" );

// RAVEN BEGIN
// bdube: frame command debugging
idCVar g_showFrameCmds(				"g_showFrameCmds",			"0",			CVAR_GAME | CVAR_BOOL, "displays frame commands as they are executed" );
idCVar g_showGodDamage(				"g_showGodDamage",			"0",			CVAR_GAME | CVAR_BOOL, "displays the amount of damage taken while in god mode on the hud" );
idCVar g_debugVehicle(				"g_debugVehicle",			"0",			CVAR_GAME | CVAR_INTEGER, "" );
// RAVEN END

// RAVEN BEGIN
// twhitaker: for rvVehicleDriver
idCVar g_debugVehicleDriver(		"g_debugVehicleDriver",		"0",			CVAR_GAME | CVAR_INTEGER, "enables debug features for the func_vehicle_driver" );
idCVar g_debugVehicleAI(			"g_debugVehicleAI",			"0",			CVAR_GAME | CVAR_INTEGER, "enables debug features for the vehicle ai system" );
idCVar g_vehicleMode(				"g_vehicleMode",			"1",			CVAR_GAME | CVAR_INTEGER, "enables the new vehicle control system for the GEV." );
// RAVEN END
idCVar g_allowVehicleGunOverheat(	"g_allowVehicleGunOverheat","1",			CVAR_GAME | CVAR_BOOL, "allows disabling the gun overheating mechanism for vehicles that use it." );

idCVar ai_debugScript(				"ai_debugScript",			"-1",			CVAR_GAME | CVAR_INTEGER, "displays script calls for the specified monster entity number" );
idCVar ai_debugMove(				"ai_debugMove",				"0",			CVAR_GAME | CVAR_BOOL, "draws movement information for monsters" );
idCVar ai_debugTrajectory(			"ai_debugTrajectory",		"0",			CVAR_GAME | CVAR_BOOL, "draws trajectory tests for monsters" );
idCVar ai_debugTactical(			"ai_debugTactical",			"0",			CVAR_GAME, "draws tactical information for monsters" );
idCVar ai_debugHelpers(				"ai_debugHelpers",			"0",			CVAR_GAME, "draws ai helpers" );
idCVar ai_debugFilterString(		"ai_debugFilterString",		"",				CVAR_GAME, "see ai_debugFilter" );
idCVar ai_testPredictPath(			"ai_testPredictPath",		"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar ai_showCombatNodes(			"ai_showCombatNodes",		"0",			CVAR_GAME | CVAR_BOOL, "draws attack cones for monsters" );
idCVar ai_showPaths(				"ai_showPaths",				"0",			CVAR_GAME | CVAR_BOOL, "draws path_* entities" );
idCVar ai_showObstacleAvoidance(	"ai_showObstacleAvoidance",	"0",			CVAR_GAME | CVAR_INTEGER, "draws obstacle avoidance information for monsters.  if 2, draws obstacles for player, as well", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar ai_blockedFailSafe(			"ai_blockedFailSafe",		"1",			CVAR_GAME | CVAR_BOOL, "enable blocked fail safe handling" );
idCVar ai_debugSquad(				"ai_debugSquad",			"0",			CVAR_GAME | CVAR_BOOL, "draws squad info for allies" );
idCVar ai_debugStealth(				"ai_debugStealth",			"0",			CVAR_GAME | CVAR_INTEGER, "draws suspicion info for enemies" );
idCVar ai_allowTacticalRush(		"ai_allowTacticalRush",		"1",			CVAR_GAME | CVAR_BOOL, "allows tactical ai to rush an enemy when hurt" );


// RAVEN BEGIN
// nmckenzie: added speeds and freeze
idCVar ai_speeds(					"ai_speeds",				"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar ai_freeze(					"ai_freeze",				"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar ai_animShow(					"ai_animShow",				"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar ai_showCover(				"ai_showCover",				"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar ai_showTacticalFeatures(		"ai_showTacticalFeatures",	"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar ai_disableEntTactical(		"ai_disableEntTactical",	"0",			CVAR_GAME | CVAR_BOOL, "disables tactical points around entities" );
idCVar ai_disableAttacks(			"ai_disableAttacks",		"0",			CVAR_GAME | CVAR_BOOL, "disables attack decisions" );
idCVar ai_disableSimpleThink(		"ai_disableSimpleThink",	"0",			CVAR_GAME | CVAR_BOOL, "disables simple thinking in AI entities" );
idCVar ai_disableCover(				"ai_disableCover",			"0",			CVAR_GAME | CVAR_BOOL, "disables AI using cover points" );
//cdr: use new master move functions
idCVar ai_useRVMasterMove(			"ai_useRVMasterMove",		"0",			CVAR_GAME | CVAR_BOOL, "changes AI to use new master move function" );
//jshepard: allow out of date AAS files to be used, for testing
idCVar ai_medicApproachTimeout(		"ai_medicApproachTimeout",	"15",			CVAR_GAME | CVAR_FLOAT, "seconds a medic or tech may make no progress toward the player it chose to heal before it gives up and resumes its orders; 0 disables the watchdog" );
idCVar ai_scriptedMoveTimeout(		"ai_scriptedMoveTimeout",	"30",			CVAR_GAME | CVAR_FLOAT, "seconds a scripted move may make no forward progress before it is abandoned so the map script can continue; 0 disables the watchdog" );
idCVar ai_allowOldAAS(				"ai_allowOldAAS",			"0",			CVAR_GAME | CVAR_BOOL, "allows AI to use most recent AAS file, even if it is not up-to-date. Enable only for testing.");
// twhitaker: debugging support for eye focus
idCVar ai_debugEyeFocus(			"ai_debugEyeFocus",			"0",			CVAR_GAME | CVAR_BOOL, "draws eye focus info" );
//mcg: always allow player to push buddies, unless scripted
idCVar ai_playerPushAlways(			"ai_playerPushAlways",		"1",			CVAR_GAME | CVAR_BOOL, "always allow player to push buddies, unless scripted" );
// RAVEN END
	
idCVar g_dvTime(					"g_dvTime",					"1",			CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_dvAmplitude(				"g_dvAmplitude",			"0.001",		CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_dvFrequency(				"g_dvFrequency",			"0.5",			CVAR_GAME | CVAR_FLOAT, "" );

idCVar g_kickTime(					"g_kickTime",				"1",			CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_kickAmplitude(				"g_kickAmplitude",			"0.0001",		CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_blobTime(					"g_blobTime",				"1",			CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_blobSize(					"g_blobSize",				"1",			CVAR_GAME | CVAR_FLOAT, "" );

idCVar g_testHealthVision(			"g_testHealthVision",		"0",			CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_editEntityMode(			"g_editEntityMode",			"0",			CVAR_GAME | CVAR_INTEGER,	"0 = off\n"
																											"1 = lights\n"
																											"2 = sounds\n"
																											"3 = articulated figures\n"
																											"4 = particle systems\n"
																											"5 = monsters\n"
																											"6 = entity names\n"
// RAVEN BEGIN
// bdube: extended
																											"7 = entity models\n"
																											"8 = effects", 0, 8, idCmdSystem::ArgCompletion_Integer<0,8> );
// rhummer: Added archive flag.
idCVar g_editEntityDistance(		"g_editEntityDistance",		"512",			CVAR_GAME | CVAR_ARCHIVE,	"range to display entities to edit" );
// rhummer: Allow to customize the distance the text is drawn for edit entities, Zack request. Also added archive flag.
idCVar g_editEntityTextDistance(	"g_editEntityTextDistance",	"256",			CVAR_GAME | CVAR_ARCHIVE,	"range to display entities to edit text information");
idCVar g_testCTF(					"g_testCTF",				"0",			CVAR_GAME | CVAR_CHEAT | CVAR_BOOL, "" );
// rjohnson: entity usage stats
idCVar g_keepEntityStats(			"g_keepEntityStats",		"0",			CVAR_GAME | CVAR_CHEAT |CVAR_BOOL, "keep track of entity usage stats" );
// RAVEN END
idCVar g_dragEntity(				"g_dragEntity",				"0",			CVAR_GAME | CVAR_BOOL, "allows dragging physics objects around by placing the crosshair over them and holding the fire button" );
idCVar g_dragDamping(				"g_dragDamping",			"0.5",			CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_dragShowSelection(			"g_dragShowSelection",		"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar g_dropItemRotation(			"g_dropItemRotation",		"",				CVAR_GAME, "" );

idCVar g_vehicleVelocity(			"g_vehicleVelocity",		"1000",			CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_vehicleForce(				"g_vehicleForce",			"50000",		CVAR_GAME | CVAR_FLOAT, "" );

idCVar ik_enable(					"ik_enable",				"1",			CVAR_GAME | CVAR_BOOL, "enable IK" );
idCVar ik_debug(					"ik_debug",					"0",			CVAR_GAME | CVAR_BOOL, "show IK debug lines" );

idCVar af_useLinearTime(			"af_useLinearTime",			"1",			CVAR_GAME | CVAR_BOOL, "use linear time algorithm for tree-like structures" );
idCVar af_useImpulseFriction(		"af_useImpulseFriction",	"0",			CVAR_GAME | CVAR_BOOL, "use impulse based contact friction" );
idCVar af_useJointImpulseFriction(	"af_useJointImpulseFriction","0",			CVAR_GAME | CVAR_BOOL, "use impulse based joint friction" );
idCVar af_useSymmetry(				"af_useSymmetry",			"1",			CVAR_GAME | CVAR_BOOL, "use constraint matrix symmetry" );
idCVar af_skipSelfCollision(		"af_skipSelfCollision",		"0",			CVAR_GAME | CVAR_BOOL, "skip self collision detection" );
idCVar af_skipLimits(				"af_skipLimits",			"0",			CVAR_GAME | CVAR_BOOL, "skip joint limits" );
idCVar af_skipFriction(				"af_skipFriction",			"0",			CVAR_GAME | CVAR_BOOL, "skip friction" );
idCVar af_forceFriction(			"af_forceFriction",			"-1",			CVAR_GAME | CVAR_FLOAT, "force the given friction value" );
idCVar af_maxLinearVelocity(		"af_maxLinearVelocity",		"128",			CVAR_GAME | CVAR_FLOAT, "maximum linear velocity" );
idCVar af_maxAngularVelocity(		"af_maxAngularVelocity",	"1.57",			CVAR_GAME | CVAR_FLOAT, "maximum angular velocity" );
idCVar af_timeScale(				"af_timeScale",				"1",			CVAR_GAME | CVAR_FLOAT, "scales the time" );
idCVar af_jointFrictionScale(		"af_jointFrictionScale",	"0",			CVAR_GAME | CVAR_FLOAT, "scales the joint friction" );
idCVar af_contactFrictionScale(		"af_contactFrictionScale",	"0",			CVAR_GAME | CVAR_FLOAT, "scales the contact friction" );
idCVar af_highlightBody(			"af_highlightBody",			"",				CVAR_GAME, "name of the body to highlight" );
idCVar af_highlightConstraint(		"af_highlightConstraint",	"",				CVAR_GAME, "name of the constraint to highlight" );
idCVar af_showTimings(				"af_showTimings",			"0",			CVAR_GAME | CVAR_BOOL, "show articulated figure cpu usage" );
idCVar af_showConstraints(			"af_showConstraints",		"0",			CVAR_GAME | CVAR_BOOL, "show constraints" );
idCVar af_showConstraintNames(		"af_showConstraintNames",	"0",			CVAR_GAME | CVAR_BOOL, "show constraint names" );
idCVar af_showConstrainedBodies(	"af_showConstrainedBodies",	"0",			CVAR_GAME | CVAR_BOOL, "show the two bodies contrained by the highlighted constraint" );
idCVar af_showPrimaryOnly(			"af_showPrimaryOnly",		"0",			CVAR_GAME | CVAR_BOOL, "show primary constraints only" );
idCVar af_showTrees(				"af_showTrees",				"0",			CVAR_GAME | CVAR_BOOL, "show tree-like structures" );
idCVar af_showLimits(				"af_showLimits",			"0",			CVAR_GAME | CVAR_BOOL, "show joint limits" );
idCVar af_showBodies(				"af_showBodies",			"0",			CVAR_GAME | CVAR_BOOL, "show bodies" );
idCVar af_showBodyNames(			"af_showBodyNames",			"0",			CVAR_GAME | CVAR_BOOL, "show body names" );
idCVar af_showMass(					"af_showMass",				"0",			CVAR_GAME | CVAR_BOOL, "show the mass of each body" );
idCVar af_showTotalMass(			"af_showTotalMass",			"0",			CVAR_GAME | CVAR_BOOL, "show the total mass of each articulated figure" );
idCVar af_showInertia(				"af_showInertia",			"0",			CVAR_GAME | CVAR_BOOL, "show the inertia tensor of each body" );
idCVar af_showVelocity(				"af_showVelocity",			"0",			CVAR_GAME | CVAR_BOOL, "show the velocity of each body" );
idCVar af_showActive(				"af_showActive",			"0",			CVAR_GAME | CVAR_BOOL, "show tree-like structures of articulated figures not at rest" );
idCVar af_testSolid(				"af_testSolid",				"1",			CVAR_GAME | CVAR_BOOL, "test for bodies initially stuck in solid" );

idCVar rb_showTimings(				"rb_showTimings",			"0",			CVAR_GAME | CVAR_BOOL, "show rigid body cpu usage" );
idCVar rb_showBodies(				"rb_showBodies",			"0",			CVAR_GAME | CVAR_BOOL, "show rigid bodies" );
idCVar rb_showMass(					"rb_showMass",				"0",			CVAR_GAME | CVAR_BOOL, "show the mass of each rigid body" );
idCVar rb_showInertia(				"rb_showInertia",			"0",			CVAR_GAME | CVAR_BOOL, "show the inertia tensor of each rigid body" );
idCVar rb_showVelocity(				"rb_showVelocity",			"0",			CVAR_GAME | CVAR_BOOL, "show the velocity of each rigid body" );
idCVar rb_showActive(				"rb_showActive",			"0",			CVAR_GAME | CVAR_BOOL, "show rigid bodies that are not at rest" );

// RAVEN BEGIN
// bdube: more rigid body debug
idCVar rb_showContacts(				"rb_showContacts",			"0",			CVAR_GAME | CVAR_BOOL, "show rigid body contacts" );
// RAVEN END

// The default values for player movement cvars are set in def/player.def
idCVar pm_jumpheight(				"pm_jumpheight",			"48",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "approximate hieght the player can jump" );
idCVar pm_stepsize(					"pm_stepsize",				"16",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "maximum height the player can step up without jumping" );
idCVar pm_crouchspeed(				"pm_crouchspeed",			"80",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "speed the player can move while crouched" );
// RAVEN BEGIN
idCVar pm_speed(					"pm_speed",					"160",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "speed the player can move while running" );
idCVar pm_walkspeed(				"pm_walkspeed",				"80",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "speed the player can move while walking" );
// RAVEN END
idCVar pm_noclipspeed(				"pm_noclipspeed",			"270",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "speed the player can move while in noclip" );
idCVar pm_spectatespeed(			"pm_spectatespeed",			"450",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "speed the player can move while spectating" );
idCVar pm_spectatebbox(				"pm_spectatebbox",			"32",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "size of the spectator bounding box" );
idCVar pm_usecylinder(				"pm_usecylinder",			"1",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_INTEGER | CVAR_NORESET | CVAR_ARCHIVE, "use a cylinder approximation instead of a bounding box for player collision detection (>= 3 - custom number of sides)" );
idCVar pm_minviewpitch(				"pm_minviewpitch",			"-89",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "amount player's view can look up (negative values are up)" );
idCVar pm_maxviewpitch(				"pm_maxviewpitch",			"89",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "amount player's view can look down" );
idCVar pm_stamina(					"pm_stamina",				"24",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "length of time player can run" );
idCVar pm_staminathreshold(			"pm_staminathreshold",		"45",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "when stamina drops below this value, player gradually slows to a walk" );
idCVar pm_staminarate(				"pm_staminarate",			"0.75",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "rate that player regains stamina. divide pm_stamina by this value to determine how long it takes to fully recharge." );

// ddynerman: adjusted bboxes to actual height
idCVar pm_normalheight(				"pm_normalheight",			"77",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "height of player's bounding box while standing" );
idCVar pm_crouchheight(				"pm_crouchheight",			"49",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "height of player's bounding box while crouched" );

idCVar pm_crouchviewheight(			"pm_crouchviewheight",		"32",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "height of player's view while crouched" );
idCVar pm_normalviewheight(			"pm_normalviewheight",		"68",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "height of player's view while standing" );

idCVar pm_deadheight(				"pm_deadheight",			"20",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "height of player's bounding box while dead" );
idCVar pm_deadviewheight(			"pm_deadviewheight",		"10",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "height of player's view while dead" );
idCVar pm_crouchrate(				"pm_crouchrate",			"0.87",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "time it takes for player's view to change from standing to crouching" );
idCVar pm_bboxwidth(				"pm_bboxwidth",				"32",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NORESET, "x/y size of player's bounding box" );
idCVar pm_crouchbob(				"pm_crouchbob",				"0.5",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NOCHEAT | CVAR_NORESET, "bob much faster when crouched" );
idCVar pm_walkbob(					"pm_walkbob",				"0.3",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NOCHEAT | CVAR_NORESET, "bob slowly when walking" );
idCVar pm_runbob(					"pm_runbob",				"0.4",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NOCHEAT | CVAR_NORESET, "bob faster when running" );
idCVar pm_runpitch(					"pm_runpitch",				"0.002",		CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NOCHEAT | CVAR_NORESET, "" );
idCVar pm_runroll(					"pm_runroll",				"0.005",		CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NOCHEAT | CVAR_NORESET, "" );
idCVar pm_bobup(					"pm_bobup",					"0.005",		CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NOCHEAT | CVAR_NORESET, "" );
idCVar pm_bobpitch(					"pm_bobpitch",				"0.002",		CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NOCHEAT | CVAR_NORESET, "" );
idCVar pm_bobroll(					"pm_bobroll",				"0.002",		CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT | CVAR_NOCHEAT, "" );
idCVar pm_thirdPersonRange(			"pm_thirdPersonRange",		"80",			CVAR_GAME | CVAR_FLOAT | CVAR_NORESET, "camera distance from player in 3rd person" );
idCVar pm_thirdPersonHeight(		"pm_thirdPersonHeight",		"0",			CVAR_GAME | CVAR_FLOAT | CVAR_NORESET, "height of camera from normal view height in 3rd person" );
idCVar pm_thirdPersonAngle(			"pm_thirdPersonAngle",		"0",			CVAR_GAME | CVAR_FLOAT | CVAR_NORESET, "direction of camera from player in 3rd person in degrees (0 = behind player, 180 = in front)" );
idCVar pm_thirdPersonClip(			"pm_thirdPersonClip",		"1",			CVAR_GAME | CVAR_BOOL, "clip third person view into world space" );
idCVar pm_thirdPerson(				"pm_thirdPerson",			"0",			CVAR_GAME | CVAR_BOOL, "enables third person view" );
idCVar pm_thirdPersonDeath(			"pm_thirdPersonDeath",		"0",			CVAR_GAME | CVAR_BOOL, "enables third person view when player dies" );
idCVar pm_modelView(				"pm_modelView",				"0",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_INTEGER, "draws camera from POV of player model (1 = always, 2 = when dead)", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar pm_airTics(					"pm_air",					"1800",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_INTEGER, "how many 60Hz frames the player can spend in a vacuum before taking damage" );

// openQ4 BEGIN
idCVar pm_waterAirTics(				"pm_waterAir",				"720",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_INTEGER, "how many 60Hz frames the player can stay submerged before drowning, 720 is Quake 3's twelve seconds" );
idCVar pm_swimSpeed(				"pm_swimSpeed",				"160",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT, "speed the player can swim, 160 is Quake 3's (its 320 run speed times pm_swimScale 0.5)" );
idCVar pm_swimSpeedFast(			"pm_swimSpeedFast",			"200",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_FLOAT, "swim speed in multiplayer and after stroggification" );
idCVar g_liquidDamageInterval(		"g_liquidDamageInterval",	"500",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_INTEGER, "milliseconds between lava and slime damage ticks" );
idCVar g_drownDamageMax(			"g_drownDamageMax",			"15",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_INTEGER, "maximum damage per drowning tick, the ramp starts at the damage def's value and climbs to this" );
idCVar g_liquidScreenTint(			"g_liquidScreenTint",		"1",			CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "strength of the full screen tint while the camera is under a liquid surface, 0 disables it", 0.0f, 1.0f );
idCVar g_debugLiquid(				"g_debugLiquid",			"0",			CVAR_GAME | CVAR_BOOL, "print player liquid state changes and liquid damage to the console" );
idCVar g_liquidTestVolume(          "g_liquidTestVolume",       "",             CVAR_GAME, "dev aid: spawn a liquid volume around the player on spawn - water, slime or lava, empty to disable" );
idCVar g_liquidTestVolumeSize(      "g_liquidTestVolumeSize",   "640",          CVAR_GAME | CVAR_FLOAT, "cube size of the g_liquidTestVolume box" );
// openQ4 END

// RAVEN BEGIN
// asalmon: parameters for aim assistance on Xenon - or a non-final pc build so Caryn can edit the guis
#if defined( _XBOX ) || !defined( _FINAL )
idCVar pm_AimAssist(				"pm_AimAssist",				"2",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE , "Enable Xbox aim assistance. 1 to use change player view method. 2 to use change muzzle aim method.\n");
idCVar pm_AimAssistDistance(		"pm_AimAssistDistance",		"1000",			CVAR_GAME | CVAR_INTEGER, "The max aim assist distance.\n");
idCVar pm_AimAssistThreshold(		"pm_AimAssistThreshold",	"1.0",			CVAR_GAME | CVAR_FLOAT, "Threshold by which the projectile is aimed at the offending target.\n");
idCVar pm_AimAssistFOV(				"pm_AimAssistFOV",			"10",			CVAR_GAME | CVAR_INTEGER, "The field of view for aim assistance.\n");
idCVar pm_AimAssistBump(			"pm_AimAssistBump",			"10",			CVAR_GAME | CVAR_INTEGER, "The percentage of correction applied either to the view or the muzzle aim.\n");
idCVar pm_showAimAssist(			"pm_showAimAssist",			"0",			CVAR_GAME | CVAR_BOOL, "Draw aim assist frustum and bounding boxes.\n");
idCVar pm_AimAssistSlow(			"pm_AimAssistSlow",			"50",			CVAR_GAME | CVAR_INTEGER, "The percentage to slow the turning motion by when targeting an enemy.\n");

//asalmon: xenon controller config cvars
idCVar pm_ThumbstickConfig(			"pm_ThumbstickConfig",		"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_GUI, "Change the thumbstick config on Xenon. 0 right handed, 1 left handed.\n");
idCVar pm_ButtonConfig(				"pm_ButtonConfig",			"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_GUI, "Change the button configuration for Xenon.\n");
idCVar pm_Inversion(				"pm_Inversion",				"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_GUI, "invert look up and down\n");

idCVar pm_VLookSens(				"pm_VLookSens",		"1.0",	CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "Xenon sensitivity\n");
idCVar pm_HLookSens(				"pm_HLookSens",		"1.0",	CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "Xenon sensitivity\n");
idCVar pm_VMoveSens(				"pm_VMoveSens",		"1.0",	CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "Xenon sensitivity\n");
idCVar pm_HMoveSens(				"pm_HMoveSens",		"1.0",	CVAR_GAME | CVAR_ARCHIVE | CVAR_FLOAT, "Xenon sensitivity\n");

idCVar pm_voiceEnabled(				"pm_voiceEnabled",	"1",	CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL | CVAR_GUI, "Enable/disable voice.\n");

//asalmon: Xenon leaderboard cvars
idCVar ui_LeaderboardView(			"ui_LeaderboardView",		"17",			CVAR_INTEGER | CVAR_NOCHEAT, "Which leaderboard to show.\n");
idCVar ui_LeaderboardSort(		"ui_LeaderboardSort",		"1",			CVAR_INTEGER | CVAR_NOCHEAT, "How to sort the leaderboard. 0 for rating, 1 for ranking, 2 for friends, 3 find logged in player.\n");

//nrausch
idCVar pm_RocketJumpAutocenter(	"pm_RocketJumpAutocenter",		"1",	CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "Automatic autocentering following a rocket jump\n");
idCVar pm_IAmACheater(			"pm_IAmACheater",		"0",	CVAR_GAME | CVAR_BOOL, "Whomever is playing is a dirty, rotten cheater\n");

idCVar g_systemLinkMatch(	"g_systemLinkMatch",		"0",	CVAR_INTEGER, "In a system link game\n");

//asalmon: cvars for Live teams.  Teams will now be a post launch feature but this was left here in case it is of use on future projects
//idCVar ui_LiveClanName(				"ui_LiveClanName",			"My Clan",				CVAR_GAME | CVAR_USERINFO, "The name of the live clan being created\n");
//idCVar ui_LiveClanDesc(				"ui_LiveClanDesc",			"A Quake 4 clan",		CVAR_GAME | CVAR_USERINFO, "The description of the live clan being created\n");
//idCVar ui_LiveClanMotto(			"ui_LiveClanMotto",			"We love Quake 4",		CVAR_GAME | CVAR_USERINFO, "The motto of the live clan being created\n");
//idCVar ui_LiveClanUrl(				"ui_LiveClanUrl",			"www.ravensoft.com",	CVAR_GAME | CVAR_USERINFO, "The url of the live clan being created\n");
//
//idCVar ui_LiveRecruitName(				"ui_LiveRecruitName",			"Recruit Name Here",	CVAR_GAME | CVAR_USERINFO, "name of the gamer you are trying recruit\n");
//idCVar ui_LiveRecruitPDelete(			"ui_LiveRecruitPDelete",		"0",					CVAR_GAME | CVAR_USERINFO, "give the recruit delete permissions\n");
//idCVar ui_LiveRecruitPData(				"ui_LiveRecruitPData",			"0",					CVAR_GAME | CVAR_USERINFO, "give the recruit modify data permissions\n");
//idCVar ui_LiveRecruitPMemberPermissions("ui_LiveRecruitPMemberPermissions",		"0",					CVAR_GAME | CVAR_USERINFO, "give the recruit member modify permissions\n");
//idCVar ui_LiveRecruitPMemberDelete(		"ui_LiveRecruitPMemberDelete",	"0",					CVAR_GAME | CVAR_USERINFO, "give the recruit member delete permissions\n");
//idCVar ui_LiveRecruitPMemberRecruit(	"ui_LiveRecruitPMemberRecruit",	"0",					CVAR_GAME | CVAR_USERINFO, "give the recruit member recruit permissions\n");
#endif

idCVar pm_zoomedSlow(				"pm_zoomedSlow",			"50",			CVAR_GAME | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT | CVAR_NORESET, "look sensitivity while scoped/zoomed (1..100% of normal speed)", 1, 100, idCmdSystem::ArgCompletion_Integer<1,100> );

#ifndef _XENON
idCVar pm_isZoomed(					"pm_isZoomed",			"0",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT | CVAR_NORESET, "active scoped look sensitivity percent; 0 when not scoped");
#endif

// nmckenzie: added ability to try alternate accelerations.
idCVar pm_acceloverride(			"pm_acceloverride",			"0",			CVAR_GAME | CVAR_FLOAT, "Adjust the player acceleration." );
idCVar pm_frictionoverride(			"pm_frictionoverride",		"-1",			CVAR_GAME | CVAR_FLOAT, "Adjust the player friciton." );
idCVar pm_forcespectatormove(		"pm_forcespectatormove",	"0",			CVAR_GAME | CVAR_FLOAT, "Force the player to move like a spectator (fly)." );
// bdube: added vehicle cvars
idCVar pm_vehicleCameraSnap(		"pm_vehicleCameraSnap",			"1",		CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "" );
idCVar pm_vehicleCameraMinDist(		"pm_vehicleCameraMinDist",		"300",		CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "" );
idCVar pm_vehicleCameraSpeedScale(	"pm_vehicleCameraSpeedScale",	"0.5",		CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "" );
idCVar pm_vehicleCameraScaleMax(	"pm_vehicleCameraScaleMax",		"300",		CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "" );
idCVar pm_vehicleSoundLerpScale(	"pm_vehicleSoundLerpScale",		"10",		CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "" );
// RAVEN END

idCVar g_showPlayerShadow(			"g_showPlayerShadow",		"1",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "enables shadow of player model" );
idCVar g_showPresentationPose(		"g_showPresentationPose",	"0",			CVAR_GAME | CVAR_BOOL, "report whether the local player's body reaches the presentation clock" );
idCVar g_presentationClockCapture(	"g_presentationClockCapture",	"0",		CVAR_GAME | CVAR_INTEGER, "capture this many presentation-clock samples, then dump them once" );

idCVar g_skipPlayerShadowsMP(		"g_skipPlayerShadowsMP",	"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "disables all player shadows in multiplayer" );
idCVar g_skipItemShadowsMP(			"g_skipItemShadowsMP",		"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "disables all item shadows in multiplayer" );
idCVar g_simpleItems(				"g_simpleItems",			"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_INTEGER, "multiplayer item presentation: 0 original models, 1 legacy icons, 2 flat icon colors, 3 flat icon colors with an upward light sweep", 0, 3 );
idCVar g_mpFlatOpponentWeapons(		"g_mpFlatOpponentWeapons",	"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "flat-shade weapons held by multiplayer opponents using their icon colors" );
idCVar g_showHud(					"g_showHud",				"1",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "" );
idCVar g_showProjectilePct(			"g_showProjectilePct",		"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "enables display of player hit percentage" );
// RAVEN BEGIN
// dluetscher: changed to g_brassTime
idCVar g_brassTime(					"g_brassTime",				"1",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "amount of time brass should stay in the world before dissapearing, set to 0 to disable brass" );
// RAVEN END
idCVar g_gun_x(						"g_gunX",					"1",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "" );
idCVar g_gun_y(						"g_gunY",					"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "" );
idCVar g_gun_z(						"g_gunZ",					"-1",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_FLOAT, "" );
idCVar cl_gun_x(					"cl_gun_x",					"0",			PC_CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "client first-person weapon right offset" );
idCVar cl_gun_y(					"cl_gun_y",					"0",			PC_CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "client first-person weapon forward offset" );
idCVar cl_gun_z(					"cl_gun_z",					"0",			PC_CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "client first-person weapon up offset" );
idCVar g_weaponFovEffect(			"g_weaponFovEffect",		"0",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "Adjusts the position of the weapon model with increased fov" );
idCVar g_weaponMuzzleKick(			"g_weaponMuzzleKick",	"1",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "view weapon kicks back when it fires; 0 holds the view model still" );

idCVar g_viewNodalX(				"g_viewNodalX",				"0",			CVAR_GAME | CVAR_FLOAT, "" );
idCVar g_viewNodalZ(				"g_viewNodalZ",				"0",			CVAR_GAME | CVAR_FLOAT, "" );
// RAVEN BEGIN
// jshepard: fov as a float for smoother transitions?
idCVar g_fov(						"g_fov",					"90",			CVAR_GAME | CVAR_FLOAT | PC_CVAR_ARCHIVE, "" );
// RAVEN END
idCVar g_skipViewEffects(			"g_skipViewEffects",		"0",			CVAR_GAME | CVAR_BOOL, "skip damage and other view effects" );
idCVar g_mpWeaponAngleScale(		"g_mpWeaponAngleScale",		"0",			CVAR_GAME | CVAR_FLOAT, "Control the weapon sway in MP" );

// RAVEN BEGIN
// bdube: crosshairs
// mekberg: custom size
idCVar g_crosshairSize(				"g_crosshairSize",			"32",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE, "crosshair size: 16,24,32,40,48", 16, 48 );
//idCVar g_crosshairColor(			"g_crosshairColor",			"0.458 0.894 0.247 .75", CVAR_GAME | CVAR_ARCHIVE, "sets the combat crosshair color" );
idCVar g_crosshairColor(			"g_crosshairColor",			"1 1 1 1", CVAR_GAME | CVAR_ARCHIVE, "sets the combat crosshair color" );
// cnicholson: Custom crosshair
idCVar g_crosshairCustom(			"g_crosshairCustom",		"0",			CVAR_GAME | PC_CVAR_ARCHIVE, "sets the custom combat crosshair" );
idCVar g_crosshairCustomFile(		"g_crosshairCustomFile",	"0",			CVAR_GAME | PC_CVAR_ARCHIVE, "stores the custom crosshair's filename" );
idCVar g_crosshairCharInfoFar(		"g_crosshairCharInfoFar",	"1",			CVAR_GAME | CVAR_BOOL, "instead of a green crosshair from far away, full character info always draws" );
// bdube: database entries
idCVar g_showHudPopups(				"g_showHudPopups",			"1",			CVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL, "displays objective and database popups on the hud" );
idCVar g_showRange(					"g_showRange",				"0",			CVAR_GAME | CVAR_CHEAT | CVAR_BOOL, "shows the range from the player to the first collision under the players crosshair" );
// bdube: debug hud
idCVar g_showDebugHud(				"g_showDebugHud",			"0",			CVAR_GAME | CVAR_INTEGER, "displays the debug hud\n"
																										  "0  = off\n"
																										  "1  = player\n"
																										  "2  = physics\n"
																										  "3  = AI\n"
																										  "4  = vehicle\n"
																										  "5  = performance\n"
																										  "6  = effects\n"
																										  "7  = map information\n"
																										  "8  = AI performance\n"
																										  "9  = MP\n"
																										  "10 = Sound\n"
																				  "32 = scratch\n", 0, 32, idCmdSystem::ArgCompletion_Integer<0,32> );
// bdube: cvar for messing with foreshortening and gun position
idCVar g_gun_pitch(					"g_gunPitch",				"0",			CVAR_GAME | CVAR_FLOAT,		"" );
idCVar g_gun_yaw(					"g_gunYaw",					"0",			CVAR_GAME | CVAR_FLOAT,		"" );
idCVar g_gun_roll(					"g_gunRoll",				"0",			CVAR_GAME | CVAR_FLOAT,		"" );
// abahr:
idCVar g_gunViewStyle(				"g_gunViewStyle",			"0",			CVAR_GAME | CVAR_NOCHEAT | PC_CVAR_ARCHIVE | CVAR_INTEGER,	"style presets\n"
																											"0 = Q3 style\n"
																											"1 = Shouldered style\n");
// jscott: cvar for debugging playbacks
idCVar g_showPlayback(				"g_showPlayback",			"0",			CVAR_GAME | CVAR_INTEGER, "show g_currentPlayback" );
idCVar g_currentPlayback(			"g_currentPlayback",		"",				CVAR_GAME, "name of playback shown by g_showPlayback" );
// jscott: unused
//idCVar g_testParticle(				"g_testParticle",			"0",			CVAR_GAME | CVAR_INTEGER, "test particle visualation, set by the particle editor" );
//idCVar g_testParticleName(			"g_testParticleName",		"",				CVAR_GAME, "name of the particle being tested by the particle editor" );
// RAVEN END
idCVar g_testModelRotate(			"g_testModelRotate",		"0",			CVAR_GAME, "test model rotation speed" );
idCVar g_testPostProcess(			"g_testPostProcess",		"",				CVAR_GAME, "name of material to draw over screen" );
idCVar g_autoExecAfterMapLoad(		"g_autoExecAfterMapLoad",	"",				CVAR_GAME | CVAR_NOCHEAT, "exec a cfg path once after map activation for automated validation" );
idCVar g_autoExecAfterMapLoadDelayMs( "g_autoExecAfterMapLoadDelayMs", "1000",	CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "delay in ms after map activation before executing g_autoExecAfterMapLoad" );
idCVar g_testModelAnimate(			"g_testModelAnimate",		"0",			CVAR_GAME | CVAR_INTEGER, "test model animation,\n"
																							"0 = cycle anim with origin reset\n"
																							"1 = cycle anim with fixed origin\n"
																							"2 = cycle anim with continuous origin\n"
																							"3 = frame by frame with continuous origin\n"
																							"4 = play anim once\n"
																							"5 = frame by frame with fixed origin", 0, 5, idCmdSystem::ArgCompletion_Integer<0,5> );
idCVar g_testModelBlend(			"g_testModelBlend",			"0",			CVAR_GAME | CVAR_INTEGER, "number of frames to blend" );
idCVar g_testDeath(					"g_testDeath",				"0",			CVAR_GAME | CVAR_BOOL, "" );
// RAVEN BEGIN
// bdube: added scoreboard testing
idCVar g_testScoreboard(			"g_testScoreboard",			"0",			CVAR_GAME | CVAR_INTEGER, "number of clients to test in the scoreboard gui" );
idCVar g_testPlayer(				"g_testPlayer",				"",				CVAR_GAME, "test player classname" );
// RAVEN END
idCVar g_exportMask(				"g_exportMask",				"",				CVAR_GAME, "" );
idCVar g_flushSave(					"g_flushSave",				"0",			CVAR_GAME | CVAR_BOOL, "1 = don't buffer file writing for save games." );

idCVar aas_test(					"aas_test",					"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar aas_showAreas(				"aas_showAreas",			"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar aas_showAreaBounds(			"aas_showAreaBounds",		"0",			CVAR_GAME | CVAR_INTEGER, "When show areas is on, this draws the bounds of the areas, too..." );
idCVar aas_showPath(				"aas_showPath",				"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar aas_showFlyPath(				"aas_showFlyPath",			"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar aas_showWallEdges(			"aas_showWallEdges",		"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar aas_showHideArea(			"aas_showHideArea",			"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar aas_pullPlayer(				"aas_pullPlayer",			"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar aas_randomPullPlayer(		"aas_randomPullPlayer",		"0",			CVAR_GAME | CVAR_BOOL, "" );
idCVar aas_goalArea(				"aas_goalArea",				"0",			CVAR_GAME | CVAR_INTEGER, "" );
idCVar aas_showPushIntoArea(		"aas_showPushIntoArea",		"0",			CVAR_GAME | CVAR_BOOL, "" );
// RAVEN BEGIN
// rjohnson: added aas help
idCVar aas_showProblemAreas(		"aas_showProblemAreas",		"0",			CVAR_GAME | CVAR_INTEGER, "" );
// cdr: added rev reach
idCVar aas_showRevReach(			"aas_showRevReach",			"0",			CVAR_GAME | CVAR_INTEGER, "" );
// RAVEN END

idCVar g_password(					"g_password",				"",				CVAR_GAME | PC_CVAR_ARCHIVE, "game password" );
idCVar password(					"password",					"",				CVAR_GAME | CVAR_NOCHEAT, "client password used when connecting" );

// RAVEN BEGIN
idCVar g_gameReviewPause(			"g_gameReviewPause",		"30",			CVAR_GAME | CVAR_NETWORKSYNC | CVAR_INTEGER | PC_CVAR_ARCHIVE, "scores review time in seconds (at end game)", 2, 3600 );
// RAVEN END
idCVar net_clientPredictGUI(		"net_clientPredictGUI",		"1",			CVAR_GAME | CVAR_BOOL, "test guis in networking without prediction" );

idCVar si_voteFlags(				"si_voteFlags",				"0",			CVAR_GAME | CVAR_SERVERINFO | CVAR_INTEGER | PC_CVAR_ARCHIVE, "vote flags. bit mask of votes not allowed on this server\n"
																					"bit  0 (+1)    restart now\n"
																					"bit  1 (+2)    min players\n"
																					"bit  2 (+4)    auto balance teams\n"
																					"bit  3 (+8)    shuffle teams\n"
																					"bit  4 (+16)   kick player\n"
																					"bit  5 (+32)   change map\n"
																					"bit  6 (+64)   change gametype\n"
																					"bit  7 (+128)  time limit\n"
																	"bit  8 (+256)  tourney limit\n"
																	"bit  9 (+512)  capture limit\n"
																	"bit 10 (+1024) frag limit\n"
																	"bit 11 (+2048) control time" );

idCVar g_mapCycle(					"g_mapCycle",				"mapcycle",		CVAR_GAME | CVAR_ARCHIVE, "map cycling script for multiplayer games - see mapcycle.scriptcfg" );

// RAVEN BEGIN
// bdube: client entitiy cvars
idCVar g_gamelog(						"g_gamelog",					"0",			CVAR_GAME | CVAR_BOOL, "enables game logging" );
idCVar cl_showEntityInfo(			"cl_showEntityInfo",		"0",			CVAR_GAME | CVAR_BOOL, "" );
// ddynerman: announcer delay time
idCVar g_announcerDelay( "g_announcerDelay", "1000", CVAR_SOUND | PC_CVAR_ARCHIVE, "no more than one announcer sound will be played in this many ms" );
// jnewquist: Option to force undying state
idCVar g_forceUndying(				"g_forceUndying",			"0",			CVAR_GAME | CVAR_BOOL, "forces undying state" );
// mcg: combat performance testing cvars
idCVar g_perfTest_weaponNoFX(				"g_perfTest_weaponNoFX",			"0",			CVAR_GAME | CVAR_BOOL, "no muzzle flash, brass eject, muzzle fx, tracers, impact fx, blood decals or blood splats (whew!)" );
idCVar g_perfTest_hitscanShort(				"g_perfTest_hitscanShort",			"0",			CVAR_GAME | CVAR_BOOL, "all hitscans capped at 2048" );
idCVar g_perfTest_hitscanBBox(				"g_perfTest_hitscanBBox",			"0",			CVAR_GAME | CVAR_BOOL, "all hitscans vs bbox, not rendermodel" );
idCVar g_perfTest_aiStationary(				"g_perfTest_aiStationary",			"0",			CVAR_GAME | CVAR_BOOL, "ai attempts no combat movement" );
idCVar g_perfTest_aiNoDodge(				"g_perfTest_aiNoDodge",				"0",			CVAR_GAME | CVAR_BOOL, "ai attempts no dodging" );
idCVar g_perfTest_aiNoRagdoll(				"g_perfTest_aiNoRagdoll",			"0",			CVAR_GAME | CVAR_BOOL, "ai does not ragdoll" );
idCVar g_perfTest_aiNoObstacleAvoid(		"g_perfTest_aiNoObstacleAvoid",		"0",			CVAR_GAME | CVAR_BOOL, "ai does not attempt obstacle avoidance" );
idCVar g_perfTest_aiUndying(				"g_perfTest_aiUndying",				"0",			CVAR_GAME | CVAR_BOOL, "makes all AI undying" );
idCVar g_perfTest_aiNoVisTrace(				"g_perfTest_aiNoVisTrace",			"0",			CVAR_GAME | CVAR_BOOL, "ai does no vis traces" );
idCVar g_perfTest_noJointTransform(			"g_perfTest_noJointTransform",		"0",			CVAR_GAME | CVAR_BOOL, "all joint transforms return origin" );
idCVar g_perfTest_noPlayerFocus(			"g_perfTest_noPlayerFocus",			"0",			CVAR_GAME | CVAR_BOOL, "doesn't do player focus traces/logic" );
idCVar g_perfTest_noProjectiles(			"g_perfTest_noProjectiles",			"0",			CVAR_GAME | CVAR_BOOL, "all projectiles are removed instantly" );

idCVar net_serverDownload(					"net_serverDownload",				"0",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE, "enable server download redirects. 0: off 1: client exits and opens si_serverURL in web browser 2: client downloads pak files from an URL and connects again 3: client downloads pak files from built-in http server and connects again. See net_serverDl* cvars for configuration" );
idCVar net_serverDlBaseURL(					"net_serverDlBaseURL",				"",				CVAR_GAME | CVAR_ARCHIVE, "base URL for the download redirection.  also overrides the URL when net_serverDownload is set to 3 (built-in HTTP server)." );
idCVar net_serverDlTable(					"net_serverDlTable",				"",				CVAR_GAME | CVAR_ARCHIVE, "pak names for which download is provided, seperated by ; - use a * to mark all paks" );

// Stock mainmenu.gui binds these directly while configuring a server. Mirror
// the game-sp declarations so a direct game-mp launch never creates typeless
// placeholder CVars from GUI state.
idCVar net_menulanserver(					"net_menulanserver",				"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "treat menu server list as LAN only" );
idCVar net_serverMenuDedicated(				"net_serverMenuDedicated",			"0",			CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL, "menu dedicated server toggle" );

idCVar si_serverURL(						"si_serverURL",						"",				CVAR_GAME | CVAR_SERVERINFO | CVAR_ARCHIVE, "server information page" );

idCVar g_repeaterReliableOnly(				"g_repeaterReliableOnly",			"1",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE, "send unreliable messages reliably. 1: to repeaters 2: to repeaters and viewers" );

idCVar net_warnStale(						"net_warnStale",					"1",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "Warn stale entity occurences on network client - == 1: only on ClientStale call, > 1 all times" );

idCVar pm_slidevelocity(					"pm_slidevelocity",					"1",			CVAR_GAME | CVAR_BOOL | CVAR_NETWORKSYNC, "what to do with velocity when hitting a surface at an angle. 0: use horizontal speed, 1: keep some of the impact speed to push along the slide" );
idCVar pm_powerslide(						"pm_powerslide",					"0.09",			CVAR_GAME | CVAR_FLOAT | CVAR_NETWORKSYNC, "adjust the push when pm_slidevelocity == 1, set power < 1 -> more speed, > 1 -> closer to pm_slidevelocity 0", 0, 4 );

idCVar g_playerLean(						"g_playerLean",						"1",			CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "scale down or disable client-side player lean" );

idCVar net_clientPredictWeaponSwitch(		"net_clientPredictWeaponSwitch",	"1",			CVAR_GAME | CVAR_BOOL | CVAR_ARCHIVE, "predict weapon switches locally (most noticeable on high ping servers)" );

// openQ4: server side hitscan lag compensation.  Same names, flags and ranges as the
// single player tree it was ported from - but NOT the same default: the SP copy is
// dead code (game-sp never runs multiplayer) and still declares "1".
// This one ships OFF.  The machinery is complete and guarded.  A client in mode 1
// extrapolates remote players forward from the relayed user commands rather than
// interpolating between snapshots; it advertises that (net_mpRemoteExtrapolation)
// and the rewind takes it off, so the two corrections no longer double-count.
// Turning it on still changes hit registration for everybody on the server, so it
// stays an opt-in until it has been measured against real ping distributions.
// openQ4: a remote player's Move() is gated on gameLocal.isNewFrame below, so a client
// integrates a remote exactly once per client frame while every snapshot hard-resets that
// remote's physics.  Its simulated state therefore sits permanently about the client's own
// lead behind the server - a positional error of order ping, at the crosshair.  Mode 1
// re-simulates remote players on the engine's replay passes as well, using the usercmds the
// server already relays.  NOT CVAR_NETWORKSYNC: it changes only what a client simulates
// locally, and a server must not be able to force per-frame cost onto a client.
// (src/game/gamesys/SysCvar.cpp carries a copy of this cvar; the single-player fork never
// runs multiplayer, so THIS is the live declaration.)
idCVar net_mpPredictMode(					"net_mpPredictMode",				"0",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "multiplayer remote-client prediction mode (0=legacy limited, 1=enhanced per-frame)", 0, 1, idCmdSystem::ArgCompletion_Integer<0,1> );
// Ceiling on the extra replay tics one remote may be simulated for in a single chain.
// net_clientMaxPrediction lets the engine hand over as many as 62 after a stall, for every
// visible player at once.
idCVar net_mpPredictMaxFrames(				"net_mpPredictMaxFrames",		"16",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "maximum extra replay tics a remote player may be re-simulated for per chain", 1, 64 );
idCVar net_mpPredictDebug(					"net_mpPredictDebug",			"0",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "report how far a client's predicted remote-player position was from the server's for the same frame", 0, 1 );
// What a client tells the server it extrapolates remote players by, in tics: 0 in legacy mode,
// net_mpPredictMaxFrames in mode 1.  Lag compensation takes it off that client's rewind
// (idGameLocal::RemoteExtrapolationMS), so the two corrections no longer stack.  Read-only and
// re-derived from the two cvars above every client frame, so a server that echoes a different
// value back in the user info changes nothing about what this client simulates.
idCVar net_mpRemoteExtrapolation(			"net_mpRemoteExtrapolation",		"0",			CVAR_GAME | CVAR_INTEGER | CVAR_USERINFO | CVAR_ROM, "remote-player extrapolation this client advertises to the server, in tics (derived from net_mpPredictMode and net_mpPredictMaxFrames)" );
idCVar net_mpLagCompensation(				"net_mpLagCompensation",			"0",			CVAR_GAME | CVAR_BOOL | CVAR_NOCHEAT, "enable server-side multiplayer lag compensation for hitscan traces" );
idCVar net_mpLagCompMaxMS(					"net_mpLagCompMaxMS",				"200",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "maximum rewind window in milliseconds for multiplayer lag compensation", 0, 1000 );
idCVar net_mpLagCompBiasMS(					"net_mpLagCompBiasMS",				"0",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "additional rewind bias in milliseconds applied to multiplayer lag compensation", -200, 200 );
// How much further back than the measured round trip a rewind may reach.  The shooter's own
// claim is what the rewind follows; this is only the ceiling that keeps a client which
// misreports, or which delays its ping replies, from choosing its own rewind.
idCVar net_mpLagCompSlackMS(				"net_mpLagCompSlackMS",				"48",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "how far past the measured round trip a lag-compensation rewind may reach", 0, 200 );
idCVar net_mpLagCompDebug(					"net_mpLagCompDebug",				"0",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "multiplayer lag compensation debug output (0=off, 1=summary, 2=verbose)", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );

// Multiplayer bots.  Navigation is generated at map load from the collision
// world, so none of this needs per-map authoring.
idCVar bot_enable(							"bot_enable",						"1",			CVAR_GAME | CVAR_BOOL | CVAR_ARCHIVE, "allow bots to be added to multiplayer matches" );
idCVar bot_minPlayers(						"bot_minPlayers",					"0",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE, "keep the match topped up to this many players with bots, 0 disables" );
idCVar bot_skill(							"bot_skill",						"3",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE, "bot difficulty, 1 (easiest) to 5 (hardest)", 1, 5 );
idCVar bot_debug(							"bot_debug",						"0",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "log bot decisions: 1 navigation events, 2 adds periodic bot status, 3 adds edge and traversal traces, 4 adds live jump predictions" );
idCVar bot_debugNav(						"bot_debugNav",						"0",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "draw the navmesh (1) and bot routes (2) near the local player" );
idCVar bot_navCellSize(						"bot_navCellSize",					"24",			CVAR_GAME | CVAR_FLOAT | CVAR_NOCHEAT, "navmesh sampling resolution in world units; smaller finds more ground and costs more to build", 8, 64 );
idCVar bot_pause(							"bot_pause",						"0",			CVAR_GAME | CVAR_BOOL | CVAR_NOCHEAT, "freeze all bot input" );

// Bot personalities.  Skill is the baseline curve; a character file layers a
// play style and a voice on top of it, so two bots at the same skill still play
// and talk differently.
idCVar bot_characters(						"bot_characters",					"1",			CVAR_GAME | CVAR_BOOL | CVAR_ARCHIVE, "give bots named characters with their own play style and voice; 0 leaves them on the plain skill curve" );
idCVar bot_forceCharacter(					"bot_forceCharacter",				"",				CVAR_GAME, "put every bot on this one character, by name; for tuning a single personality" );
idCVar bot_skillVariance(					"bot_skillVariance",				"0",			CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "spread bot skill this many levels either side of bot_skill, so a match is not all one difficulty", 0, 2 );
idCVar bot_chat(							"bot_chat",							"1",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE, "bot chat: 0 silent, 1 normal, 2 chatty", 0, 2 );
idCVar bot_chatDelay(						"bot_chatDelay",					"600",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE, "initial thinking pause before a bot starts typing, in milliseconds" );
idCVar bot_chatCPM(							"bot_chatCPM",						"900",			CVAR_GAME | CVAR_INTEGER | CVAR_ARCHIVE, "base bot typing speed in visible characters per minute", 60, 6000 );
idCVar bot_debugAim(						"bot_debugAim",						"0",			CVAR_GAME | CVAR_INTEGER, "log the aim and fire decisions behind every shot, for tuning skill levels" );
idCVar bot_profile(						"bot_profile",						"0",			CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "print bot CPU and navigation query statistics every N seconds, 0 disables", 0, 600 );

