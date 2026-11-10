// RAVEN BEGIN
// ddynerman: note that this file is no longer merged with Doom3 updates
//
// MERGE_DATE 09/30/2004

#ifndef __MULTIPLAYERGAME_H__
#define	__MULTIPLAYERGAME_H__

/*
===============================================================================

	Quake IV multiplayer

===============================================================================
*/

#include "mp/GameTypeIds.h"
#include "mp/Buying.h"
#include "mp/HitFeedback.h"
#include "mp/ObjectiveMarkers.h"
#include "mp/MvdMarkers.h"
#include "../game/gamesys/HudLines.h"
#include "mp/match/MatchRules.h"
#include "mp/match/MatchSeries.h"
#include "mp/match/MatchSeriesRecovery.h"
#include "mp/match/MatchSeriesReport.h"
#include "mp/match/MatchSeriesReportStorage.h"
#include "mp/match/MatchSession.h"
#include "mp/match/MatchTeamCommunication.h"
#include "mp/match/MatchTerminationPolicy.h"
#include "mp/match/MatchTeams.h"
#include "mp/match/MatchAuthentication.h"
#include "mp/match/MatchEvidence.h"
#include "mp/match/MatchEvidenceObserver.h"
#include "mp/match/MatchEvidenceStorage.h"
#include "mp/match/MatchDisclosurePolicy.h"
#include "mp/match/MatchItemTiming.h"
#include "mp/match/MatchOperations.h"
#include "mp/match/MatchProposal.h"
#include "mp/match/MatchView.h"
#include "mp/match/MatchControlModel.h"
class idPlayer;
class idItem;
class rvCTF_AssaultPoint;
class rvItemCTFFlag;

// jmarshall - the engine file system interface only exposes map decls by
// index; this resolves a map path the way the old GetMapDecl( name ) did.
const idDict *MultiplayerResolveMapDecl( const char *mapPath );
// shouchard:  server admin command types
typedef enum {
	SERVER_ADMIN_KICK,
	SERVER_ADMIN_BAN,
	SERVER_ADMIN_REMOVE_BAN,
	SERVER_ADMIN_FORCE_SWITCH,
} serverAdmin_t;

// shouchard:  vote struct for packing up interface values to be handled later
//             note that we have two mechanisms for dealing with vote data that
//             should be consolidated:  this one that handles the interface and
//             multi-field votes and the one that handles console commands and
//             single line votes.  
struct voteStruct_t {
	int				m_fieldFlags;	// flags for which fields are valid
	int				m_kick;			// id of the player
	idStr			m_map;			// name of the map
	int				m_gameType;		// game type enum
	int				m_timeLimit;
	int				m_fragLimit;
	int				m_tourneyLimit;
	int				m_captureLimit;
	int				m_buying;
	int				m_teamBalance;
	int				m_controlTime;
	// restart is a flag only
	// nextmap is a flag only but we don't technically support it (but doom had it so it's here)
};

typedef enum {
	VOTEFLAG_RESTART		= 0x0001,
	VOTEFLAG_BUYING			= 0x0002,
	VOTEFLAG_TEAMBALANCE	= 0x0004,
	VOTEFLAG_SHUFFLE		= 0x0008,
	VOTEFLAG_KICK			= 0x0010,
	VOTEFLAG_MAP			= 0x0020,
	VOTEFLAG_GAMETYPE		= 0x0040,
	VOTEFLAG_TIMELIMIT		= 0x0080,
	VOTEFLAG_TOURNEYLIMIT	= 0x0100,
	VOTEFLAG_CAPTURELIMIT	= 0x0200,
	VOTEFLAG_FRAGLIMIT		= 0x0400,
	VOTEFLAG_CONTROLTIME	= 0x0800,
} voteFlag_t;

#define NUM_VOTES			12	// VOTEFLAG_CONTROLTIME is bit 11 and must participate in vote masking
#define VOTEFLAG_ALL			( ( 1 << NUM_VOTES ) - 1 )
#define MAX_PRINT_LEN 128

// more compact than a chat line
typedef enum {
	MSG_SUICIDE = 0,
	MSG_KILLED,
	MSG_KILLEDTEAM,
	MSG_DIED,
	MSG_VOTE,
	MSG_VOTEPASSED,
	MSG_VOTEFAILED,
	MSG_SUDDENDEATH,
	MSG_FORCEREADY,
	MSG_JOINEDSPEC,
	MSG_TIMELIMIT,
	MSG_FRAGLIMIT,
	MSG_CAPTURELIMIT,
	MSG_TELEFRAGGED,
	MSG_JOINTEAM,
	MSG_HOLYSHIT,
	MSG_COUNT
} msg_evt_t;

typedef enum {
	PLAYER_VOTE_NONE,
	PLAYER_VOTE_NO,
	PLAYER_VOTE_YES,
	PLAYER_VOTE_WAIT	// mark a player allowed to vote
} playerVote_t;

typedef enum {
	PRM_AUTO,
	PRM_SCORE,
	PRM_TEAM_SCORE,
	PRM_TEAM_SCORE_PLUS_SCORE,
	PRM_WINS
} playerRankMode_t;

enum announcerSound_t {
	// General announcements
	AS_GENERAL_ONE,
	AS_GENERAL_TWO,
	AS_GENERAL_THREE,
	AS_GENERAL_YOU_WIN,
	AS_GENERAL_YOU_LOSE,
	AS_GENERAL_FIGHT,
	AS_GENERAL_SUDDEN_DEATH,
	AS_GENERAL_VOTE_FAILED,
	AS_GENERAL_VOTE_PASSED,
	AS_GENERAL_VOTE_NOW,
	AS_GENERAL_ONE_FRAG,
	AS_GENERAL_TWO_FRAGS,
	AS_GENERAL_THREE_FRAGS,
	AS_GENERAL_ONE_MINUTE,
	AS_GENERAL_FIVE_MINUTE,
	AS_GENERAL_PREPARE_TO_FIGHT,
	AS_GENERAL_QUAD_DAMAGE,
	AS_GENERAL_REGENERATION,
	AS_GENERAL_HASTE,
	AS_GENERAL_INVISIBILITY,
	// DM announcements
	AS_DM_YOU_TIED_LEAD,
	AS_DM_YOU_HAVE_TAKEN_LEAD,
	AS_DM_YOU_LOST_LEAD,
	// Team announcements
	AS_TEAM_ENEMY_SCORES,
	AS_TEAM_YOU_SCORE,
	AS_TEAM_TEAMS_TIED,
	AS_TEAM_STROGG_LEAD,
	AS_TEAM_MARINES_LEAD,
	AS_TEAM_JOIN_MARINE,
	AS_TEAM_JOIN_STROGG,
	// CTF announcements
	AS_CTF_YOU_HAVE_FLAG,
	AS_CTF_YOUR_TEAM_HAS_FLAG,
	AS_CTF_ENEMY_HAS_FLAG,
	AS_CTF_YOUR_TEAM_DROPS_FLAG,
	AS_CTF_ENEMY_DROPS_FLAG,
	AS_CTF_YOUR_FLAG_RETURNED,
	AS_CTF_ENEMY_RETURNS_FLAG,
	// Tourney announcements
	AS_TOURNEY_ADVANCE,
	AS_TOURNEY_JOIN_ARENA_ONE,
	AS_TOURNEY_JOIN_ARENA_TWO,
	AS_TOURNEY_JOIN_ARENA_THREE,
	AS_TOURNEY_JOIN_ARENA_FOUR,
	AS_TOURNEY_JOIN_ARENA_FIVE,
	AS_TOURNEY_JOIN_ARENA_SIX,
	AS_TOURNEY_JOIN_ARENA_SEVEN,
	AS_TOURNEY_JOIN_ARENA_EIGHT,
	AS_TOURNEY_JOIN_ARENA_WAITING,
	AS_TOURNEY_DONE,
	AS_TOURNEY_START,
	AS_TOURNEY_ELIMINATED,
	AS_TOURNEY_WON,
	AS_TOURNEY_PRELIMS,
	AS_TOURNEY_QUARTER_FINALS,
	AS_TOURNEY_SEMI_FINALS,
	AS_TOURNEY_FINAL_MATCH,
	AS_GENERAL_TEAM_AMMOREGEN,
	AS_GENERAL_TEAM_DOUBLER,
	AS_NUM_SOUNDS
};

// openQ4 BEGIN
// Announcer events for the modes carried over from Quake Live.  Quake 4 ships
// no round, overtime or elimination voice-overs, and openQ4's rule is to run
// on the retail assets, so these are aliases onto stock clips rather than new
// announcerSound_t values.  announcerSoundDefs[] is index-parallel with no
// compile-time check, so aliasing also avoids the easiest way to break it.
// Point these at dedicated shaders if a voice pack ever ships.
const announcerSound_t AS_ROUND_PREPARE			= AS_GENERAL_PREPARE_TO_FIGHT;
const announcerSound_t AS_ROUND_FIGHT			= AS_GENERAL_FIGHT;
const announcerSound_t AS_ROUND_YOU_WIN			= AS_TEAM_YOU_SCORE;
const announcerSound_t AS_ROUND_YOU_LOSE		= AS_TEAM_ENEMY_SCORES;
const announcerSound_t AS_ROUND_DRAW			= AS_TEAM_TEAMS_TIED;
const announcerSound_t AS_MATCH_OVERTIME		= AS_GENERAL_SUDDEN_DEATH;
const announcerSound_t AS_MATCH_LAST_STANDING	= AS_GENERAL_ONE_FRAG;
const announcerSound_t AS_OBJECTIVE_ATTACKED	= AS_CTF_ENEMY_HAS_FLAG;
const announcerSound_t AS_OBJECTIVE_SECURED		= AS_CTF_YOUR_FLAG_RETURNED;

// Localized team name and colour escape, used by every message that names a
// team.  Quake 4's teams are Marine and Strogg, which stand in for Quake
// Live's red and blue throughout the port.
const char *	MPLocalizedTeamName( int team );
const char *	MPTeamColor( int team );
// openQ4 END

const int VOTEMAPS_WAITING_MAPLIST		= (1<<0);
const int VOTEMAPS_WAITING_SAMAPLIST	= (1<<1);
const int VOTEMAPS_WAITING_LISTMAPS		= (1<<2);
// openQ4: the multiplayer card's Vote page asked for the server's maps
const int VOTEMAPS_WAITING_RETAINED		= (1<<3);

typedef struct mpPlayerState_s {
	int				ping;			// player ping
	int				fragCount;		// kills
	int				teamFragCount;	// teamplay awards
	int				deadZoneScore;  // Score in dead zone
	int				wins;
	playerVote_t	vote;			// player's vote
	bool			scoreBoardUp;	// toggle based on player scoreboard button, used to activate de-activate the scoreboard gui
	bool			ingame;
} mpPlayerState_t;

const int MAX_INSTANCES = 8;

const int NUM_CHAT_NOTIFY	= 5;
const int CHAT_FADE_TIME	= 400;
const int FRAGLIMIT_DELAY	= 2000;
const int CAPTURELIMIT_DELAY = 750;

const int MP_PLAYER_MINFRAGS = -100;
const int MP_PLAYER_MAXFRAGS = 999;
const int MP_PLAYER_MAXWINS	= 100;
const int MP_PLAYER_MAXPING	= 999;

const int MP_PLAYER_MAXKILLS = 999;
const int MP_PLAYER_MAXDEATHS = 999;

// openQ4: team scores ride the snapshot as a short.  Domination accrues a
// point per control point per tick, so an unclamped total could wrap.
const int MP_TEAM_MAXSCORE = 30000;

const int MAX_AP = 5;

const int CHAT_HISTORY_SIZE = 32768;
const int RCON_HISTORY_SIZE = 4096;

const int KILL_NOTIFICATION_LEN = 256;
//RAVEN BEGIN
//asalmon: update stats for Xenon
#ifdef _XENON
const int XENON_STAT_UPDATE_INTERVAL = 1000;
#endif

const int ASYNC_PLAYER_FRAG_BITS = -idMath::BitsForInteger( MP_PLAYER_MAXFRAGS - MP_PLAYER_MINFRAGS );	// player can have negative frags
const int ASYNC_PLAYER_WINS_BITS = idMath::BitsForInteger( MP_PLAYER_MAXWINS );
const int ASYNC_PLAYER_PING_BITS = idMath::BitsForInteger( MP_PLAYER_MAXPING );
const int ASYNC_PLAYER_INSTANCE_BITS = idMath::BitsForInteger( MAX_INSTANCES );
const int ASYNC_PLAYER_DEATH_BITS = idMath::BitsForInteger( MP_PLAYER_MAXDEATHS );
const int ASYNC_PLAYER_KILL_BITS = idMath::BitsForInteger( MP_PLAYER_MAXKILLS );
//RAVEN END
//RITUAL BEGIN
const int MAX_TEAM_POWERUPS = 5;
//RITUAL END
// ddynerman: game state
#include "mp/GameState.h"
// openQ4: gametypes carried over from Quake Live
#include "mp/RoundGameState.h"
#include "mp/RoundModes.h"
#include "mp/Duel.h"

typedef struct mpChatLine_s {
	idStr			line;
	short			fade;			// starts high and decreases, line is removed once reached 0
} mpChatLine_t;

typedef struct mpBanInfo_s {
	idStr			name;
	char			guid[ CLIENT_GUID_LENGTH ];
//	unsigned char	ip[ 15 ];
} mpBanInfo_t;

class idPhysics_Player;

// currentMenu's value while the single-player Arena's pause menu is open.
static const int MP_MENU_ARENA_PAUSE = 5;

class idMultiplayerGame {

	// rvGameState manages our state
	friend class rvGameState;

public:

					idMultiplayerGame();

	void			Shutdown( void );

	// resets everything and prepares for a match
	void			Reset( void );

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
	// Made this public so that level heap can be emptied.
	void			Clear( void );
// RAVEN END

	// setup local data for a new player
	void			SpawnPlayer( int clientNum );

	// Run the MP Game
	void			Run( void );

	// Run the local client
	void			ClientRun( void );
	void			ClientEndFrame( void );

	// Run common code (client & server)
	void			CommonRun( void );

	// draws mp hud, scoredboard, etc.. 
	bool			Draw( int clientNum );
	

	// updates a player vote
	void			PlayerVote( int clientNum, playerVote_t vote );

	// updates frag counts and potentially ends the match in sudden death
	void			PlayerDeath( idPlayer *dead, idPlayer *killer, int methodOfDeath );

	void			AddChatLine( const char *fmt, ... ) id_attribute((format(printf,2,3)));
	void			PrintChatLine( const char *message, const bool teamChat );

// RITUAL BEGIN
// squirrel: Mode-agnostic buymenus
	void			OnBuyModeTeamVictory( int winningTeam );
// squirrel: added DeadZone multiplayer mode
	void			OnDeadZoneTeamVictory( int winningTeam );
// RITUAL END

	void			UpdateMainGui( void );
// RAVEN BEGIN
// bdube: global pickup sounds (powerups, etc)
	// Global item acquire sounds 
	void			PlayGlobalItemAcquireSound ( int entityDefIndex );

	bool			CanTalk( idPlayer *from, idPlayer *to, bool echo );
	void			ReceiveAndForwardVoiceData( int clientNum, const idBitMsg &inMsg, int messageType );

// jscott: game side voice comms
	void			XmitVoiceData( void );
	void			ReceiveAndPlayVoiceData( const idBitMsg &inMsg );
	void			ResetVoiceState( int client = -1 );
	static void		ServerVoiceMute_f( const idCmdArgs& args );
	static void		ServerAdminTarget_f( const idCmdArgs& args );
	static void		ServerVoiceStats_f( const idCmdArgs& args );

// jshepard: selects a map at random that will run with the current game type
	bool			PickMap( idStr gameType, bool checkOnly = false );
	void			ClearVote( int clientNum = -1 );
	void			ResetRconGuiStatus( void );

// RAVEN END

	idUserInterface*	StartMenu( void );
	void			ShowInitialJoinMenu( void );
	void			UpdateJoinScreenGui( void );
	void			SetJoinScreenSoftFocus( bool enabled );
	// openQ4: the retained menu card (section 14.18). The session covers
	// mainGui with it and asks for its mp.* keys; the menu, its state and its
	// commands stay here, and mainGui stops drawing while covered.
	void			SetRetainedMenuCover( bool covered, idUserInterface *card );
	void			PublishRetainedMenu( idUserInterface *card );
	bool			RetainedMenuCovered( void ) const { return retainedMenuCovered; }
	// The Arena pause menu's match block, for the stock menu or its card.
	void			PublishArenaPauseMenu( idUserInterface *gui, bool retained );
	// The Arena pause menu's "arenaPause <action>" commands.
	const char *	HandleArenaPauseCommand( const char *action );
	idStr			RetainedScoreLine( void );
	// The card's commands ("retained ..."); true when the menu closes.
	bool			HandleRetainedMenuCommand( const idCmdArgs &args, int &icmd );
	// Which card the menu opens: Welcome while the player has not answered the
	// join offer (the offer stands, or ui_joined is still 0 while spectating),
	// Escape otherwise.
	bool			RetainedMenuWelcome( void );
	// What joining a team would refuse, as the stock CheckTeamBalance tells
	// it ("#str_202039" to "#str_202042"), or NULL; and the team "join auto"
	// picks.
	const char *	TeamJoinRefusal( int team );
	int				AutoJoinTeam( void );
	// The map si_mapCycle plays after the current one, or "" without a cycle.
	idStr			PredictNextMap( void ) const;

	const char*		HandleGuiCommands( const char *menuCommand );

	void			WriteToSnapshot( idBitMsgDelta &msg ) const;
	void			ReadFromSnapshot( const idBitMsgDelta &msg );

	void			ShuffleTeams( void );
	void			SetGameType( void );
	void			SetMatchStartedTime( int time ) { matchStartedTime = time; }

	rvGameState*	GetGameState( void );

	// One authoritative competitive aggregate owns phase, round, clocks and
	// frozen rules.  rvGameState remains the gameplay adapter, but may mutate
	// its legacy state only after these methods accept the same transition.
	const mpMatchSession &GetMatchSession( void ) const { return matchSession; }
	const mpCompetitiveRules &GetCompetitiveRules( void ) const { return matchRules; }
	const mpCompetitionSeries &GetCompetitionSeries( void ) const { return matchSeries; }
	const mpSessionView *GetClientMatchView( void ) const {
		return clientMatchViewValid ? &clientMatchView : NULL;
	}
	// Resolve both current slot-generation bindings on every attempt.  This is
	// the authoritative camera gate; recipient views are discovery data only.
	bool			CanSpectatorFollow( int observerSlot, int targetSlot ) const;
	// Placed, respawning major items report only authoritative lifecycle
	// transitions.  The registry derives identity and recipient disclosure;
	// item code never writes client UI or network state directly.
	void			ObserveCompetitiveItemPickup( const idItem *item,
						float respawnSeconds );
	void			ObserveCompetitiveItemAvailable( const idItem *item );
	// Called once at the authoritative frame boundary, before any gameplay
	// input, events or entity thinking.  A pending pause therefore takes effect
	// atomically for the whole simulation frame.
	void			BeginCompetitiveFrame( void );
	bool			IsGameplayFrozen( void ) const;
	bool			CanCommitMatchPhaseTransition( mpGameState_t newState ) const;
	bool			CommitMatchPhaseTransition( mpGameState_t newState );
	bool			CommitMatchPhaseTransition( mpGameState_t newState,
						mpMatchTransitionReason_t reason, mpParticipantId authorizer );
	bool			CommitMatchPhaseTransition( mpGameState_t newState,
						mpMatchTransitionReason_t reason, mpParticipantId authorizer,
						int forfeitingSide );
	bool			CommitMatchRoundTransition( roundState_t newState );
	bool			ApplyRoundTeamAssignment( idPlayer *player, int team, bool respawn );
	bool			BeginMatchOvertimePeriod( void );
	void			ServerReceiveMatchOperation( int clientNum, const idBitMsg &msg );
	void			ClientReceiveMatchOperationResult( const idBitMsg &msg );
	void			ClientReceiveMatchView( const idBitMsg &msg );
	void			ClientReceiveRefereeAuthChallenge( const idBitMsg &msg );
	bool			SubmitMatchOperation( mpMatchOperationRequest_t &request );
	bool			RequestRefereeAuthentication( const char *password );


	void			PrintMessageEvent( int to, msg_evt_t evt, int parm1 = -1, int parm2 = -1 );
	void			PrintMessage( int to, const char* message );

// openQ4 BEGIN
// Server driven centre-screen notice.  Quake 4 had no such channel: every big
// message was derived client side from an observed state delta, which cannot
// express text that varies with who did what to whom.  The payload is a
// localization id plus up to two typed parameters, so nothing on the wire is
// pre-translated.
	typedef enum {
		CPARM_NONE = 0,		// slot unused
		CPARM_INT,			// printed as a number
		CPARM_CLIENT,		// resolved to that client's name, with team colour
		CPARM_TEAM			// resolved to the localized team name
	} centerPrintParm_t;

	// to == -1 broadcasts.  strId must be a #str_ id, never display text.
	void			CenterPrint( int to, const char *strId, bool persist = false );
	void			CenterPrint( int to, const char *strId, centerPrintParm_t type1, int parm1, bool persist = false );
	void			CenterPrint( int to, const char *strId, centerPrintParm_t type1, int parm1, centerPrintParm_t type2, int parm2, bool persist = false );
	void			CenterPrintTeam( int team, const char *strId, centerPrintParm_t type1 = CPARM_NONE, int parm1 = 0, bool persist = false );
	void			CenterPrintTeam( int team, const char *strId, centerPrintParm_t type1, int parm1, centerPrintParm_t type2, int parm2, bool persist = false );
	void			ReceiveCenterPrint( const idBitMsg &msg );

	// Server driven announcer cue.  ScheduleAnnouncerSound only ever reaches the
	// machine it runs on, so a cue decided by server-only game logic - who was
	// left standing, whose round it was - never reached the player it was about.
	// to == -1 broadcasts.
	void			AnnounceTo( int to, announcerSound_t sound );
	void			AnnounceToTeam( int team, announcerSound_t sound );
	void			ReceiveAnnouncer( const idBitMsg &msg );

	// authoritative ready state, set from the client's reliable ready message
	void			ServerSetPlayerReady( int clientNum, bool ready );
	bool			IsManagedMatch( void ) const;
	bool			RotateManagedDuelQueue( int firstSlot, int secondSlot, int losingSlot );
	void			RecordManagedDuelResult( mpMatchTransitionReason_t reason,
						int forfeitingSide, mpParticipantId authorizer );
	bool			ResolveManagedDuelForfeitParticipants( int forfeitingSide,
						mpParticipantId authorizer, mpParticipantId &forfeiter,
						mpParticipantId &winner ) const;
	// Legacy server administration is intentionally unavailable while the
	// revisioned Match Control authority owns the session.  Command and GUI
	// adapters call this before touching cvars, maps, teams, or clients.
	bool			RejectManagedLegacyMutation( const char *action );
	bool			ServerReconcileManagedUserInfo( int clientNum, idDict &userInfo );
	static void		Ready_f( const idCmdArgs &args );
	static void		NotReady_f( const idCmdArgs &args );
	static void		ReadyUp_f( const idCmdArgs &args );
// openQ4 END

	void			DisconnectClient( int clientNum );
	static void		ForceReady_f( const idCmdArgs &args );
	static void		SeriesBind_f( const idCmdArgs &args );
	static void		Broadcaster_f( const idCmdArgs &args );
	static void		DropWeapon_f( const idCmdArgs &args );
	static void		MessageMode_f( const idCmdArgs &args );
	static void		VoiceChat_f( const idCmdArgs &args );
	static void		VoiceChatTeam_f( const idCmdArgs &args );


// RAVEN BEGIN
// shouchard:  added console commands to mute/unmute voice chat
	static void		VoiceMute_f( const idCmdArgs &args );
	static void		VoiceUnmute_f( const idCmdArgs &args );

// jshepard: command wrappers
	static void		ForceTeamChange_f( const idCmdArgs& args );
	static void		RemoveClientFromBanList_f( const idCmdArgs& args );
	
// autobalance helper for the guis
	static void		CheckTeamBalance_f( const idCmdArgs &args );

// activates the admin console when a rcon password challenge returns.
	void			ProcessRconReturn( bool success );
// RAVEN END

	typedef enum {
		VOTE_RESTART = 0,
		VOTE_TIMELIMIT,
		VOTE_FRAGLIMIT,
		VOTE_GAMETYPE,
		VOTE_KICK,
		VOTE_MAP,
		VOTE_BUYING,
		VOTE_NEXTMAP,
// RAVEN BEGIN
// shouchard:  added capturelimit, round limit, and autobalance to vote flags
		VOTE_CAPTURELIMIT,
		VOTE_ROUNDLIMIT,
		VOTE_AUTOBALANCE,
		VOTE_MULTIFIELD,	// all the "packed" vote functions
// RAVEN END
		VOTE_CONTROLTIME,
		VOTE_COUNT,
		VOTE_NONE
	} vote_flags_t;

	typedef enum {
		VOTE_UPDATE,
		VOTE_FAILED,
		VOTE_PASSED,	// passed, but no reset yet
		VOTE_ABORTED,
		VOTE_RESET		// tell clients to reset vote state
	} vote_result_t;

// RAVEN BEGIN
// shouchard:  added enum to remove magic numbers
	// openQ4: mirrors mpVoteGameTypeOrder[] in mp/GameTypes.cpp, which is the
	// authority.  The gametype menu dropdowns index this positionally, so it
	// is append-only.
	typedef enum {
		VOTE_GAMETYPE_DM = 0,
		VOTE_GAMETYPE_TOURNEY,
		VOTE_GAMETYPE_TDM,
		VOTE_GAMETYPE_CTF,
		VOTE_GAMETYPE_ARENA_CTF,
//RITUAL BEGIN
//
		VOTE_GAMETYPE_DEADZONE,
//RITUAL END
		VOTE_GAMETYPE_DUEL,
		VOTE_GAMETYPE_CA,
		VOTE_GAMETYPE_FREEZETAG,
		VOTE_GAMETYPE_REDROVER,
		VOTE_GAMETYPE_1F_CTF,
		VOTE_GAMETYPE_ARENA_1F_CTF,
		VOTE_GAMETYPE_OVERLOAD,
		VOTE_GAMETYPE_HARVESTER,
		VOTE_GAMETYPE_DOMINATION,
		VOTE_GAMETYPE_ATTACK_DEFEND,
		VOTE_GAMETYPE_COUNT
	} vote_gametype_t;
// RAVEN END

	void			SendMapList( int clientNum );
	void			ReadMapList( const idBitMsg &msg );

	static void		Vote_f( const idCmdArgs &args );
	static void		CallVote_f( const idCmdArgs &args );
	void			ClientCallVote( vote_flags_t voteIndex, const char *voteValue, int kickIdentity = 0 );
	void			ServerCallVote( int clientNum, const idBitMsg &msg );
	void			ClientStartVote( int clientNum, const char *voteString );
	void			ServerStartVote( int clientNum, vote_flags_t voteIndex, const char *voteValue );
// RAVEN BEGIN
// shouchard:  multiline vote support
	void			ClientUpdateVote( vote_result_t result, int yesCount, int noCount, const voteStruct_t &voteData );
// RAVEN END
	void			CastVote( int clientNum, bool vote );
	void			ExecuteVote( void );
// RAVEN BEGIN
// shouchard:  multiline vote handlers
	void			ClientCallPackedVote( const voteStruct_t &voteData, int kickIdentity = 0 );
	void			ServerCallPackedVote( int clientNum, const idBitMsg &msg );
	void			ClientStartPackedVote( int clientNum, const voteStruct_t &voteData );
	void			ServerStartPackedVote( int clientNum, const voteStruct_t &voteData );
	void			ExecutePackedVote( void );
	const char *	LocalizeGametype( void );
// RAVEN END

	void			WantKilled( int clientNum );
	int				NumActualClients( bool countSpectators, int *teamcount = NULL );
	void			DropWeapon( int clientNum );
	void			MapRestart( void );
	void			JoinTeam( const char* team );
	// called by idPlayer whenever it detects a team change (init or switch)
	void			SwitchToTeam( int clientNum, int oldteam, int newteam );
	
	bool			IsPureReady( void ) const;
	void			ProcessChatMessage( int clientNum, bool team, const char *name, const char *text,
									const char *sound, bool triggerBotReplies );
	void			ProcessVoiceChat( int clientNum, bool team, int index );
// RAVEN BEGIN
// shouchard:  added commands to mute/unmute voice chat
	void			ClientVoiceMute( int clientNum, bool mute );
	int				GetClientNumFromPlayerName( const char *playerName );
	void			ServerHandleVoiceMuting( int clientSrc, int clientDest, int identity, bool mute );
// shouchard:  fixing a bug in multiplayer where round timer sounds (5 minute
//             warning, etc.) don't go away at the end of the round.
	void			ClearAnnouncerSounds( void );
// shouchard:  server admin stuff
	typedef struct 
	{
		bool		restartMap;
		idStr		mapName;
		int			gameType;
		int			captureLimit;
		int			fragLimit;
		int			tourneyLimit;
		int			timeLimit;
		int			controlTime;
		bool		buying;
		bool		autoBalance;
		bool		shuffleTeams;
	} serverAdminData_t;

	void			HandleServerAdminBanPlayer( int clientNum );
	void			HandleServerAdminRemoveBan( const char * info );
	void			HandleServerAdminKickPlayer( int clientNum );
	void			HandleServerAdminForceTeamSwitch( int clientNum );
	bool			HandleServerAdminCommands( serverAdminData_t &data );
// RAVEN END

// RITUAL BEGIN
	typedef struct mpTeamPowerups_s {
		int powerup;
		int time;
		bool update;
		int endTime;
	} mpTeamPowerups_t;

	mpTeamPowerups_t teamPowerups[TEAM_MAX][MAX_TEAM_POWERUPS];

	void			AddTeamPowerup(int powerup, int time, int team);
	void			UpdateTeamPowerups();
	void			SetUpdateForTeamPowerups(int team);
// RITUAL END

	void			Precache( void );
	
	// throttle UI switch rates
	void			ThrottleUserInfo( void );
	void			ToggleSpectate( void );
	void			ToggleReady( void );
	void			ToggleTeam( void );
	// openQ4: scrolls the held Remastered scoreboard by whole rows
	void			ScrollScoreboard( int rows );

	void			ClearFrags( int clientNum );

	void			EnterGame( int clientNum );
	bool			CanPlay( idPlayer *p );
	bool			IsRankedParticipant( idPlayer *player );
	bool			IsInGame( int clientNum );
	bool			WantRespawn( idPlayer *p );

	void			ServerWriteInitialReliableMessages( const idMessageSender &sender, int clientNum );
	void			ClientReadStartState( const idBitMsg &msg );

	void			ServerClientConnect( int clientNum );

	void			PlayerStats( int clientNum, char *data, const int len );

	void			AddTeamScore ( int team, int amount );
	void			AddPlayerScore( idPlayer* player, int amount );
	void			AddPlayerTeamScore( idPlayer* player, int amount );
	void			AddPlayerWin( idPlayer* player, int amount );
	void			SetPlayerTeamScore( idPlayer* player, int value );
	void			SetPlayerDeadZoneScore( idPlayer* player, float value );
	void			SetPlayerScore( idPlayer* player, int value );
	void			SetPlayerWin( idPlayer* player, int value );
	void			SetHudOverlay( idUserInterface* overlay, int duration );

	void			ClearMap ( void );
	void			PrepareForMapShutdown( void );

	void			EnableDamage( bool enable = true );

	idPlayer*		GetRankedPlayer( int i );
	int				GetRankedPlayerScore( int i );
	int				GetNumRankedPlayers( void );
	
	idPlayer*		GetUnrankedPlayer( int i );
	int				GetNumUnrankedPlayers( void );

	int				GetScore( int i );
	int				GetScore( idPlayer* player );

	int				GetTeamScore( int i );
	int				GetTeamScore( idPlayer* player );

	int				GetWins( int i );
	int				GetWins( idPlayer* player );

	// asalmon: Get the score for a team.
	int				GetScoreForTeam( int i );
	int				GetTeamsTotalFrags( int i );
	int				GetTeamsTotalScore( int i );
	idUserInterface *GetMainGUI() {return mainGui;}

	float			GetPlayerDeadZoneScore(idPlayer* player);

	int				TeamLeader( void );

	int				GetPlayerTime( idPlayer* player );

	const char*		GetLongGametypeName( const char* gametype );
	const char*		VoteGameTypeToString( int gameTypeInt );
	int				GameTypeToVote( const char *gameType );

	void			ReceiveRemoteConsoleOutput( const char* output );

	void			ClientSetInstance( const idBitMsg& msg );
	void			ServerSetInstance( int instance );

	void			AddPrivatePlayer( int clientId );
	void			RemovePrivatePlayer( int clientId );

//RAVEN BEGIN
//asalmon: Xenon scoreboard update
#ifdef _XENON
	void			UpdateXenonScoreboard( idUserInterface *scoreBoard );
	int				lastScoreUpdate;
// mekberg: for selecting local player
	void			SelectLocalPlayer( idUserInterface *scoreBoard );
#endif
//RAVEN END
	int				VerifyTeamSwitch( int wantTeam, idPlayer *player );

	void			RemoveAnnouncerSound( int type );
	void			RemoveAnnouncerSoundRange( int startType, int endType );
	void			ScheduleAnnouncerSound ( announcerSound_t sound, float time, int instance = -1, bool allowOverride = false );
	void			ScheduleTimeAnnouncements( void );
// RAVEN END

	void			SendDeathMessage( idPlayer *attacker, idPlayer *victim, int methodOfDeath, bool quadKill );
	void			ReceiveDeathMessage( idPlayer *attacker, int attackerScore, idPlayer *victim, int victimScore, int methodOfDeath, bool quadKill );

	rvCTF_AssaultPoint*		NextAP( int team );
	int						OpposingTeam( int team );

	idList<idEntityPtr<rvCTF_AssaultPoint> > assaultPoints;

	// Buying Manager - authority for buying system game balance constants (awards,
	// costs, etc.)
	riBuyingManager	mpBuyingManager;

	idUserInterface* statSummary;			// stat summary
	rvTourneyGUI	tourneyGUI;

	void			ShowStatSummary( void );
	// Called by rvGameState at the authoritative GAMEON/GAMEREVIEW
	// transitions.  Keeping campaign result capture on the transition itself
	// means disconnect/forfeit paths cannot skip it by changing state outside
	// idMultiplayerGame::Run.
	void			OnMatchStarted( void );
	void			OnMatchEnded( void );
	// The Arena campaign is a local single-player experience hosted by the MP
	// game.  Keep its staging and review presentation behind the campaign token
	// so ordinary multiplayer retains the stock ready, spectate and camera flow.
	bool			IsArenaCampaignMatch( void ) const;
	bool			ArenaCampaignLocksPlayers( void ) const;
	bool			ArenaCampaignAllowsFreeLook( void ) const;
	// The local view is an Arena presentation shot rather than gameplay: the
	// HUD keeps only the match layer that carries the cinematic cards.
	bool			ArenaCampaignPresentationActive( void ) const;
	// The single-player Arena pauses while its in-game menu (or the main menu
	// over it) is open, the way a story level does. Host-local, like the rest
	// of the ceremony: the paused frames rebase every match deadline.
	bool			ArenaCampaignPausesWorld( void ) const;
	void			UpdateArenaSoundPause( bool pause );
	void			RebaseArenaCampaignPauseFrame( int deltaMsec );
	// Escape during the end-of-match ceremony skips ahead to the match report
	// instead of opening a menu whose Disconnect would forfeit the result.
	bool			ArenaCampaignOwnsEscape( void );
	// The Arena pause menu (guis/arena_pause.gui) replaces the multiplayer
	// menu during a live Arena match: Resume, Restart match, Settings and
	// Leave match. The retained card covers it like the menu's own card.
	bool			IsArenaPauseMenuOpen( void ) const { return currentMenu == MP_MENU_ARENA_PAUSE; }
	// Restart match resets the live match in place. The campaign authorizes
	// one restart by setting ui_arenaRestartMatch to the match's token and
	// running serverMapRestart, which then comes here instead of the
	// administration guard; clearing the CVar tells the campaign it happened.
	bool			ArenaCampaignRestartAuthorized( void ) const;
	void			RestartArenaCampaignMatch( void );
	// openq4_arenaCeremony: drives a local Arena match without device input.
	void			DebugArenaCampaignCeremony( const idCmdArgs &args );
	void			AdvanceArenaCampaignCeremony( void );
	void			SetupArenaCampaignStatSummary( void );
	int				GetMapWeaponMask( void );
	bool			ArenaCampaignFreezesWorld( void ) const;
	bool			BuildArenaCampaignSpawnInView( idPlayer *viewer, renderView_t *view );
	bool			BuildArenaCampaignIntroView( idPlayer *viewer, renderView_t *view );
	idPlayer *		ArenaCampaignIntroSubject( void );
	int				ArenaCampaignIntroCount( void );
	void			ShowArenaCampaignIntroCard( idPlayer *subject );
	bool			ArenaCampaignIntroBlocksCountdown( void );
	float			ArenaCampaignCeremonyFade( void ) const;
	void			DrawArenaCampaignCeremonyFade( void );
	bool			BuildArenaCampaignPresentationView( idPlayer *viewer, renderView_t *view );
	void			BeginArenaCampaignEntrancePresentation( void );
	void			ShowArenaCampaignVictoryPresentation( void );
	void			ClearArenaCampaignPresentation( void );
	bool			CanCapture( int team );
	void			FlagCaptured( idPlayer *player );
	
	void			UpdatePlayerRanks( playerRankMode_t rankMode = PRM_AUTO );
	void			UpdateTeamRanks( void );
	void			UpdateHud( idUserInterface* _mphud );
	idPlayer *		FragLimitHit( void );
	idPlayer *		FragLeader( void );
	bool			TimeLimitHit( void );
// openQ4 BEGIN
// Exit rules carried over from Quake Live.
	// true when the match is level at the top.  The optional output receives
	// the authoritative leading score, including valid zero/negative scores.
	bool			ScoreIsTied( int *leadingScore = NULL );
	// team that has pulled far enough ahead to end the match early, or -1
	int				MercyLimitHit( void );
	// team left holding the match when the other side empties out, or -1
	int				ForfeitTeam( void );
	// respawn delay in milliseconds while a match is in overtime, 0 otherwise
	int				GetOvertimeRespawnDelay( void );
	// total match length in milliseconds including every overtime granted
	int				GetMatchLengthMsec( void );
	// true while scoring is turned off, which is warmup unless si_warmupScoring
	bool			ScoringSuppressed( void ) const;
// openQ4 END
	int				GetCurrentMenu( void ) { return currentMenu; }

	void			SetFlagEntity( idEntity* ent, int team );
	idEntity*		GetFlagEntity( int team );

	void			WriteNetworkInfo( idFile *file, int clientNum );
	void			ReadNetworkInfo( idFile* file, int clientNum );

	void			SetShaderParms( renderView_t *view );
	
// RITUAL BEGIN
// squirrel: added DeadZone multiplayer mode
	int				NumberOfPlayersOnTeam( int team );
	int				NumberOfAlivePlayersOnTeam( int team );
	void			ReportZoneControllingPlayer( idPlayer* player );
	void			ReportZoneController(int team, int pCount, int situation, idEntity* zoneTrigger = 0);
	bool			IsValidTeam(int team);
	void			ControlZoneStateChanged( int team );

	void			ListMaps( void );

	int				powerupCount;
	int				prevAnnouncerSnd;
	int				defaultWinner;
	int				deadZonePowerupCount;
	dzState_t		dzState[ TEAM_MAX ];
	float			marineScoreBarPulseAmount;
	float			stroggScoreBarPulseAmount;
// RITUAL END

// RITUAL BEGIN
// squirrel: Mode-agnostic buymenus
	bool			isBuyingAllowedRightNow;

	void			OpenLocalBuyMenu( void );
	void			RedrawLocalBuyMenu( void );
	void			GiveCashToTeam( int team, float cashAmount );
	bool			IsBuyingAllowedInTheCurrentGameMode( void );
	bool			IsBuyingAllowedRightNow( void );
// RITUAL END
	static const char*	teamNames[ TEAM_MAX ];

private:
	static const char	*MPGuis[];
	static const char	*ThrottleVars[];
	static const char	*ThrottleVarsInEnglish[];
	static const int	ThrottleDelay[];

	char			killNotificationMsg[ KILL_NOTIFICATION_LEN ];

	int				pingUpdateTime;			// time to update ping

	mpPlayerState_t	playerState[ MAX_CLIENTS ];

	// game state
	rvGameState*	gameState;
	mpCompetitiveRules matchRules;
	mpMatchSession	matchSession;
	mpCompetitionSeries matchSeries;
	mpCompetitionSeriesReport matchSeriesReport;
	uint64_t		matchSeriesId;
	uint64_t		matchSeriesLinkedSessionId;
	mpSeriesRecoveryWorkspace matchSeriesRecoveryWorkspace;
	mpSeriesReportStorageWorkspace matchSeriesReportWorkspace;
	int			matchSeriesContestantSlot[ MP_SERIES_SIDE_COUNT ];
	uint64_t		matchSeriesContestantConnection[ MP_SERIES_SIDE_COUNT ];
	int			matchSeriesCompetitionSide[ MAX_CLIENTS ];
	uint64_t		matchSeriesCompetitionConnection[ MAX_CLIENTS ];
	int			matchSeriesGameSideForCompetition[ MP_SERIES_SIDE_COUNT ];
	bool			matchSeriesNeedsBindingRecovery;
	bool			matchSeriesAwaitingMapSession;
	bool			matchSessionOperational;
	// The client slot whose committed match-layer side ApplyMatchTeamsPlanToLegacy
	// is publishing, or -1. VerifyTeamSwitch leaves that side as it is.
	int				matchTeamApplicationSlot;
	uint64_t		nextMatchConnectionId;
	uint64_t		matchConnectionId[ MAX_CLIENTS ];
	mpMatchTeams	matchTeams;
	mpProposalService matchProposals;
	mpMatchOperationExecutor matchOperationExecutor;
	mpRefereeAuthenticationService matchRefereeAuthentication;
	mpMatchEvidence	matchEvidence;
	mpMatchEvidenceObserver matchEvidenceObserver;
	mpEvidenceStorageWorkspace matchEvidenceWorkspace;
	mpMatchItemTimingRegistry matchItemTiming;
	mpSessionView	clientMatchView;
	bool			clientMatchViewValid;
	mpMatchProtocolSessionId_t clientSummaryAnnouncedSession;
	mpMatchProtocolRevision_t clientSummaryAnnouncedResult;
	mpMatchControlModel clientMatchControlModel;
	mpMatchControlError_t clientMatchControlError;
	bool			clientMatchControlErrorValid;
	uint64_t		clientMatchControlChoiceSessionId;
	uint64_t		clientMatchMenuProjectedViewRevision;
	uint64_t		clientMatchHudProjectedViewRevision;
	uint64_t		clientMatchScoreboardProjectedViewRevision;
	mpMatchOperationResult_t clientMatchOperationResult;
	bool			clientMatchOperationResultValid;
	mpMatchOperationRequest_t clientPendingMatchConfirmation;
	bool			clientPendingMatchConfirmationValid;
	uint64_t		matchViewRevision;
	uint64_t		matchControlRevision;
	uint64_t		matchViewObservedSessionRevision;
	uint32_t		matchViewObservedRulesRevision;
	uint64_t		matchViewObservedRulesDigest;
	mpProposalRevision_t matchViewObservedProposalRevision;
	uint64_t		matchViewObservedSeriesRevision;
	uint64_t		matchViewObservedTeamsRevision;
	uint64_t		matchViewObservedEvidenceRevision;
	uint64_t		matchViewObservedItemTimingRevision;
	bool			matchViewObservedEvidenceFinalized;
	bool			matchViewObservedEvidencePersisted;
	bool			matchViewObservedMVDRecording;
	bool			matchItemTimingNeedsInitialScan;
	int			matchViewNextClockUpdateTime;
	uint64_t		matchViewSentRevision[ MAX_CLIENTS ];
	uint32_t		lastMatchRequestId[ MAX_CLIENTS ];
	mpMatchOperationResult_t lastMatchRequestResult[ MAX_CLIENTS ];
	bool			lastMatchRequestResultValid[ MAX_CLIENTS ];
	int			matchOperationNextAllowedTime[ MAX_CLIENTS ][ MP_MATCH_COOLDOWN_COUNT ];
	uint32_t		nextClientMatchRequestId;
	uint32_t		nextTrustedLocalMatchRequestId;
	mpProposalId_t	nextMatchProposalId;
	// The newest passed proposal executed in each scope.  Proposal ids only grow
	// within a session, so a record at or below it ran already and only its
	// acknowledgement is retried: a ballot never executes twice.
	mpProposalId_t	lastExecutedMatchProposalId[ MP_PROPOSAL_SCOPE_COUNT ];
	uint64_t		nextMatchSessionId;
	bool			matchRefereeCredentialInitialized;
	bool			matchRefereeCredentialIsReal;
	char			pendingRefereePassword[ MP_REFEREE_AUTH_MAX_PASSWORD_BYTES + 1 ];
	int			pendingRefereePasswordLength;
	int			pendingRefereePasswordDeadline;
	mpRefereeAuthChallenge pendingRefereeChallenge;
	bool			pendingRefereeChallengeValid;
	bool			competitiveRulesValidForSession;
	bool			competitiveRulesInitialized;
	mpRuleValidationReason_t competitiveRulesFailure;
	bool			matchEvidenceFinalized;
	bool			matchEvidencePersisted;
	bool			matchEvidenceFinalizationPending;
	int				matchEvidenceMode;
	bool			matchMVDStartedBySession;
	bool			matchMVDAttemptedBySession;
	bool			matchMVDOperatorOwnedBySession;
	char			matchMVDQPath[ MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES + 1 ];
	uint64_t		matchPhaseEffectsSessionId;
	uint64_t		matchPhaseEffectsRevision;
	uint64_t		matchTerminalResultSessionId;
	mpMatchViewTerminalResult_t matchTerminalResult;
	mpEvidenceMapResult matchTerminalEvidenceResult;

	bool			InitializeCompetitiveRules( void );
	bool			CanEnterMatchCountdown( void ) const;
	bool			IsManagedTeamCommunicationActive( void ) const;
	bool			BuildManagedTeamCommunicationBinding( int clientNum,
						mpMatchTeamCommunicationBinding_t &binding ) const;
	mpMatchRulesValidationContext_t BuildCompetitiveRuleValidationContext( void ) const;
	bool			ConfigureMatchSessionFromCompetitiveRules( void );
	mpMatchTeamsPolicy_t BuildMatchTeamsPolicy( void ) const;
	bool			ApplyMatchTeamsTransaction(
						const mpMatchTeamsJoinDecision_t &decision,
						mpOperationExecutionResult_t &execution );
	bool			ApplyMatchSpectatorTransition( mpParticipantId participant,
						mpOperationExecutionResult_t &execution );
	void			ReconcileGameplayPhaseAfterMatchMutation( void );
	bool			MatchTeamJoinUnbalances( mpParticipantId participant, int side );
	void			ApplyMatchTeamsPlanToLegacy(
						const mpMatchTeamsTransactionPlan_t &plan );
	void			ProcessMatchTeamQueue( void );
	bool			ConfigureMatchSessionForRules( mpMatchSession &session,
						const mpMatchRulesSnapshot &rules, bool rulesValid ) const;
	bool			BeginMatchSession( void );
	bool			BeginMatchEvidence( void );
	void			ReconcileMatchEvidenceForCommittedRules( void );
	bool			IsCompetitionSeriesModeSupported( void ) const;
	bool			CollectCompetitionSeriesContestants(
						int slots[ MP_SERIES_SIDE_COUNT ],
						uint64_t connections[ MP_SERIES_SIDE_COUNT ] ) const;
	int			ResolveCompetitionSide( mpParticipantId participant ) const;
	bool			BuildCompetitionSeriesMapPool(
						const mpSeriesProfileDescriptor &profile,
						char storage[ MP_SERIES_MAX_MAP_POOL ][ MP_SERIES_MAP_TOKEN_BYTES ],
						const char *tokens[ MP_SERIES_MAX_MAP_POOL ], int &count,
						mpSeriesReason_t &reason ) const;
	bool			PersistCompetitionSeries( void );
	bool			PersistCompetitionSeriesCandidate(
						const mpCompetitionSeries &series,
						const mpCompetitionSeriesReport &report,
						uint64_t seriesId, uint64_t linkedSessionId );
	bool			AbortUnstartedCompetitionSeriesMap(
						mpCompetitionSeries &series,
						mpCompetitionSeriesReport &report,
						uint64_t seriesId, uint64_t linkedSessionId ) const;
	bool			InitializeCompetitionSeriesReport(
						const mpCompetitionSeries &series, uint64_t seriesId,
						const int contestantSlots[ MP_SERIES_SIDE_COUNT ],
						mpCompetitionSeriesReport &report ) const;
	bool			FinalizeCompetitionSeriesReport(
						mpCompetitionSeries &series,
						mpCompetitionSeriesReport &report,
						mpParticipantId authorizer );
	bool			CommitCompetitionSeriesMapEvidence(
						const mpEvidenceStorageResult &evidenceStorage );
	bool			RestoreCompetitionSeriesIfRequested( void );
	bool			BindCompetitionSeriesContestant( int competitionSide,
						int clientNum );
	bool			ScheduleCompetitionSeriesMap( mpCompetitionSeries &candidate,
						const char *mapToken, mpOperationExecutionResult_t &execution );
	void			LinkCurrentSeriesEvidence( void );
	mpEvidenceCommittedStamp BuildMatchEvidenceStamp( void ) const;
	mpEvidenceActorRef MatchEvidenceActor( mpParticipantId participant,
						bool serverOperator = false ) const;
	void			ObserveMatchEvidence( mpParticipantId actor,
						bool serverOperator = false );
	void			RecordMatchEvidenceFinalStats( void );
	void			RecordMatchEvidenceParticipantStats( int clientNum,
						mpParticipantId participant );
	void			RecordMatchEvidenceResult( mpMatchTransitionReason_t reason,
						mpParticipantId authorizer, int forfeitingSide = MP_MATCH_SIDE_NONE );
	mpEvidenceMapResult BuildMatchTerminalEvidenceResult( mpMatchTransitionReason_t reason,
						mpParticipantId authorizer, int forfeitingSide );
	void			ClearMatchTerminalResult( void );
	void			FreezeMatchTerminalResult( const mpEvidenceMapResult &result );
	bool			PersistMatchEvidence(
						mpEvidenceStorageResult *storageResult = NULL );
	bool			FinalizeMatchEvidence( bool abortedIfUndecided );
	void			ProjectMatchMVDReportArtifact(
						mpSeriesReportArtifactInput &artifact, idStr &qpathStorage ) const;
	bool			ReconcileCompetitionSeriesMVDResults(
						mpCompetitionSeriesReport &report, bool sealing );
	void			StartMatchMVDIfRequired( void );
	void			StopMatchMVD( const char *reason );
	bool			ApplyCommittedMatchPhaseEffects( int forfeitingSide );
	void			PublishCompetitiveRulesIdentity( void );
	void			MirrorCompetitiveRulesToLegacy( void );
	void			SynchronizeMatchParticipant( int clientNum );
	void			SynchronizeAllMatchParticipants( void );
	void			RebaseCompetitivePauseFrame( int deltaMsec );
	void			AdvanceMatchViewRevision( bool forceClockSample = false );
	void			InitializeMatchItemTimingObservations( void );
	bool			BuildMatchItemTimingIdentity( const idItem *item,
						mpMatchItemTimingKind_t &kind, char *adapterToken,
						int adapterTokenBytes ) const;
	void			ObserveCompetitiveItemState( const idItem *item,
						bool available, int respawnMsec );
	void			SendChangedMatchViews( bool force = false );
	mpMatchDisclosurePolicy_t BuildMatchDisclosurePolicy( void ) const;
	bool			BuildMatchDisclosureRecipient( int clientNum,
						mpParticipantId &participant,
						mpMatchDisclosureRecipient_t &recipient ) const;
	int				ResolveMatchDisclosureTargetSide(
						const mpMatchParticipantState &target ) const;
	bool			BuildMatchView( int clientNum, mpSessionView &view ) const;
	bool			AcceptClientMatchView( const mpSessionView &incoming );
	bool			RefreshLocalClientMatchView( void );
	void			ProjectClientMatchControlMenu( bool notifyGui );
	void			ProjectClientManagedMatchContext( idUserInterface *gui );
	bool			HandleMatchControlCommand( const char *token );
	void			ClearClientPendingMatchConfirmation( bool closeModal );
	void			ClearClientMatchControlConnectionState( bool clearGuiCredential );
	bool			WriteMatchViewMessage( int clientNum, idBitMsg &msg ) const;
	mpMatchViewAllowedOperationMask_t AllowedMatchOperationsFor( mpParticipantId participant ) const;
	mpMatchOperationResult_t MakeMatchOperationResult( const mpMatchOperationRequest_t &request,
						const mpOperationExecutionResult_t &execution ) const;
	bool			StoreClientMatchOperationResult(
						const mpMatchOperationResult_t &result );
	void			ClearMatchOperationTransportSlot( int clientNum );
	void			SendMatchOperationResult( int clientNum,
						const mpMatchOperationResult_t &result );
	bool			MatchOperationRateLimitAccepted( int clientNum,
						const mpMatchOperationDescriptor_t &descriptor );
	bool			VoteRateLimitAccepted( int clientNum );
	void			StampVoteRateLimit( int clientNum );
	void			ResetVoteCooldownSlot( int clientNum );
	bool			HasRecoveredCompetitionReview( void ) const;
	void			BuildMatchOperationContext( int clientNum,
						mpMatchOperationOpcode_t opcode, bool enforceTransportCooldown,
						mpOperationAdapterContext_t &context );
	void			ProcessPassedMatchProposals( void );
	void			RetireUnpassedMatchProposals( void );
	bool			ApplyMatchOperationContinuation( int clientNum,
						const mpMatchOperationRequest_t &request,
						mpOperationExecutionResult_t &execution );
	bool			ExecuteTrustedLocalMatchOperation(
						mpMatchOperationRequest_t &request,
						mpOperationExecutionResult_t &execution );
	bool			InitializeRefereeAuthentication( void );
	bool			SendRefereeAuthChallenge( int clientNum,
						const mpRefereeAuthChallenge &challenge );
	bool			CompleteRefereeAuthChallenge( const mpRefereeAuthChallenge &challenge );
	void			ClearPendingRefereePassword( void );
	void			ApplyMatchOperationLegacyMirror( int clientNum,
						const mpMatchOperationRequest_t &request,
						const mpOperationExecutionResult_t &execution );
	mpMatchTransitionReason_t InferMatchTransitionReason( mpGameState_t from,
						mpGameState_t to ) const;
	mpMatchRoundTransitionReason_t InferRoundTransitionReason( roundState_t from,
						roundState_t to ) const;
	void			BeginArenaCampaignResult( void );
	void			UpdateArenaCampaignResult( void );
	// Publishes a decided, contested result (ui_arenaDecidedResult) for as long
	// as the ceremony holds it back, so the campaign keeps it if the player
	// leaves or quits before the handoff.
	void			PublishArenaCampaignDecidedResult( void );
	// Resets every ceremony, camera and introduction field. No HUD events and
	// no renderer calls, so the constructor and ClearMap can share it.
	void			ResetArenaCampaignCeremonyState( void );
	void			BuildArenaCampaignIntroOrder( void );
	void			SkipArenaCampaignCeremony( void );
	void			UpdateArenaCampaignSkipInput( void );
	const char *	ArenaCampaignPresentedGameType( void );
	const char *	ArenaCampaignMatchTitle( void ) const;
	void			ArenaCampaignAnnounceResult( void );

	// Ordered end-of-match ceremony. An Arena match is always one human on a
	// listen server with no remote clients, so these phases are host-local and
	// need no wire state - deliberately NOT mpGameState_t values, which are
	// replicated bytes every gametype branches on.
	enum arenaCeremonyPhase_t {
		ARENA_CEREMONY_NONE = 0,
		ARENA_CEREMONY_INTRO,		// warmup: present each authored opponent in turn
		ARENA_CEREMONY_SPAWN_IN,	// match start: orbit own body, land in first person
		ARENA_CEREMONY_TABLEAU,		// frozen world, player-steered orbit of the victor
		ARENA_CEREMONY_SCOREBOARD,
		ARENA_CEREMONY_STATS,
		ARENA_CEREMONY_DONE			// ceremony over; the result may be reported
	};
	int				arenaCeremonyPhase;
	int				arenaCeremonyPhaseStartTime;
	int				arenaCeremonyPhaseEndTime;
	int				arenaTableauStartTime;

	// What the HUD was last asked to present: 0 nothing, 1 the match entrance,
	// 2 the victory tableau, 3 an opponent introduction. A clear is only sent
	// over something visible, so it cannot outlive a card armed after it.
	int				arenaHudPresentation;
	// Escape, or a fresh fire press after a short grace, skips the rest of the
	// introductions or of the end-of-match ceremony.
	bool			arenaCeremonySkipping;
	bool			arenaSkipButtonHeld;
	bool			arenaMenuWasPaused;
	bool			arenaSoundPauseOwned;
	// The introduction order, latched when it arms: teammates first, then the
	// opposition, each in slot order.
	int				arenaIntroOrder[ MAX_CLIENTS ];
	int				arenaIntroTotal;
	int				arenaIntroTeammates;
	// The current subject's camera side, latched on its first frame so a wall
	// behind one candidate cannot flip the shot mid-introduction.
	bool			arenaIntroShotLatched;
	float			arenaIntroShotBase;
	float			arenaVictorLookPitch;

	bool			arenaResultPending;
	bool			arenaResultReported;
	int				arenaResultToken;
	int				arenaResultOutcome;	// 0 loss, 1 win, 2 draw, 3 aborted; framework handoff contract
	int				arenaResultPlayerScore;
	int				arenaResultOpponentScore;
	int				arenaResultReportTime;
	int				arenaPresentationVictor;
	int				arenaPresentationFocus;
	bool			arenaPresentationBlurEnabled;
	// Completed serverMapRestart resets, so the Arena's Restart match can tell
	// a restart from one MapRestart refused or turned into a map reload.
	int				mapRestartCount;
	// The connect-time join screen is a compact panel drawn over a live view of
	// the map, so it borrows the same Raven special-effect blur the arena
	// presentation uses and holds it for as long as that panel is up.
	bool			joinScreenSoftFocusEnabled;
	// True from the moment a connect-time join is offered until the player
	// answers it.  The menu can legitimately be opened more than once before
	// that happens - a listen-server host is offered the screen again after its
	// own player entity respawns - so this cannot be a one-shot GUI state bit.
	bool			joinScreenPending;
	// The session's retained card covers mainGui; the header texts last
	// published to it, and the revision that tells the session they changed.
	bool			retainedMenuCovered;
	int				retainedMenuRevision;
	idDict			retainedMenuPublished;
	// The Team page's three action slots (section 14.18), derived from the
	// local player's state each time they are published or chosen.
	enum retainedTeamAction_t { RTA_NONE, RTA_JOIN_MARINE, RTA_JOIN_STROGG, RTA_JOIN_AUTO, RTA_SPECTATE, RTA_READY, RTA_TOURNEY };
	struct retainedTeamSlot_t {
		retainedTeamAction_t	action;
		idStr					label, reason, detail;
		bool					available;
	};
	static const int RETAINED_TEAM_SLOTS = 3;
	void			RetainedTeamSlots( retainedTeamSlot_t slots[ RETAINED_TEAM_SLOTS ] );
	// The Welcome card's Join page (section 14.18): in team modes the two team
	// cards, Auto join and Spectate; in Tourney joining or leaving the
	// tournament; elsewhere Join game and Spectate.
	static const int RETAINED_WELCOME_SLOTS = 4;
	void			RetainedWelcomeSlots( retainedTeamSlot_t slots[ RETAINED_WELCOME_SLOTS ] );
	void			PublishRetainedWelcome( idUserInterface *card, bool &changed );
	// Takes a card action as the stock buttons take it and closes the menu;
	// false for an action that does nothing.
	bool			RunRetainedAction( retainedTeamAction_t action );
	bool			PublishRetainedValue( idUserInterface *card, const char *key, const char *value );
	// The Players page's lists: the player's own team (Marines while
	// spectating, everyone outside team modes), the other team and the
	// spectators, each up to the rows a server can hold. A list keeps its
	// order while the menu stays open, and sorts again by score only when it
	// opens or the list's players change. The page shows the statistics of
	// retainedStatClient.
	static const int RETAINED_PLAYER_LISTS = 3;
	static const int RETAINED_PLAYER_ROWS = 32;
	static_assert( RETAINED_PLAYER_ROWS == MAX_CLIENTS, "retained rosters cover every client slot" );
	idList<int>		retainedPlayerOrder[ RETAINED_PLAYER_LISTS ];
	int				retainedStatClient;
	idStr			retainedStatIdentity;
	idStr			RetainedPlayerIdentity( int client ) const;
	void			RetainedPlayerLists( idList<int> lists[ RETAINED_PLAYER_LISTS ], int colors[ RETAINED_PLAYER_LISTS ] );
	bool			RetainedListed( int client );
	void			PublishRetainedPlayers( idUserInterface *card, bool &changed );
	// The summary card (section 14.14) covers statSummary at the end of a
	// match: the result, the standings (the top ten, or each team's top five)
	// with each player's three most accurate weapons, and the chat's newest
	// messages; retained_mp_summary.py lays out the same numbers.
	static const int RETAINED_SUMMARY_TOP = 10;
	static const int RETAINED_SUMMARY_TEAM = 5;
	static const int RETAINED_SUMMARY_CHAT = 10;
	static const int RETAINED_SUMMARY_AWARDS = 3;
	void			PublishRetainedSummary( idUserInterface *card, bool &changed );
	// Marks or unmarks a friend: the platform's friends service where there is
	// one, and the player's own mark, which the lists and scoreboard show.
	void			ToggleFriend( int client );
	// The Vote page (section 14.18). The call being drafted is reset to the
	// server's values each time the card covers the menu; "retained voteSet"
	// changes one field and "retained callVote" calls a vote with the fields
	// that differ from the server's. The map is drafted by its path; its
	// choice lists the maps the drafted game type plays, and the kick choice
	// the other players. The session's RETAINED_MP_VOTE_FIELDS and the
	// document's rows follow this order.
	enum retainedVoteField_t {
		RVF_MAP, RVF_GAMETYPE, RVF_TIMELIMIT, RVF_FRAGLIMIT, RVF_CAPTURELIMIT, RVF_TOURNEYLIMIT, RVF_CONTROLTIME,
		RVF_BALANCE, RVF_SHUFFLE, RVF_RESTART, RVF_BUYING, RVF_KICK, RVF_COUNT
	};
	static const int RETAINED_VOTE_MAPS = 128;
	static const int RETAINED_VOTE_LINES = 6;
	int				retainedVoteDraft[ RVF_COUNT ];
	idStr			retainedVoteMap;
	idList<int>		retainedVoteMaps;
	idList<int>		retainedVoteKick;
	idStr			retainedVoteKickCatalog;
	int				retainedVoteKickIdentity = 0;
	bool			retainedVoteKickStale = false;
	bool			SetRetainedVoteKick( int row, const char *catalog );
	bool			retainedVoteMapsRequested;
	void			ResetRetainedVoteDraft( void );
	void			RetainedVoteChoices( void );
	// The rows a drafted game type uses: its own limit, team balance and
	// shuffle in team modes, buying where the mode has a buy menu.
	static bool		RetainedDraftFieldShown( int voteGameType, int field );
	bool			RetainedVoteFieldShown( int field ) const;
	bool			RetainedVoteFieldAllowed( int field ) const;
	int				RetainedVoteChanges( voteStruct_t &data );
	const char *	RetainedVoteRefusal( void );
	const char *	RetainedBallotRefusal( void ) const;
	bool			SetRetainedVoteField( int field, int value );
	void			RetainedVoteLines( const voteStruct_t &data, idStrList &lines );
	void			PublishRetainedVote( idUserInterface *card, bool &changed );
	// The Escape card's Admin page (section 14.18): the remote console
	// password first, then Players (kick, ban, force team switch and the ban
	// list), Server (the rules, drafted as the Vote page drafts a call, and
	// Apply changes) and Console. A listen server's host is the admin; a
	// client becomes one once the server accepts its password, until the map
	// changes. The drafted fields follow retainedVoteField_t without the kick,
	// as do the session's RETAINED_MP_ADMIN_FIELDS and the document's rows.
	enum retainedAdminState_t { RAS_NONE, RAS_CHECKING, RAS_REFUSED };
	enum retainedAdminAction_t { RAA_KICK, RAA_BAN, RAA_SWITCH, RAA_APPLY, RAA_COUNT };
	static const int RETAINED_ADMIN_PLAYERS = 32;
	static const int RETAINED_ADMIN_BANS = 32;
	static const int RETAINED_ADMIN_CONSOLE_LINES = 12;
	bool			retainedAdminValid;
	int				retainedAdminState;
	int				retainedAdminClient;		// the player chosen, by client number (-1: none)
	idStr			retainedAdminIdentity;
	int				retainedAdminBan;			// the ban chosen, by row (-1: none)
	int				retainedAdminDraft[ RVF_COUNT ];
	idStr			retainedAdminMap;
	idList<int>		retainedAdminMaps;
	bool			RetainedAdmin( void ) const;
	void			ResetRetainedAdminDraft( void );
	void			RetainedAdminChoices( void );
	bool			RetainedAdminFieldShown( int field ) const;
	bool			SetRetainedAdminField( int field, int value );
	bool			RetainedAdminChanges( serverAdminData_t &data );
	const char *	RetainedAdminRefusal( int action );
	void			PublishRetainedAdmin( idUserInterface *card, bool &changed );
	bool			HandleRetainedAdminCommand( const idStr &sub, const idCmdArgs &args, int &icmd );
	// The stock Admin console's Enter ("command") and Tab ("tab") on the
	// menu's admin_console_input.
	void			RunAdminConsole( idUserInterface *gui, const char *mode );
	// The Settings pages (section 14.18): the player's name and clan, three
	// model lists (the player's own model for the mode and team, the model
	// forced on enemies and, in team modes, the one forced on teammates) by
	// row, the stock rail color swatches and the custom crosshairs.
	static const int RETAINED_MODEL_SLOTS = 3;
	static const int RETAINED_MODEL_ROWS = 24;
	static const int RETAINED_RAIL_COLORS = 7;
	static const char *	RetainedRailColor( int row );
	bool			RetainedModelChoice( int slot, idStr &cvar, idStrList &values, idStrList &names, int &current );
	void			RetainedCrosshairs( idStrList &crosshairs );
	void			PublishRetainedSettings( idUserInterface *card, bool &changed );
	// The Escape card's Match page (section 14.18): Match Control, mirrored
	// from the game's own projection of it on the menu (the match_* states),
	// the status lines one key each and each list's rows. The replacements,
	// proposals, profiles, rule fields, map pool and evidence hold every row
	// Match Control can (MatchControlModel.h); the team rows hold a full
	// 32-player server's sides, participants and 30 seats, invitations or
	// queue places, and the series history 24 steps. Past those two (the
	// Escape card is near the runtime's 16 MiB document limit) a list ends
	// with the way to the stock page. tools/ui/retained_mp_menus.py reads
	// these numbers; PublishRetainedMatch asserts them against the model.
	static const int RETAINED_MATCH_STATUS_LINES = 6;
	static const int RETAINED_MATCH_TEAM_ROWS = 64;
	static const int RETAINED_MATCH_REPLACEMENT_ROWS = 32;
	static const int RETAINED_MATCH_PROPOSAL_ROWS = 7;
	static const int RETAINED_MATCH_PROFILE_ROWS = 16;
	static const int RETAINED_MATCH_RULE_ROWS = 34;
	static const int RETAINED_MATCH_SERIES_MAP_ROWS = 32;
	static const int RETAINED_MATCH_HISTORY_ROWS = 24;
	static const int RETAINED_MATCH_EVIDENCE_ROWS = 5;
	void			PublishRetainedMatch( idUserInterface *card, bool &changed );
	void			PublishRetainedMatchList( idUserInterface *card, bool &changed, const char *list, const char *key, int count, int rows,
						int columns );
	bool			arenaEntranceCameraResolved;
	// Which presentation the latched camera belongs to.  The entrance and the
	// final tableau both latch a collision-safe anchor, but they resolve it
	// around different points in the map, so the latch has to be re-resolved
	// when the phase changes instead of carrying the spawn shot into review.
	bool			arenaEntranceCameraIsEntrance;
	bool			arenaEntranceCameraFallback;
	bool			arenaEntranceCameraValid;
	idVec3			arenaEntranceCameraForward;
	idVec3			arenaEntranceCameraLeft;
	idVec3			arenaEntranceCameraRadial;
	float			arenaEntranceCameraHeightLimit;
	// Reference yaw for the final tableau's player-steered orbit, latched on its
	// first frame so the shot opens at the authored angle wherever the player
	// happened to be looking when the match ended.
	bool			arenaVictorLookLatched;
	float			arenaVictorLookYaw;
	// Match-start orbit basis, latched once so the shot cannot swim.
	// Weapon slots present as pickups on the loaded map, for the warmup
	// arsenal. Invalidated by ClearMap.
	int				mapWeaponMask;
	bool			mapWeaponMaskValid;
	bool			arenaSpawnInLatched;
	int				arenaIntroIndex;
	int				arenaIntroSubjectStartTime;
	int				arenaIntroArmDeadline;
	idVec3			arenaSpawnInForward;
	idVec3			arenaSpawnInLeft;

	void			SelectArenaCampaignPresentationFocus( idPlayer *host );
	idPlayer *		GetArenaCampaignPresentationFocus( void ) const;
	void			SetArenaCampaignDepthOfField( bool enabled, float focusDistance = 0.0f, float strength = 0.0f );

	// vote vars
	vote_flags_t	vote;					// active vote or VOTE_NONE
	int				voteTimeOut;			// when the current vote expires
	int				voteExecTime;			// delay between vote passed msg and execute
	int				voteKickIdentity = 0;	// connection captured when the server starts a kick vote
	bool			VoteKickTargetCurrent( void ) const;
	int				yesVotes;				// counter for yes votes
	int				noVotes;				// and for no votes
	int				voteEligibleCount;		// frozen electorate size for the active inherited vote
	idStr			voteValue;				// the data voted upon ( server )
	idStr			voteString;				// the vote string ( client )
	bool			voted;					// hide vote box ( client )
	int				kickVoteMap[ MAX_CLIENTS ];
	int				kickVoteMapIdentities[ MAX_CLIENTS ] = {};
// RAVEN BEGIN
// shouchard:  names for kickVoteMap
	idStr			kickVoteMapNames[ MAX_CLIENTS ];
	voteStruct_t	currentVoteData;		// used for multi-field votes
// RAVEN END
	// openQ4: who called the running vote, when it started and how long it
	// runs, kept on clients too for the multiplayer card's Vote page
	int				voteCaller;
	int				voteStartTime;
	int				voteLength;
	bool			votePacked;
	// openQ4: per-caller cool-off so one client cannot own the vote channel by
	// re-issuing on the frame after its own vote resolves
	int				nextVoteAllowedTime[ MAX_CLIENTS ];
	int				nextVoteRejectNoticeTime[ MAX_CLIENTS ];

	idStr			localisedGametype;

	idList<int>		voteMapDecls;
	int				voteMapsWaiting;
	// openQ4: one-shot operator diagnostics, reset per map
	bool			mapListTruncationWarned;
	bool			matchItemTimingFullWarned;

	// time related
	int				matchStartedTime;		// time current match started

	// guis
// RITUAL BEGIN
// squirrel: added DeadZone multiplayer mode
	//int				sqRoundNumber;			// round number in DeadZone; match expires when this equals "sq_numRoundsPerMatch" (cvar)
// squirrel: Mode-agnostic buymenus
	idUserInterface *buyMenu;				// buy menu
// RITUAL END
	idUserInterface *scoreBoard;			// scoreboard
	idUserInterface *mainGui;				// ready / nick / votes etc.
	idListGUI		*mapList;
	idUserInterface *msgmodeGui;			// message mode
	idUserInterface *arenaPauseGui;			// the single-player Arena's pause menu
	int				currentMenu;			// 0 - none, 1 - mainGui, 2 - msgmodeGui, 3 - statSummary, 4 - buyMenu, 5 - arenaPauseGui
	int				nextMenu;				// if 0, will do mainGui
	bool			bCurrentMenuMsg;		// send menu state updates to server


	enum {
		MPLIGHT_CTF_MARINE,
		MPLIGHT_CTF_STROGG,
		MPLIGHT_QUAD,
		MPLIGHT_HASTE,
		MPLIGHT_REGEN,
		MPLIGHT_MAX
	};

	int				lightHandles[ MPLIGHT_MAX ];
	renderLight_t	lights[ MPLIGHT_MAX ];

	// chat buffer
	idStr			chatHistory;

	// openQ4: the Remastered kill feed and chat lines, timed like the stock
	// ones and published to the multiplayer HUD as fields (ui-visual-design.md 14.9)
	rvHudLines		remasteredKills;
	rvHudLines		remasteredChat;
	// the last 64 chat lines, untimed: the history the open input shows,
	// eight at a time
	idList<idDict>	remasteredChatLog;
	// the chat entry last published (a change restarts the caret's blink)
	bool			remasteredChatOpen;
	idStr			remasteredChatEntry;
	int				remasteredChatCaret;
	// the transmit displays last sent to the HUD (-1 unknown; the talker's client)
	int				hudTransmitSelf;
	int				hudTransmitOther;
	// the Remastered HUD's flag, clock-warning and match states last announced
	// (oq4_flags, oq4_clockWarn, oq4_state start and stop its looping motion)
	int				hudFlagStates[ 3 ];
	int				hudClockWarn;
	int				hudMatchState;
	// the respawn prompt last published (oq4_respawn fades it in as it appears)
	int				hudRespawn;
	// Dead Zone's score pulse: the scores last seen on this map and when the
	// pulse last faded, so it starts clean on each map and fades by time
	// the HUD and game clocks PublishRemasteredHud last published for: a frame
	// drawn again inside one tic publishes nothing
	idUserInterface *hudPublishGui;
	int				hudPublishRealTime;
	int				hudPublishTime;
	bool			hudZoneScoresValid;
	int				hudZoneScores[ 2 ];
	int				hudZonePulseTime;
	// the held Remastered scoreboard's scroll in rows, the most it scrolls, and
	// whether ui_scoreboardScrolls last said it does
	int				remasteredScoreboardScroll;
	int				remasteredScoreboardScrollMax;
	bool			remasteredScoreboardScrolls;
	void			ResetRemasteredHudAnnouncements( void );

	void			PublishRemasteredHud( idUserInterface *_mphud, idPlayer *localPlayer );
	void			PublishRemasteredNameTags( idUserInterface *_mphud, idPlayer *localPlayer, bool freeCamera );
	void			PublishRemasteredCompetitive( idUserInterface *_mphud, idPlayer *localPlayer, idPlayer *followed, int limit );
	void			PublishRemasteredChat( idUserInterface *_mphud, int now );

	// rcon buffer
	idStr			rconHistory;

//RAVEN BEGIN
//asalmon: Need to refresh stats periodically if the player is looking at stats
	int currentStatClient;			// GUI list selection index, NOT a client num
	int currentStatTeam;
	// openQ4: client num currentStatClient/currentStatTeam last resolved to, so the
	// periodic refresh polls the selected player instead of an unrelated slot
	int currentStatClientNum;
//RAVEN END

public:
	// current player rankings
	idList<rvPair<idPlayer*, int> > 	rankedPlayers;
	idList<idPlayer*>		unrankedPlayers;

	rvPair<int, int>		rankedTeams[ TEAM_MAX ];

	// openQ4: live damage numbers for the local view.  Client side only, but
	// a listen server host stages into the same slab.
	rvDamageNumbers			damageNumbers;
	rvObjectiveMarkers		objectiveMarkers;

private:

	int				lastVOAnnounce;

	int				lastReadyToggleTime;
	bool			pureReady;				// defaults to false, set to true once server game is running with pure checksums
	// openQ4: match state last compared for MVD markers; primed while recording
	mpMVDMarkerState mvdMarkerState;
	bool			mvdMarkersPrimed;
	void			PublishMVDMarkers( void );
	bool			currentSoundOverride;
	int				switchThrottle[ 3 ];
	static void		SendReady( bool isReady );
	int				voiceChatThrottle;

	void			SetupBuyMenuItems();

	idList<int>		privateClientIds;
	uint32_t		privatePlayers;

	// player who's rank info we're displaying
	idEntityPtr<idPlayer>		rankTextPlayer;

	idEntityPtr<idEntity>		flagEntities[ MAX_CTF_FLAGS ];
	idEntityPtr<idPlayer>		flagCarriers[ TEAM_MAX ];

	// updates the passed gui with current score information
	void			UpdateRankColor( idUserInterface *gui, const char *mask, int i, const idVec3 &vec );	

	// bdube: test scoreboard
	void			UpdateTestScoreboard( idUserInterface *scoreBoard );
	
	// ddynerman: gametype specific scoreboard
	void			UpdateScoreboard( idUserInterface *scoreBoard );

	void			UpdateDMScoreboard( idUserInterface *scoreBoard );
	void			UpdateTeamScoreboard( idUserInterface *scoreBoard );
	// openQ4: the Remastered scoreboard's rows as fields (ui-visual-design.md 14.14)
	void			PublishRemasteredScoreboard( idUserInterface *scoreBoard );
	void			UpdateSummaryBoard( idUserInterface *scoreBoard );
	bool			UpdateManagedSummaryResult( idUserInterface *gui, bool announce );
	const mpMatchViewTerminalResult_t *ClientTerminalResult( void );

	int				GetPlayerRank( idPlayer* player, bool& isTied );
	idPlayer*		GetRankLeader( void );
	const char*		GetPlayerRankText( idPlayer* player );
	const char*		GetPlayerRankText( int rank, bool tied, int score );

	const char*		BuildSummaryListString( idPlayer* player, int rankedScore );

	void			UpdatePrivatePlayerCount( void );

	typedef struct announcerSoundNode_s {
		announcerSound_t					soundShader;
		float								time;
		idLinkList<announcerSoundNode_s>	announcerSoundNode;
		int									instance;
		bool								allowOverride;
	} announcerSoundNode_t;

	idLinkList<announcerSoundNode_t>	announcerSoundQueue;
	announcerSound_t					lastAnnouncerSound;

	static const char* announcerSoundDefs[ AS_NUM_SOUNDS ];

	float			announcerPlayTime;

	void			PlayAnnouncerSounds ( void );

	int				teamScore[ TEAM_MAX ];
	int				teamDeadZoneScore[ TEAM_MAX];
	void			ClearTeamScores ( void );

	// openQ4: last ready tally computed by AllPlayersReady, for the warmup HUD
	int				readyPlayerCount;
	int				eligiblePlayerCount;

	void			UpdateLeader( idPlayer* oldLeader );

	void			ClearGuis( void );
	void			DrawScoreBoard( idPlayer *player );
	void			CheckVote( void );
	bool			AbortInheritedVoteForManagedMatch( void );
	bool			AllPlayersReady( idStr* reason = NULL );
	
	const char *	GameTime( void );

	bool			EnoughClientsToPlay( void );
	void			DrawStatSummary( void );
	// go through the clients, and see if they want to be respawned, and if the game allows it
	// called during normal gameplay for death -> respawn cycles
	// and for a spectator who want back in the game (see param)
	void			CheckRespawns( idPlayer *spectator = NULL );
	// puts a spectating player's camera on a living team mate, if they have not
	// already picked one themselves
	void			FollowTeamMate( idPlayer *p );

	void			FreeLight ( int lightID );
	void			UpdateLight ( int lightID, idPlayer *player );
	void			CheckSpecialLights( void );
	void			ForceReady();
	// when clients disconnect or join spectate during game, check if we need to end the game
	void			CheckAbortGame( void );
	void			CheckAbortGame( mpParticipantId departedParticipant,
						int departedGameSide, int departedCompetitionSide );
	void			MessageMode( const idCmdArgs &args );
	void			DisableMenu( void );
	void			SetMapShot( void );
	// scores in TDM
	void			VoiceChat( const idCmdArgs &args, bool team );

// RAVEN BEGIN
// mekberg: added
	void			UpdateMPSettingsModel( idUserInterface* currentGui );
// RAVEN END

	void			WriteStartState( int clientNum, idBitMsg &msg, bool withLocalClient );

	bool			RequestVoteMaps( int flags );

	void			SetMapList( const char *listName, const char *mapName, int gameTypeInt );
	void			SetVoteMapList( void );
	void			SetSAMapList( void );
};

ID_INLINE bool idMultiplayerGame::IsPureReady( void ) const {
	return pureReady;
}

ID_INLINE void idMultiplayerGame::ClearFrags( int clientNum ) {
	playerState[ clientNum ].fragCount = 0;
}

ID_INLINE bool idMultiplayerGame::IsInGame( int clientNum ) {
	return playerState[ clientNum ].ingame;
}

ID_INLINE int idMultiplayerGame::OpposingTeam( int team ) {
	return (team == TEAM_STROGG ? TEAM_MARINE : TEAM_STROGG);
}

ID_INLINE idPlayer* idMultiplayerGame::GetRankedPlayer( int i ) {
	if( i >= 0 && i < rankedPlayers.Num() ) {
		return rankedPlayers[ i ].First();
	} else {
		return NULL;
	}
}

ID_INLINE int idMultiplayerGame::GetRankedPlayerScore( int i ) {
	if( i >= 0 && i < rankedPlayers.Num() ) {
		return rankedPlayers[ i ].Second();
	} else {
		return 0;
	}
}

ID_INLINE int idMultiplayerGame::GetNumUnrankedPlayers( void ) {
	return unrankedPlayers.Num();
}

ID_INLINE idPlayer* idMultiplayerGame::GetUnrankedPlayer( int i ) {
	if( i >= 0 && i < unrankedPlayers.Num() ) {
		return unrankedPlayers[ i ];
	} else {
		return NULL;
	}
}

ID_INLINE int idMultiplayerGame::GetNumRankedPlayers( void ) {
	return rankedPlayers.Num();
}

ID_INLINE int idMultiplayerGame::GetTeamScore( int i ) {
	return playerState[ i ].teamFragCount;
}

ID_INLINE int idMultiplayerGame::GetScore( int i ) {
	return playerState[ i ].fragCount;
}

ID_INLINE int idMultiplayerGame::GetWins( int i ) {
	return playerState[ i ].wins;
}

ID_INLINE void idMultiplayerGame::ResetRconGuiStatus( void ) {
	if( mainGui) {
		mainGui->SetStateInt( "password_valid", 0 );
	}
}

// asalmon: needed access team scores for rich presence
ID_INLINE int idMultiplayerGame::GetScoreForTeam( int i ) {
	if( i < 0 || i >= TEAM_MAX ) {
		return 0;
	}
	return teamScore[ i ];
}

ID_INLINE int idMultiplayerGame::TeamLeader( void ) {
	if( teamScore[ TEAM_MARINE ] == teamScore[ TEAM_STROGG ] ) {
		return -1;
	} else {
		return ( teamScore[ TEAM_MARINE ] > teamScore[ TEAM_STROGG ] ? TEAM_MARINE : TEAM_STROGG );
	}
}

int ComparePlayersByScore( const void* left, const void* right );
int CompareTeamsByScore( const void* left, const void* right );

#endif	/* !__MULTIPLAYERGAME_H__ */

// RAVEN END
