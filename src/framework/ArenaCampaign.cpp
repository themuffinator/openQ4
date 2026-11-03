/*
===========================================================================

openQ4 arena campaign

The campaign is presented as single-player, but every arena is an offline
listen-server match hosted by game_mp.  Keeping orchestration in the framework
lets progression survive game-module swaps while game_mp remains authoritative
for the final score.

===========================================================================
*/

#include "Session_local.h"
#include "ArenaCampaign.h"
#include "../ui/UserInterfaceManaged.h"
#include "async/AsyncNetwork.h"
#include "../ui/RetainedUI.h"

namespace {

static const char *ARENA_CAMPAIGN_FILE = "arena/openq4_campaign.cfg";
static const int ARENA_TIER_COUNT = 5;
static const int ARENA_MATCHES_PER_TIER = 4;
static const int ARENA_TOTAL_MATCHES = ARENA_TIER_COUNT * ARENA_MATCHES_PER_TIER;
static const int ARENA_BOSS_MATCH = ARENA_MATCHES_PER_TIER - 1;
static const int ARENA_PROGRESS_MASK = ( 1 << ARENA_TOTAL_MATCHES ) - 1;
static const int ARENA_SCORE_UNAVAILABLE = -9999;
static const int ARENA_ENTRANCE_MSEC = 1250;

// The real seat ceiling is game_mp's si_maxPlayers range (1..16), not the
// engine's MAX_ASYNC_CLIENTS of 32.  Validating against 32 let a match author
// more bots than the server can seat: PrepareServer's si_maxPlayers write was
// silently clamped, PopulateBots then ran out of slots part way through the
// roster, and the launch aborted with a bot-population failure instead of the
// campaign load rejecting the match up front.
static const int ARENA_MAX_SEATS = 16;

// End-game awards travel from game_mp as a bit mask, one bit per endGameAward_t
// id, so the whole set fits a single bounded integer argument. Bit 0 is
// EGA_INVALID and is never set. The names are stock Quake 4 strings, contiguous
// from EGA_LEMMING, so the framework can render them without a second channel.
static const int ARENA_AWARD_COUNT = 11;
static const int ARENA_AWARD_MASK = ( 1 << ARENA_AWARD_COUNT ) - 1;
static const int ARENA_AWARD_FIRST_STRING = 107259;

static const char *ArenaAwardStringKey( int award ) {
	if ( award < 1 || award >= ARENA_AWARD_COUNT ) {
		return "";
	}
	return va( "#str_%d", ARENA_AWARD_FIRST_STRING + award );
}

// GUI contract. Ordinary multiplayer never observes these states because the
// framework only advances them for a validated si_arenaCampaign transaction.
enum arenaTransitionPhase_t {
	ARENA_TRANSITION_IDLE = 0,
	ARENA_TRANSITION_ENTRANCE,
	ARENA_TRANSITION_MATCH,
	ARENA_TRANSITION_RETURN,
	ARENA_TRANSITION_RESULT
};

enum arenaOutcome_t {
	ARENA_OUTCOME_LOSS = 0,
	ARENA_OUTCOME_WIN = 1,
	ARENA_OUTCOME_DRAW = 2,
	// game_mp ended the match without a contest: every opponent left, or a
	// forfeit or abort ended it. Nothing is recorded.
	ARENA_OUTCOME_ABORTED = 3
};

// Why an Arena launch or result handoff was abandoned.  Every abandoned
// transaction reports one of these on the shared "MATCH ABORTED" card: bouncing
// the player back to the browser with no explanation is indistinguishable from
// the menu ignoring the button, and the only other diagnostic is a console
// warning behind a fullscreen menu.
enum arenaFailure_t {
	ARENA_FAILURE_NONE = 0,
	ARENA_FAILURE_SERVER,
	ARENA_FAILURE_BOTS,
	ARENA_FAILURE_RESULT,
	// The match itself ended without a result. Both keep the match's token so
	// the card can name it and offer it again.
	ARENA_FAILURE_OPPONENTS,
	ARENA_FAILURE_ABANDONED
};

static const char *ArenaFailureMessageKey( int failure ) {
	switch ( failure ) {
		case ARENA_FAILURE_SERVER:		return "#str_42150";
		case ARENA_FAILURE_BOTS:		return "#str_42069";
		case ARENA_FAILURE_RESULT:		return "#str_42151";
		case ARENA_FAILURE_OPPONENTS:	return "#str_42161";
		case ARENA_FAILURE_ABANDONED:	return "#str_42162";
		default:						return "";
	}
}

// The ledger's title for a result that recorded nothing: a launch or handoff
// that failed is aborted; a match the player walked out of is abandoned.
static const char *ArenaFailureTitleKey( int failure ) {
	return failure == ARENA_FAILURE_ABANDONED ? "#str_42163" : "#str_42068";
}

static idCVar ui_arenaProgressVersion(
	"ui_arenaProgressVersion", "1",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"version of the persisted arena campaign progression data",
	0, 999 );

static idCVar ui_arenaProgress(
	"ui_arenaProgress", "0",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"arena campaign completion bit mask",
	0, ARENA_PROGRESS_MASK );

// Each played match's best result, beside the win bits it must agree with:
// space-separated "<match>:<outcome>:<player>:<opponent>:<difficulty>"
// entries, match a zero-based index and outcome 0 loss, 1 win, 2 draw.
static idCVar ui_arenaBest(
	"ui_arenaBest", "",
	CVAR_SYSTEM | CVAR_ARCHIVE,
	"arena campaign best result per match" );

static idCVar ui_arenaDifficulty(
	"ui_arenaDifficulty", "3",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"arena campaign bot difficulty",
	1, 5 );

static idCVar ui_arenaSelectedTier(
	"ui_arenaSelectedTier", "0",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"last selected arena campaign tier",
	0, ARENA_TIER_COUNT - 1 );

static idCVar ui_arenaSelectedMatch(
	"ui_arenaSelectedMatch", "0",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"last selected arena campaign match",
	0, ARENA_MATCHES_PER_TIER - 1 );

static idCVar ui_arenaCampaignActive(
	"ui_arenaCampaignActive", "0",
	CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT,
	"true while a local arena campaign match is active" );

static idCVar ui_arenaActiveMatch(
	"ui_arenaActiveMatch", "0",
	CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT,
	"one-based token for the active arena campaign match",
	0, ARENA_TOTAL_MATCHES );

// The active match's card, as localization keys, for game_mp's presentation:
// its entrance, scoreboard and loading card. game_mp cannot derive them - a
// campaign Duel runs as deathmatch, and the copy is authored here. Always the
// same process, because an Arena match is this engine's own listen server.
static idCVar ui_arenaMatchTitle(
	"ui_arenaMatchTitle", "", CVAR_SYSTEM | CVAR_NOCHEAT,
	"localization key of the active arena campaign match's title" );
static idCVar ui_arenaMatchTier(
	"ui_arenaMatchTier", "", CVAR_SYSTEM | CVAR_NOCHEAT,
	"localization key of the active arena campaign match's tier" );
static idCVar ui_arenaMatchRole(
	"ui_arenaMatchRole", "", CVAR_SYSTEM | CVAR_NOCHEAT,
	"localization key of the active arena campaign match's place in its tier" );
static idCVar ui_arenaMatchGameType(
	"ui_arenaMatchGameType", "", CVAR_SYSTEM | CVAR_NOCHEAT,
	"localization key of the game type the active arena campaign match presents" );

// True while a menu has the screen over the live match. An Arena match is a
// single-player game, so game_mp pauses it while this is set, the way a story
// level pauses behind its menu. game_mp cannot see the engine's menus itself:
// its own menu hands over to the engine's main menu and settings screens.
// Chat is not a menu here - the bots answer it.
static idCVar ui_arenaMenuOpen(
	"ui_arenaMenuOpen", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT | CVAR_ROM,
	"true while a menu covers the live arena campaign match, which pauses it" );

// Restart match's authorization for game_mp: the live match's token for the
// length of one serverMapRestart, which game_mp clears once the match has
// restarted in place. Read-only, so the console cannot authorize a restart.
static idCVar ui_arenaRestartMatch(
	"ui_arenaRestartMatch", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT | CVAR_ROM,
	"arena campaign match token authorized to restart in place" );

// game_mp's decided, contested result while its ceremony holds the handoff
// back: the arenaComplete arguments ("token outcome player opponent awards"),
// or empty. Kept if the match is left or the game quits before the handoff.
static idCVar ui_arenaDecidedResult(
	"ui_arenaDecidedResult", "", CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ROM,
	"the live arena campaign match's decided result while its ceremony plays" );

// Match-scoped values published to and by game_mp, cleared with the match.
static void ArenaClearMatchPresentation() {
	ui_arenaMatchTitle.SetString( "" );
	ui_arenaMatchTier.SetString( "" );
	ui_arenaMatchRole.SetString( "" );
	ui_arenaMatchGameType.SetString( "" );
	ui_arenaMenuOpen.SetBool( false );
	ui_arenaDecidedResult.SetString( "" );
}

#ifndef ID_DEDICATED
// The main menu at rest on its home state, with no page or transition under
// way. Over a live Arena match it only gets there by backing out of the
// settings page the pause menu opened.
static bool ArenaMainMenuAtHome() {
	idUserInterface *menu = sessLocal.guiMainMenu;
	if ( menu == NULL || sessLocal.guiActive != menu ) {
		return false;
	}
	idStr curr, active;
	return menu->GetPresentationValue( "desktop::curr", curr ) && atoi( curr.c_str() ) == 0 &&
		menu->GetPresentationValue( "desktop::active", active ) && atoi( active.c_str() ) == 0;
}

static bool ArenaMenuCoversMatch() {
	if ( RetainedUI_IsOpen() ) {
		return true;
	}
	const idUserInterface *gui = sessLocal.GetActiveGUI();
	return gui != NULL && idStr::Icmp( gui->Name(), "guis/mpmsgmode.gui" ) != 0;
}
#endif

struct arenaBot_t {
	idStr	name;
	int		skillOffset;

	arenaBot_t() : skillOffset( 0 ) {
	}
};

struct arenaMatch_t {
	idStr				name;
	idStr				description;
	idStr				map;
	idStr				gameType;
	int					baseSkill;
	int					fragLimit;
	int					timeLimit;
	int					roundLimit;
	int					scoreLimit;
	idList<arenaBot_t>	bots;

	arenaMatch_t() :
		baseSkill( 1 ),
		fragLimit( 10 ),
		timeLimit( 8 ),
		roundLimit( 5 ),
		scoreLimit( 5 ) {
	}
};

struct arenaTier_t {
	idStr					name;
	idStr					description;
	idList<arenaMatch_t>	matches;
};

static const char *ARENA_SAVED_CVARS[] = {
	"net_serverDedicated",
	"net_LANServer",
	"net_serverReloadEngine",
	"si_map",
	"si_mapCycle",
	"si_gameType",
	"si_pure",
	"si_maxPlayers",
	"si_minPlayers",
	"si_privatePlayers",
	"si_fragLimit",
	"si_timeLimit",
	"si_captureLimit",
	"si_scoreLimit",
	"si_roundLimit",
	"si_roundTimeLimit",
	"si_roundWarmupDelay",
	"si_roundEndDelay",
	"g_gameReviewPause",
	"g_matchProfile",
	"si_tourneyLimit",
	"si_useReady",
	"si_warmupReadyPercentage",
	"si_allowVoting",
	"si_isBuyingEnabled",
	"si_dropWeaponsInBuyingModes",
	"si_usePass",
	"si_teamDamage",
	"si_weaponStay",
	"si_mercyLimit",
	"si_overtime",
	"si_suddenDeathRestart",
	"si_suddenDeathRespawnDelay",
	"si_suddenDeathRespawnIncrease",
	"si_suddenDeathRespawnMax",
	"si_forfeit",
	"si_warmup",
	"si_warmupWeapons",
	"si_warmupScoring",
	"si_countDown",
	"si_teamSizeMin",
	"si_teamForcePresent",
	"si_autoShuffle",
	"si_shuffle",
	"si_autobalance",
	"si_spectators",
	"si_fps",
	"ui_autoJoin",
	"ui_joined",
	"ui_ready",
	"ui_spectate",
	"ui_team",
	"bot_enable",
	"bot_minPlayers",
	"bot_characters",
	"bot_skillVariance",
	NULL
};

static const char *ArenaLocalized( const char *text ) {
	if ( text == NULL ) {
		return "";
	}
	return common->GetLanguageDict()->GetString( text );
}

static bool ArenaStringIsSafeToken( const idStr &text, bool allowSlash, bool allowSpaces ) {
	if ( text.IsEmpty() || text.Length() > 96 ) {
		return false;
	}

	for ( int i = 0; i < text.Length(); i++ ) {
		const unsigned char ch = static_cast<unsigned char>( text[i] );
		if ( idStr::CharIsAlpha( ch ) || idStr::CharIsNumeric( ch ) ||
			 ch == '_' || ch == '-' || ( allowSpaces && ch == ' ' ) ||
			 ( allowSlash && ch == '/' ) ) {
			continue;
		}
		return false;
	}

	return text.Find( ".." ) < 0;
}

static bool ArenaStringIsLocalizationKey( const idStr &text ) {
	static const char *prefix = "#str_";
	const int prefixLength = idStr::Length( prefix );
	if ( text.Length() <= prefixLength || text.Length() > 32 ||
		 text.Cmpn( prefix, prefixLength ) != 0 ) {
		return false;
	}

	for ( int i = prefixLength; i < text.Length(); i++ ) {
		if ( !idStr::CharIsNumeric( static_cast<unsigned char>( text[i] ) ) ) {
			return false;
		}
	}
	return true;
}

static bool ArenaGameTypeIsSupported( const idStr &gameType ) {
	return gameType.Icmp( "DM" ) == 0 ||
		gameType.Icmp( "Duel" ) == 0 ||
		gameType.Icmp( "Team DM" ) == 0 ||
		gameType.Icmp( "Clan Arena" ) == 0 ||
		gameType.Icmp( "Red Rover" ) == 0;
}

static bool ArenaGameTypeIsTeam( const idStr &gameType ) {
	return gameType.Icmp( "Team DM" ) == 0 ||
		gameType.Icmp( "Clan Arena" ) == 0 ||
		gameType.Icmp( "Red Rover" ) == 0;
}

// The rules the server actually runs, which are not always the rules the
// campaign advertises. A Duel in the arena is simply a one-on-one deathmatch:
// game_mp's Duel is a queue-based tournament mode that seats two contenders and
// holds everyone else in spectator, and with an authored one-bot roster there is
// nothing for that machinery to arbitrate - it can only ever take the player out
// of their own match. The browser still presents these as Duels.
static const char *ArenaEffectiveGameType( const idStr &gameType ) {
	if ( gameType.Icmp( "Duel" ) == 0 ) {
		return "DM";
	}
	return gameType.c_str();
}

static const char *ArenaGameTypeMapKey( const idStr &gameType ) {
	if ( gameType.Icmp( "Duel" ) == 0 ) {
		return "DM";
	}
	if ( gameType.Icmp( "Clan Arena" ) == 0 || gameType.Icmp( "Red Rover" ) == 0 ) {
		return "Team DM";
	}
	return gameType.c_str();
}

static const char *ArenaGameTypeStringKey( const idStr &gameType ) {
	if ( gameType.Icmp( "DM" ) == 0 ) {
		return "#str_42200";
	}
	if ( gameType.Icmp( "Duel" ) == 0 ) {
		return "#str_42201";
	}
	if ( gameType.Icmp( "Team DM" ) == 0 ) {
		return "#str_42202";
	}
	if ( gameType.Icmp( "Red Rover" ) == 0 ) {
		return "#str_42203";
	}
	if ( gameType.Icmp( "Clan Arena" ) == 0 ) {
		return "#str_42204";
	}
	return "";
}

static const char *ArenaDifficultyStringKey( int difficulty ) {
	static const char *keys[] = {
		"#str_42060", "#str_42061", "#str_42062", "#str_42063", "#str_42064"
	};
	return keys[idMath::ClampInt( 1, 5, difficulty ) - 1];
}

static int ArenaMatchIndex( int tier, int match ) {
	return tier * ARENA_MATCHES_PER_TIER + match;
}

static int ArenaMatchBit( int tier, int match ) {
	return 1 << ArenaMatchIndex( tier, match );
}

static bool ArenaMatchComplete( int progress, int tier, int match ) {
	return ( progress & ArenaMatchBit( tier, match ) ) != 0;
}

// Persisted progress is a dependency graph, not an arbitrary set of bits.
// Qualifiers in later tiers require the preceding boss, and a boss bit is
// meaningful only after all three of its qualifiers. Repairing the mask keeps
// corrupted or manually edited configs from skipping campaign progression.
static int ArenaCanonicalProgress( int progress ) {
	progress &= ARENA_PROGRESS_MASK;
	int canonical = 0;

	for ( int tier = 0; tier < ARENA_TIER_COUNT; tier++ ) {
		if ( tier > 0 && !ArenaMatchComplete( canonical, tier - 1, ARENA_BOSS_MATCH ) ) {
			break;
		}

		bool qualifiersComplete = true;
		for ( int match = 0; match < ARENA_BOSS_MATCH; match++ ) {
			if ( ArenaMatchComplete( progress, tier, match ) ) {
				canonical |= ArenaMatchBit( tier, match );
			} else {
				qualifiersComplete = false;
			}
		}

		if ( qualifiersComplete && ArenaMatchComplete( progress, tier, ARENA_BOSS_MATCH ) ) {
			canonical |= ArenaMatchBit( tier, ARENA_BOSS_MATCH );
		}
	}

	return canonical;
}

static void ArenaRepairPersistedBest();

// The best results are checked against the repaired progress, so they are
// always repaired with it.
static void ArenaRepairPersistedProgress() {
	const int persisted = ui_arenaProgress.GetInteger();
	const int canonical = ArenaCanonicalProgress( persisted );
	if ( canonical != persisted ) {
		common->Warning(
			"arena campaign: repaired non-canonical progress mask 0x%x to 0x%x",
			persisted, canonical );
		ui_arenaProgress.SetInteger( canonical );
	}
	ArenaRepairPersistedBest();
}

static bool ArenaParseBoundedInteger( const char *text, int minimum, int maximum, int &value ) {
	if ( text == NULL || text[0] == '\0' || minimum > maximum ) {
		return false;
	}

	int index = 0;
	bool negative = false;
	if ( text[index] == '-' ) {
		if ( minimum >= 0 || text[++index] == '\0' ) {
			return false;
		}
		negative = true;
	}

	const int magnitudeLimit = negative ? -minimum : maximum;
	int magnitude = 0;
	for ( ; text[index] != '\0'; index++ ) {
		const unsigned char ch = static_cast<unsigned char>( text[index] );
		if ( !idStr::CharIsNumeric( ch ) ) {
			return false;
		}
		const int digit = ch - '0';
		if ( magnitude > ( magnitudeLimit - digit ) / 10 ) {
			return false;
		}
		magnitude = magnitude * 10 + digit;
	}

	value = negative ? -magnitude : magnitude;
	return value >= minimum && value <= maximum;
}

static bool ArenaTierUnlocked( int progress, int tier ) {
	return tier == 0 || ArenaMatchComplete( progress, tier - 1, ARENA_BOSS_MATCH );
}

static bool ArenaBossUnlocked( int progress, int tier ) {
	if ( !ArenaTierUnlocked( progress, tier ) ) {
		return false;
	}
	for ( int match = 0; match < ARENA_BOSS_MATCH; match++ ) {
		if ( !ArenaMatchComplete( progress, tier, match ) ) {
			return false;
		}
	}
	return true;
}

static bool ArenaMatchUnlocked( int progress, int tier, int match ) {
	if ( !ArenaTierUnlocked( progress, tier ) ) {
		return false;
	}
	return match < ARENA_BOSS_MATCH || ArenaBossUnlocked( progress, tier );
}

static int ArenaCountCompleted( int progress ) {
	int completed = 0;
	for ( int i = 0; i < ARENA_TOTAL_MATCHES; i++ ) {
		if ( progress & ( 1 << i ) ) {
			completed++;
		}
	}
	return completed;
}

// A match's best result. game_mp derives the outcome from the two scores, so a
// result with both scores must agree with them; a loss handed over without
// scores (the player never took a side) records the outcome alone.
struct arenaBestResult_t {
	bool	recorded;
	int		outcome;
	int		playerScore;
	int		opponentScore;
	int		difficulty;

	arenaBestResult_t() :
		recorded( false ),
		outcome( ARENA_OUTCOME_LOSS ),
		playerScore( ARENA_SCORE_UNAVAILABLE ),
		opponentScore( ARENA_SCORE_UNAVAILABLE ),
		difficulty( 0 ) {
	}
};

static bool ArenaBestHasScore( const arenaBestResult_t &result ) {
	return result.playerScore != ARENA_SCORE_UNAVAILABLE &&
		result.opponentScore != ARENA_SCORE_UNAVAILABLE;
}

// A recorded result the campaign could have produced: an outcome game_mp
// records, both scores or neither, and an outcome that agrees with its scores.
static bool ArenaBestResultValid( const arenaBestResult_t &result ) {
	if ( result.outcome != ARENA_OUTCOME_LOSS && result.outcome != ARENA_OUTCOME_WIN &&
		 result.outcome != ARENA_OUTCOME_DRAW ) {
		return false;
	}
	if ( result.difficulty < 1 || result.difficulty > 5 ) {
		return false;
	}
	if ( ( result.playerScore == ARENA_SCORE_UNAVAILABLE ) != ( result.opponentScore == ARENA_SCORE_UNAVAILABLE ) ) {
		return false;
	}
	if ( !ArenaBestHasScore( result ) ) {
		return true;
	}
	const int margin = result.playerScore - result.opponentScore;
	return result.outcome == ARENA_OUTCOME_WIN ? margin > 0 :
		( result.outcome == ARENA_OUTCOME_DRAW ? margin == 0 : margin < 0 );
}

// Win, then draw, then loss; then the wider margin; then the harder setting;
// then the higher score. A tie keeps the record that was set first.
static bool ArenaBestResultBeats( const arenaBestResult_t &candidate, const arenaBestResult_t &record ) {
	if ( !record.recorded ) {
		return candidate.recorded;
	}
	static const int outcomeRank[] = { 0, 2, 1 };	// loss, win, draw
	const int candidateRank = outcomeRank[candidate.outcome];
	const int recordRank = outcomeRank[record.outcome];
	if ( candidateRank != recordRank ) {
		return candidateRank > recordRank;
	}
	const bool candidateScored = ArenaBestHasScore( candidate );
	if ( candidateScored != ArenaBestHasScore( record ) ) {
		return candidateScored;
	}
	if ( candidateScored ) {
		const int candidateMargin = candidate.playerScore - candidate.opponentScore;
		const int recordMargin = record.playerScore - record.opponentScore;
		if ( candidateMargin != recordMargin ) {
			return candidateMargin > recordMargin;
		}
	}
	if ( candidate.difficulty != record.difficulty ) {
		return candidate.difficulty > record.difficulty;
	}
	return candidateScored && candidate.playerScore > record.playerScore;
}

// The record a match carries: its persisted best result, or for a match won
// before best results were kept, a scoreless win that any new win beats.
static arenaBestResult_t ArenaEffectiveBest( const arenaBestResult_t &persisted, int progress, int tier, int match ) {
	if ( persisted.recorded || !ArenaMatchComplete( progress, tier, match ) ) {
		return persisted;
	}
	arenaBestResult_t legacyWin;
	legacyWin.recorded = true;
	legacyWin.outcome = ARENA_OUTCOME_WIN;
	return legacyWin;
}

// Parses ui_arenaBest against a canonical progress mask, keeping only entries
// that are well formed, in range, unique and consistent with it: the match must
// be unlocked, and it holds a win exactly when it is complete. Returns the
// number of entries dropped.
static int ArenaReadBestResults( const char *text, int progress, arenaBestResult_t results[ARENA_TOTAL_MATCHES] ) {
	for ( int i = 0; i < ARENA_TOTAL_MATCHES; i++ ) {
		results[i] = arenaBestResult_t();
	}

	int dropped = 0;
	const idStr persisted = text != NULL ? text : "";
	int cursor = 0;
	while ( cursor < persisted.Length() ) {
		if ( persisted[cursor] == ' ' ) {
			cursor++;
			continue;
		}
		int end = cursor;
		while ( end < persisted.Length() && persisted[end] != ' ' ) {
			end++;
		}
		const idStr entry = persisted.Mid( cursor, end - cursor );
		cursor = end;

		idStr fields[5];
		int fieldCount = 0;
		int fieldStart = 0;
		for ( int i = 0; i <= entry.Length() && fieldCount <= 5; i++ ) {
			if ( i == entry.Length() || entry[i] == ':' ) {
				if ( fieldCount < 5 ) {
					fields[fieldCount] = entry.Mid( fieldStart, i - fieldStart );
				}
				fieldCount++;
				fieldStart = i + 1;
			}
		}

		int index = 0;
		arenaBestResult_t result;
		result.recorded = true;
		if ( fieldCount != 5 ||
			 !ArenaParseBoundedInteger( fields[0].c_str(), 0, ARENA_TOTAL_MATCHES - 1, index ) ||
			 !ArenaParseBoundedInteger( fields[1].c_str(), ARENA_OUTCOME_LOSS, ARENA_OUTCOME_DRAW, result.outcome ) ||
			 !ArenaParseBoundedInteger( fields[2].c_str(), -9999, 9999, result.playerScore ) ||
			 !ArenaParseBoundedInteger( fields[3].c_str(), -9999, 9999, result.opponentScore ) ||
			 !ArenaParseBoundedInteger( fields[4].c_str(), 1, 5, result.difficulty ) ||
			 !ArenaBestResultValid( result ) || results[index].recorded ) {
			dropped++;
			continue;
		}
		const int tier = index / ARENA_MATCHES_PER_TIER;
		const int match = index % ARENA_MATCHES_PER_TIER;
		if ( !ArenaMatchUnlocked( progress, tier, match ) ||
			 ( result.outcome == ARENA_OUTCOME_WIN ) != ArenaMatchComplete( progress, tier, match ) ) {
			dropped++;
			continue;
		}
		results[index] = result;
	}
	return dropped;
}

static idStr ArenaWriteBestResults( const arenaBestResult_t results[ARENA_TOTAL_MATCHES] ) {
	idStr text;
	for ( int i = 0; i < ARENA_TOTAL_MATCHES; i++ ) {
		if ( !results[i].recorded ) {
			continue;
		}
		if ( !text.IsEmpty() ) {
			text += " ";
		}
		text += va( "%d:%d:%d:%d:%d", i, results[i].outcome,
			results[i].playerScore, results[i].opponentScore, results[i].difficulty );
	}
	return text;
}

static bool ArenaHasBestResults() {
	return ui_arenaBest.GetString()[0] != '\0';
}

// Rewrites ui_arenaBest in canonical form when it is malformed, out of range or
// disagrees with the (already repaired) progress mask.
static void ArenaRepairPersistedBest() {
	arenaBestResult_t results[ARENA_TOTAL_MATCHES];
	const int dropped = ArenaReadBestResults( ui_arenaBest.GetString(), ui_arenaProgress.GetInteger(), results );
	const idStr canonical = ArenaWriteBestResults( results );
	if ( canonical == ui_arenaBest.GetString() ) {
		return;
	}
	common->Warning(
		"arena campaign: repaired best results '%s' to '%s' (%d entries dropped)",
		ui_arenaBest.GetString(), canonical.c_str(), dropped );
	ui_arenaBest.SetString( canonical );
}

// "VICTORY   10 - 3   VETERAN"; a match won before best results were kept has
// the outcome alone.
static idStr ArenaDescribeBest( const arenaBestResult_t &result ) {
	idStr text = ArenaLocalized( result.outcome == ARENA_OUTCOME_WIN ? "#str_42030" :
		( result.outcome == ARENA_OUTCOME_DRAW ? "#str_42077" : "#str_42031" ) );
	if ( ArenaBestHasScore( result ) ) {
		text += va( "   %d - %d", result.playerScore, result.opponentScore );
	}
	if ( result.difficulty >= 1 && result.difficulty <= 5 ) {
		text += "   ";
		text += ArenaLocalized( ArenaDifficultyStringKey( result.difficulty ) );
	}
	return text;
}

static const idDict *ArenaFindMapDecl( const idStr &map ) {
	const int mapCount = fileSystem->GetNumMaps();
	for ( int i = 0; i < mapCount; i++ ) {
		const idDict *dict = fileSystem->GetMapDecl( i );
		if ( dict != NULL && map.Icmp( dict->GetString( "path" ) ) == 0 ) {
			return dict;
		}
	}
	return NULL;
}

static bool ArenaMapSupportsMatch( const arenaMatch_t &match, const idDict **mapDecl = NULL ) {
	const idDict *dict = ArenaFindMapDecl( match.map );
	if ( mapDecl != NULL ) {
		*mapDecl = dict;
	}
	if ( dict == NULL || !dict->GetBool( ArenaGameTypeMapKey( match.gameType ) ) ) {
		return false;
	}

	idStr mapFile = "maps/";
	mapFile += match.map;
	mapFile.SetFileExtension( ".map" );
	return fileSystem->FindFile( mapFile, false ) != FIND_NO;
}

static bool ArenaReadText( idLexer &lexer, idStr &out, const char *field ) {
	idToken token;
	if ( !lexer.ReadToken( &token ) || token.type != TT_STRING ) {
		lexer.Warning( "missing quoted string value for '%s'", field );
		return false;
	}
	out = token;
	return true;
}

static bool ArenaMarkUniqueField( idLexer &lexer, int &fields, int bit, const char *field ) {
	if ( fields & bit ) {
		lexer.Warning( "duplicate '%s' field", field );
		return false;
	}
	fields |= bit;
	return true;
}

static bool ArenaParseBots( idLexer &lexer, arenaMatch_t &match ) {
	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	idToken token;
	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( token.Icmp( "bot" ) != 0 ) {
			lexer.Warning( "unknown bots field '%s'", token.c_str() );
			return false;
		}

		arenaBot_t bot;
		if ( !ArenaReadText( lexer, bot.name, "bot" ) ) {
			return false;
		}
		bot.skillOffset = lexer.ParseInt();
		match.bots.Append( bot );
	}

	lexer.Warning( "unexpected end of file in bots block" );
	return false;
}

static bool ArenaParseMatch( idLexer &lexer, arenaMatch_t &match ) {
	enum {
		MATCH_FIELD_NAME = 1 << 0,
		MATCH_FIELD_DESCRIPTION = 1 << 1,
		MATCH_FIELD_MAP = 1 << 2,
		MATCH_FIELD_GAME_TYPE = 1 << 3,
		MATCH_FIELD_BASE_SKILL = 1 << 4,
		MATCH_FIELD_FRAG_LIMIT = 1 << 5,
		MATCH_FIELD_TIME_LIMIT = 1 << 6,
		MATCH_FIELD_ROUND_LIMIT = 1 << 7,
		MATCH_FIELD_SCORE_LIMIT = 1 << 8,
		MATCH_FIELD_BOTS = 1 << 9,
		MATCH_REQUIRED_FIELDS = ( 1 << 10 ) - 1
	};

	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	int fields = 0;
	idToken token;
	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( fields != MATCH_REQUIRED_FIELDS ) {
				lexer.Warning( "match block is missing one or more required fields" );
				return false;
			}
			return !lexer.HadError();
		}

		if ( token.Icmp( "name" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_NAME, "name" ) ||
				 !ArenaReadText( lexer, match.name, "name" ) ) {
				return false;
			}
		} else if ( token.Icmp( "description" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_DESCRIPTION, "description" ) ||
				 !ArenaReadText( lexer, match.description, "description" ) ) {
				return false;
			}
		} else if ( token.Icmp( "map" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_MAP, "map" ) ||
				 !ArenaReadText( lexer, match.map, "map" ) ) {
				return false;
			}
		} else if ( token.Icmp( "gameType" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_GAME_TYPE, "gameType" ) ||
				 !ArenaReadText( lexer, match.gameType, "gameType" ) ) {
				return false;
			}
		} else if ( token.Icmp( "baseSkill" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_BASE_SKILL, "baseSkill" ) ) {
				return false;
			}
			match.baseSkill = lexer.ParseInt();
		} else if ( token.Icmp( "fragLimit" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_FRAG_LIMIT, "fragLimit" ) ) {
				return false;
			}
			match.fragLimit = lexer.ParseInt();
		} else if ( token.Icmp( "timeLimit" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_TIME_LIMIT, "timeLimit" ) ) {
				return false;
			}
			match.timeLimit = lexer.ParseInt();
		} else if ( token.Icmp( "roundLimit" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_ROUND_LIMIT, "roundLimit" ) ) {
				return false;
			}
			match.roundLimit = lexer.ParseInt();
		} else if ( token.Icmp( "scoreLimit" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_SCORE_LIMIT, "scoreLimit" ) ) {
				return false;
			}
			match.scoreLimit = lexer.ParseInt();
		} else if ( token.Icmp( "bots" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, MATCH_FIELD_BOTS, "bots" ) ||
				 !ArenaParseBots( lexer, match ) ) {
				return false;
			}
		} else {
			lexer.Warning( "unknown match field '%s'", token.c_str() );
			return false;
		}
	}

	lexer.Warning( "unexpected end of file in match block" );
	return false;
}

static bool ArenaParseTier( idLexer &lexer, arenaTier_t &tier ) {
	enum {
		TIER_FIELD_NAME = 1 << 0,
		TIER_FIELD_DESCRIPTION = 1 << 1,
		TIER_REQUIRED_FIELDS = TIER_FIELD_NAME | TIER_FIELD_DESCRIPTION
	};

	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	int fields = 0;
	idToken token;
	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( fields != TIER_REQUIRED_FIELDS ) {
				lexer.Warning( "tier block is missing a name or description" );
				return false;
			}
			return !lexer.HadError();
		}

		if ( token.Icmp( "name" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, TIER_FIELD_NAME, "name" ) ||
				 !ArenaReadText( lexer, tier.name, "name" ) ) {
				return false;
			}
		} else if ( token.Icmp( "description" ) == 0 ) {
			if ( !ArenaMarkUniqueField( lexer, fields, TIER_FIELD_DESCRIPTION, "description" ) ||
				 !ArenaReadText( lexer, tier.description, "description" ) ) {
				return false;
			}
		} else if ( token.Icmp( "match" ) == 0 ) {
			arenaMatch_t match;
			if ( !ArenaParseMatch( lexer, match ) ) {
				return false;
			}
			tier.matches.Append( match );
		} else {
			lexer.Warning( "unknown tier field '%s'", token.c_str() );
			return false;
		}
	}

	lexer.Warning( "unexpected end of file in tier block" );
	return false;
}

static void ArenaSaveCurrentCVars( arenaCampaignState_t *state );
static void ArenaRestoreSavedCVars( arenaCampaignState_t *state );

} // namespace

struct arenaCampaignState_t {
	idUserInterface *		gui;
	idUserInterface *		returnGui;
	idList<arenaTier_t>		tiers;
	idDict					savedCVars;
	idStrList				resetCVars;
	int						version;
	int						view;
	bool					loaded;
	bool					cvarsSaved;
	bool					finishCommandRegistered;
	bool					botsPopulated;
	bool					moduleTransitionPending;
	arenaTransitionPhase_t	transitionPhase;
	int					transitionDeadlineMsec;
	int					launchToken;
	bool					completionPending;
	int						pendingToken;
	int						pendingOutcome;
	int						pendingPlayerScore;
	int						pendingOpponentScore;
	int						pendingAwards;
	int						matchDifficulty;
	int						result;
	int						resultFailure;
	int						resultPlayerScore;
	int						resultOpponentScore;
	int						resultAwards;
	int						resultToken;
	bool					resultWon;
	int					resultPreviousProgress;
	int					resultTier;
	int					resultMatch;
	bool					resultNewlyCompleted;
	bool					resultUnlockedBoss;
	bool					resultUnlockedTier;
	bool					resultUnlockedCampaign;
	bool					resultRevealPending;
	bool					resultNewBest;
	bool					returnWipeHeld;
	bool					resetConfirmation;
	idStr					resultUnlock;
	idStr					resultTierName;
	idStr					resultMatchName;

	arenaCampaignState_t() :
		gui( NULL ),
		returnGui( NULL ),
		version( 0 ),
		view( 0 ),
		loaded( false ),
		cvarsSaved( false ),
		finishCommandRegistered( false ),
		botsPopulated( false ),
		moduleTransitionPending( false ),
		transitionPhase( ARENA_TRANSITION_IDLE ),
		transitionDeadlineMsec( 0 ),
		launchToken( 0 ),
		completionPending( false ),
		pendingToken( 0 ),
		pendingOutcome( ARENA_OUTCOME_LOSS ),
		pendingPlayerScore( 0 ),
		pendingOpponentScore( 0 ),
		pendingAwards( 0 ),
		matchDifficulty( 0 ),
		result( 0 ),
		resultFailure( ARENA_FAILURE_NONE ),
		resultPlayerScore( 0 ),
		resultOpponentScore( 0 ),
		resultAwards( 0 ),
		resultToken( 0 ),
		resultWon( false ),
		resultPreviousProgress( 0 ),
		resultTier( -1 ),
		resultMatch( -1 ),
		resultNewlyCompleted( false ),
		resultUnlockedBoss( false ),
		resultUnlockedTier( false ),
		resultUnlockedCampaign( false ),
		resultRevealPending( false ),
		resultNewBest( false ),
		returnWipeHeld( false ),
		resetConfirmation( false ) {
	}
};

namespace {

static void ArenaClearResultMetadata( arenaCampaignState_t *state ) {
	if ( state == NULL ) {
		return;
	}

	state->result = 0;
	state->resultFailure = ARENA_FAILURE_NONE;
	state->resultPlayerScore = 0;
	state->resultOpponentScore = 0;
	state->resultAwards = 0;
	state->resultToken = 0;
	state->resultWon = false;
	state->resultPreviousProgress = 0;
	state->resultTier = -1;
	state->resultMatch = -1;
	state->resultNewlyCompleted = false;
	state->resultUnlockedBoss = false;
	state->resultUnlockedTier = false;
	state->resultUnlockedCampaign = false;
	state->resultRevealPending = false;
	state->resultNewBest = false;
	state->resultUnlock.Clear();
	state->resultTierName.Clear();
	state->resultMatchName.Clear();
}

static bool ArenaHasProgressReveal( const arenaCampaignState_t *state ) {
	return state != NULL && ( state->resultNewlyCompleted ||
		state->resultUnlockedBoss || state->resultUnlockedTier ||
		state->resultUnlockedCampaign );
}

static void ArenaSaveCurrentCVars( arenaCampaignState_t *state ) {
	if ( state == NULL ) {
		return;
	}

	for ( int i = 0; ARENA_SAVED_CVARS[i] != NULL; i++ ) {
		const char *name = ARENA_SAVED_CVARS[i];
		if ( state->savedCVars.FindKey( name ) != NULL ) {
			continue;
		}

		bool resetAfterMatch = false;
		for ( int resetIndex = 0; resetIndex < state->resetCVars.Num(); resetIndex++ ) {
			if ( state->resetCVars[resetIndex].Icmp( name ) == 0 ) {
				resetAfterMatch = true;
				break;
			}
		}
		if ( resetAfterMatch ) {
			continue;
		}

		// game_sp and game_mp deliberately register different non-archived
		// defaults for si_pure (0 and 1 respectively).  An Arena launched from
		// the single-player front end must return this to game_mp's own default,
		// rather than preserving game_sp's unrelated value.
		if ( idStr::Icmp( name, "si_pure" ) == 0 &&
			 idStr::Icmp( cvarSystem->GetCVarString( "com_activeGameModule" ), "game_mp" ) != 0 ) {
			state->resetCVars.Append( name );
			state->cvarsSaved = true;
			continue;
		}

		idCVar *cvar = cvarSystem->Find( name );
		if ( cvar != NULL ) {
			state->savedCVars.Set( name, cvar->GetString() );
		} else {
			// Some multiplayer-only CVars do not exist until game_mp has
			// registered them. Remember that they were absent before creating
			// temporary values; after the reload, "reset" will use game_mp's
			// authoritative default instead of preserving a campaign value.
			state->resetCVars.Append( name );
		}
		state->cvarsSaved = true;
	}
}

static void ArenaRestoreSavedCVars( arenaCampaignState_t *state ) {
	if ( state == NULL || !state->cvarsSaved ) {
		return;
	}

	for ( int i = 0; i < state->savedCVars.GetNumKeyVals(); i++ ) {
		const idKeyValue *keyValue = state->savedCVars.GetKeyVal( i );
		if ( keyValue != NULL && cvarSystem->Find( keyValue->GetKey() ) != NULL ) {
			cvarSystem->SetCVarString( keyValue->GetKey(), keyValue->GetValue() );
		}
	}

	for ( int i = 0; i < state->resetCVars.Num(); i++ ) {
		const char *name = state->resetCVars[i].c_str();
		if ( cvarSystem->Find( name ) != NULL ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "reset %s\n", name ) );
		}
	}

	state->savedCVars.Clear();
	state->resetCVars.Clear();
	state->cvarsSaved = false;
}

static const arenaMatch_t *ArenaMatchForToken( const arenaCampaignState_t *state, int token, int *tierOut = NULL, int *matchOut = NULL ) {
	if ( state == NULL || !state->loaded || token < 1 || token > ARENA_TOTAL_MATCHES ) {
		return NULL;
	}

	const int index = token - 1;
	const int tier = index / ARENA_MATCHES_PER_TIER;
	const int match = index % ARENA_MATCHES_PER_TIER;
	if ( tier < 0 || tier >= state->tiers.Num() ||
		 match < 0 || match >= state->tiers[tier].matches.Num() ) {
		return NULL;
	}

	if ( tierOut != NULL ) {
		*tierOut = tier;
	}
	if ( matchOut != NULL ) {
		*matchOut = match;
	}
	return &state->tiers[tier].matches[match];
}

// Retry and Replay are offered only when that very match can start again. The
// button and RetryResult must agree: the report collapses as soon as the button
// is pressed, so a retry the campaign then refused stranded an empty frame.
static bool ArenaResultCanRetry( const arenaCampaignState_t *state ) {
	if ( state == NULL ) {
		return false;
	}
	const bool played = state->result == 1 || state->result == 2 || state->result == 4 ||
		( state->result == 3 && ( state->resultFailure == ARENA_FAILURE_OPPONENTS ||
			state->resultFailure == ARENA_FAILURE_ABANDONED ) );
	if ( !played ) {
		return false;
	}
	int tier = 0;
	int match = 0;
	const arenaMatch_t *retryMatch = ArenaMatchForToken( state, state->resultToken, &tier, &match );
	return retryMatch != NULL && ArenaMatchUnlocked( ui_arenaProgress.GetInteger(), tier, match ) &&
		ArenaMapSupportsMatch( *retryMatch );
}

// Fills the ledger's identity for a match that ended without a result, so the
// card can still name it and, where it can start again, offer it again.
static void ArenaSetResultMatch( arenaCampaignState_t *state, int token ) {
	int tier = 0;
	int match = 0;
	if ( ArenaMatchForToken( state, token, &tier, &match ) == NULL ) {
		return;
	}
	state->resultToken = token;
	state->resultTier = tier;
	state->resultMatch = match;
	state->resultTierName = state->tiers[tier].name;
	state->resultMatchName = state->tiers[tier].matches[match].name;
	state->resultPlayerScore = ARENA_SCORE_UNAVAILABLE;
	state->resultOpponentScore = ARENA_SCORE_UNAVAILABLE;
}

static int ArenaPreferredMatch( int progress, int tier ) {
	for ( int match = 0; match < ARENA_BOSS_MATCH; match++ ) {
		if ( !ArenaMatchComplete( progress, tier, match ) ) {
			return match;
		}
	}
	if ( ArenaBossUnlocked( progress, tier ) ) {
		return ARENA_BOSS_MATCH;
	}
	return 0;
}

// The match's place in its tier: QUALIFIER 1-3 or BOSS MATCH.
static const char *ArenaMatchRoleKey( int match ) {
	static const char *qualifiers[] = { "#str_42088", "#str_42089", "#str_42090" };
	if ( match == ARENA_BOSS_MATCH ) {
		return "#str_42024";
	}
	return qualifiers[ idMath::ClampInt( 0, ARENA_BOSS_MATCH - 1, match ) ];
}

// The challenge NEXT leads to after a win: the first open, unwon match from
// this tier onward, then from the start of the circuit. Choosing the tier's
// preferred match instead sent a replayed win in a finished tier to that tier's
// already-beaten boss. False when every open match is won.
static bool ArenaNextChallenge( int progress, int fromTier, int &tierOut, int &matchOut ) {
	for ( int pass = 0; pass < 2; pass++ ) {
		for ( int tier = pass == 0 ? fromTier : 0; tier < ARENA_TIER_COUNT; tier++ ) {
			if ( pass == 1 && tier >= fromTier ) {
				break;
			}
			if ( !ArenaTierUnlocked( progress, tier ) ) {
				continue;
			}
			for ( int match = 0; match < ARENA_MATCHES_PER_TIER; match++ ) {
				if ( ArenaMatchUnlocked( progress, tier, match ) && !ArenaMatchComplete( progress, tier, match ) ) {
					tierOut = tier;
					matchOut = match;
					return true;
				}
			}
		}
	}
	return false;
}

// A tier's browser status. IN PROGRESS marks a tier with some qualifiers won;
// without it a part-cleared tier read exactly like one never played.
static const char *ArenaTierStatusKey( int progress, int tier ) {
	if ( !ArenaTierUnlocked( progress, tier ) ) {
		return "#str_42022";
	}
	if ( ArenaMatchComplete( progress, tier, ARENA_BOSS_MATCH ) ) {
		return "#str_42023";
	}
	if ( ArenaBossUnlocked( progress, tier ) ) {
		return "#str_42045";
	}
	for ( int match = 0; match < ARENA_BOSS_MATCH; match++ ) {
		if ( ArenaMatchComplete( progress, tier, match ) ) {
			return "#str_42039";
		}
	}
	return "#str_42065";
}

static void ArenaResolveSelection( arenaCampaignState_t *state ) {
	if ( state == NULL || !state->loaded ) {
		return;
	}

	ArenaRepairPersistedProgress();
	const int progress = ui_arenaProgress.GetInteger();
	int tier = idMath::ClampInt( 0, ARENA_TIER_COUNT - 1, ui_arenaSelectedTier.GetInteger() );
	if ( !ArenaTierUnlocked( progress, tier ) ) {
		tier = 0;
		for ( int i = 1; i < ARENA_TIER_COUNT; i++ ) {
			if ( !ArenaTierUnlocked( progress, i ) ) {
				break;
			}
			tier = i;
		}
	}

	int match = idMath::ClampInt( 0, ARENA_MATCHES_PER_TIER - 1, ui_arenaSelectedMatch.GetInteger() );
	if ( !ArenaMatchUnlocked( progress, tier, match ) ) {
		match = ArenaPreferredMatch( progress, tier );
	}

	ui_arenaSelectedTier.SetInteger( tier );
	ui_arenaSelectedMatch.SetInteger( match );
}

// A match's levelshot: its map's loadscreen, else its thumbnail, else the
// map's screenshot, else the generic loadscreen.
static idStr ArenaLevelshot( const arenaMatch_t &match, const idDict *mapDecl ) {
	idStr levelshot = "gfx/guis/loadscreens/generic";
	if ( mapDecl != NULL ) {
		levelshot = mapDecl->GetString( "loadimage" );
		if ( levelshot.IsEmpty() ) {
			levelshot = mapDecl->GetString( "mp_thumb" );
		}
	}
	if ( levelshot.IsEmpty() ) {
		char screenshot[MAX_STRING_CHARS];
		fileSystem->FindMapScreenshot( match.map, screenshot, sizeof( screenshot ) );
		levelshot = screenshot;
	}
	return levelshot;
}

static idStr ArenaBuildRoster( const arenaMatch_t &match ) {
	idStr roster;
	for ( int i = 0; i < match.bots.Num(); i++ ) {
		if ( i > 0 ) {
			roster += ", ";
		}
		roster += match.bots[i].name;
	}
	return roster;
}

static idStr ArenaBuildLimits( const arenaMatch_t &match ) {
	idStr limits;
	if ( match.gameType.Icmp( "Clan Arena" ) == 0 || match.gameType.Icmp( "Red Rover" ) == 0 ) {
		limits = va( "%s: %d", ArenaLocalized( "#str_42043" ), match.roundLimit );
	} else {
		limits = va( "%s: %d", ArenaLocalized( "#str_42041" ), match.fragLimit );
	}
	if ( match.timeLimit > 0 ) {
		limits += va( "   %s: %d", ArenaLocalized( "#str_42042" ), match.timeLimit );
	}
	return limits;
}

static void ArenaSetMatchCVar( const char *name, int value ) {
	cvarSystem->SetCVarInteger( name, value );
}

} // namespace

idArenaCampaign arenaCampaign;

/*
================
idArenaCampaign::idArenaCampaign
================
*/
idArenaCampaign::idArenaCampaign() : state( NULL ) {
}

/*
================
idArenaCampaign::~idArenaCampaign
================
*/
idArenaCampaign::~idArenaCampaign() {
	delete state;
	state = NULL;
}

/*
================
ArenaCampaign_Finish_f
================
*/
void ArenaCampaign_Finish_f( const idCmdArgs &args ) {
	arenaCampaign.FinishPendingCompletion();
}

/*
================
ArenaCampaign_Restart_f
================
*/
#ifndef ID_DEDICATED
static void ArenaCampaign_Restart_f( const idCmdArgs &args ) {
	arenaCampaign.RestartMatch();
}
#endif

/*
================
idArenaCampaign::Init
================
*/
void idArenaCampaign::Init() {
#ifdef ID_DEDICATED
	return;
#else
	if ( state == NULL ) {
		state = new arenaCampaignState_t;
	}

	state->gui = uiManager->FindGui( "guis/arena_menu.gui", true, false, true );
	state->returnGui = NULL;
	LoadCampaign();
	// The levelshots, and the retained Arena page's pictures of them, precache
	// on the front end's idle frames, and the page reads any still waiting when
	// it opens, so choosing a match never loads one mid-frame. Read here, they
	// held the title back; an engine reload still precaches them in its load.
	if ( state->loaded && sessLocal.RetainedArenaGui() != NULL ) {
		for ( int tierIndex = 0; tierIndex < state->tiers.Num(); tierIndex++ ) {
			for ( int matchIndex = 0; matchIndex < state->tiers[tierIndex].matches.Num(); matchIndex++ ) {
				const arenaMatch_t &match = state->tiers[tierIndex].matches[matchIndex];
				const idDict *mapDecl = NULL;
				ArenaMapSupportsMatch( match, &mapDecl );
				sessLocal.QueueRetainedLevelshot( ArenaLevelshot( match, mapDecl ).c_str(), true );
			}
		}
	}

	// Reaching game_mp completes the only module transition that is allowed to
	// carry a live Arena transaction across session shutdown. If the replayed
	// spawn command is later lost, a subsequent reload will abort and restore
	// settings instead of preserving a stranded launch indefinitely.
	if ( state->moduleTransitionPending &&
		 idStr::Icmp( cvarSystem->GetCVarString( "com_activeGameModule" ), "game_mp" ) == 0 ) {
		state->moduleTransitionPending = false;
	}

	if ( !state->finishCommandRegistered ) {
		cmdSystem->AddCommand(
			"arenaCampaignFinish", ArenaCampaign_Finish_f, CMD_FL_SYSTEM,
			"internal - completes a reported arena campaign match" );
		// The Arena pause menu's Restart match.
		cmdSystem->AddCommand(
			"arenaCampaignRestart", ArenaCampaign_Restart_f, CMD_FL_SYSTEM,
			"restarts the live arena campaign match from its introduction; nothing is recorded" );
		state->finishCommandRegistered = true;
	}
#endif
}

/*
================
idArenaCampaign::Frame

The Arena GUI may animate the entrance independently, but framework time owns
the launch. A missing named event or interrupted GUI script can therefore never
strand the campaign between the browser and spawnServer. Accepted results are
also finished here, immediately after the async game/server frame has returned,
so clearing game_mp's showcase state cannot expose a live first-person frame.
================
*/
void idArenaCampaign::Frame() {
#ifdef ID_DEDICATED
	return;
#else
	if ( state == NULL ) {
		return;
	}
	const bool menuOpen = IsActive() && state->transitionPhase == ARENA_TRANSITION_MATCH &&
		sessLocal.IsMapSpawned() && ArenaMenuCoversMatch();
	if ( ui_arenaMenuOpen.GetBool() != menuOpen ) {
		ui_arenaMenuOpen.SetBool( menuOpen );
	}
	// The pause menu's Settings opens the main menu's settings page over the
	// paused match. Backing out of it lands on the main menu's multiplayer
	// home, which offers nothing an Arena match can use, so the pause menu
	// takes the screen back as soon as the main menu comes to rest there.
	if ( menuOpen && ArenaMainMenuAtHome() && game != NULL ) {
		sessLocal.ExitMenu();
		idUserInterface *pause = game->StartMenu();
		if ( pause != NULL ) {
			sessLocal.SetGUI( pause, NULL );
		}
	}
	if ( state->transitionPhase == ARENA_TRANSITION_RETURN && state->completionPending ) {
		FinishPendingCompletion();
		return;
	}
	if ( state->transitionPhase != ARENA_TRANSITION_ENTRANCE ) {
		return;
	}
	if ( Sys_Milliseconds() < state->transitionDeadlineMsec ) {
		return;
	}

	CommitPendingMatch();
#endif
}

/*
================
idArenaCampaign::Shutdown

The state object intentionally survives the campaign's game_sp -> game_mp
reload.  A normal shutdown aborts instead, so temporary arena server settings
cannot leak into the player's archived multiplayer configuration.
================
*/
void idArenaCampaign::Shutdown() {
	if ( state == NULL ) {
		return;
	}

	const bool enteringArenaModule =
		IsActive() &&
		state->moduleTransitionPending &&
		idStr::Icmp( cvarSystem->GetCVarString( "com_nextGameModule" ), "game_mp" ) == 0;
	if ( !enteringArenaModule ) {
		AbortMatch();
	}
	if ( state->finishCommandRegistered ) {
		cmdSystem->RemoveCommand( "arenaCampaignFinish" );
		cmdSystem->RemoveCommand( "arenaCampaignRestart" );
		state->finishCommandRegistered = false;
	}

	state->gui = NULL;
	state->returnGui = NULL;
	state->tiers.Clear();
	state->loaded = false;
}

/*
================
idArenaCampaign::LoadCampaign
================
*/
bool idArenaCampaign::LoadCampaign() {
	if ( state == NULL ) {
		return false;
	}

	state->tiers.Clear();
	state->version = 0;
	state->loaded = false;

	idLexer lexer( LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS | LEXFL_NOFATALERRORS );
	if ( !lexer.LoadFile( ARENA_CAMPAIGN_FILE ) ) {
		common->Warning( "arena campaign: could not load '%s'", ARENA_CAMPAIGN_FILE );
		return false;
	}

	if ( !lexer.ExpectTokenString( "campaign" ) ) {
		return false;
	}
	state->version = lexer.ParseInt();
	if ( state->version <= 0 || state->version > 999 ) {
		lexer.Warning( "campaign version %d is outside the supported range 1..999", state->version );
		return false;
	}
	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	idToken token;
	bool campaignClosed = false;
	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			campaignClosed = true;
			break;
		}
		if ( token.Icmp( "tier" ) != 0 ) {
			lexer.Warning( "unknown campaign field '%s'", token.c_str() );
			return false;
		}

		arenaTier_t tier;
		if ( !ArenaParseTier( lexer, tier ) ) {
			return false;
		}
		state->tiers.Append( tier );
	}

	idToken trailingToken;
	if ( campaignClosed && lexer.ReadToken( &trailingToken ) ) {
		lexer.Warning( "unexpected trailing token '%s'", trailingToken.c_str() );
		state->tiers.Clear();
		return false;
	}

	if ( lexer.HadError() || !campaignClosed || state->tiers.Num() != ARENA_TIER_COUNT ) {
		common->Warning(
			"arena campaign: expected %d tiers, found %d",
			ARENA_TIER_COUNT, state->tiers.Num() );
		state->tiers.Clear();
		return false;
	}

	for ( int tierIndex = 0; tierIndex < state->tiers.Num(); tierIndex++ ) {
		arenaTier_t &tier = state->tiers[tierIndex];
		if ( !ArenaStringIsLocalizationKey( tier.name ) ||
			 !ArenaStringIsLocalizationKey( tier.description ) ||
			 tier.matches.Num() != ARENA_MATCHES_PER_TIER ) {
			common->Warning(
				"arena campaign: tier %d is incomplete (expected %d matches)",
				tierIndex + 1, ARENA_MATCHES_PER_TIER );
			state->tiers.Clear();
			return false;
		}

		for ( int matchIndex = 0; matchIndex < tier.matches.Num(); matchIndex++ ) {
			arenaMatch_t &match = tier.matches[matchIndex];
			if ( !ArenaStringIsLocalizationKey( match.name ) ||
				 !ArenaStringIsLocalizationKey( match.description ) ||
				 !ArenaStringIsSafeToken( match.map, true, false ) ||
				 !ArenaGameTypeIsSupported( match.gameType ) ||
				 match.baseSkill < 1 || match.baseSkill > 5 ||
				 match.fragLimit < 1 || match.fragLimit > 100 ||
				 match.timeLimit < 0 || match.timeLimit > 60 ||
				 match.roundLimit < 1 || match.roundLimit > 99 ||
				 match.scoreLimit < 1 || match.scoreLimit > 9999 ||
				 match.bots.Num() < 1 || match.bots.Num() + 1 > ARENA_MAX_SEATS ||
				 ( match.gameType.Icmp( "Duel" ) == 0 && match.bots.Num() != 1 ) ||
				 ( ArenaGameTypeIsTeam( match.gameType ) && ( match.bots.Num() & 1 ) == 0 ) ) {
				common->Warning(
					"arena campaign: invalid tier %d match %d",
					tierIndex + 1, matchIndex + 1 );
				state->tiers.Clear();
				return false;
			}

			for ( int botIndex = 0; botIndex < match.bots.Num(); botIndex++ ) {
				arenaBot_t &bot = match.bots[botIndex];
				if ( !ArenaStringIsSafeToken( bot.name, false, true ) ||
					 bot.skillOffset < -1 || bot.skillOffset > 1 ) {
					common->Warning(
						"arena campaign: invalid bot in tier %d match %d",
						tierIndex + 1, matchIndex + 1 );
					state->tiers.Clear();
					return false;
				}

				for ( int previousBot = 0; previousBot < botIndex; previousBot++ ) {
					if ( match.bots[previousBot].name.Icmp( bot.name ) == 0 ) {
						common->Warning(
							"arena campaign: duplicate bot '%s' in tier %d match %d",
							bot.name.c_str(), tierIndex + 1, matchIndex + 1 );
						state->tiers.Clear();
						return false;
					}
				}
			}
		}
	}

	state->loaded = true;
	if ( ui_arenaProgressVersion.GetInteger() != state->version ) {
		ui_arenaProgressVersion.SetInteger( state->version );
		ui_arenaProgress.SetInteger( 0 );
		ui_arenaBest.SetString( "" );
		ui_arenaSelectedTier.SetInteger( 0 );
		ui_arenaSelectedMatch.SetInteger( 0 );
	} else {
		ArenaRepairPersistedProgress();
	}
	ArenaResolveSelection( state );
	common->Printf(
		"arena campaign: loaded %d tiers and %d matches from %s\n",
		state->tiers.Num(), ARENA_TOTAL_MATCHES, ARENA_CAMPAIGN_FILE );
	return true;
}

/*
================
idArenaCampaign::IsGui
================
*/
bool idArenaCampaign::IsGui( const idUserInterface *gui ) const {
	return state != NULL && state->gui != NULL && gui == state->gui;
}

/*
================
idArenaCampaign::IsActive
================
*/
bool idArenaCampaign::IsActive() const {
	return ui_arenaCampaignActive.GetBool() &&
		ui_arenaActiveMatch.GetInteger() >= 1 &&
		ui_arenaActiveMatch.GetInteger() <= ARENA_TOTAL_MATCHES;
}

/*
================
idArenaCampaign::IsEntering
================
*/
bool idArenaCampaign::IsEntering() const {
	return state != NULL && state->transitionPhase == ARENA_TRANSITION_ENTRANCE;
}

/*
================
idArenaCampaign::PresentedGui

The browser on screen, stock or retained (NULL: neither presents).
================
*/
idUserInterface *idArenaCampaign::PresentedGui() const {
	if ( state == NULL || sessLocal.guiActive == NULL ) {
		return NULL;
	}
	if ( sessLocal.guiActive == state->gui || sessLocal.guiActive == sessLocal.RetainedArenaGui() ) {
		return sessLocal.guiActive;
	}
	return NULL;
}

/*
================
idArenaCampaign::NeedsCleanup

This is deliberately broader than IsActive.  It also catches interrupted
starts whose runtime token was lost after the ordinary multiplayer settings
had already been snapshotted and overridden.
================
*/
bool idArenaCampaign::NeedsCleanup() const {
	return ui_arenaCampaignActive.GetBool() || ui_arenaActiveMatch.GetInteger() != 0 ||
		( state != NULL && ( state->cvarsSaved || state->launchToken != 0 ||
			state->transitionPhase == ARENA_TRANSITION_ENTRANCE ) );
}

/*
================
idArenaCampaign::OpenSelector
================
*/
void idArenaCampaign::OpenSelector() {
#ifdef ID_DEDICATED
	return;
#else
	if ( state == NULL || state->gui == NULL ) {
		return;
	}
	if ( !state->loaded ) {
		LoadCampaign();
	}

	// A result ledger belongs to the browser, and the browser is the only view
	// that can dismiss one. Showing the mode selector underneath an undismissed
	// result would leave its blocking overlay up with no way to clear it, so
	// route an outstanding presentation to the view that owns it.
	if ( state->result != 0 ) {
		OpenBrowser();
		return;
	}

	if ( sessLocal.guiActive != state->gui ) {
		state->returnGui = sessLocal.guiActive != NULL ? sessLocal.guiActive : sessLocal.guiMainMenu;
	}
	state->resetConfirmation = false;
	state->view = 0;
	// Set the state before activation: the selector's onActivate chooses its
	// entrance timeline from arena_view, and SetGUI activates synchronously.
	UpdateGui();
	sessLocal.SetGUI( state->gui, NULL );
#endif
}

/*
================
idArenaCampaign::OpenBrowser
================
*/
void idArenaCampaign::OpenBrowser() {
#ifdef ID_DEDICATED
	return;
#else
	if ( state == NULL || state->gui == NULL ) {
		return;
	}
	if ( !state->loaded ) {
		LoadCampaign();
	}

	// The retained Arena page presents the browser where it can (Single
	// Player's Arena sub-page, which it leaves through the session); else this
	// one does.
	idUserInterface *view = sessLocal.RetainedArenaPage();
	if ( view == NULL ) {
		view = state->gui;
		if ( sessLocal.guiActive != state->gui ) {
			state->returnGui = sessLocal.guiActive != NULL ? sessLocal.guiActive : sessLocal.guiMainMenu;
		}
	} else {
		// Any levelshot the front end's idle frames have not read yet (Init).
		sessLocal.PrecacheRetainedLevelshots( true, false );
	}
	state->resetConfirmation = false;
	state->view = 1;
	// See OpenSelector: the browser needs this state before SetGUI runs its
	// activation script or it can briefly choose the previous view's reveal.
	UpdateGui();
	sessLocal.SetGUI( view, NULL );
	// SetGUI re-runs the browser's activation, which resets the report's
	// staged reveal. Any report still open has to be revealed again, not only
	// a new one, or the reopened browser shows an empty report frame that
	// nothing can dismiss.
	if ( state->result != 0 ) {
		state->resultRevealPending = false;
		view->HandleNamedEvent( "arenaResultReveal" );
	}
#endif
}

/*
================
idArenaCampaign::CloseMenu
================
*/
void idArenaCampaign::CloseMenu() {
	if ( state == NULL || state->gui == NULL ) {
		return;
	}

	if ( sessLocal.guiActive == state->gui ) {
		state->gui->Activate( false, common->GetPresentationTime() );
		sessLocal.guiActive = NULL;
	}

	if ( state->returnGui != NULL && state->returnGui != state->gui ) {
		idUserInterface *returnGui = state->returnGui;
		if ( returnGui == sessLocal.guiMainMenu ) {
			// Keep SetGUI from starting the generic main-menu intro before the
			// selector's route-aware return event takes over.
			returnGui->SetStateInt( "desktop::curr", 1 );
			returnGui->SetStateInt( "desktop::active", 1 );
		}
		sessLocal.SetGUI( returnGui, NULL );
		if ( returnGui == sessLocal.guiMainMenu ) {
			// The Single Player selector is a parent of the legacy Mission page.
			// Restore the main-menu landing state only after its own exit has
			// finished, so the selector never returns to a half-hidden page.
			returnGui->HandleNamedEvent( "returnFromSinglePlayer" );
		}
	} else if ( sessLocal.guiMainMenu != NULL ) {
		sessLocal.StartMenu( false );
		sessLocal.guiMainMenu->HandleNamedEvent( "returnFromSinglePlayer" );
	}
	state->resetConfirmation = false;
	state->returnGui = NULL;
}

/*
================
idArenaCampaign::UpdateMainMenuGui
================
*/
void idArenaCampaign::UpdateMainMenuGui( idUserInterface *gui ) {
	if ( gui == NULL ) {
		return;
	}
	gui->SetStateBool( "arenaCampaignActive", IsActive() );
	gui->SetStateInt( "arenaCampaignProgress", ArenaCountCompleted( ui_arenaProgress.GetInteger() ) );
}

/*
================
idArenaCampaign::SelectedLevelshot
================
*/
idStr idArenaCampaign::SelectedLevelshot() {
	if ( state == NULL || !state->loaded ) {
		return idStr();
	}
	ArenaResolveSelection( state );
	const arenaMatch_t &match = state->tiers[ui_arenaSelectedTier.GetInteger()].matches[ui_arenaSelectedMatch.GetInteger()];
	const idDict *mapDecl = NULL;
	ArenaMapSupportsMatch( match, &mapDecl );
	return ArenaLevelshot( match, mapDecl );
}

/*
================
idArenaCampaign::UpdateGui
================
*/
void idArenaCampaign::UpdateGui() {
	if ( state == NULL || state->gui == NULL ) {
		return;
	}
	PublishGui( state->gui );
	// The retained Arena page reads the same state while it is loaded.
	idUserInterface *page = sessLocal.RetainedArenaGui();
	if ( page != NULL ) {
		PublishGui( page );
	}
}

/*
================
idArenaCampaign::PublishGui
================
*/
void idArenaCampaign::PublishGui( idUserInterface *gui ) {
	gui->SetStateInt( "arena_view", state->view );
	gui->SetStateBool( "arena_loaded", state->loaded );
	gui->SetStateInt( "arena_transitionPhase", state->transitionPhase );
	gui->SetStateBool(
		"arena_transitioning",
		state->transitionPhase == ARENA_TRANSITION_ENTRANCE ||
		state->transitionPhase == ARENA_TRANSITION_RETURN );
	gui->SetStateInt( "arena_transitionDurationMsec", ARENA_ENTRANCE_MSEC );
	gui->SetStateInt(
		"arena_transitionRemainingMsec",
		state->transitionPhase == ARENA_TRANSITION_ENTRANCE
			? Max( 0, state->transitionDeadlineMsec - Sys_Milliseconds() )
			: 0 );

	if ( !state->loaded ) {
		// Publish a fully locked, empty board rather than returning early. The
		// per-slot keys below are never written on this path, so an unset
		// arena_tier%d_locked reads as 0 and the browser would otherwise draw
		// every tier and match as unlocked and selectable over blank rows.
		// #str_42048 is specifically "missing required map"; a campaign that
		// failed to parse is a different failure and gets its own line.
		for ( int tierIndex = 0; tierIndex < ARENA_TIER_COUNT; tierIndex++ ) {
			gui->SetStateString( va( "arena_tier%d_name", tierIndex ), "" );
			gui->SetStateString( va( "arena_tier%d_status", tierIndex ), ArenaLocalized( "#str_42022" ) );
			gui->SetStateBool( va( "arena_tier%d_locked", tierIndex ), true );
			gui->SetStateBool( va( "arena_tier%d_selected", tierIndex ), false );
			gui->SetStateBool( va( "arena_tier%d_completed", tierIndex ), false );
		}
		for ( int matchIndex = 0; matchIndex < ARENA_MATCHES_PER_TIER; matchIndex++ ) {
			gui->SetStateString( va( "arena_match%d_name", matchIndex ), "" );
			gui->SetStateString( va( "arena_match%d_status", matchIndex ), ArenaLocalized( "#str_42022" ) );
			gui->SetStateBool( va( "arena_match%d_locked", matchIndex ), true );
			gui->SetStateBool( va( "arena_match%d_selected", matchIndex ), false );
			gui->SetStateBool( va( "arena_match%d_completed", matchIndex ), false );
		}
		gui->SetStateInt( "arenaCampaignProgress", 0 );
		gui->SetStateInt( "arena_previousCampaignProgress", 0 );
		gui->SetStateBool( "arena_progressChanged", false );
		gui->SetStateBool( "arena_campaignComplete", false );
		gui->SetStateBool( "arena_resetConfirm", false );
		gui->SetStateBool( "arena_canReset", false );
		gui->SetStateBool( "arena_selectedBestVisible", false );
		gui->SetStateBool( "arena_selectedHasBest", false );
		gui->SetStateString( "arena_selectedBest", "" );
		gui->SetStateBool( "arena_resultNewBest", false );
		gui->SetStateInt( "arena_result", 0 );
		gui->SetStateBool( "arena_resultCanRetry", false );
		gui->SetStateBool( "arena_resultHasScore", false );
		gui->SetStateBool( "arena_resultHasUnlock", false );
		gui->SetStateString( "arena_selectedTierName", "" );
		gui->SetStateString( "arena_selectedTierDescription", "" );
		gui->SetStateString( "arena_selectedTitle", ArenaLocalized( "#str_42152" ) );
		gui->SetStateString( "arena_selectedMap", "" );
		gui->SetStateString( "arena_selectedGameType", "" );
		gui->SetStateString( "arena_selectedRoster", "" );
		gui->SetStateString( "arena_selectedLimits", "" );
		gui->SetStateString( "arena_selectedSkill", "" );
		declManager->FindMaterial( "gfx/guis/loadscreens/generic" )->SetSort( SS_GUI );
		gui->SetStateString( "arena_levelshot", "gfx/guis/loadscreens/generic" );
		gui->SetStateString( "arena_difficultyName", "" );
		gui->SetStateString(
			"arena_progress",
			va( "%s  0 / %d", ArenaLocalized( "#str_42012" ), ARENA_TOTAL_MATCHES ) );
		gui->SetStateBool( "arena_canStart", false );
		gui->SetStateString( "arena_selectedDescription", ArenaLocalized( "#str_42153" ) );
		gui->StateChanged( common->GetPresentationTime() );
		return;
	}

	ArenaResolveSelection( state );
	const int progress = ui_arenaProgress.GetInteger();
	const int selectedTier = ui_arenaSelectedTier.GetInteger();
	const int selectedMatch = ui_arenaSelectedMatch.GetInteger();
	gui->SetStateInt( "arenaCampaignProgress", ArenaCountCompleted( progress ) );

	for ( int tierIndex = 0; tierIndex < ARENA_TIER_COUNT; tierIndex++ ) {
		const arenaTier_t &tier = state->tiers[tierIndex];
		const bool unlocked = ArenaTierUnlocked( progress, tierIndex );
		const bool complete = ArenaMatchComplete( progress, tierIndex, ARENA_BOSS_MATCH );
		const char *status = ArenaTierStatusKey( progress, tierIndex );

		gui->SetStateString( va( "arena_tier%d_name", tierIndex ), ArenaLocalized( tier.name ) );
		gui->SetStateString( va( "arena_tier%d_status", tierIndex ), ArenaLocalized( status ) );
		gui->SetStateBool( va( "arena_tier%d_locked", tierIndex ), !unlocked );
		gui->SetStateBool( va( "arena_tier%d_selected", tierIndex ), tierIndex == selectedTier );
		gui->SetStateBool( va( "arena_tier%d_completed", tierIndex ), complete );
	}

	const arenaTier_t &tier = state->tiers[selectedTier];
	gui->SetStateString( "arena_selectedTierName", ArenaLocalized( tier.name ) );
	gui->SetStateString( "arena_selectedTierDescription", ArenaLocalized( tier.description ) );
	for ( int matchIndex = 0; matchIndex < ARENA_MATCHES_PER_TIER; matchIndex++ ) {
		const arenaMatch_t &match = tier.matches[matchIndex];
		const bool unlocked = ArenaMatchUnlocked( progress, selectedTier, matchIndex );
		const bool complete = ArenaMatchComplete( progress, selectedTier, matchIndex );
		const char *status = !unlocked ? "#str_42022" :
			( complete ? "#str_42023" :
			  ( matchIndex == ARENA_BOSS_MATCH ? "#str_42045" : "#str_42065" ) );

		gui->SetStateString( va( "arena_match%d_name", matchIndex ), ArenaLocalized( match.name ) );
		gui->SetStateString( va( "arena_match%d_status", matchIndex ), ArenaLocalized( status ) );
		gui->SetStateBool( va( "arena_match%d_locked", matchIndex ), !unlocked );
		gui->SetStateBool( va( "arena_match%d_selected", matchIndex ), matchIndex == selectedMatch );
		gui->SetStateBool( va( "arena_match%d_completed", matchIndex ), complete );
	}

	const arenaMatch_t &match = tier.matches[selectedMatch];
	const idDict *mapDecl = NULL;
	const bool mapAvailable = ArenaMapSupportsMatch( match, &mapDecl );

	idStr mapName = match.map;
	if ( mapDecl != NULL ) {
		mapName = ArenaLocalized( mapDecl->GetString( "name", match.map.c_str() ) );
	}
	const idStr levelshot = ArenaLevelshot( match, mapDecl );
	declManager->FindMaterial( levelshot )->SetSort( SS_GUI );

	gui->SetStateString( "arena_selectedTitle", ArenaLocalized( match.name ) );
	gui->SetStateString(
		"arena_selectedDescription",
		mapAvailable ? ArenaLocalized( match.description ) : ArenaLocalized( "#str_42048" ) );
	gui->SetStateString( "arena_selectedMap", mapName );
	gui->SetStateString( "arena_selectedGameType", ArenaLocalized( ArenaGameTypeStringKey( match.gameType ) ) );
	gui->SetStateString( "arena_selectedRoster", ArenaBuildRoster( match ) );
	gui->SetStateString( "arena_selectedLimits", ArenaBuildLimits( match ) );
	int minimumSkill = 5;
	int maximumSkill = 1;
	for ( int botIndex = 0; botIndex < match.bots.Num(); botIndex++ ) {
		const int skill = idMath::ClampInt(
			1, 5, match.baseSkill + ui_arenaDifficulty.GetInteger() - 3 + match.bots[botIndex].skillOffset );
		minimumSkill = Min( minimumSkill, skill );
		maximumSkill = Max( maximumSkill, skill );
	}
	gui->SetStateInt( "arena_selectedSkillMin", minimumSkill );
	gui->SetStateInt( "arena_selectedSkillMax", maximumSkill );
	gui->SetStateString(
		"arena_selectedSkill",
		minimumSkill == maximumSkill ? va( "%d", minimumSkill ) : va( "%d - %d", minimumSkill, maximumSkill ) );
	gui->SetStateString( "arena_levelshot", levelshot );

	// The selected match's best result. A locked match has none to show; an
	// open one says so until it has been played to a result.
	arenaBestResult_t bestResults[ARENA_TOTAL_MATCHES];
	ArenaReadBestResults( ui_arenaBest.GetString(), progress, bestResults );
	const arenaBestResult_t selectedBest = ArenaEffectiveBest(
		bestResults[ArenaMatchIndex( selectedTier, selectedMatch )], progress, selectedTier, selectedMatch );
	gui->SetStateBool( "arena_selectedBestVisible", ArenaMatchUnlocked( progress, selectedTier, selectedMatch ) );
	gui->SetStateBool( "arena_selectedHasBest", selectedBest.recorded );
	gui->SetStateString(
		"arena_selectedBest",
		selectedBest.recorded ? ArenaDescribeBest( selectedBest ).c_str() : ArenaLocalized( "#str_42166" ) );
	gui->SetStateBool( "arena_canReset", progress != 0 || ArenaHasBestResults() );
	gui->SetStateString( "arena_difficultyName", ArenaLocalized( ArenaDifficultyStringKey( ui_arenaDifficulty.GetInteger() ) ) );
	gui->SetStateInt( "arena_difficulty", ui_arenaDifficulty.GetInteger() );
	gui->SetStateString(
		"arena_progress",
		va( "%s  %d / %d", ArenaLocalized( "#str_42012" ), ArenaCountCompleted( progress ), ARENA_TOTAL_MATCHES ) );
	gui->SetStateBool(
		"arena_canStart",
		ArenaMatchUnlocked( progress, selectedTier, selectedMatch ) && mapAvailable && !IsActive() &&
		state->transitionPhase == ARENA_TRANSITION_IDLE );

	gui->SetStateInt( "arena_result", state->result );
	gui->SetStateInt(
		"arena_previousCampaignProgress",
		ArenaCountCompleted( state->resultPreviousProgress ) );
	gui->SetStateBool( "arena_progressChanged", state->resultNewlyCompleted );
	gui->SetStateInt( "arena_resultTier", state->resultTier );
	gui->SetStateInt( "arena_resultMatch", state->resultMatch );
	gui->SetStateBool( "arena_resultNewlyCompleted", state->resultNewlyCompleted );
	gui->SetStateBool( "arena_resultUnlockedBoss", state->resultUnlockedBoss );
	gui->SetStateBool( "arena_resultUnlockedTier", state->resultUnlockedTier );
	gui->SetStateBool( "arena_resultUnlockedCampaign", state->resultUnlockedCampaign );
	gui->SetStateBool( "arena_resultNewBest", state->result != 0 && state->resultNewBest );
	gui->SetStateString(
		"arena_resultTierName",
		state->resultTierName.IsEmpty() ? "" : ArenaLocalized( state->resultTierName ) );
	gui->SetStateString(
		"arena_resultMatchName",
		state->resultMatchName.IsEmpty() ? "" : ArenaLocalized( state->resultMatchName ) );
	gui->SetStateString(
		"arena_resultTitle",
		state->result == 1 ? ArenaLocalized( "#str_42030" ) :
		( state->result == 2 ? ArenaLocalized( "#str_42031" ) :
		  ( state->result == 3 ? ArenaLocalized( ArenaFailureTitleKey( state->resultFailure ) ) :
		    ( state->result == 4 ? ArenaLocalized( "#str_42077" ) : "" ) ) ) );
	// A win that changed nothing - a replay of a match already on the record -
	// must not promise that the circuit opened ahead.
	gui->SetStateString(
		"arena_resultMessage",
		state->result == 1 ? ArenaLocalized( state->resultNewlyCompleted ? "#str_42066" : "#str_42164" ) :
		( state->result == 2 ? ArenaLocalized( "#str_42067" ) :
		  ( state->result == 3 ? ArenaLocalized( ArenaFailureMessageKey( state->resultFailure ) ) :
		    ( state->result == 4 ? ArenaLocalized( "#str_42078" ) : "" ) ) ) );
	const bool resultHasScore =
		state->resultToken >= 1 && state->resultToken <= ARENA_TOTAL_MATCHES &&
		state->resultPlayerScore != ARENA_SCORE_UNAVAILABLE &&
		state->resultOpponentScore != ARENA_SCORE_UNAVAILABLE;
	gui->SetStateString(
		"arena_resultScore",
		resultHasScore
			? va( "%d  -  %d", state->resultPlayerScore, state->resultOpponentScore )
			: "" );
	gui->SetStateString(
		"arena_resultUnlock",
		state->resultUnlock.IsEmpty() ? "" : ArenaLocalized( state->resultUnlock ) );
	gui->SetStateBool( "arena_resultHasScore", resultHasScore );
	gui->SetStateBool( "arena_resultHasUnlock", !state->resultUnlock.IsEmpty() );

	// End-game awards earned in the completed match, packed one bit per
	// endGameAward_t id. Published as an ordered, compacted list so the ledger
	// can lay out rows without knowing the award ids.
	int awardsShown = 0;
	idStr awardLine;
	for ( int award = 1; award < ARENA_AWARD_COUNT; award++ ) {
		if ( ( state->resultAwards & ( 1 << award ) ) == 0 ) {
			continue;
		}
		const char *awardName = ArenaLocalized( ArenaAwardStringKey( award ) );
		gui->SetStateString( va( "arena_resultAward%d", awardsShown ), awardName );
		if ( awardsShown > 0 ) {
			awardLine += "   ";
		}
		awardLine += awardName;
		awardsShown++;
	}
	gui->SetStateString( "arena_resultAwardLine", awardLine );
	for ( int slot = awardsShown; slot < ARENA_AWARD_COUNT - 1; slot++ ) {
		gui->SetStateString( va( "arena_resultAward%d", slot ), "" );
	}
	gui->SetStateInt( "arena_resultAwardCount", awardsShown );
	gui->SetStateBool( "arena_resultHasAwards", awardsShown > 0 );
	gui->SetStateBool( "arena_resultCanRetry", ArenaResultCanRetry( state ) && !IsActive() );
	gui->SetStateBool( "arena_resetConfirm", state->resetConfirmation );
	gui->SetStateBool(
		"arena_campaignComplete",
		ArenaMatchComplete( progress, ARENA_TIER_COUNT - 1, ARENA_BOSS_MATCH ) );

	gui->StateChanged( common->GetPresentationTime() );
}

/*
================
idArenaCampaign::HandleGuiCommand
================
*/
bool idArenaCampaign::HandleGuiCommand( const char *menuCommand ) {
	if ( state == NULL || menuCommand == NULL ) {
		return false;
	}

	idCmdArgs args;
	args.TokenizeString( menuCommand, false );
	if ( args.Argc() < 1 ) {
		return false;
	}

	bool handled = false;
	for ( int icmd = 0; icmd < args.Argc(); ) {
		const char *cmd = args.Argv( icmd++ );

		// Arena GUI actions follow the stock menu convention of playing a
		// selection sound before issuing the action in the same command string.
		if ( !idStr::Icmp( cmd, "play" ) ) {
			if ( icmd < args.Argc() ) {
				idStr snd = args.Argv( icmd++ );
				int channel = 1;
				if ( snd.Length() == 1 && idStr::IsNumeric( snd.c_str() ) &&
					 icmd < args.Argc() ) {
					channel = atoi( snd.c_str() );
					snd = args.Argv( icmd++ );
				}
				if ( sessLocal.menuSoundWorld != NULL ) {
					sessLocal.menuSoundWorld->PlayShaderDirectly( snd, channel );
				}
			}
			handled = true;
			continue;
		}

		// The result ledger, the reset prompt and the transition curtain cover
		// the browser and return this so idWindow stops walking their siblings.
		// Without a returned command the event falls through to whatever control
		// happens to sit underneath, which could start a match from behind an
		// open overlay. It only marks the event as taken: the engine sends a
		// transition's pending timeline command in the same string as the clicked
		// window's, so ending the string here dropped the dismissal or navigation
		// that finished a transition, and its curtain then stayed up for good.
		if ( !idStr::Icmp( cmd, "arenaBlockInput" ) ) {
			handled = true;
			continue;
		}

		// Commands that carry a value consume it whether or not they run.
		const bool takesValue = !idStr::Icmp( cmd, "arenaSelectTier" ) || !idStr::Icmp( cmd, "arenaStepTier" ) ||
			!idStr::Icmp( cmd, "arenaSelectMatch" ) || !idStr::Icmp( cmd, "arenaDifficulty" );
		const char *value = "";
		if ( takesValue ) {
			if ( icmd >= args.Argc() ) {
				handled = true;
				continue;
			}
			value = args.Argv( icmd++ );
		}

		// The entrance is deliberately short and framework-timed. Ignore menu
		// navigation after launch acceptance so a late click cannot change the
		// authored token while the transition is on screen.
		if ( state->transitionPhase == ARENA_TRANSITION_ENTRANCE ) {
			common->DPrintf( "arena campaign: ignored GUI command '%s' during entrance\n", cmd );
			handled = true;
			continue;
		}

		// An open report or reset prompt owns the screen. Keyboard and controller
		// focus can still reach the controls beneath it - the browser stays
		// visible under every overlay - so only the overlay's own actions run.
		// BACK under a report reopened the browser without its reveal and left
		// an empty frame that nothing could dismiss.
		const bool resultOpen = state->result != 0;
		const bool resetOpen = state->resetConfirmation;
		const bool overlayCommand =
			( resultOpen && ( !idStr::Icmp( cmd, "arenaDismissResult" ) || !idStr::Icmp( cmd, "arenaRetry" ) ) ) ||
			( resetOpen && ( !idStr::Icmp( cmd, "arenaResetConfirm" ) || !idStr::Icmp( cmd, "arenaResetCancel" ) ) );
		if ( ( resultOpen || resetOpen ) && !overlayCommand ) {
			common->DPrintf( "arena campaign: ignored GUI command '%s' behind an open %s\n",
				cmd, resultOpen ? "report" : "reset prompt" );
			handled = true;
			continue;
		}

		// These hand the screen to another menu; nothing after them in the same
		// string belongs to this one.
		if ( !idStr::Icmp( cmd, "arenaCampaigns" ) ) {
			sessLocal.OpenCampaignSelector( true );
			return true;
		}
		if ( !idStr::Icmp( cmd, "arenaMission" ) ) {
			sessLocal.SelectCampaign( "quake4" );
			return true;
		}
		if ( !idStr::Icmp( cmd, "arenaMissionOpen" ) ) {
			idUserInterface *mainMenu = sessLocal.guiMainMenu;
			state->returnGui = NULL;
			if ( mainMenu != NULL ) {
				// The main page was intentionally parked when it handed off to the
				// selector.  Suppress its generic Activate intro so openMission can
				// continue directly into the legacy New Game transition.
				mainMenu->SetStateInt( "desktop::curr", 1 );
				mainMenu->SetStateInt( "desktop::active", 1 );
			}
			sessLocal.SetGUI( mainMenu, NULL );
			if ( mainMenu != NULL ) {
				mainMenu->HandleNamedEvent( "openMission" );
			}
			return true;
		}
		if ( !idStr::Icmp( cmd, "arenaClose" ) ) {
			CloseMenu();
			return true;
		}
		if ( !idStr::Icmp( cmd, "arenaBackToModes" ) ) {
			sessLocal.OpenCampaignSelector( false );
			return true;
		}

		bool known = true;
		if ( !idStr::Icmp( cmd, "arenaBrowse" ) ) {
			state->resetConfirmation = false;
			state->view = 1;
			UpdateGui();
		} else if ( !idStr::Icmp( cmd, "arenaLegacyModes" ) ) {
			state->resetConfirmation = false;
			state->view = 0;
			UpdateGui();
		} else if ( !idStr::Icmp( cmd, "arenaSelectTier" ) ) {
			const int tier = atoi( value );
			const int progress = ui_arenaProgress.GetInteger();
			if ( tier >= 0 && tier < ARENA_TIER_COUNT && ArenaTierUnlocked( progress, tier ) ) {
				ui_arenaSelectedTier.SetInteger( tier );
				ui_arenaSelectedMatch.SetInteger( ArenaPreferredMatch( progress, tier ) );
				UpdateGui();
			}
		} else if ( !idStr::Icmp( cmd, "arenaStepTier" ) ) {
			// The next unlocked tier before or after the chosen one, on its
			// preferred match; nothing past the first or the last open tier.
			const int step = atoi( value ) < 0 ? -1 : 1;
			const int progress = ui_arenaProgress.GetInteger();
			for ( int tier = ui_arenaSelectedTier.GetInteger() + step; tier >= 0 && tier < ARENA_TIER_COUNT; tier += step ) {
				if ( ArenaTierUnlocked( progress, tier ) ) {
					ui_arenaSelectedTier.SetInteger( tier );
					ui_arenaSelectedMatch.SetInteger( ArenaPreferredMatch( progress, tier ) );
					UpdateGui();
					break;
				}
			}
		} else if ( !idStr::Icmp( cmd, "arenaSelectMatch" ) ) {
			const int match = atoi( value );
			const int tier = ui_arenaSelectedTier.GetInteger();
			if ( match >= 0 && match < ARENA_MATCHES_PER_TIER &&
				 ArenaMatchUnlocked( ui_arenaProgress.GetInteger(), tier, match ) ) {
				ui_arenaSelectedMatch.SetInteger( match );
				UpdateGui();
			}
		} else if ( !idStr::Icmp( cmd, "arenaDifficulty" ) ) {
			const int delta = atoi( value );
			ui_arenaDifficulty.SetInteger(
				idMath::ClampInt( 1, 5, ui_arenaDifficulty.GetInteger() + delta ) );
			UpdateGui();
		} else if ( !idStr::Icmp( cmd, "arenaStart" ) ) {
			StartSelectedMatch();
		} else if ( !idStr::Icmp( cmd, "arenaReset" ) ) {
			RequestResetProgress();
		} else if ( !idStr::Icmp( cmd, "arenaResetConfirm" ) ) {
			ResetProgress();
		} else if ( !idStr::Icmp( cmd, "arenaResetCancel" ) ) {
			CancelResetProgress();
		} else if ( !idStr::Icmp( cmd, "arenaRetry" ) ) {
			RetryResult();
		} else if ( !idStr::Icmp( cmd, "arenaDismissResult" ) ) {
			DismissResult();
		} else {
			known = false;
			common->DPrintf( "arena campaign: unknown GUI command '%s'\n", cmd );
		}
		handled = handled || known;
	}

	if ( !handled ) {
		common->DPrintf( "arena campaign: unknown GUI command '%s'\n", menuCommand );
	}
	return handled;
}

/*
================
idArenaCampaign::RequestResetProgress
================
*/
void idArenaCampaign::RequestResetProgress() {
	if ( state == NULL || IsActive() ) {
		return;
	}
	ArenaRepairPersistedProgress();
	if ( ui_arenaProgress.GetInteger() == 0 && !ArenaHasBestResults() ) {
		return;
	}
	state->resetConfirmation = true;
	UpdateGui();
	if ( idUserInterface *gui = PresentedGui() ) {
		gui->HandleNamedEvent( "arenaResetReveal" );
	}
}

/*
================
idArenaCampaign::CancelResetProgress
================
*/
void idArenaCampaign::CancelResetProgress() {
	if ( state == NULL || !state->resetConfirmation ) {
		return;
	}
	state->resetConfirmation = false;
	UpdateGui();
}

/*
================
idArenaCampaign::ResetProgress
================
*/
void idArenaCampaign::ResetProgress() {
	if ( state == NULL || IsActive() || !state->resetConfirmation ) {
		return;
	}
	ArenaRepairPersistedProgress();
	if ( ui_arenaProgress.GetInteger() == 0 && !ArenaHasBestResults() ) {
		state->resetConfirmation = false;
		UpdateGui();
		return;
	}
	ui_arenaProgress.SetInteger( 0 );
	ui_arenaBest.SetString( "" );
	ui_arenaSelectedTier.SetInteger( 0 );
	ui_arenaSelectedMatch.SetInteger( 0 );
	ArenaClearResultMetadata( state );
	state->transitionPhase = ARENA_TRANSITION_IDLE;
	state->transitionDeadlineMsec = 0;
	state->launchToken = 0;
	state->resetConfirmation = false;
	common->Printf( "arena campaign: progress and best results reset\n" );
	UpdateGui();
}

/*
================
idArenaCampaign::RetryResult
================
*/
void idArenaCampaign::RetryResult() {
	if ( state == NULL || IsActive() || state->result == 0 ) {
		return;
	}

	// The report has already collapsed when the button is pressed, so a retry
	// that cannot start must still close it rather than strand an empty frame.
	int tier = 0;
	int match = 0;
	if ( !ArenaResultCanRetry( state ) ||
		 ArenaMatchForToken( state, state->resultToken, &tier, &match ) == NULL ) {
		DismissResult();
		return;
	}

	ui_arenaSelectedTier.SetInteger( tier );
	ui_arenaSelectedMatch.SetInteger( match );
	ArenaClearResultMetadata( state );
	state->transitionPhase = ARENA_TRANSITION_IDLE;
	state->resetConfirmation = false;
	StartSelectedMatch();
}

/*
================
idArenaCampaign::DismissResult
================
*/
void idArenaCampaign::DismissResult() {
	if ( state == NULL || state->result == 0 ) {
		return;
	}
	const bool revealProgress = ArenaHasProgressReveal( state );

	int tier = ui_arenaSelectedTier.GetInteger();
	int match = ui_arenaSelectedMatch.GetInteger();
	if ( ArenaMatchForToken( state, state->resultToken, &tier, &match ) != NULL ) {
		ui_arenaSelectedTier.SetInteger( tier );
		ui_arenaSelectedMatch.SetInteger( match );
	}
	if ( state->resultWon ) {
		// NEXT leads to the next open, unwon challenge: the rest of this tier,
		// then the tiers after it. A won boss therefore lands on the new tier,
		// and a replayed win in a finished tier on the first match still to win
		// rather than that tier's already-beaten boss. With nothing left to win
		// the selection stays on the match just played.
		int nextTier = tier;
		int nextMatch = match;
		if ( ArenaNextChallenge( ui_arenaProgress.GetInteger(), tier, nextTier, nextMatch ) ) {
			ui_arenaSelectedTier.SetInteger( nextTier );
			ui_arenaSelectedMatch.SetInteger( nextMatch );
		}
	}

	// Keep completed-match metadata alive for the browser's follow-up
	// progression animation. StartSelectedMatch/ResetProgress clear it when a
	// new transaction supersedes this presentation.
	state->result = 0;
	state->resultRevealPending = false;
	state->transitionPhase = ARENA_TRANSITION_IDLE;
	state->resetConfirmation = false;
	UpdateGui();
	if ( idUserInterface *gui = PresentedGui() ) {
		gui->HandleNamedEvent( revealProgress ? "arenaProgressReveal" : "arenaBrowserReveal" );
	}
}

/*
================
idArenaCampaign::StartSelectedMatch
================
*/
void idArenaCampaign::StartSelectedMatch() {
	if ( state == NULL || !state->loaded || state->result != 0 ||
		 state->transitionPhase != ARENA_TRANSITION_IDLE ) {
		return;
	}

	ArenaResolveSelection( state );
	const int tier = ui_arenaSelectedTier.GetInteger();
	const int matchIndex = ui_arenaSelectedMatch.GetInteger();
	const int progress = ui_arenaProgress.GetInteger();
	if ( !ArenaMatchUnlocked( progress, tier, matchIndex ) ) {
		return;
	}

	const arenaMatch_t &match = state->tiers[tier].matches[matchIndex];
	if ( !ArenaMapSupportsMatch( match ) ) {
		UpdateGui();
		return;
	}

	// Arena matches always start from a fresh listen server.  Besides avoiding
	// stale match state, this guarantees that bot population cannot race queued
	// bot removals from an active multiplayer session.  Restart match resets a
	// live match on its own server instead (RestartMatchInPlace), where the
	// roster this populated is still connected and nothing is added again.
	if ( NeedsCleanup() || idAsyncNetwork::server.IsActive() || idAsyncNetwork::client.IsActive() ) {
		AbortMatch();
		sessLocal.Stop();
	}

	ArenaClearResultMetadata( state );
	state->resetConfirmation = false;
	state->completionPending = false;
	state->botsPopulated = false;
	state->launchToken = ArenaMatchIndex( tier, matchIndex ) + 1;
	state->transitionPhase = ARENA_TRANSITION_ENTRANCE;
	state->transitionDeadlineMsec = Sys_Milliseconds() + ARENA_ENTRANCE_MSEC;

	common->Printf(
		"arena campaign: presenting entrance for match %d (%d ms)\n",
		state->launchToken,
		ARENA_ENTRANCE_MSEC );
	UpdateGui();
	if ( idUserInterface *gui = PresentedGui() ) {
		gui->HandleNamedEvent( "arenaMatchLaunch" );
	}
}

/*
================
idArenaCampaign::CommitPendingMatch

Commits the token chosen before the entrance. Selection changes are ignored
during that presentation and the authored match is validated again here so a
reload or content change cannot redirect the queued launch.
================
*/
void idArenaCampaign::CommitPendingMatch() {
	if ( state == NULL || state->transitionPhase != ARENA_TRANSITION_ENTRANCE ) {
		return;
	}

	int tier = 0;
	int matchIndex = 0;
	const int token = state->launchToken;
	const arenaMatch_t *match = ArenaMatchForToken( state, token, &tier, &matchIndex );
	if ( match == NULL || !ArenaMatchUnlocked( ui_arenaProgress.GetInteger(), tier, matchIndex ) ||
		 !ArenaMapSupportsMatch( *match ) ) {
		common->Warning( "arena campaign: entrance could not commit match token %d", token );
		AbortMatch();
		OpenBrowser();
		return;
	}

	state->transitionPhase = ARENA_TRANSITION_MATCH;
	state->transitionDeadlineMsec = 0;
	state->launchToken = 0;
	ArenaSaveCurrentCVars( state );
	ui_arenaCampaignActive.SetBool( true );
	ui_arenaActiveMatch.SetInteger( token );

	if ( !PrepareServer() ) {
		UpdateGui();
		return;
	}
	common->Printf( "arena campaign: entrance complete, launching match %d\n", token );
	sessLocal.ExitMenu();
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "spawnServer \"%s\"\n", match->map.c_str() ) );
}

/*
================
idArenaCampaign::PrepareServer
================
*/
bool idArenaCampaign::PrepareServer() {
	if ( !NeedsCleanup() ) {
		return true;
	}
	if ( state == NULL || !IsActive() ) {
		common->Warning( "arena campaign: refusing an incomplete match launch state" );
		AbortMatch();
		return false;
	}

	const int token = ui_arenaActiveMatch.GetInteger();
	int tierIndex = 0;
	int matchIndex = 0;
	const arenaMatch_t *match = ArenaMatchForToken( state, token, &tierIndex, &matchIndex );
	if ( match == NULL ) {
		common->Warning( "arena campaign: active match token %d has no campaign entry", token );
		AbortMatch();
		return false;
	}
	ui_arenaMatchTitle.SetString( match->name );
	ui_arenaMatchTier.SetString( state->tiers[tierIndex].name );
	ui_arenaMatchRole.SetString( ArenaMatchRoleKey( matchIndex ) );
	ui_arenaMatchGameType.SetString( ArenaGameTypeStringKey( match->gameType ) );
	if ( state->transitionPhase == ARENA_TRANSITION_IDLE ) {
		// Supports command-line/runtime recovery where the persisted active token
		// reaches SpawnServer without first visiting the browser entrance.
		state->transitionPhase = ARENA_TRANSITION_MATCH;
	}

	state->moduleTransitionPending =
		idStr::Icmp( cvarSystem->GetCVarString( "com_activeGameModule" ), "game_mp" ) != 0;

	// The second call happens after game_mp has registered its CVAR_GAME vars,
	// so this incremental save also captures settings unavailable in game_sp.
	ArenaSaveCurrentCVars( state );

	// Arena authors its limits through the casual legacy-CVar adapter. Force a
	// fresh import for every match: the multiplayer object survives MapShutdown,
	// and otherwise two consecutive DM-backed cards can reuse the first card's
	// committed rules. The player's archived profile is restored with the other
	// transactional overrides after the match.
	cvarSystem->SetCVarString( "g_matchProfile", "casual" );
	idCVar *matchProfile = cvarSystem->Find( "g_matchProfile" );
	if ( matchProfile != NULL ) {
		matchProfile->SetModified();
	}

	cvarSystem->SetCVarInteger( "net_serverDedicated", 0 );
	cvarSystem->SetCVarBool( "net_LANServer", true );
	// A full engine reload during ExecuteMapChange replays a bare "spawnServer"
	// after session shutdown has already aborted this transaction, which would
	// drop the player into an ordinary multiplayer match on the campaign map.
	// The campaign's own launch never needs that reload, so suppress it for the
	// duration and hand the player's setting back with the rest.
	cvarSystem->SetCVarInteger( "net_serverReloadEngine", 0 );
	cvarSystem->SetCVarString( "si_map", match->map );
	cvarSystem->SetCVarString( "si_mapCycle", "" );
	cvarSystem->SetCVarString( "si_gameType", ArenaEffectiveGameType( match->gameType ) );
	cvarSystem->SetCVarBool( "si_pure", false );
	ArenaSetMatchCVar( "si_maxPlayers", match->bots.Num() + 1 );
	ArenaSetMatchCVar( "si_minPlayers", 2 );
	ArenaSetMatchCVar( "si_privatePlayers", 0 );
	ArenaSetMatchCVar( "si_fragLimit", match->fragLimit );
	ArenaSetMatchCVar( "si_timeLimit", match->timeLimit );
	ArenaSetMatchCVar( "si_captureLimit", match->scoreLimit );
	ArenaSetMatchCVar( "si_scoreLimit", match->scoreLimit );
	ArenaSetMatchCVar( "si_roundLimit", match->roundLimit );
	ArenaSetMatchCVar( "si_roundTimeLimit", 120 );
	ArenaSetMatchCVar( "si_roundWarmupDelay", 4 );
	ArenaSetMatchCVar( "si_roundEndDelay", 3 );
	// g_gameReviewPause is the player's archived multiplayer value and its range
	// floor is 2 seconds.  The Arena ceremony owns the whole review beat, so pin
	// it well beyond the ceremony rather than letting a user setting decide when
	// NEXTGAME restarts the map out from under the presentation.
	ArenaSetMatchCVar( "g_gameReviewPause", 60 );
	ArenaSetMatchCVar( "si_tourneyLimit", 1 );
	cvarSystem->SetCVarBool( "si_useReady", false );
	// The typed match-rule adapter requires a zero threshold when readiness is
	// disabled. Leaving the archived 0.51 default in place rejects the complete
	// rule draft and strands Arena play in non-scoring WARMUP forever.
	ArenaSetMatchCVar( "si_warmupReadyPercentage", 0 );
	cvarSystem->SetCVarBool( "si_allowVoting", false );
	cvarSystem->SetCVarBool( "si_isBuyingEnabled", false );
	cvarSystem->SetCVarBool( "si_dropWeaponsInBuyingModes", false );
	cvarSystem->SetCVarBool( "si_usePass", false );
	cvarSystem->SetCVarBool( "si_teamDamage", false );
	cvarSystem->SetCVarBool( "si_weaponStay", false );
	ArenaSetMatchCVar( "si_mercyLimit", 0 );
	ArenaSetMatchCVar( "si_overtime", 120 );
	cvarSystem->SetCVarBool( "si_suddenDeathRestart", true );
	ArenaSetMatchCVar( "si_suddenDeathRespawnDelay", 3 );
	ArenaSetMatchCVar( "si_suddenDeathRespawnIncrease", 1 );
	ArenaSetMatchCVar( "si_suddenDeathRespawnMax", 10 );
	cvarSystem->SetCVarBool( "si_forfeit", true );
	cvarSystem->SetCVarBool( "si_warmup", true );
	cvarSystem->SetCVarBool( "si_warmupWeapons", true );
	cvarSystem->SetCVarBool( "si_warmupScoring", false );
	ArenaSetMatchCVar( "si_countDown", 4 );
	ArenaSetMatchCVar( "si_teamSizeMin", 1 );
	cvarSystem->SetCVarBool( "si_teamForcePresent", true );
	cvarSystem->SetCVarBool( "si_autoShuffle", false );
	cvarSystem->SetCVarBool( "si_shuffle", false );
	cvarSystem->SetCVarBool( "si_autobalance", true );
	// Single player: there is nobody to spectate and no side to pick. With this
	// false, idPlayer::UserInfoChanged forces ui_spectate back to "Play" and
	// clears wantSpectate on the server, which is the authoritative block; the
	// game_mp menu entry points refuse an Arena request on top of it.
	cvarSystem->SetCVarBool( "si_spectators", false );
	ArenaSetMatchCVar( "si_fps", 60 );
	cvarSystem->SetCVarBool( "ui_autoJoin", true );
	cvarSystem->SetCVarBool( "ui_joined", true );
	cvarSystem->SetCVarString( "ui_ready", "Ready" );
	cvarSystem->SetCVarString( "ui_spectate", "Play" );
	cvarSystem->SetCVarString( "ui_team", "Marine" );
	cvarSystem->SetCVarBool( "bot_enable", true );
	ArenaSetMatchCVar( "bot_minPlayers", 0 );
	cvarSystem->SetCVarBool( "bot_characters", true );
	cvarSystem->SetCVarFloat( "bot_skillVariance", 0.0f );
	ArenaSetMatchCVar( "si_arenaCampaign", token );
	return true;
}

/*
================
idArenaCampaign::ReturnToBrowserAfterStartFailure
================
*/
void idArenaCampaign::ReturnToBrowserAfterStartFailure( int failure ) {
	if ( state == NULL ) {
		return;
	}

	// Tear the server down before restoring its archived serverInfo values so
	// a partial listen server cannot react to the player's normal MP settings.
	// The token goes with AbortMatch, but the report should still name the
	// match that failed.
	const int token = ui_arenaActiveMatch.GetInteger();
	sessLocal.Stop();
	AbortMatch();
	ArenaClearResultMetadata( state );
	const bool report = failure != ARENA_FAILURE_NONE;
	if ( report ) {
		ArenaSetResultMatch( state, token );
	}
	state->result = report ? 3 : 0;
	state->resultFailure = report ? failure : ARENA_FAILURE_NONE;
	state->resultPreviousProgress = ui_arenaProgress.GetInteger();
	state->resultRevealPending = report;
	state->transitionPhase = report ? ARENA_TRANSITION_RESULT : ARENA_TRANSITION_IDLE;
	state->resetConfirmation = false;
	sessLocal.StartMenu( false );
	OpenBrowser();
}

/*
================
idArenaCampaign::ReportStartFailure

The spawnServer path fails outside this class, before any listen server exists.
Route it through the same presentation instead of returning to the browser in
silence: a taken UDP port is the one launch failure an ordinary player hits.
================
*/
void idArenaCampaign::ReportStartFailure() {
	if ( state == NULL ) {
		return;
	}
	ReturnToBrowserAfterStartFailure( ARENA_FAILURE_SERVER );
}

/*
================
idArenaCampaign::ActiveMatchToken
================
*/
int idArenaCampaign::ActiveMatchToken() const {
	return IsActive() ? ui_arenaActiveMatch.GetInteger() : 0;
}

/*
================
idArenaCampaign::ReportAbandoned

Leaving is always allowed and never costs progress - nothing is recorded for
a match that did not finish. The report says so, names the match and offers
it again, where the player previously landed on the title screen in silence.
================
*/
void idArenaCampaign::ReportAbandoned( int token ) {
#ifdef ID_DEDICATED
	return;
#else
	if ( state == NULL || IsActive() ) {
		return;
	}
	if ( !state->loaded ) {
		LoadCampaign();
	}
	if ( ArenaMatchForToken( state, token ) == NULL ) {
		return;
	}
	ArenaClearResultMetadata( state );
	ArenaSetResultMatch( state, token );
	state->result = 3;
	state->resultFailure = ARENA_FAILURE_ABANDONED;
	state->resultPreviousProgress = ui_arenaProgress.GetInteger();
	state->resultRevealPending = true;
	state->transitionPhase = ARENA_TRANSITION_RESULT;
	state->resetConfirmation = false;
	ui_arenaSelectedTier.SetInteger( state->resultTier );
	ui_arenaSelectedMatch.SetInteger( state->resultMatch );
	common->Printf( "arena campaign: match %d abandoned; nothing recorded\n", token );
	OpenBrowser();
#endif
}

/*
================
idArenaCampaign::RestartMatchInPlace

Restart match on the running server: serverMapRestart respawns everyone,
resets the map, the scores and the clock, and warmup replays the
introductions. The bots keep their client slots, so nothing is removed and
added again and the roster cannot race a queued bot removal. It is refused
unless this is still the match's own listen server with exactly the
authored roster connected, and game_mp refuses a match whose result is
already due. True when the match restarted.
================
*/
bool idArenaCampaign::RestartMatchInPlace( int token ) {
#ifdef ID_DEDICATED
	return false;
#else
	const arenaMatch_t *match = ArenaMatchForToken( state, token );
	if ( match == NULL || !state->botsPopulated || game == NULL || !sessLocal.IsMapSpawned() ||
		 idStr::Icmp( cvarSystem->GetCVarString( "com_activeGameModule" ), "game_mp" ) != 0 ||
		 !idAsyncNetwork::server.IsActive() ) {
		common->Printf( "arena campaign: match %d has no running server to restart\n", token );
		return false;
	}
	const int clients = idAsyncNetwork::server.GetNumClients();
	if ( clients != 1 + match->bots.Num() ) {
		common->Printf( "arena campaign: match %d has %d of its %d combatants; relaunching it\n",
			token, clients, 1 + match->bots.Num() );
		return false;
	}

	ui_arenaRestartMatch.SetInteger( token );
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, "serverMapRestart\n" );
	const bool restarted = ui_arenaRestartMatch.GetInteger() == 0;
	ui_arenaRestartMatch.SetInteger( 0 );
	if ( !restarted ) {
		return false;
	}

	// The pause menu asked for this. It closes as Resume closes it, and the
	// restarted warmup runs; any other menu over the match stays up and keeps
	// the restarted match paused behind it.
	idUserInterface *gui = sessLocal.GetActiveGUI();
	if ( gui != NULL && idStr::Icmp( gui->Name(), "guis/arena_pause.gui" ) == 0 ) {
		sessLocal.HandleGameMenuReturn( game->HandleGuiCommands( "arenaPause resume" ) );
	}
	// Publish it now rather than on the next Frame, which may come after the
	// first game frame of the restarted warmup.
	ui_arenaMenuOpen.SetBool( ArenaMenuCoversMatch() );
	return true;
#endif
}

/*
================
idArenaCampaign::RestartMatch

The pause menu's Restart match: the live match again from its introductions,
with nothing recorded. It restarts in place when it can. Otherwise it leaves
the match as Leave match does and takes the abandoned report's own Retry, so
the restart follows the one launch path every match takes: the browser's
entrance curtain, PrepareServer and a fresh listen server, which adds the
authored roster again. A match that can no longer start shows the abandoned
report instead.
================
*/
void idArenaCampaign::RestartMatch() {
#ifdef ID_DEDICATED
	return;
#else
	const int token = ActiveMatchToken();
	if ( state == NULL || token == 0 || state->transitionPhase != ARENA_TRANSITION_MATCH ) {
		common->Printf( "arena campaign: no live match to restart\n" );
		return;
	}
	common->Printf( "arena campaign: restarting match %d; nothing recorded\n", token );
	if ( RestartMatchInPlace( token ) ) {
		common->Printf( "arena campaign: match %d restarted in place\n", token );
		return;
	}
	common->Printf( "arena campaign: relaunching match %d\n", token );
	// As ReturnToBrowserAfterStartFailure: the server goes before its
	// archived serverInfo values come back.
	sessLocal.Stop();
	AbortMatch();
	sessLocal.StartMenu( false );
	if ( !state->loaded ) {
		LoadCampaign();
	}
	// The browser opens under the entrance curtain: a live match holds no
	// result, so it opens without a report.
	OpenBrowser();
	ArenaClearResultMetadata( state );
	ArenaSetResultMatch( state, token );
	state->result = 3;
	state->resultFailure = ARENA_FAILURE_ABANDONED;
	if ( !ArenaResultCanRetry( state ) ) {
		ArenaClearResultMetadata( state );
		ReportAbandoned( token );
		return;
	}
	RetryResult();
#endif
}

/*
================
idArenaCampaign::PopulateBots
================
*/
void idArenaCampaign::PopulateBots() {
	if ( state == NULL ) {
		return;
	}

	if ( !IsActive() || state->botsPopulated ) {
		return;
	}
	if ( idStr::Icmp( cvarSystem->GetCVarString( "com_activeGameModule" ), "game_mp" ) != 0 ||
		 !idAsyncNetwork::server.IsActive() ) {
		common->Warning(
			"arena campaign: could not populate match %d (module='%s', server=%d)",
			ui_arenaActiveMatch.GetInteger(),
			cvarSystem->GetCVarString( "com_activeGameModule" ),
			idAsyncNetwork::server.IsActive() ? 1 : 0 );
		ReturnToBrowserAfterStartFailure( ARENA_FAILURE_SERVER );
		return;
	}

	const arenaMatch_t *match = ArenaMatchForToken( state, ui_arenaActiveMatch.GetInteger() );
	if ( match == NULL ) {
		common->Warning(
			"arena campaign: active match token %d disappeared before bot population",
			ui_arenaActiveMatch.GetInteger() );
		ReturnToBrowserAfterStartFailure( ARENA_FAILURE_SERVER );
		return;
	}

	const int difficulty = ui_arenaDifficulty.GetInteger();
	const int clientsBeforePopulation = idAsyncNetwork::server.GetNumClients();
	if ( clientsBeforePopulation != 1 ) {
		common->Warning(
			"arena campaign: expected one local player before bot population, found %d",
			clientsBeforePopulation );
		ReturnToBrowserAfterStartFailure( ARENA_FAILURE_SERVER );
		return;
	}
	int botsAdded = 0;
	for ( int i = 0; i < match->bots.Num(); i++ ) {
		const int skill = idMath::ClampInt(
			1, 5, match->baseSkill + difficulty - 3 + match->bots[i].skillOffset );
		const int clientsBeforeBot = idAsyncNetwork::server.GetNumClients();
		cmdSystem->BufferCommandText(
			CMD_EXEC_NOW,
			va( "addbot \"%s\" %d exact\n", match->bots[i].name.c_str(), skill ) );
		if ( idAsyncNetwork::server.GetNumClients() != clientsBeforeBot + 1 ) {
			break;
		}
		botsAdded++;
	}

	if ( botsAdded != match->bots.Num() ||
		 idAsyncNetwork::server.GetNumClients() != clientsBeforePopulation + match->bots.Num() ) {
		common->Warning(
			"arena campaign: match %d added only %d of %d authored bots",
			ui_arenaActiveMatch.GetInteger(), botsAdded, match->bots.Num() );

		ReturnToBrowserAfterStartFailure( ARENA_FAILURE_BOTS );
		return;
	}

	state->botsPopulated = true;
	state->matchDifficulty = difficulty;
	common->Printf(
		"arena campaign: populated match %d with %d bots at difficulty %d\n",
		ui_arenaActiveMatch.GetInteger(), match->bots.Num(), difficulty );
}

/*
================
idArenaCampaign::QueueFailedCompletionCleanup

game_mp reports a result only once. If that handoff is malformed or no longer
matches the live token, fail closed outside its RunFrame callback so the Arena
server and temporary CVar transaction cannot be stranded in NEXTGAME.
================
*/
void idArenaCampaign::QueueFailedCompletionCleanup() {
	if ( state == NULL || !IsActive() || state->completionPending ) {
		return;
	}

	state->completionPending = true;
	state->pendingToken = 0;
	state->pendingOutcome = ARENA_OUTCOME_LOSS;
	state->pendingPlayerScore = 0;
	state->pendingOpponentScore = 0;
	state->transitionPhase = ARENA_TRANSITION_RETURN;
	state->transitionDeadlineMsec = 0;
	cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "arenaCampaignFinish\n" );
}

/*
================
ArenaParseCompletion

The arenaComplete arguments, strictly parsed: token, outcome, both scores and
the optional award mask.
================
*/
static bool ArenaParseCompletion( const idCmdArgs &args, int &token, int &outcome, int &playerScore,
								  int &opponentScore, int &awards ) {
	token = 0;
	outcome = ARENA_OUTCOME_LOSS;
	playerScore = 0;
	opponentScore = 0;
	awards = 0;
	// The optional sixth argument is a bit mask of the end-game awards the player
	// earned.  It is accepted as optional on purpose: a game_mp built before the
	// framework learned about awards must keep working, and more importantly a
	// game_mp built after it must not be able to trip the malformed-command path
	// below, which fails closed by fabricating a token-0 loss and destroying a
	// legitimate result.  Absent means "no awards", never "reject".
	const bool arity = args.Argc() == 5 || args.Argc() == 6;
	return arity && idStr::Icmp( args.Argv( 0 ), "arenaComplete" ) == 0 &&
		ArenaParseBoundedInteger( args.Argv( 1 ), 1, ARENA_TOTAL_MATCHES, token ) &&
		ArenaParseBoundedInteger( args.Argv( 2 ), ARENA_OUTCOME_LOSS, ARENA_OUTCOME_ABORTED, outcome ) &&
		ArenaParseBoundedInteger( args.Argv( 3 ), -9999, 9999, playerScore ) &&
		ArenaParseBoundedInteger( args.Argv( 4 ), -9999, 9999, opponentScore ) &&
		( args.Argc() == 5 || ArenaParseBoundedInteger( args.Argv( 5 ), 0, ARENA_AWARD_MASK, awards ) );
}

/*
================
idArenaCampaign::AcceptCompletion
================
*/
void idArenaCampaign::AcceptCompletion( int token, int outcome, int playerScore, int opponentScore, int awards ) {
	state->completionPending = true;
	state->pendingToken = token;
	state->pendingOutcome = outcome;
	state->pendingPlayerScore = playerScore;
	state->pendingOpponentScore = opponentScore;
	state->pendingAwards = awards & ARENA_AWARD_MASK;
	state->transitionPhase = ARENA_TRANSITION_RETURN;
	state->transitionDeadlineMsec = 0;
	const char *outcomeName = outcome == ARENA_OUTCOME_WIN ? "win" :
		( outcome == ARENA_OUTCOME_DRAW ? "draw" :
		  ( outcome == ARENA_OUTCOME_ABORTED ? "aborted" : "loss" ) );
	common->Printf( "arena campaign: accepted result token %d (%s, %d-%d)\n",
		token,
		outcomeName,
		state->pendingPlayerScore,
		state->pendingOpponentScore );
}

/*
================
idArenaCampaign::QueueCompletion
================
*/
bool idArenaCampaign::QueueCompletion( const idCmdArgs &args ) {
	int token = 0;
	int outcome = ARENA_OUTCOME_LOSS;
	int playerScore = 0;
	int opponentScore = 0;
	int awards = 0;
	if ( !ArenaParseCompletion( args, token, outcome, playerScore, opponentScore, awards ) ) {
		common->Warning( "arena campaign: rejected malformed completion command" );
		QueueFailedCompletionCleanup();
		return false;
	}

	if ( state == NULL || !IsActive() || state->completionPending ) {
		common->DPrintf( "arena campaign: ignored completion token %d outside its active match\n", token );
		return false;
	}
	if ( !state->botsPopulated || token != ui_arenaActiveMatch.GetInteger() ||
		 ArenaMatchForToken( state, token ) == NULL ) {
		common->Warning( "arena campaign: result token %d did not match its active transaction", token );
		QueueFailedCompletionCleanup();
		return false;
	}

	AcceptCompletion( token, outcome, playerScore, opponentScore, awards );

	// Finish outside the game-frame callback, but ahead of older buffered work.
	// Appending here can leave an accepted result behind a long wait/script (or
	// even a queued quit), preventing campaign cleanup and CVar restoration.
	cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "arenaCampaignFinish\n" );
	return true;
}

/*
================
idArenaCampaign::TakeDecidedResult

The end-of-match ceremony presents a result the match has already decided; it
is not part of the contest. When the match is left or the game quits during
it, the result game_mp published (ui_arenaDecidedResult) is accepted as the
handoff would have been. True when a result is pending completion, accepted
here or by the handoff already.
================
*/
bool idArenaCampaign::TakeDecidedResult() {
	if ( state == NULL || !IsActive() ) {
		return false;
	}
	if ( state->completionPending ) {
		return true;
	}
	const idStr decided = ui_arenaDecidedResult.GetString();
	if ( decided.IsEmpty() ) {
		return false;
	}
	const idCmdArgs args( va( "arenaComplete %s", decided.c_str() ), false );
	int token = 0;
	int outcome = ARENA_OUTCOME_LOSS;
	int playerScore = 0;
	int opponentScore = 0;
	int awards = 0;
	if ( !ArenaParseCompletion( args, token, outcome, playerScore, opponentScore, awards ) ||
		 outcome == ARENA_OUTCOME_ABORTED || !state->botsPopulated ||
		 token != ui_arenaActiveMatch.GetInteger() || ArenaMatchForToken( state, token ) == NULL ) {
		common->Warning( "arena campaign: ignored decided result '%s'", decided.c_str() );
		return false;
	}
	common->Printf( "arena campaign: match %d left during its ceremony; keeping its decided result\n", token );
	AcceptCompletion( token, outcome, playerScore, opponentScore, awards );
	return true;
}

/*
================
idArenaCampaign::FinishDecidedResult

Leaving during the ceremony: the decided result is recorded and reported in
the browser, as skipping the ceremony would have done. False when there is
none, and the match is abandoned as before.
================
*/
bool idArenaCampaign::FinishDecidedResult() {
	if ( !TakeDecidedResult() ) {
		return false;
	}
	FinishPendingCompletion();
	return true;
}

/*
================
idArenaCampaign::RecordDecidedResult

Quitting, or an error dropping the session, during the ceremony: the decided
result is recorded so the configuration written next keeps it. Nothing is
presented; AbortMatch then restores the match's temporary settings.
================
*/
void idArenaCampaign::RecordDecidedResult() {
	if ( TakeDecidedResult() && RecordPendingCompletion() ) {
		state->completionPending = false;
	}
}

/*
================
idArenaCampaign::RecordPendingCompletion

Records the accepted result: progression, the match's best result and the
report's fields. False, with nothing recorded, when it no longer matches the
live match.
================
*/
bool idArenaCampaign::RecordPendingCompletion() {
	if ( !IsActive() || state->pendingToken != ui_arenaActiveMatch.GetInteger() ) {
		common->Warning( "arena campaign: discarded a completion with inconsistent match state" );
		return false;
	}

	int tier = 0;
	int match = 0;
	const arenaMatch_t *completedMatch = ArenaMatchForToken( state, state->pendingToken, &tier, &match );
	if ( completedMatch == NULL ) {
		common->Warning( "arena campaign: completed match token no longer exists" );
		return false;
	}

	ArenaRepairPersistedProgress();
	const int previousProgress = ui_arenaProgress.GetInteger();
	int progress = previousProgress;
	const bool won = state->pendingOutcome == ARENA_OUTCOME_WIN;
	const bool newlyCompleted = won && !ArenaMatchComplete( previousProgress, tier, match );

	// The match's best result. Only a contested result competes: an aborted
	// match records nothing, and an abandoned one never reaches this point.
	// NEW BEST is shown only when a result already on the record is beaten.
	bool newBest = false;
	if ( state->pendingOutcome != ARENA_OUTCOME_ABORTED ) {
		arenaBestResult_t bestResults[ARENA_TOTAL_MATCHES];
		ArenaReadBestResults( ui_arenaBest.GetString(), previousProgress, bestResults );
		const int matchIndex = ArenaMatchIndex( tier, match );
		const arenaBestResult_t record = ArenaEffectiveBest( bestResults[matchIndex], previousProgress, tier, match );
		arenaBestResult_t candidate;
		candidate.recorded = true;
		candidate.outcome = state->pendingOutcome;
		candidate.playerScore = state->pendingPlayerScore;
		candidate.opponentScore = state->pendingOpponentScore;
		candidate.difficulty = state->matchDifficulty >= 1 && state->matchDifficulty <= 5
			? state->matchDifficulty : ui_arenaDifficulty.GetInteger();
		if ( !ArenaBestResultValid( candidate ) ) {
			common->Warning(
				"arena campaign: match %d result (%d, %d-%d) cannot be a best result",
				state->pendingToken, candidate.outcome, candidate.playerScore, candidate.opponentScore );
		} else if ( ArenaBestResultBeats( candidate, record ) ) {
			newBest = bestResults[matchIndex].recorded;
			bestResults[matchIndex] = candidate;
			ui_arenaBest.SetString( ArenaWriteBestResults( bestResults ) );
			common->Printf(
				"arena campaign: %s for match %d: %s\n",
				newBest ? "new best result" : "first best result",
				state->pendingToken, ui_arenaBest.GetString() );
		} else {
			common->Printf(
				"arena campaign: match %d result did not beat its best result\n",
				state->pendingToken );
		}
	}

	if ( newlyCompleted ) {
		progress |= ArenaMatchBit( tier, match );
		ui_arenaProgress.SetInteger( progress );
	}
	const bool unlockedCampaign = newlyCompleted &&
		tier == ARENA_TIER_COUNT - 1 && match == ARENA_BOSS_MATCH;
	const bool unlockedTier = newlyCompleted && match == ARENA_BOSS_MATCH &&
		tier + 1 < ARENA_TIER_COUNT &&
		!ArenaTierUnlocked( previousProgress, tier + 1 ) && ArenaTierUnlocked( progress, tier + 1 );
	const bool unlockedBoss = newlyCompleted && !unlockedCampaign && !unlockedTier &&
		!ArenaBossUnlocked( previousProgress, tier ) && ArenaBossUnlocked( progress, tier );

	// A match that ended without a contest records nothing. It is reported
	// like a failed launch, but it keeps its token so the card can name the
	// match and offer it again.
	const bool aborted = state->pendingOutcome == ARENA_OUTCOME_ABORTED;
	state->result = won ? 1 : ( aborted ? 3 : ( state->pendingOutcome == ARENA_OUTCOME_DRAW ? 4 : 2 ) );
	state->resultFailure = aborted ? ARENA_FAILURE_OPPONENTS : ARENA_FAILURE_NONE;
	state->resultToken = state->pendingToken;
	state->resultWon = won;
	state->resultPlayerScore = aborted ? ARENA_SCORE_UNAVAILABLE : state->pendingPlayerScore;
	state->resultOpponentScore = aborted ? ARENA_SCORE_UNAVAILABLE : state->pendingOpponentScore;
	state->resultAwards = aborted ? 0 : state->pendingAwards;
	state->resultPreviousProgress = previousProgress;
	state->resultTier = tier;
	state->resultMatch = match;
	state->resultNewlyCompleted = newlyCompleted;
	state->resultUnlockedBoss = unlockedBoss;
	state->resultUnlockedTier = unlockedTier;
	state->resultUnlockedCampaign = unlockedCampaign;
	state->resultNewBest = newBest;
	state->resultRevealPending = true;
	state->resultTierName = state->tiers[tier].name;
	state->resultMatchName = completedMatch->name;
	state->resultUnlock.Clear();
	if ( newlyCompleted ) {
		if ( unlockedCampaign ) {
			state->resultUnlock = "#str_42047";
		} else if ( unlockedTier ) {
			state->resultUnlock = "#str_42046";
		} else if ( unlockedBoss ) {
			state->resultUnlock = "#str_42045";
		}
	}
	ui_arenaSelectedTier.SetInteger( tier );
	ui_arenaSelectedMatch.SetInteger( match );
	return true;
}

/*
================
idArenaCampaign::FinishPendingCompletion
================
*/
void idArenaCampaign::FinishPendingCompletion() {
	if ( state == NULL || !state->completionPending ) {
		return;
	}
	if ( !RecordPendingCompletion() ) {
		ReturnToBrowserAfterStartFailure( ARENA_FAILURE_RESULT );
		return;
	}

	state->completionPending = false;
	state->pendingToken = 0;
	state->pendingOutcome = ARENA_OUTCOME_LOSS;
	state->pendingPlayerScore = 0;
	state->pendingOpponentScore = 0;
	state->pendingAwards = 0;
	state->botsPopulated = false;
	state->matchDifficulty = 0;
	state->transitionPhase = ARENA_TRANSITION_RESULT;
	state->transitionDeadlineMsec = 0;
	state->launchToken = 0;
	state->resetConfirmation = false;
	ui_arenaCampaignActive.SetBool( false );
	ui_arenaActiveMatch.SetInteger( 0 );

#ifndef ID_DEDICATED
	// game_mp has already held its authoritative GAMEREVIEW beat before the
	// handoff. Fade that final board to black before dismantling the server so
	// the browser reveal does not cut directly out of the live render world.
	// Keep si_arenaCampaign live through CompleteWipe: StartWipe draws once to
	// capture its source, and game_mp uses that serverInfo token to retain the
	// showcase camera and suppress the ordinary multiplayer HUD.
	if ( sessLocal.IsMapSpawned() && renderSystem != NULL && renderSystem->IsOpenGLRunning() ) {
		sessLocal.StartWipe( "gfx/wipes/fade", true );
		sessLocal.CompleteWipe();
		state->returnWipeHeld = true;
	}
#endif
	if ( state->returnWipeHeld ) {
		sessLocal.StopPreservingWipe();
	} else {
		sessLocal.Stop();
	}
	cvarSystem->SetCVarInteger( "si_arenaCampaign", 0 );
	ArenaClearMatchPresentation();
	ArenaRestoreSavedCVars( state );
	sessLocal.StartMenu( false );
	if ( soundSystem != NULL ) {
		soundSystem->SetMute( false );
	}
	OpenBrowser();
	if ( state->returnWipeHeld ) {
		// The result event and its black curtain are now authoritative. Release
		// the held world wipe so the GUI can perform the visible reveal.
		sessLocal.ClearWipe();
		state->returnWipeHeld = false;
	}
}

/*
================
idArenaCampaign::AbortMatch
================
*/
void idArenaCampaign::AbortMatch() {
	if ( state == NULL ) {
		return;
	}
	// An explicit disconnect/reload abandons any still-open result panel. Clear
	// the presentation atomically instead of leaving result != 0 without the
	// reveal event that makes the overlay interactive. Metadata intentionally
	// survives after DismissResult because that method has already set result 0.
	if ( state->result != 0 ) {
		ArenaClearResultMetadata( state );
	}
	if ( state->returnWipeHeld ) {
		sessLocal.ClearWipe();
		state->returnWipeHeld = false;
	}

	state->completionPending = false;
	state->pendingToken = 0;
	state->pendingOutcome = ARENA_OUTCOME_LOSS;
	state->pendingPlayerScore = 0;
	state->pendingOpponentScore = 0;
	state->botsPopulated = false;
	state->matchDifficulty = 0;
	state->moduleTransitionPending = false;
	state->transitionPhase = ARENA_TRANSITION_IDLE;
	state->transitionDeadlineMsec = 0;
	state->launchToken = 0;
	state->resultRevealPending = false;
	ui_arenaCampaignActive.SetBool( false );
	ui_arenaActiveMatch.SetInteger( 0 );
	if ( cvarSystem->Find( "si_arenaCampaign" ) != NULL ) {
		cvarSystem->SetCVarInteger( "si_arenaCampaign", 0 );
	}
	ArenaClearMatchPresentation();
	ArenaRestoreSavedCVars( state );
}
