//----------------------------------------------------------------
// Bot.h
//
// Multiplayer bots for openQ4.
//
// A bot is not an entity.  It occupies a real client slot, spawns a real
// idPlayer, and is driven by writing the user command the engine would have
// received from a remote player.  Everything downstream - movement physics,
// weapons, scoring, the scoreboard, game type rules - therefore treats it as
// an ordinary player with no special cases.
//
// Navigation comes from rvNavMesh, which is generated from the collision world
// at map load, so bots work on any map without an offline compile step.
//
// This file is only concerned with HOW a bot sees, moves, aims and shoots.
// HOW WELL and IN WHAT MANNER it does any of that is not code: it is resolved
// into one flat botTraits_t per bot per spawn by rvBotCharacterManager, out of
// a skill curve, a play style and a named character, all of which are content
// files in the pak.  See BotCharacter.h.
//----------------------------------------------------------------

#ifndef __GAME_MP_BOT_H__
#define __GAME_MP_BOT_H__

#include "BotFrameProfile.h"

// rvBot holds a botTraits_t by value and a character pointer, so the contract
// header has to be complete here rather than forward declared.  Game_local.h
// includes it immediately before this file too; the include guard makes saying
// so twice free, and it keeps Bot.h readable on its own.
#include "BotCharacter.h"

class idPlayer;
class idItem;

//----------------------------------------------------------------
// What the bot is currently trying to do.  Combat overrides navigation for
// aiming and firing but not for movement: a bot still routes to a goal while
// shooting at whatever it can see.
//----------------------------------------------------------------
typedef enum {
	BOTGOAL_NONE,
	BOTGOAL_ROAM,				// no reason to be anywhere in particular
	BOTGOAL_ITEM,				// heading for a pickup
	BOTGOAL_ENEMY,				// closing on a player
	BOTGOAL_OBJECTIVE			// flag, carrier, rescue, or control point
} botGoalType_t;

//----------------------------------------------------------------
// The ways a bot can get a combat decision wrong.  One is picked at a time on
// a roll against botTraits_t::mistakeChance and lasts mistakeMsec.
//
// These are deliberately things a player does under pressure, not handicaps.
// A bot that simply aimed worse would read as broken; a bot that loses its
// target for half a second, or pulls the trigger before the sight has stopped
// moving, reads as beatable.
//----------------------------------------------------------------
typedef enum {
	BOTMISTAKE_NONE,
	BOTMISTAKE_LOSETRACK,		// the believed target position stops being updated
	BOTMISTAKE_MISTIMEDSHOT,	// fires without waiting for the aim to settle
	BOTMISTAKE_WRONGWEAPON,		// keeps whatever is in hand instead of switching
	BOTMISTAKE_LATEDODGE,		// takes twice as long to start dodging incoming fire
	BOTMISTAKE_NUM
} botMistake_t;

//----------------------------------------------------------------
// rvBot
//----------------------------------------------------------------
class rvBot {
public:
							rvBot( void );

	// skillOverride is the optional per-bot level from "addbot <name> <skill>".
	// -1 means "follow bot_skill", which is what every other caller wants.
	void					Init( int clientNum, const char *name, int skillOverride = -1,
							  bool requireExactCharacter = false );
	void					Shutdown( void );

	// Re-point this bot at the character of the same name in a freshly parsed
	// roster.  Every pointer handed out before a reload dangles, so this has to
	// run for every live bot before anything reads a personality again.
	void					RebindCharacter( void );

	bool					IsActive( void ) const { return active; }
	int						GetClientNum( void ) const { return clientNum; }
	const char *			GetName( void ) const { return name.c_str(); }

	const rvBotCharacter *	GetCharacter( void ) const { return character; }
	const botTraits_t &		GetTraits( void ) const { return traits; }

	// The integer level the personality was resolved from - bot_skill, or this
	// bot's own override - and the fractional level it actually plays at once
	// bot_skillVariance has had its say.  Both are reported by botlist so what
	// the operator reads is what the bot is using.
	int						GetSkillLevel( void ) const { return skillLevel; }
	float					GetEffectiveSkill( void ) const { return effectiveSkill; }

	// Fill in this bot's identity so any userinfo update keeps it in the game
	// instead of dropping it back to the join menu.
	void					FillUserInfo( idDict &info );

	// One server frame.  Writes the client's user command in place.
	void					Think( usercmd_t &cmd );
	void					OnKnockback( const idVec3 &kick );
	void					RefreshKnockbackMove( usercmd_t &cmd );

	// Called when the player entity behind this bot respawns.
	void					OnSpawn( void );

	// -- chat, all server side and all driven from rvBotManager --
	//
	// Queued rather than sent: a bot that answers a frag on the frame it landed
	// reads as a script.  A pending line is dropped if the bot leaves.
	void					QueueChat( rvBotChatEvent event, const char *other, const char *weapon, const char *item );
	// Answers an order in team chat.  No chatiness roll: the commander has to
	// know the order landed.  The shared flood throttle still applies.
	void					QueueOrderAck( const char *commander );
	bool					TryQueueReply( const idStr &normalizedText, bool sourceIsBot, bool addressed,
									   const char *other, bool teamOnly );
	void					SayFarewell( void );

	void					OnKilledEnemy( idPlayer *victim, int methodOfDeath );
	void					OnKilledBy( idPlayer *killer, int methodOfDeath );
	void					OnDamaged( idPlayer *attacker, int damage, const idVec3 &dir );
	void					OnMatchStart( void );
	void					OnMatchEnd( bool won );

	// Move every absolute deadline forward by one frozen competitive-pause
	// frame.  gameLocal.time advances through a pause; this bot does not think.
	void					ShiftMatchTime( int deltaMsec );

	// Read-only coordination state used by rvBotManager to keep a whole team
	// from reserving the same pickup or support target.
	idEntity *				GetGoalEntity( void ) const { return goalEntity.GetEntity(); }
	int						GetGoalType( void ) const { return goalType; }

private:
	friend class rvBotManager;

	// Resolves the personality.  Called once per spawn, never per frame, so a
	// mid-match bot_skill edit lands on the next respawn instead of retuning a
	// bot in the middle of a fight it is already losing.
	void					ResolveTraits( void );

	idPlayer *				GetPlayer( void ) const;

	void					UpdateEnemy( idPlayer *self );
	void					UpdateGoal( idPlayer *self );
	void					UpdateMovement( idPlayer *self, usercmd_t &cmd );
	bool					HoldForPad( idPlayer *self, idVec3 &moveDir );
	void					AdoptPadLaunch( idPlayer *self );
	// Diagnostic only: the fixed full air input bot_probeAir replays, then braking once grounded.
	void					ProbeAirControl( idPlayer *self, usercmd_t &cmd );
	void					ReportProbeGrounding( idPlayer *self );
	void					TryDodgeJump( idPlayer *self, const idVec3 &direction, usercmd_t &cmd );
	bool					HoldDodgeJump( idPlayer *self, usercmd_t &cmd );
	void					ResetDodgeJump( void );
	void					UpdateAim( idPlayer *self, usercmd_t &cmd );
	// The view with nothing to shoot at: well along the route rather than at
	// the next corner, and sweeping the room while standing watch.
	idVec3					IdleAimPoint( idPlayer *self, const idVec3 &eye );
	void					UpdateWeapon( idPlayer *self );
	void					UpdateFire( idPlayer *self, usercmd_t &cmd );
	void					UpdateChat( idPlayer *self );
	// Spends buy-menu credits where the rules allow it: a real weapon first,
	// then armour, then ammunition, then a second gun or a team powerup.
	void					UpdateBuying( idPlayer *self );

	// Everything that happens the moment a target registers: the reaction
	// deadline, the peripheral penalty, the mistake roll and the belief reset.
	void					AcquireEnemy( idPlayer *self, idPlayer *foe, bool fresh );

	void					RollMistake( void );
	bool					MistakeActive( int mistake ) const;

	// Live MP-resolved projectile launch data.  Hitscan/melee return zero speed;
	// arcing projectiles return both speed and gravity for ballistic aim.
	float					ProjectileSpeed( idPlayer *self ) const;
	idVec3					ProjectileGravity( idPlayer *self ) const;
	bool					BurstAllows( idPlayer *self );

	float					TargetScore( idPlayer *other, float distSqr ) const;
	idVec3					EnemyPursuitOrigin( void ) const;
	bool					WantsHealth( idPlayer *self ) const;
	bool					AtObjectiveHoldPosition( idPlayer *self ) const;
	bool					HoldAllowsMove( idPlayer *self, const idVec3 &direction ) const;
	bool					IsDodging( idPlayer *self ) const;
	void					TrackDamage( idPlayer *self );
	// threatDirection is the direction of travel of what is coming (a round's
	// velocity, or the damage direction); threatPoint, when known, is where it
	// passes closest or lands, and the dodge goes away from it.
	void					ScheduleDodge( const idVec3 &threatDirection, const idVec3 *threatPoint = NULL );
	// The dodge step for this frame: perpendicular to the threat, or the
	// strafe side around the current enemy when the threat has no direction.
	bool					DodgeDirection( idPlayer *self, idVec3 &direction ) const;
	// Flips the combat strafe side on the personality's rhythm.
	void					UpdateStrafeRhythm( void );

	bool					CanSee( idPlayer *self, idEntity *other, idVec3 *visiblePoint = NULL ) const;
	bool					IsEnemy( idPlayer *self, idPlayer *other ) const;

	// Hearing.  A noise locates an enemy approximately and turns the bot toward
	// it; it never grants a shot, which still needs a sighting and pays the
	// full reaction for it.
	void					UpdateHearing( idPlayer *self );
	int						EnemyLastKnownTime( void ) const;

	// How the bot's whole loadout and condition compares with this opponent's,
	// 1 for an even trade.  Drives fight-or-flight, not aim.
	float					CombatAdvantage( idPlayer *self, idPlayer *foe ) const;
	float					BestWeaponPower( idPlayer *self ) const;
	// Breaking off: hurt below the retreat line, or outmatched by the current
	// opponent.  The bot still fights while it withdraws toward health.
	bool					Withdrawing( idPlayer *self ) const;

	bool					Repath( idPlayer *self, const idVec3 &goal, bool preserveProgress = false );
	bool					AdvancePath( idPlayer *self );
	// Standing on a floor the current leg does not cross - knocked off it,
	// carried away by a pad, or fallen past a landing - or beside it where the
	// walk to the corner no longer exists.  The route is then wrong, not slow,
	// and is replaced at once.
	bool					OffRoute( idPlayer *self, float &offHeight );
	idEntity *				PickItemGoal( idPlayer *self, rvNavPath &goalPath, float &goalUtility );
	float					ItemUtility( idPlayer *self, idItem *item ) const;
	// Milliseconds until a taken item returns as far as this bot knows, 0 when
	// it is available now, -1 when the bot has no idea.
	int						ItemPendingMsec( const idItem *item ) const;
	// A patrol destination with its proven route: the item spots that matter
	// and the last place an enemy was known, weighted against recent visits.
	bool					PickRoamGoal( idPlayer *self, idVec3 &roam, rvNavPath &roamPath );
	void					RememberRoamPoint( const idVec3 &point );
	float					RoamNovelty( const idVec3 &point ) const;
	float					PathDistanceRemaining( const idVec3 &origin ) const;
	void					AbandonGoal( int forMsec );
	bool					RecoverToNavMesh( idPlayer *self, usercmd_t &cmd );
	bool					IsAvoided( const idEntity *ent ) const;
	void					Avoid( idEntity *ent, int forMsec );
	void					ResetTraversal( void );
	bool					GuardLandingHealth( idPlayer *self, usercmd_t &cmd );
	bool					AlignJumpDropApproach( idPlayer *self, usercmd_t &cmd );
	bool					HandleJumpTakeoff( idPlayer *self, usercmd_t &cmd );
	bool					GuardLandingContinuation( idPlayer *self, usercmd_t &cmd );
	bool					RecoverInAir( idPlayer *self, usercmd_t &cmd );

	void					ApplyMove( const idVec3 &moveDir, usercmd_t &cmd ) const;
	void					PressJump( usercmd_t &cmd ) const;

// openQ4 BEGIN
	// Liquid handling. The navmesh is generated against solids only, so liquid volumes are
	// invisible to routing and all of this has to work locally, off point queries.
	int						LiquidAtFeet( void ) const;
	int						LiquidAtEye( void ) const;
	void					UpdateLiquidMovement( idPlayer *self, usercmd_t &cmd );
	idVec3					AvoidLiquidHazard( const idVec3 &moveDir ) const;
// openQ4 END

	int						clientNum;
	idStr					name;
	bool					active;
	bool					initialTeamAssignmentPending;	// ignore inherited slot userinfo once
	int						teamAssignment;		// pre-spawn reservation, TEAM_NONE when unset
	int						teamAssignmentInstance;

	// -- personality --
	const rvBotCharacter *	character;			// NULL with bot_characters 0, or an empty roster
	botTraits_t				traits;
	int						skillOverride;		// per-bot level from addbot, -1 for bot_skill
	int						skillLevel;			// integer level the traits were resolved from
	float					effectiveSkill;		// that level after bot_skillVariance

	// -- navigation --
	rvNavPath				path;
	int						pathCorner;
	int						pathTime;			// when the current route was built
	idVec3					goalOrigin;
	idEntityPtr<idEntity>	goalEntity;
	int						goalType;
	int						goalGiveUpTime;		// abandon a goal that is not working out
	float					goalUtility;			// benefit used for hysteresis and coordination
	int						goalCommitUntil;		// do not chatter between similar choices
	float					goalBestDistance;		// route progress extends a sensible deadline
	int						goalProgressTime;
	int						objectiveKind;			// botObjectiveKind_t for objective refreshes
	bool					objectiveHoldPosition;
	int						repathFailures;
	int						enemyPathTime;		// last attempt to route to an enemy, successful or not
	int						nextGoalSelectTime;	// throttles the full goal scan
	int						holdUntil;			// a patient bot standing still on a finished wander
	int						noRouteSince;		// pathTime is route age, not recovery age

	// A non-walk link is an action, not an ordinary proximity corner.  The bot
	// approaches its source, commits to the action, and only advances after it
	// has actually reached the destination side.
	int						traversalCorner;
	idVec3					traversalStartOrigin;
	int						traversalStartTime;
	bool					traversalEntered;
	bool					traversalStarted;
	int						landingCheckPathTime;
	int						landingCheckCorner;
	float					landingCheckLimit;

	// A voluntary hop is proved for one world-space input. Aim and firing may
	// change in flight, but the movement must keep that input until touchdown.
	bool					dodgeHopActive;
	bool					dodgeHopAirborne;
	int						dodgeHopStartTime;
	int						dodgeHopCheckTime;
	idVec3					dodgeHopDirection;
	float					dodgeHopWishSpeed;

	// Off-mesh recovery target, cached because finding it is a wide search.
	idVec3					recoverTarget;
	int						recoverSearchTime;
	bool					recoverValid;

	// With nowhere known to head for, the bot picks a heading and keeps it for
	// a while.  Turning a fixed amount every frame is a per-frame rate, and it
	// only ever walks the bot in a circle it cannot escape.
	int						recoverWanderUntil;
	float					recoverWanderYaw;

	// Goals that did not work out, so the bot does not ping-pong between two
	// items it cannot take.  One slot would only ever remember the last one.
	static const int		MAX_AVOID_GOALS = 8;
	struct avoidGoal_s {
		idEntityPtr<idEntity>	ent;
		int						until;
	}						avoidGoals[MAX_AVOID_GOALS];
	int						avoidNext;

	// -- stuck detection --
	// Progress is measured against the route, not against displacement: a bot
	// that is moving but not getting closer to its corner is stuck, and one
	// that is standing still already fails the same test.  There is deliberately
	// no "has it physically moved" member here - it read as a second signal and
	// was never anything of the kind.
	int						stuckTime;
	int						unstickUntil;
	float					unstickSide;
	int						stuckChecks;
	int						stuckPathCorner;
	float					stuckCornerDistance;
	int						offRouteProofTime;	// when OffRoute last asked the navmesh about the leg from where the bot stands
	int						combatSteeringTime;	// last frame combat footwork altered the route direction
	int						jumpRetries;		// run-ups taken at the current route's failed jump
	int						jumpRetryPathTime;	// pathTime of the route jumpRetries counts for
	int						climbCheckTime;		// since when the bot has pressed toward climbCheckTarget, 0 for not
	idVec3					climbCheckTarget;	// the corner above it, or the volume it is stepping into
	float					climbCheckDistance;	// how far off that target was then
	int						padHoldSince;		// since when the bot has waited at a pad for its corridor to clear
	// The pad this bot is about to enter - its route's next action, within
	// hold reach - published for the other bots' corridor priority: two bots
	// released from opposite pads in the same frame met over q4ctf5's void.
	int						padApproachEntity;	// entity number, -1 for none
	int						padApproachSince;	// when the bot came within reach of it
	int						padApproachTime;	// last frame the approach was confirmed
	idVec3					padApproachLaunch;	// the final walking corner before the pad
	idVec3					padApproachLanding;	// the pad corner's landing
	// Set only by the local-cheat traversal probes; cleared by OnSpawn.
	bool					probeWatch;			// report the probed bot's first grounding
	bool					probeAirActive;		// bot_probeAir owns the input instead of the controller
	idVec3					probeAirDirection;	// its fixed horizontal input, zero for none
	int						probeStartTime;
	bool					probeAirborneSeen;	// the probed bot has left the ground since the probe
	int						airRecoverReported;	// last airborne-recovery start logged, so one line per fall
	bool					airRecoverSliding;	// proven input before the knock leaves the floor
	bool					airRecovering;		// braking a knocked fall until it has stopped on the ground
	int						knockbackTime;		// last applied knockback, which alone can start that brake
	int						knockbackFrameTime;	// the frame knockbackFrameKick sums
	idVec3					knockbackFrameKick;	// every kick of that frame, a shotgun's pellets together
	int						airborneSince;		// when the current flight left the ground, 0 on the ground
	bool					airborneVoluntary;	// that flight began with the bot's own jump press
	int						jumpPressTime;		// last grounded frame the bot pressed jump
	idVec3					airRecoverWish;		// the air input that brake holds, zero for none
	int						airRecoverPlanTime;	// when it was last chosen
	int						airRecoverLanded;	// when that fall touched down, 0 while still airborne

	// -- combat --
	idEntityPtr<idPlayer>	enemy;
	int						enemyAcquiredTime;
	int						enemyLastSeenTime;
	idVec3					enemyLastSeenOrigin;	// last known position: seen, or heard approximately
	idVec3					enemyLastSeenVelocity;
	idVec3					enemyVisiblePoint;
	int						enemyLastHeardTime;	// last noise that located the current enemy
	int						nextHearingTime;
	bool					targetPickBest;		// this engagement's targetSelection roll
	bool					aimAtFeet;			// this engagement's splash-aim roll
	idEntityPtr<idPlayer>	lastAttacker;
	int						lastAttackerTime;

	// -- strategy --
	// Where recent wanders ended, so patrols cover the map instead of
	// shuttling between the two best item spots.
	static const int		ROAM_HISTORY = 6;
	idVec3					roamHistory[ROAM_HISTORY];
	int						roamHistoryTime[ROAM_HISTORY];
	int						roamHistoryNext;
	int						goalSelectedTime;	// when the current goal was committed
	bool					goalItemPending;	// waiting out a timed respawn at the item
	bool					outmatched;			// decision-tick verdict of CombatAdvantage
	bool					selfStrong;			// armed, healthy or powered up enough to take on a powered target

	// -- aim --
	// The bot aims at where it BELIEVES the target is, which trails where the
	// target actually is by aimTrackTimeConst, less the share of its perceived
	// motion aimAnticipation carries forward.  aimPoint is that belief plus
	// the projectile lead, and it is what both the view and the fire cone are
	// measured against - if the two disagreed the bot would aim at the lead
	// point and then refuse to shoot because the centre was outside the cone.
	idAngles				aimAngles;
	idAngles				aimRate;			// degrees/second, the slew's own velocity
	idVec3					aimBelief;
	bool					aimBeliefValid;
	idVec3					aimPerceivedVelocity;	// the target's velocity as the bot has registered it
	idVec3					aimPoint;
	bool					aimPointValid;
	idAngles				aimTrackAngles;		// last frame's angles to the aim point
	bool					aimTrackValid;
	idAngles				aimTargetRate;		// degrees/second, low passed: how fast the aim point crosses the view
	float					aimAngVel;			// degrees/second, low passed
	float					aimLeadRoll;		// this burst's lead error multiplier
	float					tremorPhase[4];		// seeded from clientNum so no two bots shake in step

	// Where the view rests with nothing to shoot at.  UpdateMovement says when
	// the bot is standing watch - a finished wander, a timed item - and the
	// sweep starts from the facing it stopped with.
	bool					idleScan;
	int						scanStartTime;		// 0 while not sweeping
	float					scanBaseYaw;

	// -- trigger --
	int						enemyFireTime;		// reaction deadline, set once per acquisition
	int						onTargetTime;		// how long the aim has sat inside the cone
	int						burstEndTime;
	int						burstRestTime;
	int						nextSingleShotTime;
	bool					wasFiring;			// so bot_debugAim logs the shot, not the frame

	// -- mistakes --
	int						mistake;
	int						mistakeEndTime;
	int						nextMistakeTime;

	// -- dodging --
	int						damageStamp;		// last idPlayer::lastDmgTime this bot reacted to
	int						dodgeStartTime;
	int						dodgeEndTime;
	int						nextThreatScanTime;
	float					strafeSide;
	int						strafeFlipTime;
	bool					dodgeJump;
	idVec3					dodgeDirection;		// world-space step chosen for the pending dodge, zero for "strafe"

	// -- chat --
	idStr					chatPending;
	bool					chatTeamOnly;
	bool					chatPendingIsReply;
	int						chatSendTime;
	int						killStreak;
	int						revengeTarget;		// client number owed a debt, -1 for none
	bool					announcedEntry;

	int						nextWeaponTime;
	int						lastWeaponSwitchTime;
	int						nextBuyTime;		// throttles buy-menu decisions
	int						nextRejoinTime;		// throttles the leave-spectate request
	int						nextStatusTime;		// bot_debug 2 throttle
	int						nextAimDebugTime;	// bot_debugAim 2 throttle
};

//----------------------------------------------------------------
// rvBotManager
//
// Owns the bot slots and is the only thing the rest of the game talks to.
//----------------------------------------------------------------
//----------------------------------------------------------------
// A standing order a human team-mate gave in team chat: "kane defend",
// "everyone follow me", "sorg and bagby hold here".  Orders are gameplay, not
// chat flavour, so they work with bot chat switched off; the objective planner
// reads them before it shares roles out, and team-mates allocate around them.
//----------------------------------------------------------------
typedef enum {
	BOTORDER_NONE,				// free play, or what "dismissed" restores
	BOTORDER_ATTACK,			// take the enemy flag; outside CTF, play the map
	BOTORDER_DEFEND,			// hold the base, or the spot the order was given at
	BOTORDER_FOLLOW				// stay with the player who gave it
} botOrderKind_t;

typedef struct botOrder_s {
	int						kind;				// botOrderKind_t
	int						commander;			// client number of the player who gave it
	int						commanderSpawnId;	// gameLocal.spawnIds: a reused slot is someone else
	int						team;				// the bot's team when ordered; a team change ends it
	idEntityPtr<idEntity>	anchor;				// a spot defence's hold centre, NULL for the base
	int						issuedTime;
} botOrder_t;

class rvBotManager {
public:
							rvBotManager( void );

	void					Init( void );
	void					Shutdown( void );

	// Called once per server frame from idGameLocal::RunFrame, before any
	// entity thinks, so the bots' commands are in place when players read them.
	void					Think( void );
	void					FinishFrameProfile( void );

	// Console entry points.  skillLevel is the optional per-bot override from
	// "addbot <name> <skill>"; -1 means follow bot_skill.
	bool					AddBot( const char *name, int skillLevel = -1,
								bool requireExactCharacter = false );
	bool					RemoveBot( const char *name );
	void					RemoveAll( void );
	void					ListBots( bool weaponDetail = false ) const;
	// Local cheat diagnostic: one bot, or two untargetable pad-traffic bots,
	// plus the local observer. An optional pad entry replays its walking approach;
	// climb restores a pending ordinary hop window without forcing its input.
	bool					ProbeTraversal( int clientNum, const idVec3 &origin, const idVec3 &velocity,
											const idVec3 &landing, navTravelType_t travel, idEntity *pad = NULL, bool dodge = false,
									const idVec3 *airDirection = NULL, const idVec3 *entryOrigin = NULL,
									const idVec3 *nextJump = NULL, bool naturalWalk = false, bool padTraffic = false,
									bool recover = false, bool climb = false );
	// The health, and knockback time, the next traversal probe gives its bot;
	// 0 for unchanged.
	bool					SetProbeHealth( int clientNum, int health, int knockbackMsec = 0, const idVec3 &kick = vec3_origin, int armor = -1 );

	// Re-bind every live bot after the character roster has been reparsed.
	// botreload must call this before the next frame runs, or every bot is
	// reading a freed character.
	void					RebindCharacters( void );

	// Lifecycle hooks.
	void					OnSpawnPlayer( int clientNum, bool isBot, const char *botName );
	void					OnClientDisconnect( int clientNum );
	void					OnMapShutdown( void );
	// Called from idPlayer::SpawnToPoint for every spawn, including the
	// teleports a round or match restart makes without killing anyone.
	void					OnPlayerRespawned( int clientNum );

	// Match hooks, all server side.  OnMatchStart/OnMatchEnd belong on the
	// GAMEON and GAMEREVIEW transitions in rvGameState::NewState; OnPlayerDeath
	// belongs beside statManager->Kill in idMultiplayerGame::PlayerDeath, which
	// is after scoring has been committed, so a bot reacting there already sees
	// the post-frag board.
	void					OnMatchStart( void );
	void					OnMatchEnd( void );
	void					OnPlayerDeath( idPlayer *dead, idPlayer *killer, int methodOfDeath );

	// Called once per frozen frame from
	// idMultiplayerGame::RebaseCompetitivePauseFrame.  idGameLocal::RunFrame
	// keeps advancing gameLocal.time through a competitive pause while it skips
	// rvBotManager::Think, so without this every bot resumes with every deadline
	// already expired: reactions forgiven, goals abandoned, stuck checks fired.
	void					ShiftMatchTime( int deltaMsec );
	void					OnPlayerDamaged( idPlayer *victim, idEntity *attacker, int damage, const idVec3 &dir );
	// Called from idPlayer::Damage whenever knockback is applied to a bot,
	// including impulses that friendly fire or armour leave without damage.
	void					OnPlayerKnockback( idPlayer *victim, const idVec3 &kick );
	void					RefreshKnockbackMove( idPlayer *player, usercmd_t &cmd );

	// Called once for an accepted typed-chat line on the server.  It chooses at
	// most one visible bot to answer and lets that bot's character content,
	// chatiness, delay and the shared flood throttle decide whether it speaks.
	void					OnChatMessage( int sourceClientNum, bool teamOnly, const char *visibleText );

	// The order this bot is under, or NULL for free play.  An order whose bot
	// changed team, or a follow order whose commander left, reads as none.
	const botOrder_t *		GetOrder( int clientNum ) const;

	// The navmesh node a player stands on, re-snapped only once it has moved
	// a cell or two, so role allocation can compare route bounds for the
	// whole team every decision without a ring of probes per player.
	int						ClientNavNode( int clientNum ) const;

	bool					IsBot( int clientNum ) const;
	// The pad a bot is about to enter, with when it came within reach and the
	// flight it will make, or false when it is approaching none this frame.
	bool					PadApproachOf( int clientNum, int &padEntityNum, int &since,
										   idVec3 &launch, idVec3 &landing ) const;
	int						NumBots( void ) const;
	int						GoalClaimCount( const rvBot *requester, const idEntity *goal ) const;
	// The objective role a bot is executing right now, BOTOBJ_NONE for anything
	// else, and the entity it serves.  Role allocation reads these to give an
	// incumbent a margin, so roles do not swap with every step anyone takes.
	int						ObjectiveKindOf( int clientNum ) const;
	const idEntity *		ObjectiveEntityOf( int clientNum ) const;

	// Time slicing.  A full goal decision prices pickups and routes, and when
	// several bots' decision ticks land on one server frame that frame pays for
	// all of them.  Each frame grants a few decisions; a bot refused one simply
	// decides on the next frame, a sixtieth of a second later.
	bool					ClaimDecision( void );
	void					FillUserInfo( int clientNum, idDict &info );

	// Every idItem the map has spawned, hidden or not, refreshed once per
	// server frame before any bot thinks.  A decision tick reads this instead
	// of walking the whole spawned-entity list again for every bot.
	const idList<idItem *> &	GetItems( void ) const { return items; }

	// Item timing.  Called from idItem::Pickup when a respawn is scheduled.
	// Only bots that witness the pickup - the taker, anyone within earshot,
	// or everyone for a globally announced powerup - learn the timer, and each
	// keeps it with its own itemTiming accuracy.
	void					OnItemPickup( idItem *item, idPlayer *player, float respawnSeconds );
	// Absolute respawn time as clientNum believes it, or 0 when it has no timer.
	int						KnownItemRespawn( const idItem *item, int clientNum ) const;
	// When the item was last taken and by whom; -1 for nobody since map load.
	int						ItemPickupTime( const idItem *item ) const;

	// The loudest recent noise each player made this server frame, for hearing.
	typedef struct botNoise_s {
		idVec3				origin;
		float				range;			// audible distance for an average listener
		int					time;			// gameLocal.time it was last made, 0 for never
	} botNoise_t;
	const botNoise_t &		GetNoise( int clientNum ) const { return noises[clientNum]; }

	// Selects the least-populated side in the bot's gameplay instance.  The
	// count includes active bot reservations whose idPlayer has not spawned yet.
	int						BalancedTeamForBot( int clientNum, int &balanceInstance ) const;

	// Builds the navmesh if it has not been built for this map yet.  Returns
	// false when the map has no walkable ground the bots can use.
	bool					EnsureNavMesh( void );

private:
	void					CheckMinPlayers( void );
	// The bot autofill should remove now, or -1 to wait: from the larger team,
	// dead or queued before alive, and never a seated duel or a live round
	// player in the middle of what they are playing.
	int						PickBotToRemove( void ) const;
	bool					RemoveBotAt( int clientNum );
	void					UpdateLeaderChat( void );
	void					ResetReplyCooldowns( void );
	// Parses "<names or everyone> <order>" from a human in team chat and gives
	// the order.  True when the line was an order, which is then not small talk.
	bool					HandleTeamOrder( int sourceClientNum, bool teamOnly, const idStr &normalizedText );
	void					ClearOrder( int clientNum );
	const char *			PickBotName( void ) const;
	void					RefreshWorldCache( void );
	void					UpdateProfile( double thinkTicks );

	rvBot					bots[MAX_CLIENTS];
	bool					navBuilt;
	bool					navFailed;
	int						nextMinPlayerCheck;

	idList<idItem *>		items;

	int						probeHealth;			// pending bot_probeHealth value
	int						probeKnockback;		// and its knockback milliseconds
	int						probeArmor;			// optional exact armour; -1 preserves spawn inventory
	idVec3					probeKick;			// already included in the probe velocity; reaction only
	int						decisionFrame;			// gameLocal.framenum the count belongs to
	int						decisionsThisFrame;

	// Indexed by entity number; itemTimerSpawnId ties an entry to one spawn of
	// that entity so a reused slot never inherits a stale timer.
	int						itemTimerSpawnId[MAX_GENTITIES];
	int						itemPickupAt[MAX_GENTITIES];
	int						itemRespawnAt[MAX_GENTITIES];
	unsigned int			itemTimerKnownBy[MAX_GENTITIES];	// bit per client

	botNoise_t				noises[MAX_CLIENTS];
	void					RecordNoise( int clientNum, const idVec3 &origin, float range );
	void					ResetAwareness( void );

	// bot_profile accumulators.  Wall-clock measurement only; nothing here
	// feeds a decision.
	double					profileThinkTicks;
	double					profileFrameTicks;
	int						profileFrameNumber;
	double					profileMaxTicks;
	int						profileFrames;
	rvBotFrameProfile		profileDistribution;
	int						nextProfileTime;

	// AddBot allocates the client slot and the engine calls straight back into
	// OnSpawnPlayer, which is where the bot is actually built - so a per-bot
	// skill has to be parked here for the length of that call.
	int						pendingSkill;
	bool					pendingExactCharacter;

	// Who is winning.  idMultiplayerGame::UpdateLeader is declared and never
	// defined and the lead announcements in CommonRun are written for the local
	// player only, so there is no server side hook to hang lead chat on.
	int						leaderClientNum;
	int						nextLeaderCheck;
	int						nextReplySourceTime[MAX_CLIENTS];

	botOrder_t				orders[MAX_CLIENTS];

	mutable int				clientNode[MAX_CLIENTS];
	mutable idVec3			clientNodeOrigin[MAX_CLIENTS];
	mutable int				clientNodeTime[MAX_CLIENTS];
};

extern rvBotManager			botManager;

#endif /* !__GAME_MP_BOT_H__ */
