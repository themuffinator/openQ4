//----------------------------------------------------------------
// Bot.cpp
//
// Multiplayer bot behaviour and user command synthesis.  See Bot.h.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"

// Game_local.h forward declares idProjectile but does not define it, and the
// projectile lead needs the two static accessors that read a launch speed and a
// gravity out of an entityDef dictionary.
#if !defined(__GAME_PROJECTILE_H__)
	#include "../Projectile.h"
#endif

#include "../mp/GameState.h"
#include "../mp/RoundModes.h"
#include "Bot.h"
#include "BotCombat.h"
#include "BotObjective.h"

rvBotManager				botManager;

/*
===============================================================================

	Tuning.

	Only the things that are the same for every bot live here.  Anything a
	server operator, a designer or a mod would ever want to argue about is a
	trait, and traits come from content - see BotCharacter.h.

===============================================================================
*/

static const int	BOT_REPATH_COMBAT_MSEC	= 750;		// chasing a player that keeps moving
static const int	BOT_REPATH_IDLE_MSEC	= 3000;		// heading for a fixed goal
static const float	BOT_CORNER_REACH		= 28.0f;	// horizontal distance that counts as "arrived"
static const float	BOT_CORNER_REACH_Z		= 24.0f;	// ordinary walk corners stay on their sampled floor
static const float	BOT_TRAVEL_ENTRY_REACH	= 12.0f;	// deliberate actions must be entered, not skipped a cell early
static const float	BOT_TRAVEL_REACH_Z		= 8.0f;		// landing confirmation is much tighter than ordinary steering
static const float	BOT_TRAVEL_OVERSHOOT	= 64.0f;	// a valid jump may land beyond its one-cell destination
static const int	BOT_TRAVEL_COMMIT_MSEC	= 1500;		// do not let generic unstick fight a traversal in progress
static const int	BOT_TRAVEL_TIMEOUT_MSEC	= 10000;	// a broken authored transport must still release its route
static const float	BOT_OFF_ROUTE_BELOW		= 80.0f;	// under a leg by more than its 48 dip and a step: fallen off it
static const float	BOT_OFF_ROUTE_ABOVE		= 96.0f;	// over a walk leg by more than a jump and a step: carried off it
static const float	BOT_ESCORT_KEEP_RADIUS	= 320.0f;	// an escort this close to its player is doing its job
static const float	BOT_OFF_ROUTE_LATERAL	= 384.0f;	// this far from the leg the bot was carried or teleported
static const int	BOT_OFF_ROUTE_PROOF_MSEC = 250;		// how often a bot off its leg asks whether the walk to its corner still holds
static const int	BOT_ESCORT_RETRY_MSEC	= 3000;		// an escort that lost its way tries again this soon
static const float	BOT_PAD_LAUNCH_SPEED	= 320.0f;	// faster than a jump climbs or a player runs: a pad has it
static const int	BOT_PAD_MIN_FLIGHT_MSEC	= 250;		// a pad flight is at least this long
static const int	BOT_PAD_TIMEOUT_MSEC	= 4000;		// and no stock pad's lasts this long: a bot bouncing gives up
static const int	BOT_JUMP_RETRY_MSEC		= 400;		// back on the takeoff floor this long after leaving it: fell short
static const int	BOT_JUMP_RETRIES		= 2;		// run-ups a bot takes at one jump before the stuck detector decides
static const float	BOT_JUMP_RUNUP_REACH	= 96.0f;	// a corner this close behind a takeoff serves as its run-up
static const float	BOT_CLIMB_RISE			= 4.0f;		// a walk corner this far above the feet is a climb
static const int	BOT_CLIMB_HOP_MSEC		= 250;		// pressed toward one this long without closing in: hop
static const float	BOT_CLIMB_PROGRESS		= 8.0f;		// closing in by less than this is pressing, not walking
static const float	BOT_PAD_HOLD_REACH		= 80.0f;	// this close to touching a pad a bot may wait for its corridor
static const float	BOT_PAD_CORRIDOR_WIDTH	= 64.0f;	// a flyer this close to a pad's line is in its path
static const int	BOT_PAD_HOLD_MSEC		= 2000;		// reconsider the route if its pad corridor stays busy
static const int	BOT_PAD_APPROACH_STALE_MSEC = 100;	// a pad approach not confirmed this recently has lapsed
static const float	BOT_PAD_APPROACH_REACH	= 320.0f;	// a bot this close to its pad publishes the approach
static const float	BOT_EDGE_LOOKAHEAD		= 0.15f;	// seconds of velocity the void-edge guard looks ahead
static const float	BOT_SAFE_FALL			= 512.0f;	// floor this far down is a fall, not the void (NAV_MAX_DROP)
static const float	BOT_AIR_RECOVER_STOP_SPEED = 10.0f;	// match RecoveryBrakeClear's final friction-only coast
static const int	BOT_AIR_RECOVER_RUNOUT_MSEC = 600;	// and at most this long after touchdown
static const int	BOT_AIR_RECOVER_KNOCK_MSEC = 1000;	// a fall this soon after a knockback may be braked
static const int	BOT_AIR_RECOVER_TAKEOFF_MSEC = 50;	// that came at most this long before the bot left the ground
static const int	BOT_AIR_RECOVER_REPLAN_MSEC = 250;	// how often a held fall re-chooses its air input
static const int	BOT_AIR_RECOVER_SLIDE_MSEC = 400;	// a walk-off this soon after a knock is the knock's slide
static const int	BOT_AIR_RECOVER_PRESS_MSEC = 100;	// a jump press this recent makes a takeoff the bot's own
static const int	BOT_DODGE_LAUNCH_MSEC	= 128;		// a rejected jump input must not hold walking forever
static const int	BOT_DODGE_FLIGHT_MSEC	= 1600;		// DodgeJumpClear proves at most 96 sixteen-ms flight steps
static const int	BOT_ENEMY_MEMORY_MSEC	= 3000;		// base for how long a lost enemy is chased, scaled by pursuit
static const float	BOT_ITEM_SEARCH_RANGE	= 2500.0f;	// base, scaled by itemFocus
static const int	BOT_STUCK_CHECK_MSEC	= 700;
static const int	BOT_UNSTICK_MSEC		= 900;
static const int	BOT_GOAL_GIVEUP_MSEC	= 12000;	// a goal this stale is not going to resolve
static const int	BOT_GOAL_AVOID_MSEC		= 20000;	// and how long an abandoned goal stays off the list
static const float	BOT_ITEM_ARRIVED		= 64.0f;	// close enough that a pickup would have fired
static const int	BOT_GOAL_SELECT_MSEC	= 250;		// how often the full goal scan may run
static const int	BOT_DECISIONS_PER_FRAME	= 2;		// full goal scans one server frame may carry
static const int	BOT_GOAL_COMMIT_MSEC	= 1800;		// suppress equivalent-goal chatter
static const float	BOT_GOAL_SWITCH_MARGIN	= 1.20f;	// challenger must be materially better
static const float	BOT_GOAL_PROGRESS_EPSILON = 24.0f;	// route shortening counts as progress
static const int	BOT_RECOVER_SEARCH_MSEC	= 500;		// how often the off-mesh search may run
static const float	BOT_RECOVER_RADIUS		= 1536.0f;	// how far off the mesh a bot will look for a way back
static const int	BOT_RECOVER_IDLE_MSEC	= 2000;		// no route this long means the bot is stranded
static const int	BOT_MINPLAYERS_BACKOFF_MSEC	= 30000;	// autofill retry delay after a refused addbot
static const int	BOT_RECOVER_WANDER_MSEC	= 2000;		// how long a stranded bot keeps one wander heading
static const float	BOT_RECOVER_TURN_RATE	= 150.0f;	// degrees/second it turns onto that heading
static const float	BOT_RECOVER_EDGE_SPEED	= 32.0f;	// creep from a grounded hull rim back onto supported floor
static const int	BOT_PATIENCE_HOLD_MSEC	= 4000;		// how long a maximally patient bot sits on a finished roam
static const float	BOT_DEFEND_RADIUS		= 320.0f;	// useful perimeter around an authored team base
static const float	BOT_FLAG_TOUCH_RADIUS	= 24.0f;	// inside this a 32-wide hull overlaps the 40-wide flag trigger
static const float	BOT_HOLD_STEP			= 48.0f;	// how far a holding bot's combat step is checked ahead
static const float	BOT_LOOK_AHEAD			= 320.0f;	// how far along its route an unengaged bot watches
static const float	BOT_SCAN_HALF_ANGLE		= 55.0f;	// degrees either side of its facing a standing bot sweeps
static const float	BOT_SCAN_PERIOD_SEC		= 7.0f;		// one sweep out and back
static const float	BOT_COMBAT_STEER_DOT	= 0.85f;	// combat owns the feet once it bends the route past ~30 degrees

static bool BotBrakeAtEdge( idPlayer *self, idVec3 &moveDir );
static bool BotBrakeForTransport( idPlayer *self, idVec3 &moveDir, int plannedEntityNum );

// The aim point is biased up the target's own bounding box rather than by a
// fixed number of units, so it lands in the same place on any player model.  A
// competent shot puts it in the upper chest; a poor one drags it down into the
// legs and the floor.  weaponSkill carries this instead of a trait of its own:
// they are the same competence, and content that tunes one should not have to
// remember to tune the other.
static const float	BOT_AIM_HEIGHT_BIAS		= 0.20f;

// Second tremor octave.  Deliberately not a whole multiple, so the two never
// re-align into an audible-looking beat.
static const float	BOT_TREMOR_OCTAVE		= 2.37f;

// Height up a target's bounds where an unseen contact's believed position sits.
static const float	BOT_AIM_CHEST_FRACTION	= 0.62f;

// Hearing.  Ranges are what an average listener hears; botTraits_t::hearing
// scales them per bot.  A noise is "current" this long after it was made.
static const int	BOT_NOISE_MEMORY_MSEC	= 300;
static const float	BOT_NOISE_FIRE_RANGE	= 1700.0f;	// weapon fire
static const float	BOT_NOISE_QUIET_FIRE_RANGE = 700.0f;	// gauntlet
static const float	BOT_NOISE_STEP_RANGE	= 420.0f;	// running footsteps
static const float	BOT_NOISE_STEP_SPEED	= 200.0f;	// slower than this is not audible
static const float	BOT_NOISE_PICKUP_RANGE	= 900.0f;	// an item pickup
static const float	BOT_PICKUP_WITNESS_RANGE = 1100.0f;	// close enough to learn the item's timer

// Patrols.  One cost field covers the whole of a typical arena map; a spot
// within the revisit radius of a recent patrol point counts as the same spot.
static const float	BOT_ROAM_FIELD_COST		= 9000.0f;
static const float	BOT_ROAM_REVISIT_RADIUS	= 320.0f;
// Elimination rounds: how long a round runs before the survivors are hunted,
// and how far off the guess at where they are may be.
static const int	BOT_HUNT_AFTER_MSEC		= 20000;
static const float	BOT_HUNT_BLUR			= 600.0f;

// Item timing.  Only items worth planning a route around are timed, and a bot
// will wait at one for at most this long.
static const int	BOT_ITEM_MAX_WAIT_MSEC	= 9000;
static const float	BOT_ITEM_TIMED_MIN_VALUE = 3.0f;
// A belief this far past its own predicted respawn is evidently wrong.
static const int	BOT_ITEM_TIMER_STALE_MSEC = 6000;
// A useful pickup within this route cost is taken on the way, mid-chase.
static const float	BOT_ITEM_NEARBY_ROUTE	= 320.0f;

static float BotWeaponSplashRadius( idPlayer *self );

// The measured angular rate of the target is low passed over roughly four
// frames.  Unfiltered it would chatter with every step the target takes and
// the tracking error would read as noise rather than as lag.
static const float	BOT_ANGVEL_FILTER		= 0.25f;

// How long, as a share of the reaction time, a target's change of velocity
// takes to register in the aim.  The aim carries the registered velocity
// forward, so a reversal is trailed for about this long at every skill.
static const float	BOT_AIM_PERCEIVE_FRACTION	= 0.55f;

// The aim point crossing the view faster than this in one frame has jumped -
// a new target, a new route corner - rather than moved, and is not pursued.
static const float	BOT_AIM_RATE_JUMP		= 720.0f;

// The turn is integrated explicitly, and content can ask for a high turnAccel
// with a low turnSpeed, which makes the natural frequency arbitrarily large.
// Past roughly this much phase per frame an explicit integrator diverges, and
// the symptom is a bot whose view spins up until it is unwatchable.  Clamping
// the frequency degrades the response instead, which is merely wrong-looking.
static const float	BOT_TURN_STABLE_WN		= 0.5f;

// Seconds between shots below which a weapon counts as automatic and gets
// burst discipline.  Everything slower - rail, shotgun, rocket - already has
// its own rhythm and only obeys the settle time.
static const float	BOT_AUTO_FIRE_RATE		= 0.25f;

// Centre of the smooth close-to-far weapon blend.  Deliberately NOT
// botTraits_t::combatRange: that is the distance the bot WANTS to fight at.
// What weapon suits the actual range is a property of the weapon; personality
// biases choose within that continuous opinion.
static const float	BOT_WEAPON_RANGE_SPLIT	= 500.0f;

// A gap this short does not count as losing sight of someone: it is one
// dropped trace in a running fight, not a re-peek.
static const int	BOT_CONTINUOUS_MSEC		= 100;

// Being shot tells a bot where to look.  Without this the fov cone makes a bot
// standing with its back to a fight impossible to provoke, which is worse than
// the 360 degree vision it replaced.
static const int	BOT_ALERT_MSEC			= 1500;

static const int	BOT_DODGE_MSEC			= 700;		// how long one dodge lasts once it starts
static const int	BOT_THREAT_MEMORY_MSEC	= 2500;		// damage source remains a tactical threat
static const int	BOT_INVIS_REVEAL_MSEC	= 1200;		// a firing/damaging invisible player gives itself away
static const float	BOT_STRAFE_RANGE_SCALE	= 1.5f;		// dodge inside this multiple of the active fighting range
static const float	BOT_RETREAT_RANGE_SCALE	= 1.8f;		// how much further off a bot looking for health stands

static const float	BOT_ITEM_DENIED_RANGE	= 700.0f;	// close enough that the item was really taken from under it

static const int	BOT_KILL_STREAK			= 3;		// kills without dying that count as a streak
static const int	BOT_CHAT_JITTER_MSEC	= 600;		// natural variation after the length-based typing delay
static const int	BOT_CHAT_MIN_DELAY_MSEC	= 750;		// make even a one-word line visibly typed
static const int	BOT_CHAT_MAX_DELAY_MSEC	= 5000;		// keep long authored lines from taking a bot out for too long
static const float	BOT_CHAT_CHATTY_BONUS	= 0.35f;	// added to chatiness at bot_chat 2
static const int	BOT_LEADER_CHECK_MSEC	= 2000;		// how often the scoreboard is scanned for a lead change
static const int	BOT_REPLY_SOURCE_THROTTLE_MSEC = 8000;	// one speaker cannot work through the whole roster
static const float	BOT_REPLY_PLAYER_CHANCE	= 0.45f;
static const float	BOT_REPLY_PLAYER_ADDRESSED_CHANCE = 0.85f;
static const float	BOT_REPLY_BOT_CHANCE	= 0.12f;	// ordinary bot lines may start a conversation, rarely
static const float	BOT_REPLY_BOT_ADDRESSED_CHANCE = 0.30f;

// Weapon preference, best close-range score first.  Suitability blends across
// range so a bot does not try to snipe with a shotgun or fight a knife-range
// duel with a rail gun.  A style or character multiplies each entry through
// botTraits_t::weaponBias, so a sniper reaches for the rail gun in a corridor
// and a rusher does not.
typedef struct botWeaponRange_s {
	const char *name;
	float closeScore;
	float farScore;
	float preferredRange;
} botWeaponRange_t;

// Continuous close/far opinions avoid switching at one magic distance.  The
// current weapon receives hysteresis in UpdateWeapon; these scores describe
// only suitability.  Expansion weapons are first-class choices.
static const botWeaponRange_t botWeaponRanges[] = {
	{ "weapon_shotgun",          12.0f,  2.0f, 220.0f },
	{ "weapon_lightninggun",     11.0f,  8.0f, 420.0f },
	{ "weapon_rocketlauncher",   10.0f,  9.0f, 560.0f },
	{ "weapon_hyperblaster",      9.0f,  7.0f, 500.0f },
	{ "weapon_napalmgun",         8.0f,  4.0f, 400.0f },
	{ "weapon_nailgun",           7.0f,  6.0f, 520.0f },
	{ "weapon_dmg",               6.0f, 11.0f, 760.0f },
	{ "weapon_railgun",           5.0f, 12.0f, 900.0f },
	{ "weapon_machinegun",        4.0f,  5.0f, 600.0f },
	{ "weapon_grenadelauncher",   3.0f,  3.0f, 480.0f },
	{ "weapon_blaster",           2.0f,  2.0f, 480.0f },
	{ "weapon_gauntlet",          1.0f,  1.0f,  72.0f }
};
static const int BOT_NUM_WEAPON_RANGES = sizeof( botWeaponRanges ) / sizeof( botWeaponRanges[0] );

// A "range" this short is a real reach limit; hitscan and shotgun defs carry
// effectively unlimited values that only bound the trace.
static const float	BOT_WEAPON_REACH_LIMIT	= 4096.0f;

//----------------------------------------------------------------
// What the multiplayer weapon declarations say about each weapon the bots
// know, resolved once per map through the _mp entityDef redirect: how far it
// reaches, how big its blast is, how fast it fires, and whether burst
// discipline helps it or hurts it.
//----------------------------------------------------------------
typedef struct botWeaponInfo_s {
	bool			resolved;
	float			reach;			// maximum useful distance, 0 for unlimited
	float			splashRadius;	// 0 for none
	float			fireRate;		// seconds between shots
	bool			sustained;		// hold the trigger: spin-up barrel, beam or melee
} botWeaponInfo_t;

static botWeaponInfo_t	botWeaponInfo[BOT_NUM_WEAPON_RANGES];

/*
================
BotWeaponTableIndex
================
*/
static int BotWeaponTableIndex( const char *weaponClass ) {
	if ( !weaponClass || !weaponClass[0] ) {
		return -1;
	}
	for ( int i = 0; i < BOT_NUM_WEAPON_RANGES; i++ ) {
		if ( !idStr::Icmp( weaponClass, botWeaponRanges[i].name ) ) {
			return i;
		}
	}
	return -1;
}

/*
================
BotResetWeaponInfo

Declarations can change across a map load or a game module reload.
================
*/
static void BotResetWeaponInfo( void ) {
	for ( int i = 0; i < BOT_NUM_WEAPON_RANGES; i++ ) {
		botWeaponInfo[i].resolved = false;
	}
}

/*
================
BotWeaponInfo

The gauntlet and the lightning gun declare their reach; a gauntlet "range" is
measured from the eye, so a target's centre can be half a hull further away.
The nailgun spins up on every fresh press - burst discipline would restart the
spin-up each burst and cut a careful bot's output to a fifth of a careless
one's - and a beam has no spread or recoil to recover from, so both are held.
================
*/
static const botWeaponInfo_t &BotWeaponInfo( int index ) {
	static const botWeaponInfo_t none = { true, 0.0f, 0.0f, 0.0f, false };
	if ( index < 0 || index >= BOT_NUM_WEAPON_RANGES ) {
		return none;
	}

	botWeaponInfo_t &info = botWeaponInfo[index];
	if ( info.resolved ) {
		return info;
	}

	const char *name = botWeaponRanges[index].name;
	const idDict *def = gameLocal.FindEntityDefDict( name, false );

	info.resolved		= true;
	info.reach			= 0.0f;
	info.splashRadius	= 0.0f;
	info.fireRate		= def ? def->GetFloat( "fireRate", "0" ) : 0.0f;
	info.sustained		= !idStr::Icmp( name, "weapon_nailgun" ) ||
						  !idStr::Icmp( name, "weapon_lightninggun" ) ||
						  !idStr::Icmp( name, "weapon_gauntlet" );

	const float range = def ? def->GetFloat( "range", "0" ) : 0.0f;
	if ( range > 0.0f && range < BOT_WEAPON_REACH_LIMIT ) {
		info.reach = range + pm_bboxwidth.GetFloat() * 0.5f;
	}

	const char *projectileName = def ? def->GetString( "def_projectile", "" ) : "";
	const idDict *projectile = projectileName[0] ? gameLocal.FindEntityDefDict( projectileName, false ) : NULL;
	const char *splashName = projectile ? projectile->GetString( "def_splash_damage", "" ) : "";
	const idDict *splash = splashName[0] ? gameLocal.FindEntityDefDict( splashName, false ) : NULL;
	if ( splash ) {
		info.splashRadius = Max( 0.0f, splash->GetFloat( "radius", "0" ) );
	}

	return info;
}

/*
================
BotCurrentWeaponInfo
================
*/
static const botWeaponInfo_t &BotCurrentWeaponInfo( const idPlayer *self ) {
	const char *weaponClass = self->spawnArgs.GetString( va( "def_weapon%d", self->GetCurrentWeapon() ), "" );
	return BotWeaponInfo( BotWeaponTableIndex( weaponClass ) );
}

/*
================
BotPreferredCombatRange

Personality says what sort of fight a bot seeks; the weapon in its hands says
what sort of fight it can win right now.  Range discipline blends the two, so
a disciplined sniper with a shotgun closes the gap while a reckless rusher
with a rail gun still retains some of its authored character.
================
*/
static float BotPreferredCombatRange( const idPlayer *self, const botTraits_t &traits ) {
	if ( !self ) {
		return traits.combatRange;
	}

	const int currentSlot = self->GetCurrentWeapon();
	const char *currentWeapon = self->spawnArgs.GetString( va( "def_weapon%d", currentSlot ), "" );
	for ( int i = 0; i < BOT_NUM_WEAPON_RANGES; i++ ) {
		if ( idStr::Icmp( currentWeapon, botWeaponRanges[i].name ) != 0 ) {
			continue;
		}

		// A weapon with a hard reach is used inside it whatever the personality
		// prefers: a gauntlet held at a sniper's distance is no weapon at all.
		const botWeaponInfo_t &info = BotWeaponInfo( i );
		if ( info.reach > 0.0f && info.reach < traits.combatRange ) {
			return info.reach * 0.6f;
		}

		const float discipline = idMath::ClampFloat( 0.0f, 1.0f, traits.rangeDiscipline );
		return traits.combatRange +
			( botWeaponRanges[i].preferredRange - traits.combatRange ) * discipline;
	}

	return traits.combatRange;
}

/*
================
BotObjectiveRouteOrigin

An escort should protect the carrier, not occupy the carrier's collision hull.
Give each bot a stable trailing lane, then project that lane onto a proven nav
sample.  Tight corridors or unsupported offsets fall back to the authoritative
carrier origin; Repath performs the final directed reachability proof.
================
*/
static idVec3 BotObjectiveRouteOrigin( idPlayer *self, const botObjective_t &objective ) {
	idEntity *entity = objective.entity.GetEntity();
	if ( !self || objective.kind != BOTOBJ_ESCORT || !entity ||
		 !entity->IsType( idPlayer::GetClassType() ) || !navMesh.IsValid() ) {
		return objective.origin;
	}

	idPlayer *carrier = static_cast<idPlayer *>( entity );
	idVec3 forward = carrier->GetPhysics()->GetLinearVelocity();
	forward.z = 0.0f;
	if ( forward.Normalize() < 48.0f ) {
		idVec3 viewOrigin;
		idMat3 viewAxis;
		carrier->GetViewPos( viewOrigin, viewAxis );
		forward = viewAxis[0];
		forward.z = 0.0f;
		if ( forward.Normalize() <= 0.0f ) {
			return objective.origin;
		}
	}

	const int slot = self->entityNumber & 3;
	static const float sideOffsets[4] = { -96.0f, 96.0f, -160.0f, 160.0f };
	const float trailingDistance = ( slot < 2 ) ? 112.0f : 176.0f;
	const idVec3 side = forward.Cross( idVec3( 0.0f, 0.0f, 1.0f ) );
	const idVec3 candidate = objective.origin - forward * trailingDistance +
		side * sideOffsets[slot];

	const int node = navMesh.FindNearestNode( candidate, 192.0f, true );
	return ( node >= 0 ) ? navMesh.GetNode( node ).origin : objective.origin;
}

// Fallback roster, used when the character files are missing or bot_characters
// is off.  With characters loaded the names come from the roster instead, so
// the bot in the scoreboard and the personality behind it are the same thing.
static const char *botNames[] = {
	"Voss", "Bidwell", "Cortez", "Rhodes", "Sledge", "Morris", "Strauss", "Tetzlaff",
	"Marsh", "Hollenbeck", "Sorg", "Anderson", "Gunner", "Makron", "Kane", "Bagby",
	NULL
};

/*
================
BotDisplayName

Everything a chat line refers to by name goes through here.  ui_name is the
name every other player sees, which is what a taunt has to use.
================
*/
static const char *BotDisplayName( const idPlayer *player ) {
	if ( !player || player->entityNumber < 0 || player->entityNumber >= MAX_CLIENTS ) {
		return "";
	}

	return gameLocal.userInfo[player->entityNumber].GetString( "ui_name" );
}

/*
================
BotChatSendTime

Use visible characters rather than bytes so colour and formatting escapes cost
no typing time.  chatDelayScale slows the whole action for less capable or more
deliberate characters; the final cap keeps even the longest valid line a short
piece of flavour instead of a five-minute commitment.
================
*/
static int BotChatSendTime( const idStr &line, float delayScale ) {
	const int visibleCharacters = Max( 1, line.LengthWithoutEscapes() );
	const int charactersPerMinute = Max( 1, bot_chatCPM.GetInteger() );
	const float typingMsec = ( (float)visibleCharacters * 60000.0f ) / (float)charactersPerMinute;
	const float scaledMsec = ( bot_chatDelay.GetFloat() + typingMsec ) * delayScale;
	const int variedMsec = (int)scaledMsec + gameLocal.random.RandomInt( BOT_CHAT_JITTER_MSEC );
	const int delayMsec = idMath::ClampInt( BOT_CHAT_MIN_DELAY_MSEC, BOT_CHAT_MAX_DELAY_MSEC, variedMsec );

	return gameLocal.time + delayMsec;
}

/*
================
BotReadableName

Turns a class name into something that can be dropped into a chat line.
Localised inv_name values are refused on purpose: idMultiplayerGame::
ProcessChatMessage only resolves a string id when the WHOLE line starts with
'#', so a '#str_' substituted into the middle of one would be printed raw.

Every pickup prefix is tried rather than one the caller guesses: the thing a
bot was denied is as often a weapon_ or an ammo_ as an item_, and a wrong guess
leaves the prefix in the line - "weapon hyperblaster was mine" instead of
"Hyperblaster was mine".
================
*/
static idStr BotReadableName( const idDict &spawnArgs ) {
	static const char *pickupPrefixes[] = { "weapon_", "ammo_", "item_", "powerup_", NULL };

	idStr result = spawnArgs.GetString( "inv_name", "" );

	if ( result.IsEmpty() || result[0] == '#' ) {
		result = spawnArgs.GetString( "classname", "" );

		for ( int i = 0; pickupPrefixes[i]; i++ ) {
			if ( result.StripLeadingOnce( pickupPrefixes[i] ) ) {
				break;
			}
		}

		result.Replace( "_", " " );

		// Mid-sentence it still reads as a proper noun, which is how a player
		// would type it.
		if ( result.Length() ) {
			result[0] = idStr::ToUpper( result[0] );
		}
	}

	return result;
}

/*
===============================================================================

	rvBot

===============================================================================
*/

/*
================
rvBot::rvBot
================
*/
rvBot::rvBot( void ) {
	clientNum			= -1;
	active				= false;
	initialTeamAssignmentPending = false;
	teamAssignment		= TEAM_NONE;
	teamAssignmentInstance = -1;

	character			= NULL;
	skillOverride		= -1;
	skillLevel			= -1;
	effectiveSkill		= 0.0f;

	// botTraits_t owns idStr weapon-bias entries.  Byte-zeroing it corrupts
	// those live C++ objects; ResolveTraits fills every scalar before use.
	traits.numWeaponBias	= 0;

	pathCorner			= 0;
	pathTime			= 0;
	goalType			= BOTGOAL_NONE;
	goalOrigin.Zero();
	goalGiveUpTime		= 0;
	goalUtility			= 0.0f;
	goalCommitUntil		= 0;
	goalBestDistance		= idMath::INFINITY;
	goalProgressTime		= 0;
	objectiveKind			= BOTOBJ_NONE;
	objectiveHoldPosition = false;
	repathFailures		= 0;
	enemyPathTime		= 0;
	nextGoalSelectTime	= 0;
	holdUntil			= 0;
	noRouteSince			= 0;
	traversalCorner		= -1;
	traversalStartOrigin.Zero();
	traversalStartTime	= 0;
	traversalEntered		= false;
	traversalStarted	= false;
	recoverTarget.Zero();
	landingCheckPathTime = -1;
	landingCheckCorner = -1;
	landingCheckLimit = idMath::INFINITY;
	ResetDodgeJump();
	recoverSearchTime	= 0;
	recoverValid		= false;
	recoverWanderUntil	= 0;
	recoverWanderYaw	= 0.0f;
	avoidNext			= 0;

	for ( int i = 0; i < MAX_AVOID_GOALS; i++ ) {
		avoidGoals[i].until = 0;
	}

	stuckTime			= 0;
	unstickUntil		= 0;
	unstickSide			= 1.0f;
	stuckChecks			= 0;
	stuckPathCorner		= -1;
	stuckCornerDistance	= idMath::INFINITY;
	offRouteProofTime	= 0;

	enemyAcquiredTime	= 0;
	enemyLastSeenTime	= 0;
	enemyLastSeenOrigin.Zero();
	enemyLastSeenVelocity.Zero();
	enemyVisiblePoint.Zero();
	enemyLastHeardTime	= 0;
	nextHearingTime		= 0;
	targetPickBest		= false;
	aimAtFeet			= false;
	lastAttacker			= NULL;
	lastAttackerTime	= 0;

	for ( int i = 0; i < ROAM_HISTORY; i++ ) {
		roamHistory[i].Zero();
		roamHistoryTime[i] = 0;
	}
	roamHistoryNext		= 0;
	goalSelectedTime	= 0;
	goalItemPending		= false;
	combatSteeringTime	= 0;
	jumpRetries			= 0;
	jumpRetryPathTime	= 0;
	climbCheckTime		= 0;
	climbCheckTarget.Zero();
	climbCheckDistance	= 0.0f;
	padHoldSince		= 0;
	padApproachEntity	= -1;
	padApproachSince	= 0;
	padApproachTime		= 0;
	padApproachLaunch.Zero();
	padApproachLanding.Zero();
	probeWatch			= false;
	probeAirActive		= false;
	probeAirDirection.Zero();
	probeStartTime		= 0;
	probeAirborneSeen	= false;
	airRecoverReported	= 0;
	airRecoverSliding	= false;
	airRecovering		= false;
	knockbackTime		= 0;
	knockbackFrameTime	= -1;
	knockbackFrameKick.Zero();
	airborneSince		= 0;
	airborneVoluntary	= false;
	jumpPressTime		= 0;
	airRecoverPlanTime	= 0;
	airRecoverWish.Zero();
	airRecoverLanded	= 0;
	outmatched			= false;
	selfStrong			= false;

	aimAngles.Zero();
	aimRate.Zero();
	aimBelief.Zero();
	aimBeliefValid		= false;
	aimPerceivedVelocity.Zero();
	aimPoint.Zero();
	aimPointValid		= false;
	aimTrackAngles.Zero();
	aimTrackValid		= false;
	aimTargetRate.Zero();
	aimAngVel			= 0.0f;
	aimLeadRoll			= 1.0f;
	idleScan			= false;
	scanStartTime		= 0;
	scanBaseYaw			= 0.0f;

	for ( int i = 0; i < 4; i++ ) {
		tremorPhase[i] = 0.0f;
	}

	enemyFireTime		= 0;
	onTargetTime		= 0;
	burstEndTime		= 0;
	burstRestTime		= 0;
	nextSingleShotTime	= 0;
	wasFiring			= false;

	mistake				= BOTMISTAKE_NONE;
	mistakeEndTime		= 0;
	nextMistakeTime		= 0;

	damageStamp			= 0;
	dodgeStartTime		= 0;
	dodgeEndTime			= 0;
	nextThreatScanTime	= 0;
	strafeSide			= 1.0f;
	strafeFlipTime		= 0;
	dodgeJump			= false;

	chatTeamOnly		= false;
	chatPendingIsReply = false;
	chatSendTime		= 0;
	killStreak			= 0;
	revengeTarget		= -1;
	announcedEntry		= false;

	nextWeaponTime		= 0;
	lastWeaponSwitchTime = 0;
	nextBuyTime			= 0;
	nextRejoinTime		= 0;
	nextStatusTime		= 0;
	nextAimDebugTime	= 0;
}

/*
================
rvBot::Init
================
*/
void rvBot::Init( int clientNum, const char *name, int skillOverride, bool requireExactCharacter ) {
	// A bot re-begun after a map change comes back through here on the same
	// slot, because bots hold their client slots across one.  It keeps the
	// personality it already had: characters describe opponents, and an
	// opponent who becomes somebody else between maps is not one.
	const bool rejoining = ( active && this->clientNum == clientNum );

	this->clientNum	= clientNum;
	this->name		= ( name && name[0] ) ? name : "bot";
	this->active	= true;

	// A newly allocated engine bot slot can inherit a perfectly valid-looking
	// ui_team from the server's template.  It is not an operator choice: addbot
	// has no team argument.  Mark exactly this first spawn for an authoritative
	// balance pass; a map rejoin keeps the side already assigned to the bot.
	if ( !rejoining ) {
		initialTeamAssignmentPending = true;
		teamAssignment = TEAM_NONE;
		teamAssignmentInstance = -1;
	}

	if ( skillOverride >= 1 && skillOverride <= BOT_SKILL_LEVELS ) {
		this->skillOverride = skillOverride;
	} else if ( !rejoining ) {
		this->skillOverride = -1;
	}

	const int level = idMath::ClampInt( 1, BOT_SKILL_LEVELS,
										( this->skillOverride > 0 ) ? this->skillOverride : bot_skill.GetInteger() );

	// Claim a personality.  PickCharacter honours bot_forceCharacter and hands
	// out unused characters first, so a full server is a cast rather than eight
	// copies of the same opponent.  A NULL answer - no roster, or
	// bot_characters 0 - is normal and leaves the bot on the plain skill curve.
	if ( !rejoining ) {
		if ( character ) {
			botCharacterManager.ReleaseCharacter( character );
			character = NULL;
		}
		if ( bot_characters.GetBool() ) {
			const rvBotCharacter *requestedCharacter = botCharacterManager.FindCharacter( this->name.c_str() );

			if ( requestedCharacter && !requestedCharacter->IsInUse() ) {
				character = requestedCharacter;
				botCharacterManager.MarkCharacterUsed( character );
			} else if ( !requireExactCharacter ) {
				character = botCharacterManager.PickCharacter( level );
			}
		}
	}

	// The display name follows the character, not the other way round: the slot
	// was allocated from a peek at the same roster, and FillUserInfo republishes
	// ui_name on every userinfo update, so this is what the scoreboard shows.
	if ( character ) {
		this->name = character->GetName();
	}

	// The tremor phases are seeded from the client number rather than rolled,
	// so a bot shakes the same way for its whole life and no two bots on a
	// server shake in step.
	for ( int i = 0; i < 4; i++ ) {
		tremorPhase[i] = (float)( clientNum + 1 ) * 1.7f + (float)i * 2.3f;
	}

	killStreak		= 0;
	revengeTarget	= -1;

	ResolveTraits();

	// One hello per bot, not one per respawn.  The idPlayer does not exist yet
	// when OnSpawnPlayer runs, but the line is queued and does not go out for
	// at least bot_chatDelay, by which time it does.
	if ( !announcedEntry ) {
		announcedEntry = true;
		QueueChat( BOTCHAT_ENTERGAME, NULL, NULL, NULL );
	}

	OnSpawn();
}

/*
================
rvBot::RebindCharacter

rvBotCharacterManager::Reload deletes every parsed character, so the pointer
this bot is holding is freed memory the moment it returns.  It does not reliably
crash: the new roster is parsed straight back into the same heap, so a stale
pointer usually lands on a DIFFERENT character at the same address and the bot
silently starts wearing someone else's personality.  Re-look-up by name is the
only thing that survives a reparse, and the name is authoritative because
rvBot::Init copies it out of the character in the first place.

A name that no longer exists in the roster leaves the bot on the plain skill
curve rather than inventing a replacement - the same state bot_characters 0
produces, which is known to work.
================
*/
void rvBot::RebindCharacter( void ) {
	if ( !active ) {
		return;
	}

	// Not released first: the pointer is already dangling, and the fresh roster
	// starts with nothing marked in use, so there is nothing to give back.
	character = NULL;

	if ( bot_characters.GetBool() ) {
		character = botCharacterManager.FindCharacter( name.c_str() );

		if ( character ) {
			botCharacterManager.MarkCharacterUsed( character );
		}
	}

	ResolveTraits();
}

/*
================
rvBot::Shutdown
================
*/
void rvBot::Shutdown( void ) {
	idPlayer *self = GetPlayer();

	if ( self ) {
		self->isChatting = false;
	}

	// The identity goes back in the pool for the next bot, or the roster drains
	// after a few kicks and every later bot falls through to the "any character
	// at all" case.
	if ( character ) {
		botCharacterManager.ReleaseCharacter( character );
		character = NULL;
	}

	active			= false;
	clientNum		= -1;
	initialTeamAssignmentPending = false;
	teamAssignment	= TEAM_NONE;
	teamAssignmentInstance = -1;
	announcedEntry	= false;
	killStreak		= 0;
	revengeTarget	= -1;
	chatPending.Clear();
	chatTeamOnly	= false;
	chatPendingIsReply = false;
	chatSendTime	= 0;
	path.Clear();
	ResetTraversal();
	ResetDodgeJump();
	goalEntity		= NULL;
	enemy			= NULL;
}

/*
================
rvBot::ResolveTraits

The whole personality in one call: the skill curve for the level this bot plays
at, then its style, then its character, then their per-level overrides.

Called once per spawn and never per frame.  That is deliberate - a mid-match
edit of bot_skill, bot_skillVariance or bot_characters lands on the next
respawn, so nobody's aim changes in the middle of the fight they are already in.
================
*/
void rvBot::ResolveTraits( void ) {
	// An explicit per-bot skill from "addbot <name> <skill>" bypasses the
	// variance as well: an operator who asked for a level 2 bot asked for a
	// level 2 bot, not for a roll around one.
	const int	level		= idMath::ClampInt( 1, BOT_SKILL_LEVELS,
												( skillOverride > 0 ) ? skillOverride : bot_skill.GetInteger() );
	const float	variance	= ( skillOverride > 0 ) ? 0.0f : bot_skillVariance.GetFloat();

	skillLevel		= level;
	effectiveSkill	= rvBotCharacterManager::EffectiveSkill( level, variance, clientNum );

	botCharacterManager.ResolveTraits( bot_characters.GetBool() ? character : NULL,
									   level, variance, clientNum, traits );
}

/*
================
rvBot::OnSpawn
================
*/
void rvBot::OnSpawn( void ) {
	ResolveTraits();

	path.Clear();
	pathCorner			= 0;
	pathTime			= 0;
	goalType			= BOTGOAL_NONE;
	goalEntity			= NULL;
	goalGiveUpTime		= 0;
	goalUtility			= 0.0f;
	goalCommitUntil		= 0;
	goalBestDistance		= idMath::INFINITY;
	goalProgressTime		= gameLocal.time;
	objectiveKind			= BOTOBJ_NONE;
	objectiveHoldPosition = false;
	repathFailures		= 0;
	enemyPathTime		= 0;
	nextGoalSelectTime	= 0;
	holdUntil			= 0;
	noRouteSince			= gameLocal.time;
	ResetTraversal();
	ResetDodgeJump();
	recoverValid		= false;
	recoverSearchTime	= 0;
	recoverWanderUntil	= 0;
	recoverWanderYaw	= 0.0f;
	avoidNext			= 0;

	for ( int i = 0; i < MAX_AVOID_GOALS; i++ ) {
		avoidGoals[i].until = 0;
	}

	enemy				= NULL;
	enemyAcquiredTime	= 0;
	enemyLastSeenTime	= 0;
	enemyLastSeenVelocity.Zero();
	enemyVisiblePoint.Zero();
	enemyLastHeardTime	= 0;
	nextHearingTime		= 0;
	aimAtFeet			= false;
	lastAttacker			= NULL;
	lastAttackerTime	= 0;
	goalSelectedTime	= 0;
	goalItemPending		= false;
	combatSteeringTime	= 0;
	jumpRetries			= 0;
	jumpRetryPathTime	= 0;
	climbCheckTime		= 0;
	padHoldSince		= 0;
	padApproachEntity	= -1;
	padApproachSince	= 0;
	padApproachTime		= 0;
	probeWatch			= false;
	probeAirActive		= false;
	probeAirborneSeen	= false;
	airRecoverReported	= 0;
	airRecoverSliding	= false;
	airRecovering		= false;
	knockbackTime		= 0;
	knockbackFrameTime	= -1;
	knockbackFrameKick.Zero();
	airborneSince		= 0;
	airborneVoluntary	= false;
	jumpPressTime		= 0;
	airRecoverPlanTime	= 0;
	airRecoverWish.Zero();
	airRecoverLanded	= 0;
	outmatched			= false;
	selfStrong			= false;

	// Patrol memory is an absolute clock like every other stamp here, and a map
	// change restarts that clock; a stale entry would read as "visited just now"
	// for as long as the previous map ran.
	for ( int i = 0; i < ROAM_HISTORY; i++ ) {
		roamHistoryTime[i] = 0;
	}
	enemyFireTime		= 0;
	onTargetTime		= 0;
	burstEndTime		= 0;
	burstRestTime		= 0;
	nextSingleShotTime	= 0;
	wasFiring			= false;
	stuckTime			= gameLocal.time;
	unstickUntil		= 0;
	stuckChecks			= 0;
	stuckPathCorner		= -1;
	stuckCornerDistance	= idMath::INFINITY;
	offRouteProofTime	= 0;
	nextWeaponTime		= 0;
	lastWeaponSwitchTime = 0;
	nextBuyTime			= 0;

	aimRate.Zero();
	aimBeliefValid		= false;
	aimPerceivedVelocity.Zero();
	aimPointValid		= false;
	aimTrackValid		= false;
	aimTargetRate.Zero();
	aimAngVel			= 0.0f;
	aimLeadRoll			= 1.0f;
	idleScan			= false;
	scanStartTime		= 0;

	mistake				= BOTMISTAKE_NONE;
	mistakeEndTime		= 0;
	nextMistakeTime		= 0;

	damageStamp			= 0;
	dodgeStartTime		= 0;
	dodgeEndTime			= 0;
	nextThreatScanTime	= 0;
	strafeFlipTime		= 0;
	dodgeJump			= false;

	// These are absolute stamps like every other deadline here, and OnSpawn is
	// what rvBotManager::OnMapShutdown runs to re-arm a bot that kept its slot
	// across a map change.  gameLocal.time restarts at zero there, so a stamp
	// taken on the previous map is not "a second from now", it is however long
	// that map ran - long enough that a spectating bot would never ask to
	// rejoin again.
	nextRejoinTime		= 0;
	nextStatusTime		= 0;
	nextAimDebugTime	= 0;

	// Whether this bot picks the best enemy or merely the nearest one is a
	// property of the engagement, not of the frame, so it is rolled here and
	// again on every fresh acquisition.
	targetPickBest		= ( gameLocal.random.RandomFloat() < traits.targetSelection );

	// A pending chat line deliberately survives: a bot that dies one second
	// after a frag should still get its line out.  The death line that would
	// follow it is dropped rather than queued over the top: one voice, one
	// line at a time.

	idPlayer *self = GetPlayer();
	if ( self ) {
		aimAngles	= self->viewAngles;
	}
}

/*
================
rvBot::GetPlayer
================
*/
idPlayer *rvBot::GetPlayer( void ) const {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return NULL;
	}

	idEntity *ent = gameLocal.entities[clientNum];
	if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
		return NULL;
	}

	return static_cast<idPlayer *>( ent );
}

/*
================
rvBot::FillUserInfo

The engine broadcasts a bot's user info with nothing but a name in it, and an
idPlayer with no ui_autoJoin sits in the join menu as a spectator forever.  So
every user info update for a bot is topped up here instead.
================
*/
void rvBot::FillUserInfo( idDict &info ) {
	info.Set( "ui_name", name.c_str() );
	info.SetBool( "ui_autoJoin", true );
	info.SetBool( "ui_joined", true );
	info.Set( "ui_spectate", "Play" );
	info.Set( "ui_ready", "Ready" );
	info.Set( "ui_showGun", "1" );

	if ( !info.GetInt( "ui_handicap" ) ) {
		info.SetInt( "ui_handicap", 100 );
	}

	// A character can carry a look as well as a voice.  All three keys are
	// written whatever the game type, because idPlayer::UpdateModelSetup reads
	// the one that matches the side it ends up on and ignores the rest - which
	// also means this does not have to run after the team block below.
	//
	// The name is validated first.  UpdateModelSetup silently replaces an
	// unknown model AND writes the replacement back into the userinfo, which on
	// a server buffers an updateUI command; since every bot userinfo update
	// comes back through here, a bad name would loop forever.
	if ( character && bot_characters.GetBool() ) {
		static const char *modelKeys[3]	= { "ui_model", "ui_model_marine", "ui_model_strogg" };
		const char *modelNames[3]		= { character->GetModel(), character->GetModelMarine(), character->GetModelStrogg() };

		for ( int i = 0; i < 3; i++ ) {
			if ( !modelNames[i][0] ) {
				continue;
			}
			if ( !declManager->FindType( DECL_PLAYER_MODEL, modelNames[i], false ) ) {
				gameLocal.Warning( "bot character '%s': no player model '%s'", character->GetName(), modelNames[i] );
				continue;
			}

			info.Set( modelKeys[i], modelNames[i] );
		}
	}

	// The first assignment must ignore ui_team completely.  Bot slots inherit
	// server userinfo defaults, so a non-empty value is not evidence that anyone
	// selected that side.  Later valid values are authoritative (ShuffleTeams
	// and SwitchToTeam use them), while an engine update that omits ui_team is
	// repaired from the reservation instead of triggering another rebalance.
	if ( gameLocal.IsTeamGame() ) {
		int requestedTeam = TEAM_NONE;
		const char *requestedName = info.GetString( "ui_team" );

		for ( int team = 0; team < TEAM_MAX; team++ ) {
			if ( !idStr::Icmp( requestedName, idMultiplayerGame::teamNames[team] ) ) {
				requestedTeam = team;
				break;
			}
		}

		if ( initialTeamAssignmentPending || teamAssignment < 0 || teamAssignment >= TEAM_MAX ) {
			teamAssignment = botManager.BalancedTeamForBot( clientNum, teamAssignmentInstance );
			initialTeamAssignmentPending = false;
		} else if ( requestedTeam >= 0 && requestedTeam < TEAM_MAX ) {
			teamAssignment = requestedTeam;

			idPlayer *self = GetPlayer();
			if ( self ) {
				teamAssignmentInstance = self->GetInstance();
			}
		}

		info.Set( "ui_team", idMultiplayerGame::teamNames[teamAssignment] );
	}
}

/*
================
rvBot::IsEnemy
================
*/
bool rvBot::IsEnemy( idPlayer *self, idPlayer *other ) const {
	if ( !other || other == self ) {
		return false;
	}
	// fl.notarget is "never attack or target this entity", and every other
	// thing in the game that acquires a target honours it.  Multiplayer bots
	// are the only AI a multiplayer session has, so without this the flag does
	// nothing at all in the one place an operator would reach for it - watching
	// bots navigate on a listen server without being shot at.
	if ( other->fl.notarget ) {
		return false;
	}
	if ( other->GetInstance() != self->GetInstance() ||
		 !gameLocal.mpGame.CanPlay( other ) || other->spectating || other->health <= 0 ) {
		return false;
	}
	if ( gameLocal.IsTeamGame() && other->team == self->team ) {
		return false;
	}

	return true;
}

/*
================
rvBot::CanSee
================
*/
bool rvBot::CanSee( idPlayer *self, idEntity *other, idVec3 *visiblePoint ) const {
	if ( !other || !other->IsType( idPlayer::GetClassType() ) ) {
		return false;
	}

	idVec3 point;
	if ( !BotCombatFindVisibleAimPoint( self, static_cast<idPlayer *>( other ), point ) ) {
		return false;
	}
	if ( visiblePoint ) {
		*visiblePoint = point;
	}
	return true;
}

/*
================
rvBot::WantsHealth
================
*/
bool rvBot::WantsHealth( idPlayer *self ) const {
	const float maxHealth = (float)Max( 1, self->inventory.maxHealth );

	return ( (float)self->health < traits.retreatHealth * maxHealth );
}

/*
================
rvBot::Withdrawing
================
*/
bool rvBot::Withdrawing( idPlayer *self ) const {
	return outmatched || WantsHealth( self );
}

/*
================
BotWeaponPower

What a weapon is worth in a straight fight, for fight-or-flight only; aim and
weapon choice have their own range-aware tables.
================
*/
static float BotWeaponPower( const char *weaponClass ) {
	static const struct {
		const char *	name;
		float			power;
	} powers[] = {
		{ "weapon_dmg",				1.25f },
		{ "weapon_rocketlauncher",	1.00f },
		{ "weapon_railgun",			1.00f },
		{ "weapon_lightninggun",	0.95f },
		{ "weapon_nailgun",			0.85f },
		{ "weapon_hyperblaster",	0.80f },
		{ "weapon_shotgun",			0.80f },
		{ "weapon_napalmgun",		0.80f },
		{ "weapon_grenadelauncher",	0.70f },
		{ "weapon_machinegun",		0.50f },
		{ "weapon_blaster",			0.35f },
		{ "weapon_gauntlet",		0.25f }
	};

	for ( int i = 0; i < (int)( sizeof( powers ) / sizeof( powers[0] ) ); i++ ) {
		if ( !idStr::Icmp( weaponClass, powers[i].name ) ) {
			return powers[i].power;
		}
	}
	return 0.5f;
}

/*
================
BotPowerupMultiplier
================
*/
static float BotPowerupMultiplier( const idPlayer *player ) {
	float multiplier = 1.0f;

	if ( player->PowerUpActive( POWERUP_QUADDAMAGE ) ) {
		multiplier *= 2.5f;
	}
	if ( player->PowerUpActive( POWERUP_DOUBLER ) ) {
		multiplier *= 1.6f;
	}
	if ( player->PowerUpActive( POWERUP_GUARD ) ) {
		multiplier *= 1.4f;
	}
	if ( player->PowerUpActive( POWERUP_REGENERATION ) ) {
		multiplier *= 1.3f;
	}
	if ( player->PowerUpActive( POWERUP_HASTE ) || player->PowerUpActive( POWERUP_SCOUT ) ) {
		multiplier *= 1.15f;
	}
	return multiplier;
}

/*
================
rvBot::BestWeaponPower

The fight-or-flight worth of the best weapon this bot owns with ammo.
================
*/
float rvBot::BestWeaponPower( idPlayer *self ) const {
	float best = BotWeaponPower( "weapon_gauntlet" );

	for ( int i = 0; i < BOT_NUM_WEAPON_RANGES; i++ ) {
		const char *weaponName = botWeaponRanges[i].name;
		const int slot = self->GetWeaponIndex( weaponName );
		if ( slot < 0 || slot >= MAX_WEAPONS ||
			 idStr::Icmp( self->spawnArgs.GetString( va( "def_weapon%d", slot ), "" ), weaponName ) != 0 ||
			 !( self->inventory.weapons & ( 1 << slot ) ) || !self->inventory.HasAmmo( weaponName ) ) {
			continue;
		}
		best = Max( best, BotWeaponPower( weaponName ) );
	}

	return best;
}

/*
================
rvBot::CombatAdvantage

Health with armour at its absorption value, times what the best loaded weapon
can do, times live powerups - for both sides.  An opponent's health is not on
anyone's screen, so a bot only reads it in proportion to targetSelection, the
same competence that lets it pick the weakest of several targets; a novice
assumes everyone is at full health.  Above 1 the trade favours the bot.
================
*/
float rvBot::CombatAdvantage( idPlayer *self, idPlayer *foe ) const {
	if ( !self || !foe ) {
		return 1.0f;
	}

	const float ownWeapon = BestWeaponPower( self );
	const float ownHealth = (float)Max( 0, self->health ) + (float)Max( 0, self->inventory.armor ) * 0.66f;

	const float foeActual = (float)Max( 0, foe->health ) + (float)Max( 0, foe->inventory.armor ) * 0.66f;
	const float foePrior = (float)Max( 1, foe->inventory.maxHealth );
	const float foeHealth = foePrior + ( foeActual - foePrior ) *
		idMath::ClampFloat( 0.0f, 1.0f, traits.targetSelection );
	const char *foeWeapon = foe->spawnArgs.GetString( va( "def_weapon%d", foe->GetCurrentWeapon() ), "" );

	const float own = ownHealth * ownWeapon * BotPowerupMultiplier( self );
	const float theirs = Max( 1.0f, foeHealth ) * BotWeaponPower( foeWeapon ) * BotPowerupMultiplier( foe );

	return own / Max( 1.0f, theirs );
}

/*
================
rvBot::AtObjectiveHoldPosition

Control zones use their authored trigger bounds and Freeze Tag uses the same
thaw radius as the rules.  Other hold objectives (principally waiting at a CTF
base) use a small arrival volume around the resolved objective origin.
================
*/
bool rvBot::AtObjectiveHoldPosition( idPlayer *self ) const {
	if ( !self || goalType != BOTGOAL_OBJECTIVE || !objectiveHoldPosition ) {
		return false;
	}
	// Crossing a scoring volume in flight must not discard the landing route
	// and replace its proven air input with holding/strafe commands. The game
	// rules still count the touch; this predicate only owns movement policy.
	if ( !self->pfl.onGround || self->GetPhysics()->GetLinearVelocity().z > BOT_PAD_LAUNCH_SPEED ) {
		return false;
	}

	idEntity *objectiveEntity = goalEntity.GetEntity();
	if ( !objectiveEntity || objectiveEntity->GetInstance() != self->GetInstance() ||
		 !objectiveEntity->GetPhysics() ) {
		return false;
	}

	// The rule's own touch test: a control zone counts a player whose clip
	// model touches the trigger brush, which a bounds overlap over-reports for
	// any zone that is not a box.
	if ( objectiveKind == BOTOBJ_CONTROL ) {
		const idClipModel *zoneClip = objectiveEntity->GetPhysics()->GetClipModel();
		if ( zoneClip ) {
			return self->GetPhysics()->ClipContents( zoneClip ) != 0;
		}
		return objectiveEntity->GetPhysics()->GetAbsBounds().IntersectsBounds(
			self->GetPhysics()->GetAbsBounds() );
	}

	idVec3 delta = objectiveEntity->GetPhysics()->GetOrigin() - self->GetPhysics()->GetOrigin();
	if ( objectiveKind == BOTOBJ_DEFEND ) {
		const float height = idMath::Fabs( delta.z );
		delta.z = 0.0f;
		return height <= 128.0f && delta.LengthSqr() <= BOT_DEFEND_RADIUS * BOT_DEFEND_RADIUS;
	}
	if ( objectiveKind == BOTOBJ_RESCUE ) {
		const int thawRadius = Max( 1, gameLocal.serverInfo.GetInt( "si_freezeThawRadius" ) );
		rvGameState *state = gameLocal.mpGame.GetGameState();
		if ( state && state->IsType( rvFreezeTagGameState::GetClassType() ) &&
			 objectiveEntity->IsType( idPlayer::GetClassType() ) ) {
			// Within the radius is not enough: the thaw also needs a clear line
			// from the rescuer's eyes, and a bot parked behind a thin wall
			// within range would otherwise wait for the auto-thaw.
			return static_cast<rvFreezeTagGameState *>( state )->CanReachToThaw(
				static_cast<idPlayer *>( objectiveEntity ), self, thawRadius * thawRadius );
		}
		return delta.LengthSqr() <= (float)( thawRadius * thawRadius );
	}

	// Capture: the flag's trigger is small, so arrival means touching it, not
	// coasting to a stop somewhere near the pedestal.
	const float height = idMath::Fabs( delta.z );
	delta.z = 0.0f;
	return height <= BOT_CORNER_REACH_Z * 2.0f &&
		delta.LengthSqr() <= BOT_FLAG_TOUCH_RADIUS * BOT_FLAG_TOUCH_RADIUS;
}

/*
================
rvBot::HoldAllowsMove

Whether a combat step of about this direction keeps a holding bot where the
rules still count it.  Conservative by design: a carrier waiting on the flag
stand does not step off it, while a defender, a rescuer or a zone holder may
dodge inside its volume.
================
*/
bool rvBot::HoldAllowsMove( idPlayer *self, const idVec3 &direction ) const {
	idEntity *objectiveEntity = goalEntity.GetEntity();
	if ( !objectiveEntity || !objectiveEntity->GetPhysics() ) {
		return false;
	}

	idVec3 step = direction;
	step.z = 0.0f;
	if ( step.Normalize() <= 0.0f ) {
		return false;
	}

	const idVec3 point = self->GetPhysics()->GetOrigin() + step * BOT_HOLD_STEP;
	idVec3 delta = objectiveEntity->GetPhysics()->GetOrigin() - point;

	switch ( objectiveKind ) {
		case BOTOBJ_CONTROL: {
			idBounds zone = objectiveEntity->GetPhysics()->GetAbsBounds();
			zone.ExpandSelf( -pm_bboxwidth.GetFloat() * 0.5f );
			return !zone.IsCleared() && zone.ContainsPoint( point + idVec3( 0.0f, 0.0f, 8.0f ) );
		}
		case BOTOBJ_DEFEND: {
			delta.z = 0.0f;
			return delta.LengthSqr() <= Square( BOT_DEFEND_RADIUS * 0.85f );
		}
		case BOTOBJ_RESCUE: {
			const float thawRadius = (float)Max( 1, gameLocal.serverInfo.GetInt( "si_freezeThawRadius" ) );
			return delta.LengthSqr() <= Square( thawRadius * 0.7f );
		}
		default:
			return false;
	}
}

/*
================
BotCarriesScoringObjective

Keep combat's small piece of objective awareness independent of objective
route discovery.  These are the carried states in which an enemy nearby is an
immediate threat to a teammate completing the rule.
================
*/
static bool BotCarriesScoringObjective( const idPlayer *player ) {
	return player &&
		( player->PowerUpActive( POWERUP_CTF_MARINEFLAG ) ||
		  player->PowerUpActive( POWERUP_CTF_STROGGFLAG ) ||
		  player->PowerUpActive( POWERUP_CTF_ONEFLAG ) ||
		  player->PowerUpActive( POWERUP_DEADZONE ) );
}

/*
================
rvBot::TargetScore

Lower is better.  Distance dominates - a bot that walks past someone shooting
it to reach a weaker player at the far end of the map is not playing well - but
a hurt target is worth turning towards.
================
*/
float rvBot::TargetScore( idPlayer *other, float distSqr ) const {
	const float effectiveHealth = (float)Max( 0, other->health ) + (float)Max( 0, other->inventory.armor ) * 0.66f;
	const float effectiveMaximum = (float)Max( 1, other->inventory.maxHealth ) +
							   (float)Max( 0, other->inventory.maxarmor ) * 0.66f;
	const float health = idMath::ClampFloat( 0.0f, 1.0f, effectiveHealth / effectiveMaximum );
	const float healthBias = 1.0f + traits.opportunism * ( health - 0.5f );
	const float revengeBias = ( other->entityNumber == revengeTarget ) ?
							  1.0f - 0.35f * traits.vengefulness : 1.0f;
	const float attackerBias = ( other == lastAttacker.GetEntity() &&
								 gameLocal.time - lastAttackerTime < BOT_THREAT_MEMORY_MSEC ) ? 0.55f : 1.0f;
	const bool carrier = BotCarriesScoringObjective( other );
	const float objectiveBias = carrier ? 0.45f : 1.0f;

	// Protect a friendly carrier by preferring enemies already close enough to
	// intercept it.  The bias is smooth, so a merely visible enemy does not
	// steal attention from the opponent directly on the carrier's heels.
	float carrierThreatBias = 1.0f;
	idPlayer *self = GetPlayer();
	if ( self && gameLocal.IsTeamGame() ) {
		static const float protectRange = 900.0f;
		for ( int i = 0; i < gameLocal.numClients; i++ ) {
			idPlayer *teamMate = gameLocal.GetClientByNum( i );
			if ( !teamMate || teamMate->spectating || teamMate->health <= 0 ||
				 teamMate->GetInstance() != self->GetInstance() || teamMate->team != self->team ||
				 !BotCarriesScoringObjective( teamMate ) ) {
				continue;
			}

			const float threatDistance =
				( other->GetPhysics()->GetOrigin() - teamMate->GetPhysics()->GetOrigin() ).Length();
			if ( threatDistance < protectRange ) {
				carrierThreatBias = Min( carrierThreatBias,
					0.55f + 0.45f * threatDistance / protectRange );
			}
		}
	}
	const bool powered = other->PowerUpActive( POWERUP_QUADDAMAGE ) ||
						 other->PowerUpActive( POWERUP_HASTE ) ||
						 other->PowerUpActive( POWERUP_REGENERATION ) ||
						 other->PowerUpActive( POWERUP_GUARD ) ||
						 other->PowerUpActive( POWERUP_DOUBLER ) ||
						 other->PowerUpActive( POWERUP_SCOUT );
	// A powered opponent is the one to deal with first only for a bot that can:
	// a fresh spawn with a machinegun that turns toward the quad carrier just
	// feeds it.  A weaker bot weighs it down instead and fights what it can win.
	const float powerupBias = powered ? ( selfStrong ? 0.68f : 1.25f ) : 1.0f;
	const bool firing = gameLocal.usercmds && other->entityNumber >= 0 &&
					other->entityNumber < gameLocal.numClients &&
					( gameLocal.usercmds[other->entityNumber].buttons & BUTTON_ATTACK );
	const float firingBias = firing ? 0.82f : 1.0f;

	// Freeze Tag: an enemy standing over a frozen team mate is thawing them,
	// and stopping that is worth more than any other target in reach.
	float thawBias = 1.0f;
	if ( gameLocal.gameType == GAME_FREEZETAG ) {
		const float thawRadius = (float)Max( 1, gameLocal.serverInfo.GetInt( "si_freezeThawRadius" ) );
		for ( int i = 0; i < gameLocal.numClients; i++ ) {
			idPlayer *frozen = gameLocal.GetClientByNum( i );
			if ( !frozen || frozen == other || frozen->team != other->team || frozen->health > 0 ||
				 frozen->spectating || frozen->GetInstance() != other->GetInstance() ||
				 !gameLocal.mpGame.CanPlay( frozen ) ) {
				continue;
			}
			if ( ( frozen->GetPhysics()->GetOrigin() - other->GetPhysics()->GetOrigin() ).LengthSqr() <=
				 Square( thawRadius * 1.25f ) ) {
				thawBias = 0.6f;
				break;
			}
		}
	}

	return distSqr * healthBias * revengeBias * attackerBias * objectiveBias *
		carrierThreatBias * powerupBias * firingBias * thawBias;
}

/*
================
rvBot::RollMistake

Rolled on acquisition and again every mistakeMsec while a fight lasts, so a
long engagement is a sequence of chances to get something wrong rather than one.
================
*/
void rvBot::RollMistake( void ) {
	nextMistakeTime = gameLocal.time + (int)traits.mistakeMsec;

	if ( gameLocal.random.RandomFloat() >= traits.mistakeChance ) {
		mistake			= BOTMISTAKE_NONE;
		mistakeEndTime	= 0;
		return;
	}

	mistake			= BOTMISTAKE_LOSETRACK + gameLocal.random.RandomInt( BOTMISTAKE_NUM - BOTMISTAKE_LOSETRACK );
	mistakeEndTime	= gameLocal.time + (int)traits.mistakeMsec;
}

/*
================
rvBot::MistakeActive
================
*/
bool rvBot::MistakeActive( int mistake ) const {
	return ( this->mistake == mistake && gameLocal.time < mistakeEndTime );
}

/*
================
rvBot::AcquireEnemy

Everything that happens once, at the moment a target registers.  The reaction
deadline is computed here rather than tested per frame, which is what makes a
re-acquisition cost something: the old code measured the delay from a timestamp
that only moved when the enemy had been gone for half a second, so inside a
continuous fight the reaction was zero forever.
================
*/
void rvBot::AcquireEnemy( idPlayer *self, idPlayer *foe, bool fresh ) {
	idVec3	eye;
	idMat3	axis;

	self->GetViewPos( eye, axis );

	enemy				= foe;
	enemyAcquiredTime	= gameLocal.time;

	idVec3 dir = foe->GetPhysics()->GetAbsBounds().GetCenter() - eye;

	float offAxis = 0.0f;
	if ( dir.Normalize() > 0.0f ) {
		offAxis = RAD2DEG( idMath::ACos( idMath::ClampFloat( -1.0f, 1.0f, dir * axis[0] ) ) );
	}

	// Something at the edge of vision registers late; something dead ahead does
	// not.  Linear in the angle, so it reads as a gradient rather than as a
	// switch that flips at the edge of the cone.
	const float periph	= traits.peripheralPenaltyMsec *
						  idMath::ClampFloat( 0.0f, 1.0f, offAxis / Max( traits.peripheralAngle, 1.0f ) );
	const float base	= fresh ? traits.reactionMsec : traits.reactionMsec * traits.reacquireFraction;
	const float jitter	= gameLocal.random.CRandomFloat() * traits.reactionVarianceMsec;

	// Initiative is taste, not competence, so it bends the reaction by a
	// quarter at most: 0.75 at the eager end, 1.25 at the deliberate one.
	// The old 1.5 - initiative halved a bot's reaction at 1.0, and content
	// reaches 1.0 - the rusher style adds 0.30 to a skill-1 baseline of 0.80 -
	// which put a "genuinely poor" bot on a skill-4 reaction.
	const float initiativeScale = 1.25f - 0.5f * idMath::ClampFloat( 0.0f, 1.0f, traits.initiative );

	enemyFireTime	= gameLocal.time + Max( 0, (int)( ( base + jitter + periph ) * initiativeScale ) );
	onTargetTime	= 0;
	burstEndTime	= 0;
	burstRestTime	= 0;

	if ( fresh ) {
		// A new engagement starts with the aim believing the truth rather than
		// wherever the last fight ended, and with its own roll of the dice.
		aimBeliefValid	= false;
		aimTrackValid	= false;
		aimAngVel		= 0.0f;
		aimLeadRoll		= 1.0f + gameLocal.random.CRandomFloat() * traits.aimLeadError;
		// Putting a rocket into the floor under someone instead of their chest
		// is a learned habit, so whether this engagement uses it is a skill roll.
		aimAtFeet		= ( gameLocal.random.RandomFloat() < traits.weaponSkill );
		RollMistake();
	}

	if ( bot_debugAim.GetInteger() >= 1 ) {
		gameLocal.Printf( "bot %s: %s %s at %.0f deg off centre, may fire in %d ms\n",
						  name.c_str(), fresh ? "sighted" : "re-sighted",
						  BotDisplayName( foe ), offAxis, enemyFireTime - gameLocal.time );
	}
}

/*
================
rvBot::UpdateEnemy
================
*/
void rvBot::UpdateEnemy( idPlayer *self ) {
	idVec3	eye;
	idMat3	axis;

	self->GetViewPos( eye, axis );

	const idVec3	origin		= self->GetPhysics()->GetOrigin();
	const float		sightSqr	= traits.sightRange * traits.sightRange;

	// A bot used to notice players through the back of its own head.  fov is a
	// full cone, so half of it is what the view direction is tested against.
	const float		cosFov		= idMath::Cos( DEG2RAD( idMath::ClampFloat( 1.0f, 180.0f, traits.fov * 0.5f ) ) );
	idPlayer *	nearest			= NULL;
	float		nearestDistSqr	= sightSqr;
	float		nearestScore	= idMath::INFINITY;
	idPlayer *	best			= NULL;
	float		bestScore		= idMath::INFINITY;
	idPlayer *	current			= enemy.GetEntity();
	float		currentScore	= idMath::INFINITY;

	// The exposed point each contender was accepted on, kept so the winner does
	// not have to be traced for a second time.
	idVec3		nearestVisiblePoint	= enemyVisiblePoint;
	idVec3		bestVisiblePoint	= enemyVisiblePoint;
	idVec3		currentVisiblePoint	= enemyVisiblePoint;

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[i];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *other = static_cast<idPlayer *>( ent );
		if ( !IsEnemy( self, other ) ) {
			continue;
		}

		const float distSqr = ( other->GetPhysics()->GetOrigin() - origin ).LengthSqr();
		if ( distSqr >= sightSqr ) {
			continue;
		}

		const bool damagedUs = ( other == lastAttacker.GetEntity() &&
								 gameLocal.time - lastAttackerTime < BOT_ALERT_MSEC );
		const bool invisRevealed = ( other == lastAttacker.GetEntity() &&
									gameLocal.time - lastAttackerTime < BOT_INVIS_REVEAL_MSEC );
		const bool firing = gameLocal.usercmds && other->entityNumber >= 0 &&
						other->entityNumber < gameLocal.numClients &&
						( gameLocal.usercmds[other->entityNumber].buttons & BUTTON_ATTACK );
		if ( other->PowerUpActive( POWERUP_INVISIBILITY ) &&
			 !invisRevealed && !( firing && distSqr < 700.0f * 700.0f ) ) {
			continue;
		}

		idVec3 dir = other->GetPhysics()->GetAbsBounds().GetCenter() - eye;
		if ( dir.Normalize() <= 0.0f ) {
			continue;
		}
		if ( !damagedUs && dir * axis[0] < cosFov ) {
			continue;
		}
		// A contender that cannot improve either choice still needs tracing if
		// it is our current target: continuity and stickiness depend on it.
		const float score = TargetScore( other, distSqr );
		if ( other != current && distSqr >= nearestDistSqr && score >= bestScore ) {
			continue;
		}

		idVec3 visiblePoint;
		if ( !CanSee( self, other, &visiblePoint ) ) {
			continue;
		}

		// Keep each contender's exposed point as it is sampled.  Whichever wins
		// below already had this computed here, and BotCombatFindVisibleAimPoint
		// is up to seven traces - running it a second time for the winner is a
		// full extra visibility query per bot per frame for an answer already in
		// hand.
		if ( distSqr < nearestDistSqr ) {
			nearestDistSqr		= distSqr;
			nearestScore			= score;
			nearest				= other;
			nearestVisiblePoint	= visiblePoint;
		}
		if ( score < bestScore ) {
			bestScore			= score;
			best				= other;
			bestVisiblePoint	= visiblePoint;
		}
		if ( other == current ) {
			currentScore		= score;
			currentVisiblePoint	= visiblePoint;
		}
	}

	idPlayer *pick = targetPickBest ? best : nearest;

	// Being shot from behind turns a bot around; so should a fight it can hear.
	// Only when nothing is in sight: a noise locates, it never outranks a view.
	if ( !pick ) {
		UpdateHearing( self );
		current = enemy.GetEntity();
	}

	// Stay on the target already being shot at unless the alternative is enough
	// better for this personality.  targetStickiness 0 changes readily, 1 only
	// yields to an opponent scoring less than half as much.  The neutral 0.5
	// resolves to the original 0.75 margin.
	if ( current && currentScore < idMath::INFINITY && pick != current ) {
		const float pickScore = targetPickBest ? bestScore : nearestScore;
		const float switchMargin = 1.0f - 0.5f * traits.targetStickiness;

		if ( pickScore > currentScore * switchMargin ) {
			pick = current;
		}
	}

	if ( pick ) {
		const int	gap		= gameLocal.time - enemyLastSeenTime;
		const bool	same	= ( current == pick );

		// Three cases, and the middle one is the whole point: a different
		// player or a long absence is a fresh sighting and costs the full
		// reaction; a short absence still costs reacquireFraction of it; only
		// an unbroken line of sight costs nothing.
		if ( !same || gap > BOT_CONTINUOUS_MSEC ) {
			// Switching to a second opponent inside a running fight is a
			// re-acquisition, not a cold sighting: attention is already on
			// the fight.  It still costs reacquireFraction of the reaction, and
			// the new aim still has to slew and settle before it may fire.
			const bool inFight = !same && current && gap <= BOT_CONTINUOUS_MSEC;
			AcquireEnemy( self, pick, !inFight && ( !same || gap > (int)traits.reacquireMsec ) );
		} else if ( gameLocal.time >= nextMistakeTime ) {
			RollMistake();
		}

		enemyLastSeenTime	= gameLocal.time;
		enemyLastSeenOrigin	= pick->GetPhysics()->GetOrigin();
		enemyLastSeenVelocity = pick->GetPhysics()->GetLinearVelocity();

		// Taken from the loop above rather than re-traced.  pick is always one of
		// the three contenders, and every one of them was only a contender
		// because CanSee succeeded and handed back this exact point.
		enemyVisiblePoint = ( pick == current ) ? currentVisiblePoint :
							( pick == best ) ? bestVisiblePoint : nearestVisiblePoint;
		return;
	}

	// Keep chasing a lost enemy for a moment, then forget it.  How long that
	// moment lasts is the personality's: a hunter follows someone through two
	// rooms, a roamer has already moved on to the next weapon.  A sound counts
	// as contact, so an enemy fighting out of sight stays on the bot's mind.
	const int memory = (int)( BOT_ENEMY_MEMORY_MSEC * ( 0.5f + traits.pursuit ) );

	if ( current && ( !IsEnemy( self, current ) || gameLocal.time - EnemyLastKnownTime() > memory ) ) {
		enemy			= NULL;
		enemyLastHeardTime = 0;
		aimBeliefValid	= false;
		aimTrackValid	= false;
		onTargetTime	= 0;
		targetPickBest = ( gameLocal.random.RandomFloat() < traits.targetSelection );
	}
}

/*
================
rvBot::EnemyLastKnownTime
================
*/
int rvBot::EnemyLastKnownTime( void ) const {
	return Max( enemyLastSeenTime, enemyLastHeardTime );
}

/*
================
rvBot::UpdateHearing

The manager records each player's loudest noise of the frame - weapon fire,
running footsteps, a pickup - with how far an average listener hears it.  The
nearest audible enemy becomes the bot's contact: it turns toward the noise and
may go and look, but the position it believes is the noise plus an error that
grows with distance, and it still needs a sighting, paying the full reaction,
before it may shoot.  Without this a bot ignores a firefight behind its back
until a stray round finally hits it.
================
*/
void rvBot::UpdateHearing( idPlayer *self ) {
	if ( gameLocal.time < nextHearingTime || traits.hearing <= 0.0f ) {
		return;
	}
	// Ten scans a second is a human's attention, and staggering by client keeps
	// a full server from paying every scan on the same frame.
	nextHearingTime = gameLocal.time + 100;

	const idVec3 origin = self->GetPhysics()->GetOrigin();
	idPlayer *current = enemy.GetEntity();
	idPlayer *heard = NULL;
	float heardDistance = idMath::INFINITY;
	idVec3 heardOrigin = vec3_zero;

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		const rvBotManager::botNoise_t &noise = botManager.GetNoise( i );
		if ( noise.time <= 0 || gameLocal.time - noise.time > BOT_NOISE_MEMORY_MSEC ) {
			continue;
		}

		idEntity *ent = gameLocal.entities[i];
		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		idPlayer *other = static_cast<idPlayer *>( ent );
		if ( !IsEnemy( self, other ) ) {
			continue;
		}

		const float distance = ( noise.origin - origin ).Length();
		if ( distance > noise.range * traits.hearing ) {
			continue;
		}

		// The current contact keeps priority unless something is clearly
		// closer; flipping between two distant fights helps nobody.
		const float bias = ( other == current ) ? 0.7f : 1.0f;
		if ( distance * bias < heardDistance ) {
			heardDistance = distance * bias;
			heard = other;
			heardOrigin = noise.origin;
		}
	}

	if ( !heard ) {
		return;
	}

	// A noise is a direction and a rough distance, not a position fix.
	const float distance = ( heardOrigin - origin ).Length();
	const float spread = distance * 0.12f * idMath::ClampFloat( 0.2f, 1.5f, 1.4f - traits.hearing );
	idVec3 error( gameLocal.random.CRandomFloat(), gameLocal.random.CRandomFloat(), 0.0f );
	error *= spread;

	if ( heard != current ) {
		// A new contact.  The belief starts from where it was heard, and the
		// sighting that follows is fresh: hearing someone does not make a
		// reaction free, it only makes the bot look the right way first.
		enemy			= heard;
		enemyLastSeenTime = 0;
		aimBeliefValid	= false;
		aimTrackValid	= false;
		onTargetTime	= 0;
	}

	enemyLastHeardTime		= gameLocal.time;
	enemyLastSeenOrigin		= heardOrigin + error;
	enemyLastSeenVelocity.Zero();
}

/*
================
rvBot::EnemyPursuitOrigin

A chase route should meet a moving opponent, not repeatedly terminate where
the opponent was at the last scan.  Visible opponents receive a short forward
intercept.  Once sight is lost, prediction grows only briefly from the last
observed velocity and fades with memory age; it never reads live movement
through a wall.  Vertical launch velocity is deliberately ignored because the
navigation goal is the floor the opponent is travelling across.
================
*/
idVec3 rvBot::EnemyPursuitOrigin( void ) const {
	idVec3 predicted = enemyLastSeenOrigin;
	const float age = Max( 0.0f, MS2SEC( gameLocal.time - enemyLastSeenTime ) );
	const bool visible = ( gameLocal.time == enemyLastSeenTime );

	float predictionSeconds;
	if ( visible ) {
		predictionSeconds = 0.20f + 0.30f * traits.pursuit;
	} else {
		const float confidence = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - age / 3.0f );
		predictionSeconds = Min( age, 0.75f ) * traits.pursuit * confidence;
	}

	idVec3 lead = enemyLastSeenVelocity * predictionSeconds;
	lead.z = 0.0f;
	const float leadDistance = lead.Length();
	if ( leadDistance > 256.0f ) {
		lead *= 256.0f / leadDistance;
	}

	return predicted + lead;
}

/*
================
BotRidesPadArc

Coast through a pad's flight when the route lands where the pad's own arc
comes down and the arc, ridden, lands on supported ground connected to the
corner. A mapper's pad already carries a player to its landing, but an enemy
impulse can shift touchdown beyond that floor even without much sideways drift.
Air control is kept for the landings only it reaches and for an arc that
would come down short or wide.
================
*/
static bool BotRidesPadArc( const idVec3 &origin, const idVec3 &velocity, const navCorner_t &corner, float gravity ) {
	if ( gravity <= 1.0f || navMesh.IsSteeredPadLanding( corner.actionEntityNum, corner.origin ) ) {
		return false;
	}
	idVec3 flat( velocity.x, velocity.y, 0.0f );
	const float speed = flat.Normalize();
	if ( speed < 1.0f ) {
		return false;
	}
	const float above = origin.z - corner.origin.z;
	const float discriminant = velocity.z * velocity.z + 2.0f * gravity * above;
	if ( discriminant < 0.0f ) {
		return false;
	}
	const float time = ( velocity.z + idMath::Sqrt( discriminant ) ) / gravity;
	idVec3 toCorner = corner.origin - origin;
	toCorner.z = 0.0f;
	const float along = toCorner * flat;
	const idVec3 side = toCorner - flat * along;
	if ( time <= 0.0f || along > speed * time + 8.0f || side.LengthSqr() > Square( 32.0f ) ) {
		return false;
	}
	idVec3 touchdown = origin + velocity * time;
	touchdown.z = corner.origin.z;
	// Touching the far rim at pad speed is not enough: leave the same
	// supported momentum runway that the grounded edge guard looks ahead.
	const idVec3 rollout = touchdown + flat * ( speed * BOT_EDGE_LOOKAHEAD );
	return navMesh.LandingContinuationClear( touchdown, corner.origin ) &&
		navMesh.LandingContinuationClear( touchdown, rollout );
}

/*
================
BotGapToPad

How far the bot's box is from touching the pad that owns this route action,
across the floor: the pad throws on first touch, and its centre node - the
route's entry corner - can lie half a long pad further on.
================
*/
static float BotGapToPad( idPlayer *self, const navCorner_t &action ) {
	if ( action.actionEntityNum < 0 || action.actionEntityNum >= MAX_GENTITIES ) {
		return idMath::INFINITY;
	}
	idEntity *volume = gameLocal.entities[action.actionEntityNum];
	idBounds bounds;
	if ( !volume || !volume->IsType( rvJumpPad::GetClassType() ) ||
		 !static_cast<rvJumpPad *>( volume )->GetForceFieldBounds( bounds ) ) {
		return idMath::INFINITY;
	}
	const idBounds box = self->GetPhysics()->GetAbsBounds();
	const float dx = Max( 0.0f, Max( bounds[0].x - box[1].x, box[0].x - bounds[1].x ) );
	const float dy = Max( 0.0f, Max( bounds[0].y - box[1].y, box[0].y - bounds[1].y ) );
	return idMath::Sqrt( dx * dx + dy * dy );
}

/*
================
BotPadFlightsShareCorridor

Whether another flight runs along this one's corridor: both its ends lie
within the corridor's width of this flight's line, and the two spans along
it overlap.  q4ctf5's long pads land beyond each other's launch, so one
flight's ends need not lie inside the other's span.
================
*/
static bool BotPadFlightsShareCorridor( const idVec3 &launch, const idVec3 &landing,
										const idVec3 &otherLaunch, const idVec3 &otherLanding ) {
	idVec3 along = landing - launch;
	along.z = 0.0f;
	const float length = along.Normalize();
	if ( length < 1.0f ) {
		return false;
	}
	float span[2];
	const idVec3 ends[2] = { otherLaunch, otherLanding };
	for ( int i = 0; i < 2; i++ ) {
		idVec3 offset = ends[i] - launch;
		offset.z = 0.0f;
		span[i] = offset * along;
		if ( ( offset - along * span[i] ).LengthSqr() > Square( BOT_PAD_CORRIDOR_WIDTH * 2.0f ) ) {
			return false;
		}
	}
	return Max( span[0], span[1] ) >= -BOT_PAD_CORRIDOR_WIDTH &&
		Min( span[0], span[1] ) <= length + BOT_PAD_CORRIDOR_WIDTH;
}

/*
================
BotPadCorridorBusy

Pads that throw players at each other over a gap - q4ctf5's either side of the
floating large armour - put anyone who enters one while someone is flying
the other way on a collision course over the gap.  Players are solid in
multiplayer, and a head-on meeting in the air stops both: they fell into the
void under the armour. Same-direction traffic can rebound from the far pad
before a follower lands. Reserve the finite corridor in either direction.

Flyers alone are not enough.  Two bots held at opposite pads both saw the
corridor clear on the frame the last flyer landed, both walked in, and met in
the air.  A bot about to enter a pad whose flight shares this corridor - its
published approach - holds the corridor too when it came within reach first,
the lower client number breaking a tie, so exactly one of them goes.
================
*/
static bool BotPadCorridorBusy( idPlayer *self, const idVec3 &launch, const idVec3 &landing, int approachSince ) {
	idVec3 along = landing - launch;
	along.z = 0.0f;
	const float length = along.Normalize();
	if ( length < 1.0f ) {
		return false;
	}

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer *other = gameLocal.GetClientByNum( i );
		if ( !other || other == self || other->spectating || other->health <= 0 ||
			 other->GetInstance() != self->GetInstance() || !other->GetPhysics() ) {
			continue;
		}
		int otherPad, otherSince;
		idVec3 otherLaunch, otherLanding;
		// A climb hop toward the trigger is still an approach, and may be
		// too slow for the flyer check below. Keep its fresh reservation
		// until the route releases it or the normal stale timeout expires.
		if ( botManager.PadApproachOf( i, otherPad, otherSince, otherLaunch, otherLanding ) &&
			 ( otherSince < approachSince || ( otherSince == approachSince && i < self->entityNumber ) ) &&
			 BotPadFlightsShareCorridor( launch, landing, otherLaunch, otherLanding ) ) {
			return true;	// it reserved the shared corridor first
		}
		if ( other->pfl.onGround ) {
			continue;
		}
		const idVec3 &velocity = other->GetPhysics()->GetLinearVelocity();
		if ( idMath::Fabs( velocity.x * along.x + velocity.y * along.y ) < BOT_PAD_LAUNCH_SPEED * 0.5f ) {
			continue;		// not travelling along this flight corridor
		}
		idVec3 offset = other->GetPhysics()->GetOrigin() - launch;
		offset.z = 0.0f;
		const float ahead = offset * along;
		if ( ahead < 0.0f || ahead > length ) {
			continue;
		}
		if ( ( offset - along * ahead ).LengthSqr() <= Square( BOT_PAD_CORRIDOR_WIDTH ) ) {
			return true;
		}
	}
	return false;
}

/*
================
rvBot::HoldForPad

Reserve the approach until the opposing flight clears. traversalEntered only
means the expanded trigger bounds are close; it must not cancel the wait.
Brake instead of coasting into the actual trigger, and reconsider a prolonged
blockage instead of deliberately launching into a known head-on collision.
Every bot within a second's run of its pad publishes that approach, held or
not, so that the other bots give way to it before either is too close to
stop: a bot that reached braking range on the frame its opponent launched
was still carried in.
================
*/
bool rvBot::HoldForPad( idPlayer *self, idVec3 &moveDir ) {
	if ( pathCorner < 0 || pathCorner + 1 >= path.Num() ||
		 path[pathCorner].travelType != NAVTRAVEL_WALK || traversalStarted ) {
		padHoldSince = 0;
		padApproachEntity = -1;
		return false;
	}
	// Several walking corners can fit inside the braking distance of a pad.
	// Waiting for the final corner lets existing momentum carry us into its
	// trigger before the next command can stop us. Only look through walking
	// corners: an intervening traversal must retain ownership of its approach.
	int actionCorner = pathCorner + 1;
	while ( actionCorner < path.Num() && path[actionCorner].travelType == NAVTRAVEL_WALK ) {
		actionCorner++;
	}
	const float gap = ( actionCorner < path.Num() && path[actionCorner].travelType == NAVTRAVEL_JUMPPAD ) ?
		BotGapToPad( self, path[actionCorner] ) : idMath::INFINITY;
	if ( gap >= BOT_PAD_APPROACH_REACH ) {
		padHoldSince = 0;
		padApproachEntity = -1;
		return false;
	}
	if ( padApproachEntity != path[actionCorner].actionEntityNum ) {
		padApproachEntity = path[actionCorner].actionEntityNum;
		padApproachSince = gameLocal.time;
	}
	padApproachTime = gameLocal.time;
	padApproachLaunch = path[actionCorner - 1].origin;
	padApproachLanding = path[actionCorner].origin;
	// Published from BOT_PAD_APPROACH_REACH; braking only within hold reach.
	if ( gap >= BOT_PAD_HOLD_REACH ||
		 !BotPadCorridorBusy( self, padApproachLaunch, padApproachLanding, padApproachSince ) ) {
		padHoldSince = 0;
		return false;
	}
	if ( !padHoldSince ) {
		padHoldSince = gameLocal.time;
		if ( bot_debug.GetInteger() >= 1 ) {
			gameLocal.Printf( "bot %s: waiting at %s for a pad corridor to clear (time %d)\n", name.c_str(),
				self->GetPhysics()->GetOrigin().ToString( 0 ), gameLocal.time );
		}
	}
	if ( gameLocal.time - padHoldSince >= BOT_PAD_HOLD_MSEC ) {
		padHoldSince = gameLocal.time;
		pathTime = 0;
		nextGoalSelectTime = Min( nextGoalSelectTime, gameLocal.time );
	}
	stuckTime = gameLocal.time;
	stuckChecks = 0;
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	moveDir.Set( -velocity.x, -velocity.y, 0.0f );
	if ( moveDir.LengthSqr() < Square( 12.0f ) ) {
		moveDir.Zero();
	}
	return true;
}

/*
================
rvBot::AdoptPadLaunch

An observed pad touch owns its real flight even when the route did not choose
it. Preserve the goal, but ride the pad before attempting to resume that goal.
================
*/
void rvBot::AdoptPadLaunch( idPlayer *self ) {
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	// A force field can set the launch velocity after player physics, while
	// onGround still describes the preceding frame. Catch that first command.
	if ( velocity.z <= BOT_PAD_LAUNCH_SPEED && ( self->pfl.onGround ||
		 idVec3( velocity.x, velocity.y, 0.0f ).LengthSqr() <= Square( BOT_PAD_LAUNCH_SPEED * 1.25f ) ) ) {
		return;
	}

	const int padNum = navMesh.TransportAt( self->GetPhysics()->GetAbsBounds() );
	if ( padNum < 0 || padNum >= MAX_GENTITIES ) {
		return;
	}
	// The planned action already owns this flight. A different pad may have
	// caught a drop's overshoot or a bot pushed away from its walking route.
	for ( int i = pathCorner; i <= pathCorner + 1 && i < path.Num(); i++ ) {
		if ( i >= 0 && path[i].travelType == NAVTRAVEL_JUMPPAD && path[i].actionEntityNum == padNum ) {
			ResetDodgeJump();
			return;
		}
	}
	idEntity *entity = gameLocal.entities[padNum];
	if ( !entity || !entity->IsType( rvJumpPad::GetClassType() ) || entity->GetInstance() != self->GetInstance() ) {
		return;
	}
	const idClipModel *volume = static_cast<rvJumpPad *>( entity )->GetForceFieldClipModel();
	const idClipModel *playerClip = self->GetPhysics()->GetClipModel();
	if ( !volume || !playerClip || !gameLocal.ContentsModel( self, self->GetPhysics()->GetOrigin(),
		 playerClip, self->GetPhysics()->GetAxis(), -1, volume->GetCollisionModel(), volume->GetOrigin(), volume->GetAxis() ) ) {
		return;		// a diagonal pad's bounding box alone is not a real touch
	}
	ResetDodgeJump();
	idVec3 landing;
	if ( !navMesh.NaturalPadLanding( padNum, landing ) ) {
		return;
	}

	// Commit the observed launch as a short recovery route. The existing pad
	// controller then rides its proven arc; the goal is reconsidered on landing.
	// On q4ctf5 a drop beside the diagonal pad left a WALK corner behind it,
	// whose air input braked the unintended launch into the void.
	const idVec3 origin = self->GetPhysics()->GetOrigin();
	path.Clear();
	path.Append( origin, NAVTRAVEL_WALK );
	path.Append( landing, NAVTRAVEL_JUMPPAD, padNum );
	pathCorner = 1;
	pathTime = gameLocal.time;
	ResetTraversal();
	traversalCorner = pathCorner;
	traversalStartOrigin = origin;
	traversalStartTime = gameLocal.time;
	traversalEntered = true;
	traversalStarted = true;
	stuckTime = gameLocal.time;
	stuckChecks = 0;
	unstickUntil = 0;
	padHoldSince = 0;
	if ( bot_debug.GetInteger() >= 1 ) {
		gameLocal.Printf( "bot %s: adopted pad %d at %s toward %s velocity %s (time %d)\n",
			name.c_str(), padNum, origin.ToString( 0 ), landing.ToString( 0 ), velocity.ToString( 0 ), gameLocal.time );
	}
}

/*
================
rvBot::ProbeAirControl

bot_probeAir's bound on recovery: one fixed horizontal direction at full
input, the most a player's air acceleration can add along it, for the whole
flight; once grounded, brake as hard as the feet allow.  No route, goal or
pad adoption runs, so the outcome is the player physics' answer alone.
================
*/
// The strongest ordinary MP ground brake that will not accelerate backwards.
// Match friction before acceleration and leave half a command byte for rounding.
static float BotGroundBrakeStrength( float speed ) {
	const float dt = Max( 1, gameLocal.GetMSec() ) * 0.001f;
	const float afterFriction = Max( 0.0f, speed - Max( speed, 100.0f ) * 6.0f * dt );
	return Min( 1.0f, Max( 0.0f, afterFriction / ( 15.0f * dt * Max( 1.0f, pm_speed.GetFloat() ) ) - 0.5f / 127.0f ) );
}

void rvBot::ProbeAirControl( idPlayer *self, usercmd_t &cmd ) {
	if ( !self->pfl.onGround ) {
		if ( probeAirDirection.LengthSqr() > 0.0f ) {
			ApplyMove( probeAirDirection, cmd );
		}
		return;
	}
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	idVec3 brake( -velocity.x, -velocity.y, 0.0f );
	const float speed = brake.Normalize();
	if ( speed > 10.0f ) {
		// Full reverse ground acceleration can cross zero every frame and
		// walk a settled replay off its ledge. Reduce the requested speed as
		// it stops; the friction-free knock interval still needs full input.
		if ( static_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->GetKnockBack() <= 0 ) {
			brake *= BotGroundBrakeStrength( speed );
		}
		ApplyMove( brake, cmd );
	}
}

/*
================
rvBot::ReportProbeGrounding

The first frame a probed bot stands on the ground again, so a replay can
tell a landing from a flight still in progress.
================
*/
void rvBot::ReportProbeGrounding( idPlayer *self ) {
	if ( !probeWatch ) {
		return;
	}
	if ( !self->pfl.onGround ) {
		probeAirborneSeen = true;
		return;
	}
	// The probe state starts on the floor with a launch velocity; the first
	// frames still read as grounded until player physics lifts it off.
	if ( !probeAirborneSeen && gameLocal.time - probeStartTime < 500 ) {
		return;
	}
	probeWatch = false;
	gameLocal.Printf( "BOT_PROBE_GROUNDED client=%d time=%d origin=%s velocity=%s health=%d\n",
		self->entityNumber, gameLocal.time, self->GetPhysics()->GetOrigin().ToString( 3 ),
		self->GetPhysics()->GetLinearVelocity().ToString( 3 ), self->health );
}

/*
================
rvBot::ResetTraversal

The route owns the traversal state. Any route replacement or abandonment must
drop it, or corner zero in the new route can inherit a completed jump.
================
*/
void rvBot::ResetTraversal( void ) {
	traversalCorner		= -1;
	traversalStartOrigin.Zero();
	traversalStartTime	= 0;
	traversalEntered		= false;
	traversalStarted	= false;
	landingCheckPathTime = -1;
	landingCheckCorner = -1;
	landingCheckLimit = idMath::INFINITY;
}

// Use the player's actual damage calculation (including armour, damage
// scale and MP declarations) without applying damage. CalcDamagePoints only
// mutates a godmode counter, so skip immune tiers before calling it.
static float BotMaxSafeFallDelta( idPlayer *self ) {
	if ( self->pfl.noFallingDamage ) {
		return idMath::INFINITY;
	}
	const char *keys[3] = { "soft_fall_delta", "hard_fall_delta", "fatal_fall_delta" };
	const char *defaults[3] = { "30", "45", "65" };
	const char *names[3] = { "damage_softfall", "damage_hardfall", "damage_fatalfall" };
	float limit = idMath::INFINITY;
	for ( int i = 0; i < 3; i++ ) {
		const float threshold = self->spawnArgs.GetFloat( keys[i], defaults[i] );
		if ( threshold <= 0.0f ) {
			continue;
		}
		const idDict *damageDef = gameLocal.FindEntityDefDict( names[i], false );
		if ( !damageDef || damageDef->GetBool( "ignore_player", "0" ) ||
			 ( self->godmode && !damageDef->GetBool( "noGod", "0" ) ) ) {
			continue;
		}
		int damage, armor;
		self->CalcDamagePoints( gameLocal.world, gameLocal.world, damageDef, 1.0f, 0, &damage, &armor );
		if ( damage > 0 && damage >= self->health ) {
			limit = Min( limit, threshold );
		}
	}
	return limit;
}

bool rvBot::GuardLandingHealth( idPlayer *self, usercmd_t &cmd ) {
	if ( !self->pfl.onGround || traversalStarted || pathCorner < 0 || pathCorner >= path.Num() ) {
		return false;		// a committed flight must keep its landing controller
	}
	int action = pathCorner;
	// Recheck the first traversal as soon as damage makes a retained route
	// lethal. Waiting for its final walking corner may leave no braking room.
	while ( path[action].travelType == NAVTRAVEL_WALK && action + 1 < path.Num() ) {
		action++;
	}
	const navCorner_t corner = path[action];
	if ( corner.travelType != NAVTRAVEL_DROP && corner.travelType != NAVTRAVEL_JUMP &&
		 corner.travelType != NAVTRAVEL_JUMPPAD ) {
		return false;
	}
	if ( corner.travelType == NAVTRAVEL_JUMPPAD &&
		 self->GetPhysics()->GetLinearVelocity().z > BOT_PAD_LAUNCH_SPEED ) {
		return false;		// pad impulse can precede the next onGround update
	}
	const float limit = BotMaxSafeFallDelta( self );
	if ( limit == idMath::INFINITY || ( landingCheckPathTime == pathTime &&
		 landingCheckCorner == action && landingCheckLimit == limit ) ) {
		return false;
	}
	// A pad whose landing lies on another pad - q4ctf5's func_jumppad_14 drops
	// its flyers onto func_jumppad_15, which throws them across the map - is
	// one flight to the last landing; nothing in between touches the ground.
	// Every pad in that chain must be survivable before the first is entered.
	int lethal = -1;
	for ( int step = action; step < path.Num(); step++ ) {
		if ( step > action && path[step].travelType != NAVTRAVEL_JUMPPAD ) {
			break;
		}
		const idVec3 from = step > 0 ? path[step - 1].origin : self->GetPhysics()->GetOrigin();
		if ( !navMesh.LandingSurvivable( from, path[step], limit ) ) {
			lethal = step;
			break;
		}
	}
	if ( lethal < 0 ) {
		// The route/landing geometry is fixed during this approach. Do not
		// simulate a whole pad flight each frame while health is unchanged.
		landingCheckPathTime = pathTime;
		landingCheckCorner = action;
		landingCheckLimit = limit;
		return false;
	}
	if ( bot_debug.GetInteger() >= 1 ) {
		gameLocal.Printf( "bot %s: avoiding lethal landing at %s with %d health (impact limit %.1f)\n",
			name.c_str(), path[lethal].origin.ToString( 0 ), self->health, limit );
	}
	AbandonGoal( BOT_GOAL_AVOID_MSEC );
	nextGoalSelectTime = gameLocal.time;
	// Merely sending zero input still coasts into a pad/drop. Cancel momentum
	// this frame; the next decision searches with the new health limit too.
	idVec3 brake = -self->GetPhysics()->GetLinearVelocity();
	brake.z = 0.0f;
	if ( brake.Normalize() > 12.0f ) {
		ApplyMove( brake, cmd );
	}
	cmd.upmove = 0;
	return true;
}

/*
================
rvBot::Repath
================
*/
bool rvBot::Repath( idPlayer *self, const idVec3 &goal, bool preserveProgress ) {
	// This is the attempt clock the refresh throttles read, so it is stamped
	// before the search and on every outcome.  Keying it off a successful
	// search would leave the throttle dead on exactly the path it exists for.
	pathTime	= gameLocal.time;
	holdUntil	= 0;		// somewhere to be again, so no longer standing and watching

	// rvNavMesh::FindPath clears whatever path it is handed before it does
	// anything else, so searching straight into the live route destroys it on
	// every transient failure - an endpoint that stepped off the mesh for a
	// moment, or that snapped into another area.  Search into scratch and only
	// commit over the route the bot is following once there is a replacement.
	rvNavPath	found;
	const bool	routeSurvives = preserveProgress && pathCorner < path.Num();

	// A chase - an enemy, or a player the objective follows - is happy to end
	// beside its target when the target stands where no node fits.
	idEntity *goalTarget = goalEntity.GetEntity();
	const bool pursuit = goalType == BOTGOAL_ENEMY ||
		( goalType == BOTGOAL_OBJECTIVE && goalTarget && goalTarget->IsType( idPlayer::GetClassType() ) );
	if ( !navMesh.FindPath( self->GetPhysics()->GetOrigin(), goal, found, pursuit, BotMaxSafeFallDelta( self ) ) ) {
		if ( routeSurvives ) {
			// A refresh of a route that is still good.  Keeping it is the whole
			// point of preserveProgress, and a bot that still has somewhere to
			// walk is not off the mesh, so the recovery counters stay put.
			return false;
		}

		path.Clear();
		pathCorner	= 0;
		stuckPathCorner = 0;
		stuckCornerDistance = idMath::INFINITY;
		ResetTraversal();

		repathFailures++;
		if ( !noRouteSince ) {
			noRouteSince = gameLocal.time;
		}

		if ( bot_debug.GetInteger() >= 1 ) {
			gameLocal.Printf( "bot %s: no route from %s to %s (%d in a row)\n",
							  name.c_str(),
							  self->GetPhysics()->GetOrigin().ToString( 0 ),
							  goal.ToString( 0 ),
							  repathFailures );
		}
		return false;
	}

	const float pendingProgress = preserveProgress && routeSurvives ?
		Max( 0.0f, goalBestDistance - PathDistanceRemaining( self->GetPhysics()->GetOrigin() ) ) : 0.0f;
	path		= found;
	pathCorner	= 0;
	stuckPathCorner = 0;
	stuckCornerDistance = idMath::INFINITY;
	ResetTraversal();

	repathFailures = 0;
	noRouteSince = 0;
	// Refreshing a moving target's route is not evidence that the bot moved.
	// Rebase distance onto the replacement route, which may be longer when a
	// carrier moves. Keep the clock: a new path alone is not physical progress.
	goalBestDistance = PathDistanceRemaining( self->GetPhysics()->GetOrigin() ) + pendingProgress;
	if ( !preserveProgress ) {
		goalProgressTime = gameLocal.time;
	}

	return true;
}

/*
================
rvBot::PathDistanceRemaining

Measures progress along the route rather than displacement in the room.  A bot
circling a pillar can travel a long way while getting no closer to its corner;
that is not progress and must eventually trigger recovery.
================
*/
float rvBot::PathDistanceRemaining( const idVec3 &origin ) const {
	if ( pathCorner < 0 || pathCorner >= path.Num() ) {
		return 0.0f;
	}

	float distance = ( path[pathCorner].origin - origin ).Length();
	for ( int i = pathCorner; i + 1 < path.Num(); i++ ) {
		distance += ( path[i + 1].origin - path[i].origin ).Length();
	}

	return distance;
}

/*
================
rvBot::AbandonGoal

Drops the current goal and refuses to take it again for a while.  Without this
a bot that has parked on a weapon it cannot use will keep routing to the item
it is already standing on, forever.
================
*/
void rvBot::AbandonGoal( int forMsec ) {
	Avoid( goalEntity.GetEntity(), forMsec );

	goalType		= BOTGOAL_ROAM;
	goalEntity		= NULL;
	goalGiveUpTime	= 0;
	goalUtility		= 0.0f;
	goalCommitUntil	= 0;
	goalBestDistance	= idMath::INFINITY;
	goalProgressTime	= 0;
	objectiveKind		= BOTOBJ_NONE;
	objectiveHoldPosition = false;
	pathCorner		= 0;
	pathTime		= 0;
	ResetTraversal();
	path.Clear();
}

/*
================
rvBot::IsAvoided
================
*/
bool rvBot::IsAvoided( const idEntity *ent ) const {
	if ( !ent ) {
		return false;
	}

	for ( int i = 0; i < MAX_AVOID_GOALS; i++ ) {
		if ( avoidGoals[i].ent.GetEntity() == ent && gameLocal.time < avoidGoals[i].until ) {
			return true;
		}
	}

	return false;
}

/*
================
rvBot::Avoid
================
*/
void rvBot::Avoid( idEntity *ent, int forMsec ) {
	if ( !ent ) {
		return;
	}

	// Refresh an entry this entity already owns before taking a new slot.
	for ( int i = 0; i < MAX_AVOID_GOALS; i++ ) {
		if ( avoidGoals[i].ent.GetEntity() == ent ) {
			avoidGoals[i].until = gameLocal.time + forMsec;
			return;
		}
	}

	for ( int i = 0; i < MAX_AVOID_GOALS; i++ ) {
		if ( gameLocal.time >= avoidGoals[i].until ) {
			avoidGoals[i].ent	= ent;
			avoidGoals[i].until	= gameLocal.time + forMsec;
			return;
		}
	}

	// All still live: recycle round robin.
	avoidGoals[avoidNext].ent	= ent;
	avoidGoals[avoidNext].until	= gameLocal.time + forMsec;
	avoidNext = ( avoidNext + 1 ) % MAX_AVOID_GOALS;
}

/*
================
rvBot::RecoverToNavMesh

Called when there is no route at all.  Usually the bot has ended up somewhere
the mesh does not cover - blown off a ledge, telefragged into a corner - so it
heads for the closest thing it does know about, and failing that it just keeps
moving rather than standing still until the match ends.
================
*/
bool rvBot::RecoverToNavMesh( idPlayer *self, usercmd_t &cmd ) {
	const idVec3 origin = self->GetPhysics()->GetOrigin();

	// The search widens over a lot of empty grid when the bot really is off the
	// mesh, so it runs on a timer and the answer is reused in between.
	if ( gameLocal.time - recoverSearchTime > BOT_RECOVER_SEARCH_MSEC ) {
		recoverSearchTime = gameLocal.time;

		const int node = navMesh.FindNearestNode( origin, BOT_RECOVER_RADIUS, true );

		recoverValid = ( node != -1 );
		if ( recoverValid ) {
			recoverTarget = navMesh.GetNode( node ).origin;
		}
	}

	idVec3 moveDir;

	if ( recoverValid ) {
		moveDir = recoverTarget - origin;
		moveDir.z = 0.0f;

		// Only worth walking to if it is somewhere else.  A bot standing on the
		// mesh with no route needs to wander, not to shuffle on the spot.
		if ( moveDir.LengthSqr() > BOT_CORNER_REACH * BOT_CORNER_REACH && moveDir.Normalize() > 0.0f ) {
			const idVec3 recoveryDirection = moveDir;
			const bool braking = BotBrakeAtEdge( self, moveDir );
			if ( braking ) {
				// A stationary outer hull rim can lie farther from central
				// support than the guard's sixteen-unit directional probe.
				// Do not strand it there forever: creep along a separately
				// proved, continuously supported prefix toward its known node.
				idVec3 velocity = self->GetPhysics()->GetLinearVelocity();
				velocity.z = 0.0f;
				const float along = velocity * recoveryDirection;
				const idVec3 sideways = velocity - recoveryDirection * along;
				if ( velocity.LengthSqr() <= Square( BOT_RECOVER_EDGE_SPEED ) && along >= -1.0f &&
					 sideways.LengthSqr() <= 1.0f && navMesh.RecoveryStepClear( origin, recoverTarget ) ) {
					const float speed = Max( 1.0f, pm_speed.GetFloat() * self->PowerUpModifier( PMOD_SPEED ) );
					const float strength = (float)(int)( 127.0f * Min( 1.0f, BOT_RECOVER_EDGE_SPEED / speed ) ) / 127.0f;
					moveDir = recoveryDirection * strength;
					cmd.upmove = 0;		// the supported crawl was not proved as a jump
					stuckTime = gameLocal.time;
					stuckChecks = 0;
					unstickUntil = 0;
				}
			}
			// The initial no-route guard saw only momentum, before recovery
			// chose this command (or replaced its edge brake with a crawl).
			// No transport is planned while returning to the mesh.
			const bool transportBraking = BotBrakeForTransport( self, moveDir, -1 );
			ApplyMove( moveDir, cmd );
			// The hop over whatever stands between the bot and its node is
			// proved by a whole simulated flight, up to a hundred box sweeps.
			// A stranded bot asked for it on every other grounded frame; four
			// times a second is as often as the obstacle changes.
			if ( !braking && !transportBraking && ( ( gameLocal.framenum + clientNum ) % 16 ) == 0 ) {
				TryDodgeJump( self, moveDir, cmd );
			}
			return true;
		}
	}

	// Nothing to head for: wander so the bot is at least a moving target, and
	// so that a genuinely stranded one covers ground looking for the mesh.
	//
	// The heading is held for a while rather than turned every frame.  A fixed
	// per-frame increment is a per-FRAME rate, not a per-second one - at the
	// fixed 60 Hz server tic three degrees a frame is 180 deg/s - and because
	// the move direction is taken from the same angles it is spinning, and
	// UpdateAim's no-target fallback aims straight down them so the slew never
	// opposes it, the bot walks a circle barely wider than it is tall.  That
	// looks broken and it cannot relocate anything.
	if ( gameLocal.time >= recoverWanderUntil ) {
		recoverWanderUntil	= gameLocal.time + BOT_RECOVER_WANDER_MSEC;
		recoverWanderYaw	= gameLocal.random.RandomFloat() * 360.0f;
	}

	const float maxTurn	= BOT_RECOVER_TURN_RATE * MS2SEC( gameLocal.GetMSec() );
	const float yawError	= idMath::AngleNormalize180( recoverWanderYaw - aimAngles.yaw );

	aimAngles.yaw = idMath::AngleNormalize180( aimAngles.yaw +
		idMath::ClampFloat( -maxTurn, maxTurn, yawError ) );
	// UpdateAim already wrote this command before recovery chose its heading.
	// Keep the view actually used by physics in the same frame as ApplyMove.
	cmd.angles[1] = ANGLE2SHORT( aimAngles.yaw - self->GetDeltaViewAngles()[1] );

	moveDir = aimAngles.ToForward();
	moveDir.z = 0.0f;

	if ( moveDir.Normalize() > 0.0f ) {
		BotBrakeAtEdge( self, moveDir );
		BotBrakeForTransport( self, moveDir, -1 );
		ApplyMove( moveDir, cmd );
	}

	return false;
}

/*
================
rvBot::ItemUtility

Inventory-aware value for a live pickup.  A full-health bot ignores routine
health, a stocked player stops queueing on unusable ammo, and a missing weapon
or major powerup remains worth crossing a room for.  Flags and the DeadZone
artifact are reserved for the objective planner.
================
*/
float rvBot::ItemUtility( idPlayer *self, idItem *item ) const {
	if ( !self || !item || item->IsType( rvItemCTFFlag::GetClassType() ) ||
		 item->IsType( riDeadZonePowerup::GetClassType() ) ) {
		return 0.0f;
	}

	float utility = 0.0f;
	const int healthAmount = item->spawnArgs.GetInt( "inv_health", "0" );
	if ( healthAmount > 0 ) {
		const int maxHealth = Max( 1, self->inventory.maxHealth );
		const int deficit = Max( 0, maxHealth - self->health );
		if ( deficit > 0 ) {
			utility = 1.0f + 4.0f * idMath::ClampFloat( 0.0f, 1.0f,
				(float)Min( deficit, healthAmount ) / (float)maxHealth );
			if ( Withdrawing( self ) ) {
				utility *= 2.2f;
			}
		} else if ( healthAmount >= 50 && self->health < maxHealth * 2 ) {
			// Mega-style health remains useful as an overstack; small packs do not.
			utility = 1.25f;
		}
	}

	const int armorAmount = item->spawnArgs.GetInt( "inv_armor", "0" );
	if ( armorAmount > 0 ) {
		const int maxArmor = Max( 1, self->inventory.maxarmor );
		const int deficit = Max( 0, maxArmor - self->inventory.armor );
		if ( deficit > 0 ) {
			utility = Max( utility, 0.8f + 3.0f * idMath::ClampFloat( 0.0f, 1.0f,
				(float)Min( deficit, armorAmount ) / (float)maxArmor ) );
		}
	}

	const char *weaponName = item->spawnArgs.GetString( "inv_weapon", "" );
	if ( weaponName[0] ) {
		const int slot = self->GetWeaponIndex( weaponName );
		const bool validSlot = slot >= 0 && slot < MAX_WEAPONS &&
			idStr::Icmp( self->spawnArgs.GetString( va( "def_weapon%d", slot ), "" ), weaponName ) == 0;
		if ( validSlot ) {
			const bool owned = ( self->inventory.weapons & ( 1 << slot ) ) != 0;
			if ( !owned ) {
				utility = Max( utility, 5.0f );
			}
		}
	}

	for ( const idKeyValue *ammo = item->spawnArgs.MatchPrefix( "inv_ammo_", NULL );
		  ammo != NULL; ammo = item->spawnArgs.MatchPrefix( "inv_ammo_", ammo ) ) {
		idStr ammoClass = ammo->GetKey();
		ammoClass.StripLeadingOnce( "inv_" );
		const int ammoIndex = self->inventory.AmmoIndexForAmmoClass( ammoClass.c_str() );
		const int maxAmmo = self->inventory.MaxAmmoForAmmoClass( self, ammoClass.c_str() );
		if ( ammoIndex > 0 && ammoIndex < MAX_AMMO && maxAmmo > 0 &&
			 self->inventory.ammo[ammoIndex] < maxAmmo ) {
			const float need = 1.0f - idMath::ClampFloat( 0.0f, 1.0f,
				(float)self->inventory.ammo[ammoIndex] / (float)maxAmmo );
			utility = Max( utility, 0.6f + 2.4f * need );
		}
	}

	if ( item->IsType( idItemPowerup::GetClassType() ) ) {
		// Arena CTF runes: a player carries one at a time, and a rune tagged
		// for a team refuses the other side.  idItemPowerup::GiveToPlayer
		// enforces both, so a bot that ignored them walked into the enemy base
		// for a rune it could never take, gave up on arrival and tried the next.
		const int type = idPlayer::PowerupForContentType( item->spawnArgs.GetInt( "type", "0" ) );
		if ( type >= POWERUP_AMMOREGEN && type <= POWERUP_SCOUT &&
			 ( self->inventory.powerups & ARENA_POWERUP_MASK ) != 0 ) {
			return 0.0f;
		}
		const char *teamName = item->spawnArgs.GetString( "team", "" );
		if ( gameLocal.IsTeamGame() && teamName[0] ) {
			const int itemTeam = !idStr::Icmp( teamName, "strogg" ) ? TEAM_STROGG :
								 !idStr::Icmp( teamName, "marine" ) ? TEAM_MARINE : TEAM_NONE;
			if ( itemTeam != TEAM_NONE && itemTeam != self->team ) {
				return 0.0f;
			}
		}
		utility = Max( utility, 6.0f );
	}

	return utility;
}

/*
================
rvBot::ItemPendingMsec
================
*/
int rvBot::ItemPendingMsec( const idItem *item ) const {
	if ( !item ) {
		return -1;
	}
	if ( !item->pickedUp && !item->IsHidden() ) {
		return 0;
	}

	const int respawnAt = botManager.KnownItemRespawn( item, clientNum );
	if ( respawnAt <= 0 ) {
		return -1;
	}

	const int remaining = respawnAt - gameLocal.time;
	if ( remaining < -BOT_ITEM_TIMER_STALE_MSEC ) {
		return -1;		// the belief was wrong; stop waiting on it
	}

	// Past its predicted time but not back yet: the bot's estimate ran early,
	// so it keeps expecting the item any moment rather than "now".
	return Max( 1, remaining );
}

/*
================
BotItemPatrolValue

What an item spot is worth visiting regardless of this bot's inventory, for
patrols.  Powerups, the mega and heavy armour are the spots a good player
controls; shards and ammo boxes are on the way, not worth the trip.
================
*/
static float BotItemPatrolValue( const idItem *item ) {
	if ( !item || item->IsType( rvItemCTFFlag::GetClassType() ) ||
		 item->IsType( riDeadZonePowerup::GetClassType() ) ) {
		return 0.0f;
	}
	if ( item->IsType( idItemPowerup::GetClassType() ) ) {
		return 5.0f;
	}

	const int health = item->spawnArgs.GetInt( "inv_health", "0" );
	const int armor = item->spawnArgs.GetInt( "inv_armor", "0" );
	if ( health >= 100 || armor >= 100 ) {
		return 4.5f;
	}
	if ( item->spawnArgs.GetString( "inv_weapon", "" )[0] ) {
		return 3.0f;
	}
	if ( armor >= 50 || health >= 50 ) {
		return 2.5f;
	}
	if ( armor >= 25 || health >= 25 ) {
		return 1.2f;
	}
	if ( item->spawnArgs.MatchPrefix( "inv_ammo_", NULL ) ) {
		return 0.8f;
	}
	return 0.3f;
}

/*
================
BotRunSpeed
================
*/
static float BotRunSpeed( void ) {
	return Max( 120.0f, pm_speed.GetFloat() );
}

typedef struct botItemCandidate_s {
	idItem *	item;
	float	utility;		// after team claims
	float	score;
} botItemCandidate_t;

// A pickup worth considering, gathered before any search so the search can be
// told exactly which nodes it has to price.
typedef struct botItemOption_s {
	idItem *	item;
	float		utility;		// after team claims, before any wait discount
	int			pendingMsec;
	int			node;
} botItemOption_t;

/*
================
rvBot::PickItemGoal

One bounded single-source search prices every useful pickup in range from the
bot's exact position, with the same weighted travel costs a route would pay,
and it stops as soon as the last of them is priced.  The old shortlist ran a
full A* - snaps, edge validation and string pulling - for up to ten candidates
on every decision tick of every bot.  Now only the winner pays for a validated
route, and if a mover has blocked it the next-best candidate is tried instead
of abandoning the choice.  A bot that needs nothing searches nothing.
================
*/
idEntity *rvBot::PickItemGoal( idPlayer *self, rvNavPath &goalPath, float &goalUtility ) {
	static const int MAX_ROUTED_CANDIDATES = 4;
	// Decision scratch reused across calls: the game thread is the only caller.
	static idList<botItemOption_t> options;
	static idList<navFieldTarget_t> targets;
	botItemCandidate_t candidates[MAX_ROUTED_CANDIDATES];
	int numCandidates = 0;
	const float searchRange = BOT_ITEM_SEARCH_RANGE * ( 0.5f + traits.itemFocus );
	const idVec3 origin = self->GetPhysics()->GetOrigin();

	goalPath.Clear();
	goalUtility = 0.0f;
	options.SetNum( 0, false );
	targets.SetNum( 0, false );

	const idList<idItem *> &items = botManager.GetItems();
	for ( int i = 0; i < items.Num(); i++ ) {
		idItem *item = items[i];
		if ( !item || item->GetInstance() != self->GetInstance() || IsAvoided( item ) ) {
			continue;
		}

		// A taken item is only a candidate when this bot knows its timer.
		int pendingMsec = 0;
		if ( item->pickedUp || item->IsHidden() ) {
			pendingMsec = ItemPendingMsec( item );
			if ( pendingMsec <= 0 ) {
				continue;
			}
		}

		const idVec3 &itemOrigin = item->GetPhysics()->GetOrigin();
		if ( ( itemOrigin - origin ).LengthSqr() > searchRange * searchRange ) {
			continue;
		}

		const float utility = ItemUtility( self, item );
		if ( utility <= 0.0f || ( pendingMsec > 0 && utility < BOT_ITEM_TIMED_MIN_VALUE ) ) {
			continue;
		}

		const int itemNode = navMesh.NodeForEntity( item );
		if ( itemNode < 0 ) {
			continue;
		}

		const int claims = botManager.GoalClaimCount( this, item );
		const float claimScale = 1.0f + (float)claims * 1.5f;

		botItemOption_t option;
		option.item = item;
		option.utility = utility / claimScale;
		option.pendingMsec = pendingMsec;
		option.node = itemNode;
		options.Append( option );

		// The field ranks by the same score this function does: utility over
		// route cost plus the exact leg onto the item plus the 192 bias.  A
		// timed item is worth between half and all of its utility once the
		// wait is known, so the stop rule is told both bounds.
		navFieldTarget_t target;
		target.node = itemNode;
		target.weight = option.utility;
		target.minWeight = pendingMsec > 0 ? option.utility * 0.5f : option.utility;
		target.extra = ( itemOrigin - navMesh.GetNode( itemNode ).origin ).Length() + 192.0f;
		targets.Append( target );
	}

	if ( options.Num() == 0 ) {
		return NULL;
	}

	// Detours are allowed up to half again the straight-line search range.
	const int startNode = navMesh.BuildCostField( origin, searchRange * 1.5f + 256.0f,
		targets.Ptr(), targets.Num(), MAX_ROUTED_CANDIDATES );
	if ( startNode == -1 ) {
		return NULL;
	}
	const float startLeg = ( navMesh.GetNode( startNode ).origin - origin ).Length();

	// A patient bot will wait longer at a spot for its item to come back.
	const float maxWaitMsec = (float)BOT_ITEM_MAX_WAIT_MSEC *
		( 0.4f + 0.6f * idMath::ClampFloat( 0.0f, 1.0f, traits.patience ) );

	for ( int i = 0; i < options.Num(); i++ ) {
		const botItemOption_t &option = options[i];
		idItem *item = option.item;
		const float fieldCost = navMesh.CostFieldDistance( option.node );
		if ( fieldCost >= idMath::INFINITY ) {
			continue;
		}

		const float routeDistance = startLeg + fieldCost +
			( item->GetPhysics()->GetOrigin() - navMesh.GetNode( option.node ).origin ).Length();

		float utility = option.utility;
		if ( option.pendingMsec > 0 ) {
			// Arrive as it comes back, not long before.  The wait that remains
			// after the run discounts the item, up to half its worth.
			const float travelMsec = routeDistance * 1000.0f / BotRunSpeed();
			const float waitMsec = (float)option.pendingMsec - travelMsec;
			if ( waitMsec > maxWaitMsec ) {
				continue;
			}
			utility *= 1.0f - 0.5f * idMath::ClampFloat( 0.0f, 1.0f, waitMsec / maxWaitMsec );
		}

		botItemCandidate_t candidate;
		candidate.item = item;
		candidate.utility = utility;
		candidate.score = candidate.utility / ( routeDistance + 192.0f );

		int insertAt = 0;
		while ( insertAt < numCandidates && candidates[insertAt].score >= candidate.score ) {
			insertAt++;
		}
		if ( insertAt >= MAX_ROUTED_CANDIDATES ) {
			continue;
		}
		if ( numCandidates < MAX_ROUTED_CANDIDATES ) {
			numCandidates++;
		}
		for ( int j = numCandidates - 1; j > insertAt; j-- ) {
			candidates[j] = candidates[j - 1];
		}
		candidates[insertAt] = candidate;
	}

	for ( int i = 0; i < numCandidates; i++ ) {
		if ( !navMesh.FindPath( origin, candidates[i].item->GetPhysics()->GetOrigin(), goalPath, false, BotMaxSafeFallDelta( self ) ) ) {
			continue;
		}

		goalUtility = candidates[i].utility;
		return candidates[i].item;
	}

	goalPath.Clear();
	return NULL;
}

/*
================
rvBot::RememberRoamPoint
================
*/
void rvBot::RememberRoamPoint( const idVec3 &point ) {
	roamHistory[roamHistoryNext] = point;
	roamHistoryTime[roamHistoryNext] = gameLocal.time;
	roamHistoryNext = ( roamHistoryNext + 1 ) % ROAM_HISTORY;
}

/*
================
rvBot::RoamNovelty

1 for a spot this bot has not patrolled lately, falling toward 0.15 for one it
has just been to.  Interest recovers over about forty-five seconds - roughly
the respawn cycle of the items that make a spot worth visiting.
================
*/
float rvBot::RoamNovelty( const idVec3 &point ) const {
	float novelty = 1.0f;

	for ( int i = 0; i < ROAM_HISTORY; i++ ) {
		if ( roamHistoryTime[i] <= 0 ) {
			continue;
		}
		if ( ( roamHistory[i] - point ).LengthSqr() > Square( BOT_ROAM_REVISIT_RADIUS ) ) {
			continue;
		}

		const float age = MS2SEC( gameLocal.time - roamHistoryTime[i] );
		novelty = Min( novelty, idMath::ClampFloat( 0.15f, 1.0f, age / 45.0f ) );
	}

	return novelty;
}

/*
================
rvBot::PickRoamGoal

A bot with nothing better to do should run the map the way a player does -
through the places that matter - rather than toward a uniformly random patch
of floor.  Item spots are an arena map's patrol points: each is weighted by
what it holds regardless of the bot's inventory and by how recently this bot
went there, a hunter's last contact joins the list, and in a quiet elimination
round so does a rough guess at each survivor.  One cost field from the bot
prices them all and stops once the last is priced.  A spread of random weight
keeps two bots from running identical laps; uniform roaming remains the
fallback for item-less layouts.
================
*/
bool rvBot::PickRoamGoal( idPlayer *self, idVec3 &roam, rvNavPath &roamPath ) {
	static const int MAX_ROAM_CANDIDATES = 4;
	// Decision scratch reused across calls: the game thread is the only caller.
	static idList<idVec3> pointOrigin;
	static idList<float> pointValue;
	static idList<int> pointNode;
	static idList<navFieldTarget_t> targets;
	const idVec3 origin = self->GetPhysics()->GetOrigin();

	roamPath.Clear();
	pointOrigin.SetNum( 0, false );
	pointValue.SetNum( 0, false );
	pointNode.SetNum( 0, false );
	targets.SetNum( 0, false );

	// Item spots.
	const idList<idItem *> &items = botManager.GetItems();
	for ( int i = 0; i < items.Num(); i++ ) {
		idItem *item = items[i];
		if ( !item || item->GetInstance() != self->GetInstance() || IsAvoided( item ) ) {
			continue;
		}
		const float value = BotItemPatrolValue( item );
		const int node = ( value > 0.0f ) ? navMesh.NodeForEntity( item ) : -1;
		if ( node < 0 ) {
			continue;
		}
		pointOrigin.Append( item->GetPhysics()->GetOrigin() );
		pointValue.Append( value );
		pointNode.Append( node );
	}

	// Where the last enemy was seen or heard, if it was recent.  A hunter goes
	// looking; a camper lets the fight come to it.
	if ( !enemyLastSeenOrigin.Compare( vec3_zero ) &&
		 EnemyLastKnownTime() > 0 && gameLocal.time - EnemyLastKnownTime() < 12000 ) {
		const int node = navMesh.SnapNode( enemyLastSeenOrigin );
		if ( node >= 0 ) {
			pointOrigin.Append( enemyLastSeenOrigin );
			pointValue.Append( 2.0f + 3.0f * traits.pursuit * ( 0.5f + traits.aggression ) );
			pointNode.Append( node );
		}
	}

	// An elimination round that has gone quiet is won by hunting: Clan Arena
	// has no pickups to patrol, and without this the last few players wandered
	// at random until the round clock ran out.  A player who has been in the
	// round this long has a rough idea where the survivors went, so each one
	// adds a patrol point blurred well beyond a position fix.
	rvGameState *state = gameLocal.mpGame.GetGameState();
	rvRoundGameState *roundState = ( state && state->IsType( rvRoundGameState::GetClassType() ) ) ?
		static_cast<rvRoundGameState *>( state ) : NULL;
	const bool hunting = roundState && MPGameTypeHasAny( gameLocal.gameType, GTF_ELIMINATION ) &&
		roundState->RoundIsLive() &&
		gameLocal.time - roundState->GetRoundStartTime() >= BOT_HUNT_AFTER_MSEC;
	if ( hunting ) {
		for ( int c = 0; c < gameLocal.numClients; c++ ) {
			idEntity *ent = gameLocal.entities[c];
			if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
				continue;
			}
			idPlayer *other = static_cast<idPlayer *>( ent );
			if ( !IsEnemy( self, other ) ) {
				continue;
			}

			idVec3 guess = other->GetPhysics()->GetOrigin();
			guess.x += gameLocal.random.CRandomFloat() * BOT_HUNT_BLUR;
			guess.y += gameLocal.random.CRandomFloat() * BOT_HUNT_BLUR;
			const int node = navMesh.SnapNode( guess );
			if ( node < 0 ) {
				continue;
			}
			pointOrigin.Append( navMesh.GetNode( node ).origin );
			pointValue.Append( 3.0f );
			pointNode.Append( node );
		}
	}

	idVec3 candidateOrigin[MAX_ROAM_CANDIDATES];
	float candidateScore[MAX_ROAM_CANDIDATES];
	int numCandidates = 0;

	// Each point's worth - static value, how long since this bot went there,
	// and its share of randomness - is settled before the search, so the field
	// can rank the points and stop once the best few are certain.  A point the
	// bot is standing on is worth nothing.
	for ( int i = 0; i < pointNode.Num(); i++ ) {
		pointValue[i] *= RoamNovelty( pointOrigin[i] ) * ( 0.6f + 0.8f * gameLocal.random.RandomFloat() );
		if ( ( pointOrigin[i] - origin ).LengthSqr() < Square( BOT_ROAM_REVISIT_RADIUS ) ) {
			pointValue[i] = 0.0f;
		}
		navFieldTarget_t target;
		target.node = ( pointValue[i] > 0.0f ) ? pointNode[i] : -1;
		target.weight = pointValue[i];
		target.minWeight = pointValue[i];
		target.extra = 600.0f;
		targets.Append( target );
	}

	const int startNode = pointNode.Num() ?
		navMesh.BuildCostField( origin, BOT_ROAM_FIELD_COST, targets.Ptr(), targets.Num(), MAX_ROAM_CANDIDATES ) : -1;
	if ( startNode != -1 ) {
		const float startLeg = ( navMesh.GetNode( startNode ).origin - origin ).Length();

		for ( int i = 0; i < pointNode.Num(); i++ ) {
			if ( pointValue[i] <= 0.0f ) {
				continue;
			}
			const float fieldCost = navMesh.CostFieldDistance( pointNode[i] );
			if ( fieldCost >= idMath::INFINITY ) {
				continue;
			}
			const float cost = startLeg + fieldCost;
			if ( cost < BOT_ROAM_REVISIT_RADIUS ) {
				continue;		// already here
			}

			const float score = pointValue[i] / ( cost + 600.0f );

			int insertAt = 0;
			while ( insertAt < numCandidates && candidateScore[insertAt] >= score ) {
				insertAt++;
			}
			if ( insertAt >= MAX_ROAM_CANDIDATES ) {
				continue;
			}
			if ( numCandidates < MAX_ROAM_CANDIDATES ) {
				numCandidates++;
			}
			for ( int j = numCandidates - 1; j > insertAt; j-- ) {
				candidateOrigin[j] = candidateOrigin[j - 1];
				candidateScore[j] = candidateScore[j - 1];
			}
			candidateOrigin[insertAt] = pointOrigin[i];
			candidateScore[insertAt] = score;
		}
	}

	for ( int i = 0; i < numCandidates; i++ ) {
		if ( navMesh.FindPath( origin, candidateOrigin[i], roamPath, false, BotMaxSafeFallDelta( self ) ) ) {
			roam = candidateOrigin[i];
			RememberRoamPoint( roam );
			return true;
		}
	}

	if ( navMesh.RandomReachablePoint( origin, roam, &roamPath, BotMaxSafeFallDelta( self ) ) ) {
		RememberRoamPoint( roam );
		return true;
	}

	return false;
}

/*
================
rvBot::UpdateGoal
================
*/
void rvBot::UpdateGoal( idPlayer *self ) {
	if ( dodgeHopActive ) {
		return;		// HoldDodgeJump releases on landing, failed launch or bounded expiry
	}
	const idVec3 origin = self->GetPhysics()->GetOrigin();
	idPlayer *currentFoe = enemy.GetEntity();
	const bool holdingObjective = AtObjectiveHoldPosition( self );

	// A reserved volume entry and an action already in flight are one movement.
	const bool actionActive = ( traversalCorner == pathCorner );
	const bool volumeEntryActive = traversalEntered && ( traversalCorner == pathCorner + 1 );
	if ( ( actionActive || volumeEntryActive ) && traversalCorner >= 0 &&
		 traversalCorner < path.Num() && path[traversalCorner].travelType != NAVTRAVEL_WALK ) {
		return;
	}

	if ( !path.IsEmpty() && pathCorner < path.Num() ) {
		const float remaining = PathDistanceRemaining( origin );
		if ( remaining + BOT_GOAL_PROGRESS_EPSILON < goalBestDistance ) {
			goalBestDistance = remaining;
			goalProgressTime = gameLocal.time;
			goalGiveUpTime = Max( goalGiveUpTime, gameLocal.time + BOT_RECOVER_IDLE_MSEC * 3 );
		}
	}

	// An escort beside the player it escorts is doing its job however little
	// its route shortens - a carrier waiting at home for its own flag, a
	// team-mate who said "follow me" and stopped.  Without this the give-up
	// below fired and put the escorted player on the avoid list.
	if ( goalType == BOTGOAL_OBJECTIVE && objectiveKind == BOTOBJ_ESCORT ) {
		idEntity *escorted = goalEntity.GetEntity();
		if ( escorted && escorted->GetPhysics() &&
			 ( escorted->GetPhysics()->GetOrigin() - origin ).LengthSqr() < Square( BOT_ESCORT_KEEP_RADIUS ) ) {
			goalProgressTime = gameLocal.time;
			goalGiveUpTime = Max( goalGiveUpTime, gameLocal.time + BOT_RECOVER_IDLE_MSEC * 3 );
		}
	}

	// Time spent fighting is not time spent failing to get there: holding
	// range, strafing and dodging shorten no route, and a fetcher in a long
	// firefight used to give up on the flag and refuse it for twenty seconds.
	if ( combatSteeringTime > goalProgressTime ) {
		goalProgressTime = combatSteeringTime;
	}

	if ( !holdingObjective && goalGiveUpTime && gameLocal.time > goalGiveUpTime &&
		 gameLocal.time - goalProgressTime > BOT_RECOVER_IDLE_MSEC ) {
		if ( bot_debug.GetInteger() >= 1 ) {
			gameLocal.Printf( "bot %s: giving up on goal %d at %s after no route progress\n",
				name.c_str(), goalType, goalOrigin.ToString( 0 ) );
		}
		// An escort that lost its way tries again shortly with a fresh place
		// in the formation; refusing the player it escorts for twenty
		// seconds broke "follow me" whenever one lane was awkward.
		const bool escort = goalType == BOTGOAL_OBJECTIVE && objectiveKind == BOTOBJ_ESCORT;
		AbandonGoal( escort ? BOT_ESCORT_RETRY_MSEC : BOT_GOAL_AVOID_MSEC );
	}

	// Preserve the useful item-denial reaction while validating the old goal.
	bool currentItemValid = false;
	if ( goalType == BOTGOAL_ITEM ) {
		idEntity *goal = goalEntity.GetEntity();
		idItem *item = ( goal && goal->IsType( idItem::GetClassType() ) ) ? static_cast<idItem *>( goal ) : NULL;
		const bool available = item && !item->pickedUp && !item->IsHidden();

		// A timed item stays a goal while it is due back within the wait, with or
		// without a route left to walk: standing on the spot is the plan.
		const int pendingMsec = ( item && !available && goalItemPending ) ? ItemPendingMsec( item ) : -1;
		const bool pendingValid = pendingMsec > 0 && pendingMsec <= BOT_ITEM_MAX_WAIT_MSEC;
		currentItemValid = item && item->GetInstance() == self->GetInstance() &&
			( ( available && ( !path.IsEmpty() || goalItemPending ) ) || pendingValid );

		// Denied means someone else took it after this bot set out for it.  A
		// timed item was already gone when it was chosen, which is not a denial.
		const int pickupTime = item ? botManager.ItemPickupTime( item ) : -1;
		if ( item && item->pickedUp && item->pickedUpByClientNum != clientNum &&
			 pickupTime >= goalSelectedTime &&
			 ( item->GetPhysics()->GetOrigin() - origin ).LengthSqr() < BOT_ITEM_DENIED_RANGE * BOT_ITEM_DENIED_RANGE ) {
			idPlayer *thief = NULL;
			if ( item->pickedUpByClientNum >= 0 && item->pickedUpByClientNum < gameLocal.numClients ) {
				idEntity *ent = gameLocal.entities[item->pickedUpByClientNum];
				if ( ent && ent->IsType( idPlayer::GetClassType() ) ) {
					thief = static_cast<idPlayer *>( ent );
				}
			}
			const idStr itemName = BotReadableName( item->spawnArgs );
			QueueChat( BOTCHAT_ITEM_DENIED, BotDisplayName( thief ), NULL, itemName.c_str() );
			Avoid( item, BOT_GOAL_AVOID_MSEC );
			goalEntity = NULL;
		}
	}

	const bool routeActive = !path.IsEmpty() && pathCorner < path.Num();
	if ( gameLocal.time < nextGoalSelectTime ) {
		// Moving targets still receive their inexpensive, throttled route refresh;
		// aim and fire continue independently whichever goal owns movement.
		if ( goalType == BOTGOAL_ENEMY && currentFoe &&
			 gameLocal.time - enemyPathTime > BOT_REPATH_COMBAT_MSEC ) {
			enemyPathTime = gameLocal.time;
			goalOrigin = EnemyPursuitOrigin();
			Repath( self, goalOrigin, true );
		} else if ( routeActive ) {
			return;
		}
		return;
	}
	if ( !botManager.ClaimDecision() ) {
		return;		// this frame's decisions are spent; decide on the next one
	}
	nextGoalSelectTime = gameLocal.time + BOT_GOAL_SELECT_MSEC;

	// Inventory may have changed on the way (another health pack, ammo, or a
	// weapon grant). A now-useless pickup must not retain its old commitment.
	if ( currentItemValid ) {
		currentItemValid = ItemUtility( self, static_cast<idItem *>( goalEntity.GetEntity() ) ) > 0.0f;
	}

	botObjective_t objective;
	const bool hasObjective = BotFindObjective( self, objective );
	const bool objectiveAvoided = hasObjective && objective.entity.GetEntity() &&
		IsAvoided( objective.entity.GetEntity() );
	const bool objectiveAvailable = hasObjective && !objectiveAvoided;
	// A foe last seen inside a lethal volume - falling into q4dm5's void - is
	// already dead and stands on no floor any route reaches: chasing it was a
	// failed search every refresh until it respawned.  Shooting it is still fine.
	const bool enemyMovementAvailable = currentFoe && !IsAvoided( currentFoe ) &&
		!navMesh.IsHazardous( enemyLastSeenOrigin, true );
	float objectivePriority = objectiveAvailable ? objective.priority : 0.0f;
	const idVec3 objectiveRouteOrigin = objectiveAvailable ?
		BotObjectiveRouteOrigin( self, objective ) : objective.origin;

	const bool currentObjectiveValid = goalType == BOTGOAL_OBJECTIVE && objectiveAvailable &&
		objectiveKind == objective.kind && goalEntity.GetEntity() == objective.entity.GetEntity();
	const bool currentObjectiveHeld = currentObjectiveValid && holdingObjective;
	const bool currentEnemyValid = goalType == BOTGOAL_ENEMY && enemyMovementAvailable &&
		goalEntity.GetEntity() == currentFoe && routeActive;
	const bool currentRoamValid = goalType == BOTGOAL_ROAM && routeActive;
	const bool currentValid = currentItemValid || currentObjectiveValid || currentEnemyValid || currentRoamValid;

	rvNavPath itemPath;
	float itemUtility = 0.0f;
	// Fight or flight.  A bot that cannot win the trade it is in - outgunned,
	// or facing a quad with a machinegun - withdraws toward health and armour
	// the same way a hurt one does, and keeps shooting as it goes.  How bad the
	// trade must be before it breaks off is the personality's aggression.
	const float advantage = currentFoe ? CombatAdvantage( self, currentFoe ) : 1.0f;
	outmatched = currentFoe && advantage < 0.45f * ( 1.4f - traits.aggression );

	const bool wantHealth = Withdrawing( self );
	idEntity *item = NULL;
	if ( !objectiveAvailable || wantHealth || objectivePriority < 85.0f ) {
		item = PickItemGoal( self, itemPath, itemUtility );
	}
	// itemFocus is how much a pickup outweighs an enemy for this personality:
	// a roamer plays the map, a hunter barely looks down.  Only the item's own
	// worth scales; the need-driven bonuses below - arming up, health at the
	// edge of death, a pickup a few steps away - are not matters of taste.
	float itemPriority = item ? ( 10.0f + itemUtility * 5.0f ) * ( 0.5f + idMath::ClampFloat( 0.0f, 1.0f, traits.itemFocus ) ) : 0.0f;
	const bool itemIsHealth = item && item->spawnArgs.GetInt( "inv_health", "0" ) > 0;
	const bool itemIsArmor = item && item->spawnArgs.GetInt( "inv_armor", "0" ) > 0;
	const int maxHealth = Max( 1, self->inventory.maxHealth );
	const bool criticalHealth = self->health <= Max( 15, (int)( traits.retreatHealth * maxHealth * 0.65f ) );
	if ( ( itemIsHealth || ( itemIsArmor && outmatched ) ) && wantHealth ) {
		itemPriority += criticalHealth ? 42.0f : 28.0f;
	}

	// Arming up comes first.  A bot holding nothing better than the spawn
	// machinegun is no use to any role, so a real weapon outranks everything
	// short of an urgent rule change; without this a CTF bot spent whole
	// matches with the weapons it spawned with.
	const bool itemIsWeapon = item && item->spawnArgs.GetString( "inv_weapon", "" )[0];
	if ( itemIsWeapon && BestWeaponPower( self ) <= 0.55f ) {
		itemPriority += 40.0f;
	}

	// A pickup a few steps away is taken on the way, as a player would, even
	// in the middle of a chase.
	const float itemRoute = item ? itemPath.LengthFrom( origin ) : idMath::INFINITY;
	if ( item && itemUtility >= 1.0f && itemRoute < BOT_ITEM_NEARBY_ROUTE ) {
		itemPriority += 20.0f;
	}

	// Items beside a held post are part of holding it: a defender keeps its
	// base stocked instead of standing on the flag stand next to the armour.
	bool itemOnDuty = false;
	if ( item && objectiveAvailable && objective.kind == BOTOBJ_DEFEND && itemUtility >= 1.0f &&
		 ( item->GetPhysics()->GetOrigin() - objective.origin ).LengthSqr() <
			Square( BOT_DEFEND_RADIUS * 2.0f ) ) {
		itemOnDuty = true;
		itemPriority = Max( itemPriority, objectivePriority + 1.0f );
	}

	const bool foeVisible = currentFoe && gameLocal.time == enemyLastSeenTime;
	float enemyPriority = enemyMovementAvailable ?
		32.0f + traits.pursuit * 18.0f +
		traits.aggression * 8.0f + ( foeVisible ? 8.0f : 0.0f ) : 0.0f;
	if ( wantHealth ) {
		enemyPriority *= criticalHealth ? 0.35f : 0.65f;
	}
	// A winning trade is worth pressing and a losing one is not; a contact known
	// only by sound is worth investigating, but less than one in view.  A bot
	// carrying quad damage goes hunting while it lasts.
	enemyPriority *= idMath::ClampFloat( 0.6f, 1.35f, 0.75f + 0.25f * advantage );
	if ( currentFoe && !foeVisible && enemyLastHeardTime > enemyLastSeenTime ) {
		enemyPriority *= 0.75f;
	}
	if ( self->PowerUpActive( POWERUP_QUADDAMAGE ) ) {
		enemyPriority *= 1.3f;
	}
	selfStrong = self->PowerUpActive( POWERUP_QUADDAMAGE ) || self->PowerUpActive( POWERUP_DOUBLER ) ||
		( BestWeaponPower( self ) >= 0.95f && self->health >= (int)( maxHealth * 0.75f ) );
	if ( enemyMovementAvailable && currentFoe == lastAttacker.GetEntity() &&
		 gameLocal.time - lastAttackerTime < BOT_THREAT_MEMORY_MSEC ) {
		enemyPriority += 8.0f;
	}

	// Hysteresis compares the challenger with what the current goal is worth
	// now, not what it was worth when selected.  In particular, a new team claim
	// can deliberately lower an escort/rescue utility so another bot peels off.
	if ( currentObjectiveValid ) {
		goalUtility = objectivePriority;
	} else if ( currentEnemyValid ) {
		goalUtility = enemyPriority;
	}

	int desiredType = BOTGOAL_ROAM;
	float desiredPriority = 0.0f;
	if ( objectivePriority > desiredPriority ) {
		desiredType = BOTGOAL_OBJECTIVE;
		desiredPriority = objectivePriority;
	}
	if ( itemPriority > desiredPriority &&
		 ( objectivePriority < 100.0f || ( criticalHealth && self->health <= 15 ) ) ) {
		desiredType = BOTGOAL_ITEM;
		desiredPriority = itemPriority;
	}
	if ( enemyPriority > desiredPriority && objectivePriority <= 0.0f ) {
		desiredType = BOTGOAL_ENEMY;
		desiredPriority = enemyPriority;
	}

	const bool sameDesired =
		( desiredType == BOTGOAL_ITEM && goalType == desiredType && goalEntity.GetEntity() == item ) ||
		( desiredType == BOTGOAL_ENEMY && goalType == desiredType && goalEntity.GetEntity() == currentFoe ) ||
		( desiredType == BOTGOAL_OBJECTIVE && currentObjectiveValid );
	const bool urgentRuleChange = desiredType == BOTGOAL_OBJECTIVE &&
		( !currentObjectiveValid || desiredPriority >= 85.0f );

	const bool dutyPickup = desiredType == BOTGOAL_ITEM && itemOnDuty;
	if ( currentValid && !sameDesired && !urgentRuleChange && !dutyPickup &&
		 ( gameLocal.time < goalCommitUntil || desiredPriority < goalUtility * BOT_GOAL_SWITCH_MARGIN ) ) {
		return;
	}

	if ( sameDesired && currentObjectiveHeld ) {
		goalOrigin = objective.origin;
		goalUtility = desiredPriority;
		objectiveHoldPosition = objective.holdPosition;
		goalProgressTime = gameLocal.time;
		goalGiveUpTime = Max( goalGiveUpTime, gameLocal.time + BOT_GOAL_GIVEUP_MSEC );
		path.Clear();
		pathCorner = 0;
		pathTime = 0;
		repathFailures = 0;
		noRouteSince = 0;
		ResetTraversal();
		return;
	}

	if ( sameDesired && routeActive ) {
		goalUtility = desiredPriority;
		if ( desiredType == BOTGOAL_ENEMY && gameLocal.time - enemyPathTime > BOT_REPATH_COMBAT_MSEC ) {
			enemyPathTime = gameLocal.time;
			goalOrigin = EnemyPursuitOrigin();
			Repath( self, goalOrigin, true );
		} else if ( desiredType == BOTGOAL_OBJECTIVE ) {
			goalOrigin = objectiveRouteOrigin;
			idEntity *objectiveEntity = objective.entity.GetEntity();
			const bool movingObjective = objectiveEntity && objectiveEntity->IsType( idPlayer::GetClassType() );
			const int refresh = movingObjective ? BOT_REPATH_COMBAT_MSEC : BOT_REPATH_IDLE_MSEC;
			if ( gameLocal.time - pathTime > refresh ) {
				if ( !Repath( self, goalOrigin, true ) &&
					 ( goalOrigin - objective.origin ).LengthSqr() > 1.0f ) {
					goalOrigin = objective.origin;
					Repath( self, goalOrigin, true );
				}
			}
		} else if ( desiredType == BOTGOAL_ITEM && gameLocal.time - pathTime > BOT_REPATH_IDLE_MSEC ) {
			goalOrigin = item->GetPhysics()->GetOrigin();
			Repath( self, goalOrigin, true );
		}
		return;
	}

	// What the bot was already going after, captured before the selection below
	// overwrites it.  The give-up deadline may only be re-armed when the answer
	// actually changes.  The stuck detector and the traversal timeout both
	// clear the route without touching the goal, so a bot on a goal it cannot
	// physically reach comes back through here every quarter second, rebuilds
	// the same route, and would push its own deadline out forever - never
	// reaching AbandonGoal, never putting the goal on the avoid list.
	const int			previousGoalType	= goalType;
	const idEntity *	previousGoalEntity	= goalEntity.GetEntity();
	const int			previousObjective	= objectiveKind;

	// Evaluate replacements without touching the current route or its clocks.
	// A failed objective/chase must not erase a useful item route, and repairing
	// the same goal is not evidence of movement toward it.
	rvNavPath selectedPath;
	idVec3 selectedOrigin = origin;
	idEntity *selectedEntity = NULL;
	bool routeFound = false;
	if ( desiredType == BOTGOAL_OBJECTIVE && objectiveAvailable ) {
		selectedOrigin = objectiveRouteOrigin;
		selectedEntity = objective.entity.GetEntity();
		routeFound = navMesh.FindPath( origin, selectedOrigin, selectedPath, false, BotMaxSafeFallDelta( self ) );
		if ( !routeFound && ( selectedOrigin - objective.origin ).LengthSqr() > 1.0f ) {
			selectedOrigin = objective.origin;
			routeFound = navMesh.FindPath( origin, selectedOrigin, selectedPath, false, BotMaxSafeFallDelta( self ) );
		}
	} else if ( desiredType == BOTGOAL_ITEM && item ) {
		selectedPath = itemPath;
		selectedEntity = item;
		selectedOrigin = item->GetPhysics()->GetOrigin();
		routeFound = !selectedPath.IsEmpty();
	} else if ( desiredType == BOTGOAL_ENEMY && currentFoe ) {
		enemyPathTime = gameLocal.time;
		selectedOrigin = EnemyPursuitOrigin();
		selectedEntity = currentFoe;
		routeFound = navMesh.FindPath( origin, selectedOrigin, selectedPath, true, BotMaxSafeFallDelta( self ) );
	}

	// Keep a valid commitment when the preferred replacement has no route.
	// Trying lower-priority fallbacks first could silently replace it with a
	// worse goal that never passed the switch margin above.
	if ( !routeFound && currentValid && !path.IsEmpty() && pathCorner < path.Num() ) {
		return;
	}

	if ( !routeFound && desiredType != BOTGOAL_ITEM && item && !itemPath.IsEmpty() ) {
		desiredType = BOTGOAL_ITEM;
		desiredPriority = itemPriority;
		selectedPath = itemPath;
		selectedEntity = item;
		selectedOrigin = item->GetPhysics()->GetOrigin();
		routeFound = true;
	}
	if ( !routeFound && desiredType != BOTGOAL_ENEMY && currentFoe && enemyPriority > 0.0f ) {
		desiredType = BOTGOAL_ENEMY;
		desiredPriority = enemyPriority;
		enemyPathTime = gameLocal.time;
		selectedOrigin = EnemyPursuitOrigin();
		selectedEntity = currentFoe;
		routeFound = navMesh.FindPath( origin, selectedOrigin, selectedPath, true, BotMaxSafeFallDelta( self ) );
	}

	if ( routeFound ) {
		path = selectedPath;
		pathCorner = 0;
		pathTime = gameLocal.time;
		repathFailures = 0;
		noRouteSince = 0;
		ResetTraversal();
		goalEntity = selectedEntity;
		goalOrigin = selectedOrigin;
		goalType = desiredType;
		objectiveKind = desiredType == BOTGOAL_OBJECTIVE ? objective.kind : BOTOBJ_NONE;
		objectiveHoldPosition = desiredType == BOTGOAL_OBJECTIVE && objective.holdPosition;
		goalUtility = desiredPriority;
		goalCommitUntil = gameLocal.time + BOT_GOAL_COMMIT_MSEC;
		goalBestDistance = PathDistanceRemaining( origin );

		// Only a genuinely different goal gets a fresh deadline.  Identity is
		// the entity and the kind, never goalOrigin, which legitimately moves
		// every refresh while chasing a player.  An unchanged goal keeps the
		// clock it was given, and route progress above is what extends it.
		if ( goalType != previousGoalType ||
			 goalEntity.GetEntity() != previousGoalEntity ||
			 objectiveKind != previousObjective ) {
			goalProgressTime = gameLocal.time;
			goalSelectedTime = gameLocal.time;
			const int expectedTravel = (int)( goalBestDistance * ( 1000.0f / 180.0f ) );
			goalGiveUpTime = gameLocal.time + Max( BOT_GOAL_GIVEUP_MSEC, expectedTravel * 2 + 4000 );

			// Heading for an item that is not back yet is a timed run, and the
			// give-up clock has to allow for the wait at the end of it.
			idItem *goalItem = ( goalType == BOTGOAL_ITEM && selectedEntity &&
				selectedEntity->IsType( idItem::GetClassType() ) ) ? static_cast<idItem *>( selectedEntity ) : NULL;
			goalItemPending = goalItem && ( goalItem->pickedUp || goalItem->IsHidden() );
			if ( goalItemPending ) {
				goalGiveUpTime += BOT_ITEM_MAX_WAIT_MSEC;
			}
		}
		holdUntil = 0;
		stuckPathCorner = 0;
		stuckCornerDistance = idMath::INFINITY;
		return;
	}

	idVec3 roam;
	rvNavPath roamPath;
	if ( PickRoamGoal( self, roam, roamPath ) ) {
		goalType = BOTGOAL_ROAM;
		goalEntity = NULL;
		goalOrigin = roam;
		goalSelectedTime = gameLocal.time;
		goalItemPending = false;
		goalUtility = 1.0f;
		goalCommitUntil = gameLocal.time + BOT_GOAL_COMMIT_MSEC;
		goalGiveUpTime = gameLocal.time + BOT_GOAL_GIVEUP_MSEC;
		goalProgressTime = gameLocal.time;
		objectiveKind = BOTOBJ_NONE;
		objectiveHoldPosition = false;
		path = roamPath;
		pathCorner = 0;
		goalBestDistance = PathDistanceRemaining( origin );
		pathTime = gameLocal.time;
		repathFailures = 0;
		noRouteSince = 0;
		holdUntil = 0;
		stuckPathCorner = 0;
		stuckCornerDistance = idMath::INFINITY;
		ResetTraversal();
	} else {
		// The previous route is invalid and all alternatives failed. Do not
		// keep walking to a vanished pickup while recovery waits for its clock.
		path.Clear();
		pathCorner = 0;
		pathTime = gameLocal.time;
		ResetTraversal();
		repathFailures++;
		if ( !noRouteSince ) {
			noRouteSince = gameLocal.time;
		}
	}
}

/*
================
BotReachedTravelEnd

Special links join neighbouring samples, but a real jump keeps its momentum and
can land beyond that one-cell endpoint.  Accept the endpoint itself or a
bounded overshoot in the same corridor; never accept the takeoff side.
================
*/
static bool BotReachedTravelEnd( const idVec3 &origin, const idVec3 &start,
								 const idVec3 &end, float zTolerance ) {
	if ( idMath::Fabs( origin.z - end.z ) > zTolerance ) {
		return false;
	}

	idVec3 direction = end - start;
	direction.z = 0.0f;
	const float travelDistance = direction.Normalize();
	if ( travelDistance <= 0.0f ) {
		idVec3 remaining = end - origin;
		remaining.z = 0.0f;
		return remaining.LengthSqr() <= BOT_CORNER_REACH * BOT_CORNER_REACH;
	}

	// The ordinary corner radius is wider than one nav cell.  When both ends
	// share a floor, require a clear departure from the takeoff side before the
	// endpoint radius may win.  A real height change already proves that
	// departure and must accept a narrow-crate edge landing with little XY
	// progress.
	if ( idMath::Fabs( end.z - start.z ) <= zTolerance ) {
		idVec3 fromStart = origin - start;
		fromStart.z = 0.0f;
		const float minimumProgress = Min( travelDistance * 0.5f,
										   BOT_TRAVEL_ENTRY_REACH );
		if ( fromStart * direction < minimumProgress ) {
			return false;
		}
	}

	idVec3 remaining = end - origin;
	remaining.z = 0.0f;
	if ( remaining.LengthSqr() <= BOT_CORNER_REACH * BOT_CORNER_REACH ) {
		return true;
	}

	idVec3 past = origin - end;
	past.z = 0.0f;

	const float along = past * direction;
	if ( along < 0.0f || along > BOT_TRAVEL_OVERSHOOT ) {
		return false;
	}

	const idVec3 lateral = past - direction * along;
	return lateral.LengthSqr() <= BOT_CORNER_REACH * BOT_CORNER_REACH;
}

/*
================
BotTravelName
================
*/
static const char *BotTravelName( int travelType ) {
	switch ( travelType ) {
		case NAVTRAVEL_JUMP:		return "jump";
		case NAVTRAVEL_DROP:		return "drop";
		case NAVTRAVEL_JUMPPAD:	return "jump pad";
		case NAVTRAVEL_TELEPORT:	return "teleport";
		default:					return "travel";
	}
}

/*
================
BotTouchesTravelVolume

Pad and teleporter links retain their owning entity.  Expanding its live bounds
slightly lets the bot reserve the action before its hull actually activates the
volume, without guessing that every trigger has the same dimensions.
================
*/
static bool BotTouchesTravelVolume( idPlayer *self, const navCorner_t &corner ) {
	if ( corner.actionEntityNum < 0 || corner.actionEntityNum >= MAX_GENTITIES ) {
		return false;
	}

	idEntity *volume = gameLocal.entities[corner.actionEntityNum];
	if ( !volume ) {
		return false;
	}

	idBounds activationBounds;
	if ( volume->IsType( rvJumpPad::GetClassType() ) ) {
		if ( !static_cast<rvJumpPad *>( volume )->GetForceFieldBounds( activationBounds ) ) {
			return false;
		}
	} else {
		if ( !volume->GetPhysics() ) {
			return false;
		}
		activationBounds = volume->GetPhysics()->GetAbsBounds();
	}

	activationBounds.ExpandSelf( BOT_TRAVEL_ENTRY_REACH );

	return activationBounds.IntersectsBounds( self->GetPhysics()->GetAbsBounds() );
}

/*
================
rvBot::AdvancePath

Returns false once the route is exhausted.  Walk corners are proximity targets.
Every other travel type is an action with a source and destination: proximity
alone must never consume it, especially when one 24-unit nav cell is closer
than the ordinary 28-unit corner radius.
================
*/
static bool BotPadFlightComplete( bool started, bool grounded, bool reachedExit, int flightMsec, bool chained ) {
	// Being near a sampled endpoint while descending is not a landing. Keep
	// the flight controller until actual ground contact or the next pad takes it.
	return started && ( chained || ( grounded &&
		( reachedExit || flightMsec >= BOT_PAD_MIN_FLIGHT_MSEC ) ) );
}

bool rvBot::AdvancePath( idPlayer *self ) {
	const idVec3 origin = self->GetPhysics()->GetOrigin();

	while ( pathCorner < path.Num() ) {
		const navCorner_t &corner = path[pathCorner];
		idVec3 delta = corner.origin - origin;
		const float height = delta.z;
		delta.z = 0.0f;

		if ( corner.travelType == NAVTRAVEL_WALK ) {
			// The final walk corner before an action is its takeoff/entry point.
			// Reach it deliberately instead of consuming it and the adjacent
			// action destination in one pass.
			const bool actionEntry = ( pathCorner + 1 < path.Num() &&
									   path[pathCorner + 1].travelType != NAVTRAVEL_WALK );
			const int nextTravel = actionEntry ? path[pathCorner + 1].travelType : NAVTRAVEL_WALK;
			const bool volumeEntry = ( nextTravel == NAVTRAVEL_JUMPPAD ||
									   nextTravel == NAVTRAVEL_TELEPORT );

			if ( volumeEntry ) {
				const int actionCorner = pathCorner + 1;
				const navCorner_t &destination = path[actionCorner];

				// Do not consume a volume's source merely because the bot's
				// origin reached its centre.  Keep steering through the live
				// bounds until the pad launches it or the teleporter puts it on
				// the destination side.  This also catches an immediate
				// teleport that occurred between bot command frames.
				if ( traversalCorner != actionCorner ) {
					traversalCorner		= actionCorner;
					traversalStartOrigin	= corner.origin;
					traversalStartTime	= gameLocal.time;
					traversalEntered		= false;
					traversalStarted	= false;
				}

				const bool enteredVolume = BotTouchesTravelVolume( self, destination ) ||
					( destination.actionEntityNum < 0 &&
					  delta.LengthSqr() <= BOT_CORNER_REACH * BOT_CORNER_REACH &&
					  idMath::Fabs( height ) <= BOT_CORNER_REACH_Z );
				if ( enteredVolume && !traversalEntered ) {
					traversalEntered = true;
					traversalStartTime = gameLocal.time;
					unstickUntil	= 0;
					stuckChecks		= 0;
					stuckTime		= gameLocal.time;
				}

				const bool reachedExit = BotReachedTravelEnd( origin, corner.origin,
															  destination.origin,
															  BOT_CORNER_REACH_Z );
				// Airborne beside a pad is not launched by it: a jump climbs at
				// 277 and runs at 320, so the pad's evidence is speed beyond
				// either.  A hop near the volume used to take the pad as done.
				const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
				const bool padLaunched = !self->pfl.onGround &&
					( velocity.z > BOT_PAD_LAUNCH_SPEED ||
					  idVec3( velocity.x, velocity.y, 0.0f ).LengthSqr() > Square( BOT_PAD_LAUNCH_SPEED * 1.25f ) );
				const bool activated = reachedExit ||
					( nextTravel == NAVTRAVEL_JUMPPAD && traversalEntered && padLaunched ) ||
					( nextTravel == NAVTRAVEL_TELEPORT && self->IsInTeleport() );

				if ( activated ) {
					if ( bot_debug.GetInteger() >= 1 && nextTravel == NAVTRAVEL_JUMPPAD ) {
						gameLocal.Printf( "bot %s: launched at %s toward %s, velocity %s (time %d)\n",
										  name.c_str(), origin.ToString( 0 ), destination.origin.ToString( 0 ),
										  velocity.ToString( 0 ), gameLocal.time );
					}
					pathCorner			= actionCorner;
					traversalStartTime	= gameLocal.time;
					traversalStarted	= true;
					unstickUntil		= 0;
					stuckChecks			= 0;
					stuckTime			= gameLocal.time;
					continue;
				}

				return true;
			}

			const float reach = actionEntry ? BOT_TRAVEL_ENTRY_REACH : BOT_CORNER_REACH;

			if ( delta.LengthSqr() > reach * reach ||
				 idMath::Fabs( height ) > BOT_CORNER_REACH_Z ) {
				return true;
			}

			pathCorner++;
			continue;
		}

		if ( traversalCorner != pathCorner ) {
			traversalCorner		= pathCorner;
			traversalStartOrigin	= origin;
			traversalStartTime	= gameLocal.time;
			traversalEntered		= false;
			traversalStarted	= false;
			if ( bot_debug.GetInteger() >= 2 ) {
				gameLocal.Printf( "bot %s: starting %s at %s from %s to %s velocity %s\n",
					name.c_str(), BotTravelName( corner.travelType ), origin.ToString( 0 ),
					pathCorner > 0 ? path[pathCorner - 1].origin.ToString( 0 ) : "-",
					corner.origin.ToString( 0 ), self->GetPhysics()->GetLinearVelocity().ToString( 0 ) );
			}

			// A previous generic recovery must not side-push a deliberate
			// takeoff, and its progress window starts here.
			unstickUntil	= 0;
			stuckChecks		= 0;
			stuckTime		= gameLocal.time;
		}

		if ( corner.travelType != NAVTRAVEL_TELEPORT && !self->pfl.onGround ) {
			traversalStarted = true;
		}

		// A teleporter may be vertical, may move less than one horizontal cell,
		// and may hold the player in a private camera before the exit.  Observe
		// its real state or its destination rather than guessing from XY
		// displacement.
		if ( corner.travelType == NAVTRAVEL_TELEPORT ) {
			if ( self->IsInTeleport() ||
				 BotReachedTravelEnd( origin, traversalStartOrigin,
									  corner.origin, BOT_CORNER_REACH_Z ) ) {
				traversalStarted = true;
			}
		}

		bool completed = false;
		switch ( corner.travelType ) {
			case NAVTRAVEL_JUMP:
			case NAVTRAVEL_DROP: {
				// Same-height barriers make a Z-only test insufficient.  The
				// bot must first leave the ground, then land at or beyond the
				// destination on its sampled floor.
				completed = traversalStarted && self->pfl.onGround &&
							BotReachedTravelEnd( origin, traversalStartOrigin,
												 corner.origin, BOT_TRAVEL_REACH_Z );

				// A drop or a jump taken at running speed lands well past its
				// one-cell landing - a 384 unit fall carries a bot nearly 300
				// units out, a level gap jump over a hundred - and walking back
				// to the node only to leave it again wastes the move.  Down on
				// the landing's own floor, ahead of it and no further off the
				// line than two corners, the action is done.
				idVec3 direction = corner.origin - traversalStartOrigin;
				direction.z = 0.0f;
				const float linkLength = direction.Normalize();
				if ( !completed && traversalStarted && self->pfl.onGround && linkLength > 0.0f &&
					 idMath::Fabs( origin.z - corner.origin.z ) <= BOT_TRAVEL_REACH_Z ) {
					idVec3 past = origin - corner.origin;
					past.z = 0.0f;
					const float ahead = past * direction;
					completed = ahead >= 0.0f &&
						( past - direction * ahead ).LengthSqr() <= Square( BOT_CORNER_REACH * 2.0f );
				}

				// A jump that came back down on its own takeoff floor, short of
				// the middle of its link, fell short - usually from a standstill
				// at the lip.  Where the corner before the takeoff is close behind
				// it, go back there and take the jump again at speed, a couple of
				// times, before the stuck detector gives the route up.  Height
				// alone cannot tell: a level jump lands at its takeoff's height
				// whether it made it or not, and one that did must not be walked
				// back across its gap.
				idVec3 travelled = origin - traversalStartOrigin;
				travelled.z = 0.0f;
				if ( !completed && corner.travelType == NAVTRAVEL_JUMP && traversalStarted &&
					 self->pfl.onGround && gameLocal.time - traversalStartTime > BOT_JUMP_RETRY_MSEC &&
					 idMath::Fabs( origin.z - traversalStartOrigin.z ) <= BOT_CORNER_REACH_Z &&
					 travelled * direction < linkLength * 0.5f ) {
					if ( jumpRetryPathTime != pathTime ) {
						jumpRetryPathTime = pathTime;
						jumpRetries = 0;
					}
					if ( jumpRetries < BOT_JUMP_RETRIES && pathCorner >= 2 &&
						 path[pathCorner - 1].travelType == NAVTRAVEL_WALK &&
						 path[pathCorner - 2].travelType == NAVTRAVEL_WALK &&
						 ( path[pathCorner - 2].origin - path[pathCorner - 1].origin ).LengthSqr() <=
							Square( BOT_JUMP_RUNUP_REACH ) ) {
						jumpRetries++;
						pathCorner -= 2;
						ResetTraversal();

						// Going back for the run-up is the plan, not lost progress:
						// the stuck detector would read the earlier corner as none
						// and shove the bot sideways on its way back.
						stuckPathCorner		= pathCorner;
						stuckCornerDistance	= idMath::INFINITY;
						stuckTime			= gameLocal.time;
						stuckChecks			= 0;
						unstickUntil		= 0;
						return true;
					}
				}
				break;
			}

			case NAVTRAVEL_JUMPPAD:
				// The authored target is the pad's flight target, not always the
				// exact place the player finally touches down.
				completed = BotPadFlightComplete( traversalStarted,
					self->pfl.onGround && self->GetPhysics()->GetLinearVelocity().z <= BOT_PAD_LAUNCH_SPEED,
					BotReachedTravelEnd( origin, traversalStartOrigin, corner.origin, BOT_CORNER_REACH_Z ),
					gameLocal.time - traversalStartTime,
					pathCorner + 1 < path.Num() && path[pathCorner + 1].travelType == NAVTRAVEL_JUMPPAD &&
					BotTouchesTravelVolume( self, path[pathCorner + 1] ) );
				break;

			case NAVTRAVEL_TELEPORT:
				completed = traversalStarted &&
							BotReachedTravelEnd( origin, traversalStartOrigin,
												 corner.origin, BOT_CORNER_REACH_Z );
				break;

			default:
				break;
		}

		if ( !completed ) {
			return true;
		}

		if ( bot_debug.GetInteger() >= 1 ) {
			gameLocal.Printf( "bot %s: completed %s from %s to %s at %s\n",
							  name.c_str(), BotTravelName( corner.travelType ),
							  traversalStartOrigin.ToString( 0 ),
							  corner.origin.ToString( 0 ), origin.ToString( 0 ) );
		}

		ResetTraversal();
		pathCorner++;
	}

	ResetTraversal();
	return false;
}

/*
================
rvBot::OffRoute

A route is a statement about the floor the bot stands on.  A rocket, a pad
stepped on in a fight or a teammate's shove can put it on another level, and
the corner it was walking to is then straight up or straight down: steering
at it horizontally only ended after two failed stuck samples, a second and a
half of pressing into a wall, and on a stacked map that was most of what the
stuck detector ever caught.  Compare the bot's height with the height the
current leg has where the bot is instead, once it is standing on something.

Below is wrong on any leg, past the dip a string-pulled walk may float over
its ground.  Above is only wrong on a walk: a drop, and a gap jump with a
lower landing, both begin above their own leg by design.

Nearer than those limits a bot can still be somewhere its leg never went.
Combat footwork and knockback put bots a hull or two beside the lip of
q4ctf1's ramp, on the floor below it or the platform above, where the corner
was a wall's height away and the walk to it no longer existed; pressing at it
only ended at the stuck detector, a second and a half later, and not at all
while the fight kept the detector quiet.  Off a walk leg by more than a
corner's reach, or a step of height, the navmesh is asked a few times a
second whether the walk from where the bot stands to its corner still holds.

A drop or a jump that left the ground and came down more than a step below
its takeoff floor without AdvancePath completing it set down somewhere else:
momentum carried it past the landing's wall, a pad beside the landing caught
it, a gap jump fell into the gap.  Its landing may then be behind a wall, or
straight up, and steering at it only ended at the stuck detector - or bounced
the bot on that pad.
================
*/
bool rvBot::OffRoute( idPlayer *self, float &offHeight ) {
	offHeight = 0.0f;
	if ( !self->pfl.onGround || pathCorner < 0 || pathCorner >= path.Num() ) {
		return false;
	}

	const navCorner_t &corner = path[pathCorner];

	// Touching a pad or a teleporter's volume puts the bot mid-action, which
	// AdvancePath watches - but only while it is still beside the entry.  A
	// teleporter that fired between commands leaves the flag set a map away.
	// Once the action itself is current the flag still reads true, and the
	// corner is its far end: that case is the carrier's, below.
	if ( traversalEntered && traversalCorner == pathCorner + 1 ) {
		idVec3 fromEntry = self->GetPhysics()->GetOrigin() - corner.origin;
		fromEntry.z = 0.0f;
		if ( fromEntry.LengthSqr() < Square( BOT_OFF_ROUTE_LATERAL ) ) {
			return false;
		}
		offHeight = 0.0f;
		return true;
	}
	if ( corner.travelType == NAVTRAVEL_JUMPPAD || corner.travelType == NAVTRAVEL_TELEPORT ) {
		return false;		// carried, and AdvancePath watches the carrier itself
	}
	if ( ( corner.travelType == NAVTRAVEL_DROP || corner.travelType == NAVTRAVEL_JUMP ) &&
		 traversalCorner == pathCorner && traversalStarted &&
		 self->GetPhysics()->GetOrigin().z < traversalStartOrigin.z - pm_stepsize.GetFloat() ) {
		offHeight = self->GetPhysics()->GetOrigin().z - corner.origin.z;
		return true;
	}

	const idVec3 &legStart = ( pathCorner > 0 ) ? path[pathCorner - 1].origin : path.RouteStart();
	if ( pathCorner == 0 && legStart.LengthSqr() <= 0.0f ) {
		return false;		// a path FindPath did not make carries no start
	}

	const idVec3 origin = self->GetPhysics()->GetOrigin();
	idVec3 leg = corner.origin - legStart;
	leg.z = 0.0f;
	idVec3 along = origin - legStart;
	along.z = 0.0f;
	const float lengthSqr = leg.LengthSqr();
	const float fraction = ( lengthSqr > 1.0f ) ?
		idMath::ClampFloat( 0.0f, 1.0f, ( along * leg ) / lengthSqr ) : 1.0f;
	const float expected = legStart.z + ( corner.origin.z - legStart.z ) * fraction;

	offHeight = origin.z - expected;
	if ( offHeight < -BOT_OFF_ROUTE_BELOW ) {
		return true;
	}

	// A teleporter, a pad taken by accident or a long knockback can put the bot
	// on the same floor somewhere else entirely.
	const idVec3 closest = legStart + ( corner.origin - legStart ) * fraction;
	idVec3 aside = origin - closest;
	aside.z = 0.0f;
	if ( aside.LengthSqr() > Square( BOT_OFF_ROUTE_LATERAL ) ) {
		return true;
	}
	if ( corner.travelType != NAVTRAVEL_WALK ) {
		return false;
	}
	if ( corner.travelType == NAVTRAVEL_WALK && offHeight > BOT_OFF_ROUTE_ABOVE ) {
		return true;
	}

	// Displaced from the leg by more than a corner's reach, or off its height
	// by more than a step: does the walk from here to the corner still exist?
	// The proof is a few box sweeps, so it is asked a few times a second.
	if ( ( aside.LengthSqr() > Square( BOT_CORNER_REACH ) || idMath::Fabs( offHeight ) > pm_stepsize.GetFloat() ) &&
		 gameLocal.time - offRouteProofTime >= BOT_OFF_ROUTE_PROOF_MSEC && navMesh.IsValid() ) {
		offRouteProofTime = gameLocal.time;
		if ( !navMesh.WalkLegAccepted( origin, corner.origin, NULL ) ) {
			return true;
		}
	}
	return false;
}

/*
================
rvBot::PressJump
================
*/
void rvBot::PressJump( usercmd_t &cmd ) const {
	// idPhysics_Player::CheckJump only fires on a fresh press: PMF_JUMP_HELD
	// blocks a held button and is cleared only by a released frame.  Pulse on
	// alternating grounded frames: this gives every attempt an immediate fresh
	// edge, releases on the next frame, and never sprays jump input throughout
	// the flight.  The client offset keeps a pack from launching in lockstep.
	idPlayer *self = GetPlayer();
	if ( self && self->pfl.onGround && ( ( gameLocal.framenum + clientNum ) & 1 ) == 0 ) {
		cmd.upmove = 127;
	}
}

void rvBot::ResetDodgeJump( void ) {
	dodgeHopActive = false;
	dodgeHopAirborne = false;
	dodgeHopStartTime = 0;
	dodgeHopCheckTime = 0;
	dodgeHopDirection.Zero();
	dodgeHopWishSpeed = 0.0f;
}

bool rvBot::HoldDodgeJump( idPlayer *self, usercmd_t &cmd ) {
	if ( !dodgeHopActive ) {
		return false;
	}
	const int elapsed = gameLocal.time - dodgeHopStartTime;
	if ( self->health <= 0 || self->spectating || elapsed >= BOT_DODGE_FLIGHT_MSEC ||
		 ( self->pfl.onGround && ( dodgeHopAirborne || elapsed >= BOT_DODGE_LAUNCH_MSEC ) ) ) {
		if ( bot_debug.GetInteger() >= 2 ) {
			gameLocal.Printf( "bot %s: released dodge jump at %s after %d ms (%s)\n", name.c_str(),
				self->GetPhysics()->GetOrigin().ToString( 0 ), elapsed, self->pfl.onGround ? "grounded" : "expired" );
		}
		ResetDodgeJump();
		return false;
	}
	if ( knockbackTime >= dodgeHopStartTime ) {
		// A rocket under a dodge hop throws it somewhere its proof never
		// went, including one landing on the frame the hop starts (q4dm8);
		// the airborne brake answers for that flight.
		ResetDodgeJump();
		return false;
	}
	dodgeHopAirborne |= !self->pfl.onGround;
	if ( dodgeHopAirborne && gameLocal.time >= dodgeHopCheckTime ) {
		dodgeHopCheckTime = gameLocal.time + 96;
		// The takeoff proof covers static geometry, not a later player/body
		// collision. Do not hold its obsolete input all the way into the void.
		if ( !navMesh.AirInputLandingSafe( self->GetPhysics()->GetOrigin(),
			 self->GetPhysics()->GetLinearVelocity(), dodgeHopDirection, BotMaxSafeFallDelta( self ) ) ) {
			if ( bot_debug.GetInteger() >= 2 ) {
				gameLocal.Printf( "BOT_DODGE_RECOVER client=%d time=%d origin=%s velocity=%s\n",
					clientNum, gameLocal.time, self->GetPhysics()->GetOrigin().ToString( 3 ),
					self->GetPhysics()->GetLinearVelocity().ToString( 3 ) );
			}
			ResetDodgeJump();
			airRecovering = true;
			airRecoverLanded = 0;
			airRecoverPlanTime = gameLocal.time - BOT_AIR_RECOVER_REPLAN_MSEC;
			airRecoverWish.Zero();
			path.Clear();
			pathCorner = 0;
			pathTime = 0;
			ResetTraversal();
			nextGoalSelectTime = Min( nextGoalSelectTime, gameLocal.time );
			return false;
		}
	}
	ApplyMove( dodgeHopDirection, cmd );
	cmd.upmove = 0;		// a flight never holds or repeats the jump press
	stuckTime = gameLocal.time;
	return true;
}

void rvBot::TryDodgeJump( idPlayer *self, const idVec3 &direction, usercmd_t &cmd ) {
	// Match PressJump's fresh-input pulse before paying for the flight proof.
	if ( dodgeHopActive || !self->pfl.onGround || ( ( gameLocal.framenum + clientNum ) & 1 ) != 0 || !navMesh.IsValid() ) {
		return;
	}
	idVec3 wish = AvoidLiquidHazard( direction );
	wish.z = 0.0f;
	if ( wish.Normalize() <= 0.0f ) {
		return;
	}
	const float wishSpeed = pm_speed.GetFloat() * self->PowerUpModifier( PMOD_SPEED );
	if ( navMesh.DodgeJumpClear( self->GetPhysics()->GetOrigin(), self->GetPhysics()->GetLinearVelocity(),
		 wish, gameLocal.GetCurrentGravity( self ).Length(), wishSpeed ) ) {
		PressJump( cmd );
		if ( cmd.upmove <= 0 ) {
			return;
		}
		dodgeHopActive = true;
		dodgeHopAirborne = false;
		dodgeHopStartTime = gameLocal.time;
		dodgeHopDirection = wish;
		dodgeHopWishSpeed = wishSpeed;
		ApplyMove( wish, cmd );	// the takeoff command must match the proved input too
		if ( bot_debug.GetInteger() >= 2 ) {
			gameLocal.Printf( "bot %s: dodge jump at %s direction %s velocity %s wish %.0f (time %d)\n", name.c_str(),
				self->GetPhysics()->GetOrigin().ToString( 3 ), wish.ToString( 3 ),
				self->GetPhysics()->GetLinearVelocity().ToString( 3 ), wishSpeed, gameLocal.time );
		}
	}
}

// openQ4 BEGIN
// How far ahead a bot checks for lava and slime, and the small lift used on every probe so it
// samples just inside the volume rather than exactly on the floor plane.
const float BOT_LIQUID_LOOKAHEAD	= 56.0f;
const float BOT_LIQUID_PROBE_LIFT	= 2.0f;
// A route corner this far below a swimming bot is under it, not beside it: dive for it.
const float BOT_LIQUID_DIVE_DEPTH	= 16.0f;

/*
================
rvBot::LiquidAtFeet
================
*/
int rvBot::LiquidAtFeet( void ) const {
	idPlayer *self = GetPlayer();
	if ( !self ) {
		return 0;
	}

	const idVec3 probe = self->GetPhysics()->GetOrigin() + idVec3( 0.0f, 0.0f, BOT_LIQUID_PROBE_LIFT );
	return gameLocal.LiquidContentsAtPoint( probe, self );
}

/*
================
rvBot::LiquidAtEye
================
*/
int rvBot::LiquidAtEye( void ) const {
	idPlayer *self = GetPlayer();
	if ( !self ) {
		return 0;
	}

	return gameLocal.LiquidContentsAtPoint( self->GetEyePosition(), self );
}

/*
================
rvBot::UpdateLiquidMovement

Swimming and getting out of the fire. Held up rather than pulsed like PressJump when the bot is
actually swimming, because in idPhysics_Player::WaterMove upmove is an analogue swim-up axis and
not a jump edge - but that is only true above WATERLEVEL_FEET. idPhysics_Player::MovePlayer routes
anything shallower to WalkMove, where upmove IS the jump edge, so a held 127 sets PMF_JUMP_HELD and
nothing ever clears it: the bot gets exactly one jump and can then never hop the lip of the pit it
is standing in. Ankle-deep lava is precisely the case this code exists for, so it has to pulse.
================
*/
void rvBot::UpdateLiquidMovement( idPlayer *self, usercmd_t &cmd ) {
	const int feetLiquid = LiquidAtFeet();
	if ( !feetLiquid ) {
		return;
	}

	// GetPlayerPhysics rather than GetPhysics: this has to be the same object
	// idPhysics_Player::MovePlayer branches on, not whatever the player is
	// riding.
	const bool swimming = static_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->GetWaterLevel() >
		WATERLEVEL_FEET;

	// Lava and slime are killing the bot right now, so climbing is the whole plan.
	if ( feetLiquid & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) {
		if ( swimming ) {
			cmd.upmove = 127;
		} else {
			PressJump( cmd );
		}
		cmd.buttons |= BUTTON_RUN;
		return;
	}

	// Water: follow a route that leads under the surface while there is air to spare. The navmesh
	// keeps a pool's floor (water is crossable), so a route can drop onto it; surfacing
	// unconditionally left the bot treading water above that corner until the stuck check gave up
	// (the liquid lab's pools gave 20 stuck reports in 82 seconds). Half the reservoir is six
	// seconds of stock air, far more than any swim back up takes.
	if ( swimming && pathCorner >= 0 && pathCorner < path.Num() ) {
		const idVec3 &corner = path[ pathCorner ].origin;
		if ( corner.z < self->GetPhysics()->GetOrigin().z - BOT_LIQUID_DIVE_DEPTH &&
			 ( gameLocal.LiquidContentsAtPoint( corner + idVec3( 0.0f, 0.0f, BOT_LIQUID_PROBE_LIFT ), self ) & CONTENTS_WATER ) &&
			 self->GetAirTics() > pm_airTics.GetInteger() / 2 ) {
			cmd.upmove = -127;
			return;
		}
	}

	// Otherwise surface before the air runs out, then swim along the top rather than porpoising.
	if ( LiquidAtEye() ) {
		cmd.upmove = 127;
	}
}

/*
================
rvBot::AvoidLiquidHazard

Refuses to walk into lava or slime. Routing cannot help here - the navmesh is built from solids and
a liquid volume is not solid, so a path will run straight across a lava pool. Probe a short way
along the intended move and slide around the hazard instead of stepping into it.
================
*/
idVec3 rvBot::AvoidLiquidHazard( const idVec3 &moveDir ) const {
	idPlayer *self = GetPlayer();
	if ( !self ) {
		return moveDir;
	}

	idVec3 flat = moveDir;
	flat.z = 0.0f;
	if ( flat.Normalize() < VECTOR_EPSILON ) {
		return moveDir;
	}

	// already standing in it: escaping is UpdateLiquidMovement's job, and refusing to move here
	// would only pin the bot in the hazard
	const int hazardMask = CONTENTS_LAVA | CONTENTS_SLIME;
	if ( LiquidAtFeet() & hazardMask ) {
		return moveDir;
	}

	const idVec3 lift( 0.0f, 0.0f, BOT_LIQUID_PROBE_LIFT );
	const idVec3 origin = self->GetPhysics()->GetOrigin();

	// Stock lava is a nonsolid surface over clip with a trigger_hurt on it, not
	// lava contents, so the navmesh's hazard volumes are asked as well.
	const bool hazardVolumes = navMesh.IsValid() && navMesh.NumHazards() > 0;
	const bool alreadyBurning = hazardVolumes && navMesh.IsHazardous( origin, true );
	const idVec3 ahead = origin + flat * BOT_LIQUID_LOOKAHEAD;
	if ( !( gameLocal.LiquidContentsAtPoint( ahead + lift, self ) & hazardMask ) &&
		 ( !hazardVolumes || alreadyBurning || !navMesh.IsHazardous( ahead, true ) ) ) {
		return moveDir;
	}

	// try both sides before giving up, so the bot skirts the pool instead of stopping in the open
	const idVec3 side( -flat.y, flat.x, 0.0f );
	for ( int i = 0; i < 2; i++ ) {
		const idVec3 candidate = i ? -side : side;
		const idVec3 probe = origin + candidate * BOT_LIQUID_LOOKAHEAD;
		if ( !( gameLocal.LiquidContentsAtPoint( probe + lift, self ) & hazardMask ) &&
			 ( !hazardVolumes || !navMesh.IsHazardous( probe, true ) ) ) {
			return candidate;
		}
	}

	return vec3_origin;
}
// openQ4 END

/*
================
rvBot::ApplyMove

Movement is expressed in world space and resolved against the view, exactly as
a player's keys are.
================
*/
void rvBot::ApplyMove( const idVec3 &moveDir, usercmd_t &cmd ) const {
	idVec3 forward, right;

	idAngles moveAngles( 0.0f, aimAngles.yaw, 0.0f );
	moveAngles.ToVectors( &forward, &right, NULL );

// openQ4 BEGIN
	const bool routedJump = pathCorner >= 0 && pathCorner < path.Num() &&
		path[pathCorner].travelType == NAVTRAVEL_JUMP;
	const idVec3 steerDir = ( dodgeHopActive || routedJump ) ? moveDir : AvoidLiquidHazard( moveDir );
// openQ4 END

	const float f = steerDir * forward;
	const float r = steerDir * right;
	const float axisMax = Max( idMath::Fabs( f ), idMath::Fabs( r ) );
	float strength = Min( 1.0f, idMath::Sqrt( f * f + r * r ) );
	const bool action = pathCorner >= 0 && pathCorner < path.Num() &&
		( path[pathCorner].travelType != NAVTRAVEL_WALK ||
		  ( pathCorner + 1 < path.Num() && path[pathCorner + 1].travelType != NAVTRAVEL_WALK ) );
	idPlayer *self = ( action || dodgeHopActive || airRecovering ) ? GetPlayer() : NULL;
	if ( self ) {
		// Haste/Scout may boost ordinary running, but the trajectory proofs
		// use base pm_speed for both ground and air wish acceleration.
		const float modifier = Max( 1.0f, self->PowerUpModifier( PMOD_SPEED ) );
		const float speedRatio = dodgeHopActive ? Min( 1.0f,
			dodgeHopWishSpeed / Max( 1.0f, pm_speed.GetFloat() * modifier ) ) : 1.0f / modifier;
		const float maxAxis = (float)(int)( 127.0f * speedRatio );
		strength *= maxAxis / 127.0f;	// round down so the byte cannot exceed the proof speed
	}
	const float scale = axisMax > idMath::FLOAT_EPSILON ? 127.0f * strength / axisMax : 0.0f;
	// CmdScale takes its magnitude from the largest command axis. A unit
	// direction projected at 45 degrees must therefore use (127,127), not
	// (89,89), or looking aside reduces speed and air braking by 30 percent.
	// Preserve deliberately partial input, with only command-byte rounding.
	cmd.forwardmove	= idMath::ClampChar( (int)( f * scale + ( f < 0.0f ? -0.5f : 0.5f ) ) );
	cmd.rightmove	= idMath::ClampChar( (int)( r * scale + ( r < 0.0f ? -0.5f : 0.5f ) ) );

	// Full speed needs the run button held; without it idPlayer::AdjustSpeed
	// drops the bot to pm_walkspeed.
	if ( cmd.forwardmove || cmd.rightmove ) {
		cmd.buttons |= BUTTON_RUN;
	}
}

/*
================
rvBot::TrackDamage

The normal multiplayer damage path calls OnDamaged with an attacker.  This
timestamp edge remains as a fallback for hazards and mod damage paths that do
not have a player source.
================
*/
void rvBot::TrackDamage( idPlayer *self ) {
	if ( self->lastDmgTime == damageStamp ) {
		return;
	}

	damageStamp = self->lastDmgTime;
	ScheduleDodge( vec3_zero );
}

/*
================
rvBot::ScheduleDodge

A dodge owns explicit start/end times.  Repeated machinegun or lightning hits
therefore cannot keep moving the start line forward until the bot dies.
================
*/
void rvBot::ScheduleDodge( const idVec3 &threatDirection, const idVec3 *threatPoint ) {
	if ( gameLocal.time < dodgeEndTime ) {
		return;
	}

	int delay = (int)traits.dodgeReactMsec;
	if ( MistakeActive( BOTMISTAKE_LATEDODGE ) ) {
		delay *= 2;
	}

	dodgeStartTime = gameLocal.time + Max( 0, delay );
	dodgeEndTime = dodgeStartTime + BOT_DODGE_MSEC;
	dodgeJump = ( gameLocal.random.RandomFloat() < traits.jumpChance );

	// The dodge owns the strafe side until it is over.  The routed combat
	// movement re-rolls strafeSide on its own rhythm - 400 plus up to 800 ms
	// against a 700 ms dodge, so a flip lands inside the window more often than
	// not - and that would spend the second half of the dodge accelerating back
	// into the line of fire it was chosen to leave, harder than it would have
	// moved without dodging at all.  Deferring the rhythm covers the reaction
	// delay before dodgeStartTime as well.
	strafeFlipTime = Max( strafeFlipTime, dodgeEndTime );

	// The step itself is chosen once per dodge: across the threat's line of
	// travel, on the side away from where it passes closest.  Stepping along
	// the line - which a strafe relative to the current enemy does whenever
	// the round comes from the flank - only stays in its path.
	dodgeDirection.Zero();

	// Keep an existing useful side preference when the damage direction makes
	// one obvious; otherwise make the choice once for this dodge, not per frame.
	if ( threatDirection.LengthSqr() > idMath::FLOAT_EPSILON ) {
		idPlayer *self = GetPlayer();
		if ( self ) {
			idVec3 travel = threatDirection;
			travel.z = 0.0f;
			if ( travel.Normalize() > 0.0f ) {
				const idVec3 across( -travel.y, travel.x, 0.0f );
				float side = ( gameLocal.random.RandomFloat() < 0.5f ) ? -1.0f : 1.0f;
				if ( threatPoint ) {
					idVec3 away = self->GetPhysics()->GetOrigin() - *threatPoint;
					away.z = 0.0f;
					const float offset = away * across;
					if ( idMath::Fabs( offset ) > 4.0f ) {
						side = ( offset > 0.0f ) ? 1.0f : -1.0f;
					}
				}
				dodgeDirection = across * side;
			}

			idVec3 forward = aimAngles.ToForward();
			forward.z = 0.0f;
			idVec3 threat = threatDirection;
			threat.z = 0.0f;
			if ( forward.Normalize() > 0.0f && threat.Normalize() > 0.0f ) {
				strafeSide = ( forward.Cross( threat ).z < 0.0f ) ? -1.0f : 1.0f;
				return;
			}
		}
	} else if ( threatPoint ) {
		// Something lying still - a fused grenade that rolled up, a napalm
		// glob burning beside the feet - has no line of travel to step across.
		// The only sensible step is straight away from it; a strafe around the
		// enemy can walk right over it.
		idPlayer *self = GetPlayer();
		if ( self ) {
			idVec3 away = self->GetPhysics()->GetOrigin() - *threatPoint;
			away.z = 0.0f;
			if ( away.Normalize() > 0.0f ) {
				dodgeDirection = away;
			}
		}
	}

	strafeSide = ( gameLocal.random.RandomFloat() < 0.5f ) ? -1.0f : 1.0f;
}

/*
================
rvBot::UpdateStrafeRhythm
================
*/
void rvBot::UpdateStrafeRhythm( void ) {
	if ( gameLocal.time < strafeFlipTime ) {
		return;
	}

	const int rhythm = Max( 1, (int)traits.strafeRhythmMsec );
	const int variance = Max( 1, (int)traits.strafeRhythmVarianceMsec );

	strafeFlipTime	= gameLocal.time + rhythm + gameLocal.random.RandomInt( variance );
	strafeSide		= -strafeSide;
}

/*
================
rvBot::DodgeDirection
================
*/
bool rvBot::DodgeDirection( idPlayer *self, idVec3 &direction ) const {
	if ( !IsDodging( self ) ) {
		return false;
	}

	if ( dodgeDirection.LengthSqr() > 0.5f ) {
		direction = dodgeDirection;
		return true;
	}

	// No known line of travel: sidestep around the enemy if there is one in
	// view, or across the bot's own facing if there is not.
	idPlayer *foe = enemy.GetEntity();
	idVec3 reference;
	if ( foe && gameLocal.time == enemyLastSeenTime ) {
		reference = foe->GetPhysics()->GetOrigin() - self->GetPhysics()->GetOrigin();
	} else {
		reference = aimAngles.ToForward();
	}
	reference.z = 0.0f;
	if ( reference.Normalize() <= 0.0f ) {
		return false;
	}

	direction = reference.Cross( idVec3( 0.0f, 0.0f, 1.0f ) ) * strafeSide;
	return true;
}

/*
================
rvBot::IsDodging
================
*/
bool rvBot::IsDodging( idPlayer *self ) const {
	return self && gameLocal.time >= dodgeStartTime && gameLocal.time < dodgeEndTime;
}

/*
================
BotMoveDirectionSafe

Combat steering is layered over a validated nav route.  Before accepting that
layer, sweep the real player bounds a short distance and make sure there is
walkable floor beyond it.  If it fails, the caller keeps the original route
direction instead of strafing off a ledge or into a wall.
================
*/
static bool BotMoveDirectionSafe( idPlayer *self, const idVec3 &direction ) {
	idVec3 dir = direction;
	dir.z = 0.0f;
	if ( dir.Normalize() <= 0.0f ) {
		return false;
	}

	const idVec3 start = self->GetPhysics()->GetOrigin();
	const idVec3 end = start + dir * 72.0f;
	trace_t sweep;
	gameLocal.TraceBounds( self, sweep, start, end, self->GetPhysics()->GetBounds(),
						   MASK_PLAYERSOLID, self );
	if ( sweep.fraction < 0.72f ) {
		return false;
	}

	trace_t floor;
	const idVec3 floorStart = sweep.endpos + idVec3( 0.0f, 0.0f, 12.0f );
	const idVec3 floorEnd = sweep.endpos - idVec3( 0.0f, 0.0f, 88.0f );
	gameLocal.TracePoint( self, floor, floorStart, floorEnd, MASK_PLAYERSOLID, self );

	if ( floor.fraction >= 1.0f || floor.c.normal.z < 0.55f ) {
		return false;
	}

	if ( !navMesh.IsValid() ) {
		return true;
	}

	// Nor onto a jump pad or into a teleporter the route did not choose: a
	// strafe that grazes one carries the bot out of the fight it is in.
	const idBounds &box = self->GetPhysics()->GetBounds();
	idBounds swept( start );
	swept.AddPoint( sweep.endpos );
	swept[0] += box[0];
	swept[1] += box[1];
	if ( navMesh.TransportAt( swept ) >= 0 ) {
		return false;
	}

	// Solid ground can still be a lava pool or a kill volume: a dodge or a
	// strafe must not carry the bot into what routing refuses to cross.
	return !navMesh.IsHazardous( floor.endpos, true );
}

/*
================
BotDeadlyDropAt

Is the ground at this point the edge of nothing: no floor within a survivable
fall under it, or a kill volume?  Tested with the middle of the box rather
than a point, so a crack between two floor brushes is not a void.
================
*/
static bool BotDeadlyDropAt( idPlayer *self, const idVec3 &point ) {
	static const idBounds middle( idVec3( -8.0f, -8.0f, 0.0f ), idVec3( 8.0f, 8.0f, 8.0f ) );
	trace_t trace;
	gameLocal.TraceBounds( self, trace, point + idVec3( 0.0f, 0.0f, 8.0f ),
						   point - idVec3( 0.0f, 0.0f, BOT_SAFE_FALL ), middle, MASK_PLAYERSOLID, self );
	if ( trace.fraction >= 1.0f || trace.c.normal.z < 0.7f ) {
		if ( bot_debug.GetInteger() >= 3 ) {
			gameLocal.Printf( "BOT_EDGE client=%d time=%d origin=%s ahead=%s velocity=%s fraction=%.4f normal=%.3f %.3f %.3f reason=trace\n",
				self->entityNumber, gameLocal.time, self->GetPhysics()->GetOrigin().ToString( 2 ), point.ToString( 2 ),
				self->GetPhysics()->GetLinearVelocity().ToString( 2 ), trace.fraction, trace.c.normal.x, trace.c.normal.y, trace.c.normal.z );
		}
		return true;
	}
	// The graph's landing may be a raised lip beside the floor momentum
	// actually reaches (CTF7). A broad floor within BOT_SAFE_FALL can still
	// kill a hurt bot. Let the live drop proof earn that departure instead.
	navCorner_t landing;
	landing.origin = trace.endpos;
	landing.travelType = NAVTRAVEL_DROP;
	landing.actionEntityNum = -1;
	const bool unsafe = navMesh.IsValid() && ( navMesh.IsHazardous( trace.endpos, true ) ||
		!navMesh.LandingContinuationClear( trace.endpos, trace.endpos ) ||
		!navMesh.LandingSurvivable( self->GetPhysics()->GetOrigin(), landing, BotMaxSafeFallDelta( self ) ) );
	if ( unsafe && bot_debug.GetInteger() >= 3 ) {
		gameLocal.Printf( "BOT_EDGE client=%d time=%d origin=%s ahead=%s velocity=%s floor=%s fraction=%.4f normal=%.3f %.3f %.3f reason=support\n",
			self->entityNumber, gameLocal.time, self->GetPhysics()->GetOrigin().ToString( 2 ), point.ToString( 2 ),
			self->GetPhysics()->GetLinearVelocity().ToString( 2 ), trace.endpos.ToString( 2 ), trace.fraction, trace.c.normal.x, trace.c.normal.y, trace.c.normal.z );
	}
	return unsafe;
}

// Recovery has no route to vouch for its direction. It must use the same
// momentum lookahead as ordinary WALK steering, including when wandering.
static bool BotBrakeAtEdge( idPlayer *self, idVec3 &moveDir ) {
	if ( !self->pfl.onGround ) {
		return false;
	}
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	const idVec3 ahead = self->GetPhysics()->GetOrigin() +
		idVec3( velocity.x, velocity.y, 0.0f ) * BOT_EDGE_LOOKAHEAD + moveDir * 16.0f;
	if ( !BotDeadlyDropAt( self, ahead ) ) {
		return false;
	}
	// A rim node can be walkable but lack the broad support this guard needs.
	// Once stopped, braking forever cannot move the bot onto the safer parallel
	// line (CTF6's base walkways). Creep sideways only along continuously
	// supported ground; fast drift and friction-free knocks still brake first.
	const idVec3 flat( velocity.x, velocity.y, 0.0f );
	if ( flat.LengthSqr() <= Square( BOT_RECOVER_EDGE_SPEED ) && navMesh.IsValid() &&
		static_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->GetKnockBack() <= 0 ) {
		idVec3 wish = moveDir;
		wish.z = 0.0f;
		const float magnitude = wish.Normalize();
		const float turns[4] = { 45.0f, -45.0f, 90.0f, -90.0f };
		for ( int i = 0; magnitude > 0.0f && i < 4; i++ ) {
			const float c = idMath::Cos( DEG2RAD( turns[i] ) );
			const float s = idMath::Sin( DEG2RAD( turns[i] ) );
			const idVec3 inward( wish.x * c - wish.y * s, wish.x * s + wish.y * c, 0.0f );
			const idVec3 origin = self->GetPhysics()->GetOrigin();
			const idVec3 next = origin + flat * BOT_EDGE_LOOKAHEAD + inward * 16.0f;
			if ( navMesh.LandingContinuationClear( origin, next ) &&
				navMesh.TransportOnMove( origin, next, -1 ) < 0 && BotMoveDirectionSafe( self, inward ) ) {
				moveDir = inward * Min( magnitude, BOT_RECOVER_EDGE_SPEED / Max( 1.0f, pm_speed.GetFloat() ) );
				return true;
			}
		}
	}
	moveDir.Set( -velocity.x, -velocity.y, 0.0f );
	const float speed = moveDir.Normalize();
	if ( speed <= 1.0f ) {
		moveDir.Zero();
	} else {
		// Full reverse at walking speed can cross zero in one physics frame,
		// then reverse again forever on a rim. Fade the wish as momentum falls
		// so ground friction can finish the stop and supported recovery can start.
		moveDir *= Min( 1.0f, speed / Max( 1.0f, pm_speed.GetFloat() ) );
	}
	return true;
}

/*
================
BotPlannedTransport

The jump pad or teleporter this route will take next, through any walking
corners before it, or -1: the one volume its walking legs may touch.
================
*/
static int BotPlannedTransport( const rvNavPath &path, int pathCorner ) {
	for ( int i = Max( pathCorner, 0 ); i < path.Num(); i++ ) {
		if ( path[i].travelType == NAVTRAVEL_WALK ) {
			continue;
		}
		return ( path[i].travelType == NAVTRAVEL_JUMPPAD || path[i].travelType == NAVTRAVEL_TELEPORT ) ?
			path[i].actionEntityNum : -1;
	}
	return -1;
}

/*
================
BotBrakeForTransport

Routes never walk through a pad or a teleporter they did not choose, but the
box drifts off its legs - combat footwork, a landing's runout, a shove - and
q4ctf5 lays pads beside its walkways.  A bot carried onto one was thrown
across the map, through a chain of pads onto a landing its health could not
take, or into a flyer coming the other way.  Look ahead along the velocity
as the edge guard does: momentum that would touch a volume other than the
planned one brakes, and a wish that would slides along it or stops.
================
*/
static bool BotBrakeForTransport( idPlayer *self, idVec3 &moveDir, int plannedEntityNum ) {
	if ( !self->pfl.onGround || !navMesh.IsValid() ) {
		return false;
	}
	const idVec3 origin = self->GetPhysics()->GetOrigin();
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	const idVec3 flat( velocity.x, velocity.y, 0.0f );
	const idVec3 drift = flat * BOT_EDGE_LOOKAHEAD;
	const int transport = navMesh.TransportOnMove( origin, origin + drift + moveDir * 16.0f, plannedEntityNum );
	if ( transport < 0 ) {
		return false;
	}
	if ( bot_debug.GetInteger() >= 2 ) {
		gameLocal.Printf( "BOT_TRANSPORT_GUARD client=%d time=%d origin=%s velocity=%s entity=%d planned=%d\n",
			self->entityNumber, gameLocal.time, origin.ToString( 1 ), velocity.ToString( 1 ),
			transport, plannedEntityNum );
	}
	// Momentum that reaches the volume by itself is braked against. Reversing
	// a velocity that already points away pushes the bot back in: alternating
	// that with the wish crept a bot into q4ctf5's func_jumppad_14 a unit at a
	// time, so a wish that alone reaches it slides square to itself instead -
	// a diagonal still closes on the trigger - or stops pressing.
	idVec3 brake = -flat;
	if ( brake.Normalize() > 1.0f && navMesh.TransportOnMove( origin, origin + drift, plannedEntityNum ) >= 0 ) {
		moveDir = brake;
		return true;
	}
	// Slide only to the side away from the volume's centre: the side that
	// merely clears the next stride can still run along the trigger into it.
	idVec3 centre;
	if ( moveDir.LengthSqr() > 0.0f && navMesh.TransportCentre( transport, centre ) ) {
		idVec3 away = origin - centre;
		away.z = 0.0f;
		idVec3 turned( -moveDir.y, moveDir.x, 0.0f );
		if ( turned * away < 0.0f ) {
			turned = -turned;
		}
		if ( turned * away >= 0.0f &&
			 navMesh.TransportOnMove( origin, origin + drift + turned * 16.0f, plannedEntityNum ) < 0 &&
			 BotMoveDirectionSafe( self, turned ) ) {
			moveDir = turned;
			return true;
		}
	}
	moveDir.Zero();
	return true;
}

/*
================
BotAirLandingUnsafe

Whether the fall from here, with no air input, comes down anywhere but
supported ground the bot can afford to land on.
================
*/
static bool BotAirLandingUnsafe( idPlayer *self, idVec3 &touchdown ) {
	touchdown = self->GetPhysics()->GetOrigin();
	return !navMesh.AirInputLandingSafe( self->GetPhysics()->GetOrigin(), self->GetPhysics()->GetLinearVelocity(),
		vec3_origin, BotMaxSafeFallDelta( self ), &touchdown );
}

/*
================
BotRecoveryHealthAt

A pickup at a last-resort landing may save a critically hurt bot. Use the
actual pickup trigger, which can extend beyond the item's visual/physics model.
================
*/
static int BotRecoveryHealthAt( idPlayer *self, const idVec3 &touchdown ) {
	idBounds body = self->GetPhysics()->GetBounds();
	body.TranslateSelf( touchdown );
	int health = 0;
	const idList<idItem *> &items = botManager.GetItems();
	for ( int i = 0; i < items.Num(); i++ ) {
		const idItem *item = items[i];
		if ( item && !item->pickedUp && !item->IsHidden() && item->GetInstance() == self->GetInstance() &&
			 item->TouchesPickup( body ) ) {
			health += Max( 0, item->spawnArgs.GetInt( "inv_health", "0" ) );
		}
	}
	return health;
}

/*
================
BotChooseAirInput

Keep a proven air input, otherwise try the eight braking-relative headings,
the sixteen world headings used by the recovery audit, then none. Prefer an
affordable landing with stopping room, then a proven hull-contact brake,
then affordable supported ground. If none exists, retain an affordable physical touchdown as a last resort, then
a supported floor with a live health pickup, before falling back to braking.
This never approves a voluntary traversal.
================
*/
static bool BotChooseAirInput( idPlayer *self, idVec3 &wish ) {
	const idVec3 origin = self->GetPhysics()->GetOrigin();
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	const float limit = BotMaxSafeFallDelta( self );
	const int knockMsec = static_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->GetKnockBack();
	bool heldTouch = false, heldBrake = false;
	// Replanning must not replace a still-safe heading with a different one
	// whose prediction starts from a slightly different point every 200 ms.
	if ( navMesh.AirInputLandingSafe( origin, velocity, wish, limit, NULL, NULL, &heldTouch, &heldBrake, knockMsec, self ) || heldBrake ) {
		return true;
	}
	idVec3 brake( -velocity.x, -velocity.y, 0.0f );
	if ( brake.Normalize() <= 1.0f ) {
		brake.Set( 1.0f, 0.0f, 0.0f );
	}
	static const float turns[8] = { 0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f };
	bool haveSupported = false;
	idVec3 supported = brake;
	bool haveBrake = false;
	idVec3 brakeInput = brake;
	idVec3 lastTouchdown;
	bool haveLastResort = false;
	if ( limit < idMath::INFINITY && wish.LengthSqr() > 0.0f ) {
		navMesh.AirInputLandingSafe( origin, velocity, wish, idMath::INFINITY, &lastTouchdown, &haveLastResort, NULL, NULL, knockMsec, self );
	}
	idVec3 lastResort = wish;
	int lastResortHealth = haveLastResort ? BotRecoveryHealthAt( self, lastTouchdown ) : 0;
	bool haveTouch = heldTouch;
	idVec3 touchInput = wish;
	for ( int i = 0; i < 25; i++ ) {
		idVec3 candidate = vec3_origin;
		if ( i < 8 ) {
			const float c = idMath::Cos( DEG2RAD( turns[i] ) );
			const float s = idMath::Sin( DEG2RAD( turns[i] ) );
			candidate.Set( brake.x * c - brake.y * s, brake.x * s + brake.y * c, 0.0f );
		} else if ( i < 24 ) {
			const float angle = DEG2RAD( ( i - 8 ) * 22.5f );
			candidate.Set( idMath::Cos( angle ), idMath::Sin( angle ), 0.0f );
		}
		bool landed = false, touched = false, braked = false;
		if ( navMesh.AirInputLandingSafe( origin, velocity, candidate, limit, NULL, &landed, &touched, &braked, knockMsec, self ) ) {
			wish = candidate;
			return true;
		}
		if ( braked && !haveBrake ) {
			haveBrake = true;
			brakeInput = candidate;
		}
		if ( touched && !haveTouch ) {
			haveTouch = true;
			touchInput = candidate;
		}
		if ( landed && !haveSupported ) {
			// Supported floor without the stopping reserve still beats the
			// void: the touchdown runout brakes on it. A q4dm8 rocket left
			// only such a ledge within reach.
			haveSupported = true;
			supported = candidate;
		}
		idVec3 candidateTouchdown = vec3_origin;
		bool candidateSupported = false;
		if ( limit < idMath::INFINITY && !haveSupported ) {
			navMesh.AirInputLandingSafe( origin, velocity, candidate, idMath::INFINITY, &candidateTouchdown, &candidateSupported, NULL, NULL, knockMsec, self );
		}
		if ( candidateSupported ) {
			// If every affordable landing is out of reach, supported floor
			// still beats braking into the void or another pad. A health
			// pickup can make the impact survivable (q4ctf5's lower walkway).
			// This is a last resort, never approval for a voluntary jump.
			const int health = BotRecoveryHealthAt( self, candidateTouchdown );
			if ( !haveLastResort || health > lastResortHealth ) {
				haveLastResort = true;
				lastResort = candidate;
				lastResortHealth = health;
			}
		}
		if ( bot_debug.GetInteger() >= 4 ) {
			gameLocal.Printf( "BOT_AIR_OPTION time=%d input=%.3f %.3f touchdown=%.1f %.1f %.1f supported=%d health=%d\n",
				gameLocal.time, candidate.x, candidate.y, candidateTouchdown.x, candidateTouchdown.y, candidateTouchdown.z,
				candidateSupported, candidateSupported ? BotRecoveryHealthAt( self, candidateTouchdown ) : 0 );
		}
	}
	// An involuntary fall with no supported solution still benefits from an
	// affordable physical touchdown (a wall can catch a narrow hull ledge).
	// This is a last resort, never proof for a voluntary route or an exclusion.
	wish = haveBrake ? brakeInput : ( haveSupported ? supported : ( haveTouch ? touchInput : ( haveLastResort ? lastResort : brake ) ) );
	return haveBrake || haveSupported || haveTouch || haveLastResort;
}

/*
================
rvBot::RecoverInAir

A bot knocked into the air - a rocket's splash, a rail's kick - kept steering
for its corner, its drop's landing or its pad's, and on q4ctf5's walkways
the throw carried it over the void.  Replays of those impulses come down on
the floor again with full air input against the horizontal velocity, the
most the player's air acceleration can take back - or, when the fall's
simulation finds the far side within reach and not the near, toward that
(BotChooseAirInput).  Once begun the input holds for the whole fall - releasing it when the prediction first read safe
let the throw carry a bot over a lower ledge's edge - and through a short
runout after touchdown.

An unsafe unpressed WALK flight also starts it: a sloping rim can throw the
bot upward just as it consumes its final corner. Neither that rise nor an
empty route makes the coast safe. An intended pad has not launched the bot
until it actually touches the volume; planned drops, teleport entries and
observed pad flights retain their own controller.

A fall a strong knock caused starts it too: one the knock threw off the
ground, one that slid off a ledge in the knockback's friction-free time -
q4ctf5's rail kicks slid grounded bots off the walkway at 550 units a
second - or one hit in the air.  A pad's launch reads as a fall over the void
on the frame before its traversal is marked started, and braking that
dropped bots into the gap; a jump or a drop taken after an earlier knock is
the bot's own.  Inside a pad or teleporter, or in a transport flight that
caught the throw, the volume owns the bot.  A knocked jump, drop or pad
flight hands over its route, which is decided again afterwards.
================
*/
bool rvBot::RecoverInAir( idPlayer *self, usercmd_t &cmd ) {
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	idVec3 brake( -velocity.x, -velocity.y, 0.0f );
	const float speed = brake.Normalize();
	const bool transportFlight = traversalStarted && pathCorner >= 0 && pathCorner < path.Num() &&
		( path[pathCorner].travelType == NAVTRAVEL_JUMPPAD || path[pathCorner].travelType == NAVTRAVEL_TELEPORT ) &&
		traversalStartTime > knockbackTime;
	const bool transported = transportFlight ||
		navMesh.TransportOnMove( self->GetPhysics()->GetOrigin(), self->GetPhysics()->GetOrigin(), -1 ) >= 0;
	if ( airRecovering && ( transported || self->health <= 0 ) ) {
		airRecovering = false;
	}
	if ( !airRecovering || !self->pfl.onGround ) {
		airRecoverSliding = false;
	}
	const int knockMsec = static_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->GetKnockBack();
	if ( self->pfl.onGround && !transported && !traversalStarted &&
		knockbackTime && gameLocal.time - knockbackTime <= BOT_AIR_RECOVER_SLIDE_MSEC &&
		( airRecoverSliding || ( knockMsec > 0 && speed > pm_speed.GetFloat() ) ) &&
		!LiquidAtFeet() && navMesh.IsValid() ) {
		const idVec3 origin = self->GetPhysics()->GetOrigin();
		const idVec3 flat( velocity.x, velocity.y, 0.0f );
		const idVec3 stop = origin + flat * ( MS2SEC( gameLocal.GetMSec() ) +
			speed / ( 20.0f * Max( 1.0f, pm_speed.GetFloat() ) ) );
		if ( airRecoverSliding && knockMsec <= 0 && velocity.z <= 0.0f &&
			navMesh.LandingContinuationClear( origin, stop ) ) {
			airRecoverSliding = false;	// ordinary touchdown braking now has room
		} else {
			idVec3 wish = airRecoverSliding ? airRecoverWish : vec3_origin;
			bool proven = velocity.z > 0.0f ? BotChooseAirInput( self, wish ) :
				navMesh.KnockSlideLandingSafe( origin, velocity, knockMsec, BotMaxSafeFallDelta( self ), wish );
			for ( int i = 0; !proven && i < 16; i++ ) {
				const float angle = DEG2RAD( i * 22.5f );
				wish.Set( idMath::Cos( angle ), idMath::Sin( angle ), 0.0f );
				proven = navMesh.KnockSlideLandingSafe( origin, velocity, knockMsec, BotMaxSafeFallDelta( self ), wish );
			}
			if ( proven ) {
				airRecoverWish = wish;
				airRecovering = airRecoverSliding = true;
				airRecoverPlanTime = gameLocal.time;
				airRecoverLanded = 0;
				ApplyMove( wish, cmd );
				cmd.upmove = 0;
				stuckTime = gameLocal.time;
				return true;
			}
		}
	}

	if ( airRecovering && self->pfl.onGround ) {
		if ( !airRecoverLanded ) {
			airRecoverLanded = gameLocal.time;
		}
		const idVec3 origin = self->GetPhysics()->GetOrigin();
		const idVec3 flat( velocity.x, velocity.y, 0.0f );
		const idVec3 stop = origin + flat * ( MS2SEC( gameLocal.GetMSec() ) +
			speed / ( 20.0f * Max( 1.0f, pm_speed.GetFloat() ) ) );
		idVec3 supported;
		if ( !navMesh.LandingContinuationClear( origin, origin ) &&
			navMesh.RecoveryStepClear( origin, stop, &supported ) ) {
			// The air proof may admit a rim only because forward momentum
			// reaches full support. Braking before that point cancels the very
			// continuation it proved and can drop the hull off the corner.
			idVec3 inward = supported - origin;
			inward.z = 0.0f;
			inward.Normalize();
			ApplyMove( inward, cmd );
			cmd.upmove = 0;
			stuckTime = gameLocal.time;
			return true;
		}

		if ( speed <= BOT_AIR_RECOVER_STOP_SPEED || gameLocal.time - airRecoverLanded > BOT_AIR_RECOVER_RUNOUT_MSEC ) {
			airRecovering = false;
			return false;
		}
		if ( knockMsec <= 0 ) {
			brake *= BotGroundBrakeStrength( speed );
		}
		ApplyMove( brake, cmd );
		cmd.upmove = 0;
		stuckTime = gameLocal.time;
		return true;
	}
	if ( self->pfl.onGround || transported || LiquidAtFeet() || !navMesh.IsValid() || speed <= 1.0f ) {
		return false;
	}
	// The knock's own fall: thrown off the ground by it, slid off a ledge
	// during its friction-free knockback time, or hit in the air - but not a
	// jump the bot pressed after an earlier knock.
	const bool knocked = knockbackTime && gameLocal.time - knockbackTime <= BOT_AIR_RECOVER_KNOCK_MSEC &&
		airborneSince && knockbackTime >= airborneSince -
		( airborneVoluntary ? BOT_AIR_RECOVER_TAKEOFF_MSEC : BOT_AIR_RECOVER_SLIDE_MSEC );
	const bool walking = path.IsEmpty() || ( pathCorner >= 0 && pathCorner < path.Num() &&
		path[pathCorner].travelType == NAVTRAVEL_WALK );
	const bool walkingDeparture = walking && pathCorner >= 0 && pathCorner + 1 < path.Num() &&
		( path[pathCorner + 1].travelType == NAVTRAVEL_DROP ||
		  path[pathCorner + 1].travelType == NAVTRAVEL_TELEPORT );
	const bool walkingFall = walking && !walkingDeparture && airborneSince &&
		!airborneVoluntary;
	// Route steering can spoil a survivable coast before the next prediction
	// notices the lost landing. A hit during a jump/drop also invalidates its
	// takeoff proof (including its health budget): a safe coast is not proof
	// that continuing toward the old landing is still safe. Pad flights retain
	// their controller while their coast remains safe.
	const bool ballistic = pathCorner >= 0 && pathCorner < path.Num() &&
		( path[pathCorner].travelType == NAVTRAVEL_JUMP || path[pathCorner].travelType == NAVTRAVEL_DROP );
	const bool protectCoast = knocked && ( ballistic || ( !traversalStarted && ( path.IsEmpty() || walking ) ) );
	idVec3 touchdown = self->GetPhysics()->GetOrigin();
	if ( !airRecovering && ( ( !knocked && !walkingFall ) ||
		( !protectCoast && !BotAirLandingUnsafe( self, touchdown ) ) ) ) {
		return false;
	}
	if ( !airRecovering ) {
		airRecoverWish.Zero();
	}
	if ( !airRecovering || gameLocal.time - airRecoverPlanTime >= BOT_AIR_RECOVER_REPLAN_MSEC ) {
		BotChooseAirInput( self, airRecoverWish );
		airRecoverPlanTime = gameLocal.time;
	}
	if ( !airRecovering ) {
		airRecovering = true;
		airRecoverLanded = 0;
		if ( traversalStarted || ( pathCorner >= 0 && pathCorner < path.Num() &&
			 path[pathCorner].travelType != NAVTRAVEL_WALK ) ) {
			// The jump's, drop's or pad's landing is out of reach now.
			path.Clear();
			pathCorner = 0;
			pathTime = 0;
			ResetTraversal();
			nextGoalSelectTime = Min( nextGoalSelectTime, gameLocal.time );
		}
	} else {
		touchdown = self->GetPhysics()->GetOrigin();
	}
	if ( bot_debug.GetInteger() >= 2 && gameLocal.time - airRecoverReported > 1000 ) {
		airRecoverReported = gameLocal.time;
		gameLocal.Printf( "BOT_AIR_RECOVER client=%d time=%d origin=%s velocity=%s touchdown=%s input=%s\n",
			self->entityNumber, gameLocal.time, self->GetPhysics()->GetOrigin().ToString( 1 ),
			velocity.ToString( 1 ), touchdown.ToString( 1 ), airRecoverWish.ToString( 2 ) );
	}
	cmd.forwardmove = 0;
	cmd.rightmove = 0;
	if ( airRecoverWish.LengthSqr() > 0.0f ) {
		ApplyMove( airRecoverWish, cmd );
	}
	cmd.upmove = 0;
	stuckTime = gameLocal.time;
	return true;
}

// A sharp turn into a jump/drop must shed sideways momentum before the lip.
// Air acceleration is too weak to repair a full-speed transverse approach.
bool rvBot::AlignJumpDropApproach( idPlayer *self, usercmd_t &cmd ) {
	if ( !self->pfl.onGround || traversalStarted || pathCorner < 0 || pathCorner >= path.Num() ) {
		return false;
	}
	int entry = pathCorner;
	if ( path[entry].travelType == NAVTRAVEL_DROP || path[entry].travelType == NAVTRAVEL_JUMP ) {
		entry--;
	}
	if ( entry < 0 || entry + 1 >= path.Num() || path[entry].travelType != NAVTRAVEL_WALK ||
		( path[entry + 1].travelType != NAVTRAVEL_DROP && path[entry + 1].travelType != NAVTRAVEL_JUMP ) ) {
		return false;
	}
	idVec3 offset = path[entry].origin - self->GetPhysics()->GetOrigin();
	offset.z = 0.0f;
	if ( offset.LengthSqr() > Square( 64.0f ) ) {
		return false;
	}
	idVec3 along = path[entry + 1].origin - path[entry].origin;
	along.z = 0.0f;
	if ( along.Normalize() < 1.0f ) {
		return false;
	}
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	const idVec3 flat( velocity.x, velocity.y, 0.0f );
	const float forward = flat * along;
	const float runSpeed = Max( 1.0f, pm_speed.GetFloat() );
	const float lateralLimit = Min( 64.0f, runSpeed * 0.2f );
	if ( flat.LengthSqr() <= Square( runSpeed ) &&
		( flat - along * forward ).LengthSqr() <= Square( lateralLimit ) && forward >= -lateralLimit ) {
		return false;
	}
	idVec3 brake = -flat;
	brake.Normalize();
	ApplyMove( brake, cmd );
	cmd.upmove = 0;
	stuckTime = gameLocal.time;
	return true;
}

// Graph jumps start centrally along the link. The live corner has a radius,
// and an approaching bot can carry lateral momentum: prove this exact state
// before issuing a fresh jump, then use the proof's target-directed air input.
bool rvBot::HandleJumpTakeoff( idPlayer *self, usercmd_t &cmd ) {
	if ( !self->pfl.onGround || pathCorner < 0 || pathCorner >= path.Num() ||
		path[pathCorner].travelType != NAVTRAVEL_JUMP ) {
		return false;
	}
	const idVec3 origin = self->GetPhysics()->GetOrigin();
	const idVec3 velocity = self->GetPhysics()->GetLinearVelocity();
	const idVec3 landing = path[pathCorner].origin;
	idVec3 wish = landing - origin;
	wish.z = 0.0f;
	wish.Normalize();
	const float modifier = Max( 1.0f, self->PowerUpModifier( PMOD_SPEED ) );
	const float wishSpeed = Max( 1.0f, pm_speed.GetFloat() ) * modifier *
		(float)(int)( 127.0f / modifier ) / 127.0f;
	PressJump( cmd );
	if ( cmd.upmove > 0 ) {
		ApplyMove( wish, cmd );
		// CheckJump clears walking before AirMove, so the first CmdScale
		// includes upmove. Pass the actual encoded direction and speed for
		// that frame; held airborne frames release upmove and use wishSpeed.
		idVec3 forward, right;
		idAngles( 0.0f, aimAngles.yaw, 0.0f ).ToVectors( &forward, &right, NULL );
		idVec3 firstWish = forward * cmd.forwardmove + right * cmd.rightmove;
		const float horizontal = firstWish.Normalize();
		const float maxAxis = Max( idMath::Fabs( (float)cmd.upmove ),
			Max( idMath::Fabs( (float)cmd.forwardmove ), idMath::Fabs( (float)cmd.rightmove ) ) );
		const float total = idMath::Sqrt( horizontal * horizontal + Square( (float)cmd.upmove ) );
		const float firstSpeed = pm_speed.GetFloat() * modifier * maxAxis * horizontal / ( 127.0f * total );
		if ( navMesh.JumpTakeoffClear( origin, velocity, landing,
			 gameLocal.GetCurrentGravity( self ).Length(), wishSpeed, &firstWish, firstSpeed ) ) {
			if ( bot_debug.GetInteger() >= 3 ) {
				gameLocal.Printf( "BOT_JUMP_TAKEOFF client=%d time=%d origin=%s velocity=%s landing=%s firstWish=%s firstSpeed=%.3f wishSpeed=%.3f\n",
					self->entityNumber, gameLocal.time, origin.ToString( 3 ), velocity.ToString( 3 ),
					landing.ToString( 3 ), firstWish.ToString( 3 ), firstSpeed, wishSpeed );
			}
			return true;
		}
	}
	cmd.upmove = 0;
	cmd.forwardmove = 0;
	cmd.rightmove = 0;
	idVec3 brake( -velocity.x, -velocity.y, 0.0f );
	const float speed = brake.Normalize();
	if ( speed > 1.0f ) {
		ApplyMove( brake, cmd );
	}
	if ( speed <= 64.0f && pathCorner > 0 ) {
		const idVec3 entry = path[pathCorner - 1].origin;
		idVec3 inward = entry - origin;
		inward.z = 0.0f;
		if ( inward.Normalize() > 2.0f && navMesh.LandingContinuationClear( origin, entry ) ) {
			ApplyMove( inward * 0.2f, cmd );
		}
	}
	if ( gameLocal.time - traversalStartTime > BOT_TRAVEL_TIMEOUT_MSEC ) {
		if ( bot_debug.GetInteger() >= 1 ) {
			gameLocal.Printf( "bot %s: refusing unsafe jump takeoff at %s velocity %s toward %s\n",
				name.c_str(), origin.ToString( 0 ), velocity.ToString( 0 ), landing.ToString( 0 ) );
		}
		AbandonGoal( BOT_GOAL_AVOID_MSEC );
	}
	return true;
}

// A completed traversal may touch down beyond its sampled endpoint. The next
// leg was proven from that endpoint, not from an arbitrary same-height rim.
bool rvBot::GuardLandingContinuation( idPlayer *self, usercmd_t &cmd ) {
	if ( !self->pfl.onGround || !traversalStarted || !navMesh.IsValid() ||
		pathCorner < 0 || pathCorner + 1 >= path.Num() ||
		path[pathCorner + 1].travelType != NAVTRAVEL_WALK ) {
		return false;
	}
	const int travel = path[pathCorner].travelType;
	if ( travel != NAVTRAVEL_DROP && travel != NAVTRAVEL_JUMP && travel != NAVTRAVEL_JUMPPAD ) {
		return false;
	}
	const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
	if ( travel == NAVTRAVEL_JUMPPAD && velocity.z > BOT_PAD_LAUNCH_SPEED ) {
		return false;	// a new pad impulse can precede the physics ground flag
	}
	if ( navMesh.LandingContinuationClear( self->GetPhysics()->GetOrigin(), path[pathCorner + 1].origin ) ) {
		return false;
	}
	if ( bot_debug.GetInteger() >= 1 ) {
		gameLocal.Printf( "bot %s: replanning unsafe landing at %s toward %s\n", name.c_str(),
			self->GetPhysics()->GetOrigin().ToString( 0 ), path[pathCorner + 1].origin.ToString( 0 ) );
	}
	path.Clear();
	pathCorner = 0;
	pathTime = 0;
	ResetTraversal();
	nextGoalSelectTime = Min( nextGoalSelectTime, gameLocal.time );
	idVec3 brake( -velocity.x, -velocity.y, 0.0f );
	if ( brake.Normalize() > 1.0f ) {
		ApplyMove( brake, cmd );
	}
	cmd.upmove = 0;
	return true;
}

/*
================
rvBot::UpdateMovement
================
*/
void rvBot::UpdateMovement( idPlayer *self, usercmd_t &cmd ) {
	const idVec3 origin = self->GetPhysics()->GetOrigin();

	// Only the two deliberate waits below stand watch; everything else, a
	// frame between goals included, keeps the view where it was.
	idleScan = false;

	TrackDamage( self );
	if ( HoldDodgeJump( self, cmd ) ) {
		return;
	}
	if ( GuardLandingHealth( self, cmd ) || AlignJumpDropApproach( self, cmd ) ||
		 GuardLandingContinuation( self, cmd ) || RecoverInAir( self, cmd ) ) {
		return;
	}

// openQ4 BEGIN
	// Liquid comes first. A bot that cannot swim drowns in the first pool it walks into, and one
	// that cannot tell lava from floor stands in it until it dies. This only adds the vertical
	// axis, so the normal pathing below still runs and carries the bot out horizontally.
	UpdateLiquidMovement( self, cmd );
// openQ4 END

	// Look a short way down hostile projectile trajectories.  This is sampled,
	// not run every frame, and uses relative closest approach rather than a
	// simple radius so rockets travelling safely past do not cause panic.  It
	// runs before the objective hold, because a holding bot is the easiest
	// target on the map and must still get out of the way.
	if ( gameLocal.time >= nextThreatScanTime ) {
		nextThreatScanTime = gameLocal.time + 80;

		idProjectile *threat = NULL;
		float timeToClosest = 0.0f;
		idVec3 closestPoint;
		if ( BotCombatFindIncomingProjectileThreat( self, 0.85f, 52.0f,
											 threat, timeToClosest, closestPoint ) ) {
			const bool alreadyDodging = gameLocal.time < dodgeEndTime;
			ScheduleDodge( threat->GetPhysics()->GetLinearVelocity(), &closestPoint );
			if ( !alreadyDodging && !MistakeActive( BOTMISTAKE_LATEDODGE ) ) {
				// Skilled anticipation begins before impact, but never instantly:
				// dodgeReactMsec still contributes half its authored delay.
				const int predictiveDelay = Min( (int)( traits.dodgeReactMsec * 0.5f ),
											Max( 0, (int)( timeToClosest * 500.0f ) ) );
				dodgeStartTime = gameLocal.time + predictiveDelay;
				dodgeEndTime = dodgeStartTime + BOT_DODGE_MSEC;
			}
		}
	}

	// Holding is an objective action, not an exhausted route.  Keep facing and
	// firing through the normal aim/weapon stages, and keep dodging and moving
	// the feet, but only inside the scoring, thaw or defence volume the rules
	// count: off-mesh recovery and route steering stay switched off.
	if ( AtObjectiveHoldPosition( self ) ) {
		path.Clear();
		pathCorner = 0;
		pathTime = 0;
		repathFailures = 0;
		noRouteSince = 0;
		stuckChecks = 0;
		ResetTraversal();

		idVec3 step;
		bool stepping = DodgeDirection( self, step );
		idPlayer *foe = enemy.GetEntity();
		if ( !stepping && foe && gameLocal.time == enemyLastSeenTime && traits.strafeChance > 0.0f ) {
			idVec3 toFoe = foe->GetPhysics()->GetOrigin() - origin;
			toFoe.z = 0.0f;
			if ( toFoe.Normalize() > 0.0f ) {
				UpdateStrafeRhythm();
				step = toFoe.Cross( idVec3( 0.0f, 0.0f, 1.0f ) ) * ( strafeSide * traits.strafeChance );
				stepping = true;
			}
		}

		if ( stepping ) {
			bool moved = false;
			if ( HoldAllowsMove( self, step ) && BotMoveDirectionSafe( self, step ) ) {
				ApplyMove( step, cmd );
				moved = true;
			} else if ( HoldAllowsMove( self, -step ) && BotMoveDirectionSafe( self, -step ) ) {
				step = -step;
				ApplyMove( step, cmd );
				moved = true;
				strafeSide = -strafeSide;
				dodgeDirection = -dodgeDirection;
			}
			if ( moved && IsDodging( self ) && dodgeJump ) {
				TryDodgeJump( self, step, cmd );
			}
		}
		return;
	}

	bool routing = AdvancePath( self );
	float offHeight = 0.0f;
	if ( routing && OffRoute( self, offHeight ) ) {
		if ( bot_debug.GetBool() ) {
			gameLocal.Printf( "bot %s: off route at %s, %.0f %s corner %d/%d at %s goal %d\n",
							  name.c_str(), origin.ToString( 0 ), idMath::Fabs( offHeight ),
							  ( offHeight < 0.0f ) ? "below" : "above", pathCorner, path.Num(),
							  path[pathCorner].origin.ToString( 0 ), goalType );
		}

		// The goal itself may be a poor one from this floor, so it is decided
		// again on the next frame rather than only rerouted.
		path.Clear();
		pathCorner	= 0;
		pathTime	= 0;
		stuckChecks	= 0;
		ResetTraversal();
		nextGoalSelectTime = Min( nextGoalSelectTime, gameLocal.time );
		routing = false;
	}

	if ( !routing ) {
		// Arrived, or never had a route.  UpdateGoal picks a new one next frame,
		// unless routing itself keeps failing - then the bot is off the mesh and
		// has to walk its own way back on.
		path.Clear();
		pathCorner	= 0;
		pathTime	= 0;
		ResetTraversal();
		if ( !noRouteSince ) {
			noRouteSince = gameLocal.time;
		}

		// A completed pad can leave substantial momentum at the last corner.
		// Route selection and deliberate idle waits must not postpone braking
		// until off-mesh recovery, by which time that momentum has left the floor.
		idVec3 brakeDir = vec3_origin;
		if ( ( BotBrakeAtEdge( self, brakeDir ) && brakeDir.LengthSqr() > 0.0f ) ||
			 ( BotBrakeForTransport( self, brakeDir, -1 ) && brakeDir.LengthSqr() > 0.0f ) ) {
			ApplyMove( brakeDir, cmd );
			cmd.upmove = 0;
			stuckTime = gameLocal.time;
			return;
		}

		// Standing on an item that is still sitting there means it is not one
		// this bot can take - its own CTF flag, or a weapon it already has with
		// full ammo.  Routing to it again would loop forever.
		bool waitingForItem = false;
		if ( goalType == BOTGOAL_ITEM ) {
			idEntity *goal = goalEntity.GetEntity();

			idItem *item = ( goal && goal->IsType( idItem::GetClassType() ) ) ? static_cast<idItem *>( goal ) : NULL;
			const bool stillThere = item && !item->pickedUp && !item->IsHidden();
			idVec3 itemDelta = item ? goal->GetPhysics()->GetOrigin() - origin : vec3_zero;
			const float itemHeight = idMath::Fabs( itemDelta.z );
			itemDelta.z = 0.0f;
			const bool onSpot = item && itemHeight <= BOT_CORNER_REACH_Z * 2.0f &&
				itemDelta.LengthSqr() < BOT_ITEM_ARRIVED * BOT_ITEM_ARRIVED;

			// A timed run ends standing on the spot until the item returns.  The
			// respawn lands on the bot and the pickup fires on the next touch,
			// so the frame it reappears is not "it would not pick up".
			if ( goalItemPending && onSpot ) {
				if ( stillThere ) {
					goalItemPending = false;
					waitingForItem = true;
				} else {
					const int pendingMsec = ItemPendingMsec( item );
					waitingForItem = pendingMsec > 0 && pendingMsec <= BOT_ITEM_MAX_WAIT_MSEC;
				}
			} else if ( stillThere && self->pfl.onGround &&
				 itemHeight <= BOT_CORNER_REACH_Z &&
				 itemDelta.LengthSqr() < BOT_ITEM_ARRIVED * BOT_ITEM_ARRIVED ) {
				if ( bot_debug.GetInteger() >= 1 ) {
					// The entity's own class name, not the C++ class: every
					// pickup is an 'idItem', and a log of those says nothing
					// about which weapon, pack or pedestal keeps refusing.
					gameLocal.Printf( "bot %s: '%s' did not pick up at %s (%.0f away, %.0f up), looking elsewhere\n",
									  name.c_str(), goal->spawnArgs.GetString( "classname", goal->GetClassname() ),
									  goal->GetPhysics()->GetOrigin().ToString( 0 ), itemDelta.Length(),
									  goal->GetPhysics()->GetOrigin().z - origin.z );
				}
				AbandonGoal( BOT_GOAL_AVOID_MSEC );
			}
		}

		// Reached the end of a wander with nothing to fight.  A patient bot
		// stands and watches the room for a moment instead of immediately
		// walking off to the next random point; that, and not a special camping
		// goal, is the whole of what an ambusher does that a roamer does not.
		//
		// Only patience ABOVE the baseline holds at all, so a bot with the
		// default 0.5 behaves exactly as it did before this existed.  The
		// wander-and-recover path below is load bearing - a stranded bot walks
		// its own way back onto the mesh through it - and a personality knob
		// must not be able to starve it, which is why being stranded
		// (repathFailures) cancels the hold outright.
		if ( goalType == BOTGOAL_ROAM && !enemy.GetEntity() && repathFailures < 2 ) {
			const int hold = (int)( Max( 0.0f, traits.patience - 0.5f ) * 2.0f * BOT_PATIENCE_HOLD_MSEC );

			if ( hold > 0 ) {
				if ( !holdUntil ) {
					holdUntil			= gameLocal.time + hold;
					nextGoalSelectTime	= Max( nextGoalSelectTime, holdUntil );
				}
				if ( gameLocal.time < holdUntil && !IsDodging( self ) ) {
					// Standing still deliberately: no movement, and no recovery
					// either, because this bot is not lost.  Incoming fire ends
					// the wait; a camper still gets out of the way of a rocket.
					idleScan = true;
					return;
				}
			}
		}

		// The hold is over, or something cancelled it before it started: an
		// enemy walked in, or the bot turned out to be stranded after all.  The
		// goal scan was deferred to the end of the hold on purpose, because
		// UpdateGoal would otherwise hand out a new wander and end the hold
		// early - but that deferral has to be released with it.  Left standing,
		// it costs the bot up to four seconds in which it cannot chase, cannot
		// go for health and cannot react to an objective change.
		if ( holdUntil ) {
			holdUntil			= 0;
			nextGoalSelectTime	= Min( nextGoalSelectTime, gameLocal.time );
		}

		// A directed dodge needs no route either.
		idVec3 dodgeStep;
		if ( dodgeDirection.LengthSqr() > 0.5f && DodgeDirection( self, dodgeStep ) ) {
			if ( !BotMoveDirectionSafe( self, dodgeStep ) && BotMoveDirectionSafe( self, -dodgeStep ) ) {
				dodgeDirection = -dodgeDirection;
				dodgeStep = -dodgeStep;
			}
			if ( BotMoveDirectionSafe( self, dodgeStep ) ) {
				ApplyMove( dodgeStep, cmd );
				if ( dodgeJump ) {
					TryDodgeJump( self, dodgeStep, cmd );
				}
				return;
			}
		}

		// Combat movement does not require a route.  This matters both on a tiny
		// platform where start and goal collapse to one node and while a failed
		// enemy path is cooling down.  Strafe relative to the enemy, not to an
		// arbitrary old route direction.
		idPlayer *foe = enemy.GetEntity();
		if ( foe && gameLocal.time == enemyLastSeenTime ) {
			idVec3 toFoe = foe->GetPhysics()->GetOrigin() - origin;
			toFoe.z = 0.0f;
			const float range = toFoe.Normalize();
			if ( range > 1.0f ) {
				float want = BotPreferredCombatRange( self, traits ) *
					( Withdrawing( self ) ? BOT_RETREAT_RANGE_SCALE : 1.0f );
				const float err = ( range - want ) / Max( want, 1.0f );
				const float willingness = ( err > 0.0f ) ?
					( 0.5f + traits.aggression ) : ( 1.5f - traits.aggression );
				const float radial = idMath::ClampFloat( -1.0f, 1.0f, err * 2.0f ) *
					traits.rangeDiscipline * willingness;
				const idVec3 combatBase = toFoe * radial;
				idVec3 side = toFoe.Cross( idVec3( 0.0f, 0.0f, 1.0f ) );
				float strafe = traits.strafeChance;
				const bool dodging = IsDodging( self );
				if ( dodging ) {
					strafe = Max( strafe, 0.9f );
				}
				idVec3 combatMove = combatBase;
				combatMove += side * ( strafeSide * strafe );
				combatMove.Normalize();
				bool combatMoveSafe = BotMoveDirectionSafe( self, combatMove );

				// A blocked dodge has two answers.  Try the opposite side before
				// surrendering movement entirely, and remember the side that worked
				// so the next frame does not test the obstruction first again.
				if ( !combatMoveSafe && dodging ) {
					idVec3 alternate = combatBase - side * ( strafeSide * strafe );
					alternate.Normalize();
					if ( BotMoveDirectionSafe( self, alternate ) ) {
						combatMove = alternate;
						combatMoveSafe = true;
						strafeSide = -strafeSide;
					}
				}

				if ( combatMoveSafe ) {
					ApplyMove( combatMove, cmd );
					if ( dodging && dodgeJump ) {
						TryDodgeJump( self, combatMove, cmd );
					}
					return;
				}
			}
		}

		// Holding an item spot is a decision, not being stranded.
		if ( waitingForItem ) {
			noRouteSince = 0;
			idleScan = true;
			return;
		}

		if ( repathFailures >= 2 || gameLocal.time - noRouteSince > BOT_RECOVER_IDLE_MSEC ) {
			RecoverToNavMesh( self, cmd );
		}
		return;
	}

	noRouteSince = 0;

	const navCorner_t &corner = path[pathCorner];

	// The approach to a deliberate action counts as committed, not just the
	// action itself.  AdvancePath tightens the acceptance radius from
	// BOT_CORNER_REACH to BOT_TRAVEL_ENTRY_REACH for exactly this leg, and a
	// jump or a drop registers no traversal state at all while the bot is still
	// walking the corner before it - so without this the combat strafe, the
	// player separation push and the unstick shove all stay live while the bot
	// is trying to hit a twelve unit takeoff point.  A bot under fire then
	// circles the window instead of entering it and fails jumps it makes
	// reliably when nobody is shooting at it.
	const bool actionEntry = ( pathCorner + 1 < path.Num() &&
							   path[pathCorner + 1].travelType != NAVTRAVEL_WALK );
	const bool committedTravel = ( corner.travelType != NAVTRAVEL_WALK ) || actionEntry ||
		( traversalEntered && traversalCorner == pathCorner + 1 );

	idVec3 moveDir = corner.origin - origin;
	moveDir.z = 0.0f;
	idVec3 pressTarget = corner.origin;		// what the feet are pressing toward
	bool steeringIntoVolume = false;

	// Entering a pad or a teleporter means touching the volume, and the node a
	// route enters it by only has to brush it.  At that node, or once the
	// volume has been touched, steer at the volume's own centre so the bot walks
	// into it instead of standing on the edge waiting.  Not sooner: the route
	// approaches a raised pad by its ramp, and a beeline for the centre from a
	// corner away runs into the platform's side.
	if ( pathCorner + 1 < path.Num() && corner.travelType == NAVTRAVEL_WALK ) {
		const navCorner_t &action = path[pathCorner + 1];
		if ( ( action.travelType == NAVTRAVEL_JUMPPAD || action.travelType == NAVTRAVEL_TELEPORT ) &&
			 action.actionEntityNum >= 0 && action.actionEntityNum < MAX_GENTITIES &&
			 ( traversalEntered || moveDir.LengthSqr() < Square( BOT_TRAVEL_ENTRY_REACH ) ) ) {
			idEntity *volume = gameLocal.entities[action.actionEntityNum];
			idBounds bounds;
			bool haveBounds = false;
			if ( volume && volume->IsType( rvJumpPad::GetClassType() ) ) {
				haveBounds = static_cast<rvJumpPad *>( volume )->GetForceFieldBounds( bounds );
			} else if ( volume && volume->GetPhysics() ) {
				bounds = volume->GetPhysics()->GetAbsBounds();
				haveBounds = true;
			}
			if ( haveBounds ) {
				idVec3 toVolume = bounds.GetCenter() - origin;
				toVolume.z = 0.0f;
				if ( toVolume.LengthSqr() > 1.0f ) {
					moveDir = toVolume;
					pressTarget = bounds.GetCenter();
					steeringIntoVolume = true;
				}
			}
		}
	}

	// A drop's landing can lie under the lip of its own ledge.  Cells are 24
	// units apart and a standing box overhangs its floor by half its width, so
	// a bot still on the ledge can already be over the landing node: steering
	// at it then collapses to nothing and the bot stood on the edge until the
	// stuck detector threw the route away.  Within a corner of the landing,
	// keep walking the link's own direction until the bot is actually off the
	// ground.  A jump up onto a lip has the same geometry.
	if ( ( corner.travelType == NAVTRAVEL_DROP || corner.travelType == NAVTRAVEL_JUMP ) &&
		 pathCorner > 0 && self->pfl.onGround ) {
		idVec3 across = corner.origin - path[pathCorner - 1].origin;
		across.z = 0.0f;
		if ( across.Normalize() > 1.0f && moveDir * across < BOT_CORNER_REACH ) {
			moveDir = across;
		}
	}

	// In a pad's flight or a drop's fall, steer the way the navmesh predicted:
	// for the velocity that comes down on the landing, not straight at it.
	// Full air control at the spot overshot it and swung back, so a vertical
	// pad's flight meant for the ledge came down in its shaft again, and a
	// drop beside a pad drifted onto the pad.  No correction needed is no
	// input at all.
	if ( ( corner.travelType == NAVTRAVEL_JUMPPAD || corner.travelType == NAVTRAVEL_DROP ) &&
		 !self->pfl.onGround && traversalStarted ) {
		// Thrown the other way at a pad's speed - a short landing on the pad
		// opposite, a pad the route did not plan - the landing is out of reach,
		// and steering for it only brakes the throw over whatever it crosses:
		// q4ctf5's bots dropped into the void between its facing pads that way.
		// Coast, and replan where it sets down.
		const idVec3 &velocity = self->GetPhysics()->GetLinearVelocity();
		const idVec3 flat( velocity.x, velocity.y, 0.0f );
		const float gravity = gameLocal.GetCurrentGravity( self ).Length();
		idVec3 wish;
		if ( flat * moveDir < 0.0f && flat.LengthSqr() > Square( BOT_PAD_LAUNCH_SPEED * 1.25f ) ) {
			moveDir.Zero();
		} else if ( corner.travelType == NAVTRAVEL_JUMPPAD && BotRidesPadArc( origin, velocity, corner, gravity ) ) {
			// A pad's natural landing is ridden, not steered for: braking onto
			// the node slowed a flyer under the one behind it, and over
			// q4ctf5's gap two bots met in the air ten times a match.
			moveDir.Zero();
		} else if ( NavAirSteer( origin, velocity, corner.origin, gravity, wish ) ) {
			moveDir = wish;
		} else {
			moveDir.Zero();
		}
	}

	const bool holdForPad = HoldForPad( self, moveDir );

	const bool hasMoveDir = ( moveDir.Normalize() > 0.0f );
	const idVec3 routeDir = moveDir;

	bool wantDodgeJump = false;
	bool alternateDodgeValid = false;
	idVec3 alternateDodgeDir;

	// Style decides what fight the bot wants to be in.  combatRange is its
	// authored baseline, the active weapon pulls that toward a useful distance,
	// and rangeDiscipline is how hard it works at the result.  A rusher still
	// closes more eagerly, but no longer tries to use every gun at knife range.
	idPlayer *foe = enemy.GetEntity();

	if ( hasMoveDir && !committedTravel && foe && gameLocal.time == enemyLastSeenTime ) {
		idVec3 toFoe = foe->GetPhysics()->GetOrigin() - origin;
		toFoe.z = 0.0f;

		const float range = toFoe.Normalize();

		// Looking for health means wanting to be further away than usual, which
		// is a change of distance rather than of behaviour: the bot still
		// fights back while it withdraws.
		float want = BotPreferredCombatRange( self, traits );
		if ( Withdrawing( self ) ) {
			want *= BOT_RETREAT_RANGE_SCALE;
		}

		if ( range > 1.0f && want > 1.0f ) {
			const float err = ( range - want ) / want;

			// aggression is the asymmetry, not the strength: a pushy bot closes
			// hard and is reluctant to give ground, a cautious one the reverse.
			// The strength is rangeDiscipline, which is competence.
			const float willingness	= ( err > 0.0f ) ? ( 0.5f + traits.aggression ) : ( 1.5f - traits.aggression );
			const float push		= idMath::ClampFloat( -1.0f, 1.0f, err * 2.0f ) * traits.rangeDiscipline * willingness;

			moveDir += toFoe * push;

			// Sidestep so the bot is not a straight line target.  Inside the
			// fighting band it always dodges a little; once it is actually
			// being hit it dodges properly, after dodgeReactMsec.
			if ( range < want * BOT_STRAFE_RANGE_SCALE ) {
				UpdateStrafeRhythm();

				float strafe = traits.strafeChance;
				const bool dodging = IsDodging( self );

				if ( dodging ) {
					strafe			= Max( strafe, 0.85f );
					wantDodgeJump	= dodgeJump;
				}

				idVec3 side = toFoe.Cross( idVec3( 0.0f, 0.0f, 1.0f ) );
				const idVec3 strafeOffset = side * ( strafeSide * strafe );
				if ( dodging ) {
					alternateDodgeDir = moveDir - strafeOffset;
					alternateDodgeValid = alternateDodgeDir.Normalize() > 0.0f;
				}
				moveDir += strafeOffset;
			}

			moveDir.Normalize();

			// Combat owns the feet - and with them silences the stuck detector
			// and the goal give-up - only once it bends the route materially.
			// Stamping on every frame an enemy was in view left a bot pressed
			// against a lip for the whole of a long-range duel, with nothing to
			// free it until line of sight broke.  Holding range at about the
			// right distance with a light strafe still walks the route, and
			// progress along it is still the right measure of being stuck.
			if ( moveDir * routeDir < BOT_COMBAT_STEER_DOT ) {
				combatSteeringTime = gameLocal.time;
			}
		}
	}

	// A dodge against a known line of travel does not wait for the shooter to
	// be in view or in range: a rocket from behind or the flank is dodged across
	// its own path, with the route kept as a bias rather than abandoned.
	bool directedDodge = false;
	idVec3 dodgeStep;
	if ( hasMoveDir && !committedTravel && dodgeDirection.LengthSqr() > 0.5f &&
		 DodgeDirection( self, dodgeStep ) ) {
		const idVec3 bias = routeDir * 0.35f;
		moveDir = bias + dodgeStep;
		moveDir.Normalize();
		alternateDodgeDir = bias - dodgeStep;
		alternateDodgeValid = alternateDodgeDir.Normalize() > 0.0f;
		wantDodgeJump = dodgeJump;
		directedDodge = true;
		combatSteeringTime = gameLocal.time;
	}

	// Local separation keeps nearby players from becoming one collision blob at
	// a doorway or item.  It is a steering hint only and is suppressed for the
	// precise entry to a jump, pad, drop or teleporter.
	if ( hasMoveDir && !committedTravel ) {
		for ( int i = 0; i < gameLocal.numClients; i++ ) {
			idPlayer *other = gameLocal.GetClientByNum( i );
			if ( !other || other == self || other->spectating || other->health <= 0 ||
				 other->GetInstance() != self->GetInstance() ) {
				continue;
			}

			idVec3 away = origin - other->GetPhysics()->GetOrigin();
			away.z = 0.0f;
			const float distance = away.Normalize();
			if ( distance > 1.0f && distance < 96.0f ) {
				const idVec3 separation = away * ( ( 96.0f - distance ) / 96.0f ) * 0.75f;
				moveDir += separation;
				if ( alternateDodgeValid ) {
					alternateDodgeDir += separation;
				}
			}
		}
		moveDir.Normalize();
		if ( alternateDodgeValid ) {
			alternateDodgeValid = alternateDodgeDir.Normalize() > 0.0f;
		}
	}

	if ( hasMoveDir && !committedTravel && gameLocal.time < unstickUntil ) {
		// Push sideways out of whatever the bot walked into.
		idVec3 side = moveDir.Cross( idVec3( 0.0f, 0.0f, 1.0f ) );
		moveDir += side * unstickSide;
		moveDir.Normalize();
		if ( alternateDodgeValid ) {
			alternateDodgeDir += side * unstickSide;
			alternateDodgeValid = alternateDodgeDir.Normalize() > 0.0f;
		}
	}

	// The nav direction is already floor-validated.  Any combat, separation or
	// recovery alteration must earn the same confidence before replacing it.
	if ( hasMoveDir && !committedTravel &&
		 ( moveDir - routeDir ).LengthSqr() > 0.01f && !BotMoveDirectionSafe( self, moveDir ) ) {
		if ( alternateDodgeValid && BotMoveDirectionSafe( self, alternateDodgeDir ) ) {
			moveDir = alternateDodgeDir;
			strafeSide = -strafeSide;
			if ( directedDodge ) {
				dodgeDirection = -dodgeDirection;
			}
		} else {
			moveDir = routeDir;
			wantDodgeJump = false;
		}
	}

	// Never walk off into the void.  A route leg is proven, but the box drifts
	// off it - a string-pulled corner, a dodge, a team-mate's shove, a landing
	// at a pad's speed - and on q4ctf5's walkways over space momentum carried
	// bots over the edge ten times a minute.  Look ahead along the velocity:
	// with no floor within a survivable fall there, or a kill volume, brake
	// against the velocity instead.  A jump takeoff and a pad or teleporter
	// entry leave the edge on purpose.
	const bool leavesOnPurpose = corner.travelType == NAVTRAVEL_JUMP ||
		( pathCorner + 1 < path.Num() && path[pathCorner + 1].travelType != NAVTRAVEL_WALK &&
		  path[pathCorner + 1].travelType != NAVTRAVEL_DROP );
	// A strong knock's slide is not the planned departure: on q4dm8 a rocket
	// slid a bot approaching its pad off the platform's far edge unbraked.
	const bool knockSliding = knockbackTime && gameLocal.time - knockbackTime <= BOT_AIR_RECOVER_SLIDE_MSEC;
	if ( hasMoveDir && self->pfl.onGround && ( !leavesOnPurpose || knockSliding ) ) {
		// A deliberate drop crosses a partly supported lip. Do not let the
		// walking guard cancel it when the live drop trajectory is still safe;
		// the graph's old takeoff is insufficient after a sideways displacement.
		idVec3 edgeMove = moveDir;
		if ( BotBrakeAtEdge( self, edgeMove ) ) {
			if ( corner.travelType != NAVTRAVEL_DROP || knockSliding ) {
				moveDir = edgeMove;
			} else if ( !navMesh.DropTakeoffClear( origin, self->GetPhysics()->GetLinearVelocity(),
				corner.origin, moveDir, BotMaxSafeFallDelta( self ) ) ) {
				// A full-speed walk-off may overshoot onto a neighbouring pad.
				// Prove a slower departure instead of alternating run and brake
				// forever. Air steering still uses its normal full input.
				const idVec3 careful = moveDir * 0.25f;
				moveDir = navMesh.DropTakeoffClear( origin, self->GetPhysics()->GetLinearVelocity(),
					corner.origin, careful, BotMaxSafeFallDelta( self ) ) ? careful : edgeMove;
			}
		}
	}
	// Nor is a held approach: waiting for q4ctf3's func_jumppad_20 corridor,
	// which overhangs a kill pit, a bot's braking and separation carried it
	// over the side. The hold may have no input at all, so guard its momentum.
	bool heldEdgeBrake = false;
	if ( holdForPad && self->pfl.onGround ) {
		idVec3 heldDir = hasMoveDir ? moveDir : vec3_origin;
		if ( BotBrakeAtEdge( self, heldDir ) ) {
			moveDir = heldDir;
			heldEdgeBrake = true;
		}
	}
	// Nor into a pad or teleporter the route did not choose; a held approach
	// is already braking and only its own pad is ahead of it.
	const bool transportBrake = self->pfl.onGround && !holdForPad &&
		BotBrakeForTransport( self, moveDir, BotPlannedTransport( path, pathCorner ) );

	if ( HandleJumpTakeoff( self, cmd ) ) {
		return;
	}
	if ( ( hasMoveDir || transportBrake || heldEdgeBrake ) && moveDir.LengthSqr() > 0.0f ) {
		ApplyMove( moveDir, cmd );
	}

	// A rise the navmesh walks onto in one 24-unit stride can still stop the
	// player's own five-unit steps: the bevelled side of a pad's ramp, a lip, a
	// team-mate on the stair.  So can the ramp itself between a pad's entry
	// node and its trigger.  Pressing along the route toward a corner above the
	// feet, or into the volume, without closing in is what a player hops out
	// of, at once.  The generic unstick hop waits for the stuck detector and
	// stays off on the approach to an action, and q4dm1's pad ramps held bots
	// there until the route was thrown away. A planned volume entry retains
	// its transport controller; ordinary climbs must prove their own flight.
	bool climbHop = false;
	if ( hasMoveDir && !holdForPad && self->pfl.onGround && corner.travelType == NAVTRAVEL_WALK &&
		 ( steeringIntoVolume || corner.origin.z - origin.z > BOT_CLIMB_RISE ) &&
		 ( moveDir - routeDir ).LengthSqr() <= 0.01f ) {
		idVec3 toTarget = pressTarget - origin;
		toTarget.z = 0.0f;
		const float distance = toTarget.Length();
		if ( climbCheckTime <= 0 || ( pressTarget - climbCheckTarget ).LengthSqr() > 1.0f ||
			 distance + BOT_CLIMB_PROGRESS < climbCheckDistance ) {
			climbCheckTarget	= pressTarget;
			climbCheckDistance	= distance;
			climbCheckTime		= gameLocal.time;
		} else {
			climbHop = gameLocal.time - climbCheckTime >= BOT_CLIMB_HOP_MSEC;
		}
	} else if ( self->pfl.onGround ) {
		// Airborne keeps the window: a hop that lands back short hops again.
		climbCheckTime = 0;
	}

	// A special link owns its input until its exit.  Ordinary walking may still
	// use the generic obstruction and combat-dodge jumps, but those must not
	// perturb drops, jump pads, or teleport entries.
	if ( corner.travelType == NAVTRAVEL_JUMP || ( climbHop && steeringIntoVolume ) ) {
		PressJump( cmd );
	} else if ( climbHop || ( !committedTravel && ( gameLocal.time < unstickUntil || wantDodgeJump ) ) ) {
		// The climb window survives a short fall and can still be pending on
		// touchdown. Its old target says nothing about the current momentum:
		// CTF5 hopped west while sliding east off the walkway. Use the same
		// exact-velocity proof and held input as other discretionary hops.
		TryDodgeJump( self, moveDir, cmd );
	}

	// Stuck detection is horizontal: jumping in place must not count as route
	// progress.  Give a special traversal time to complete, then require two
	// consecutive failed samples before discarding its route.
	if ( gameLocal.time - stuckTime > BOT_STUCK_CHECK_MSEC ) {
		const bool actionCurrent = ( traversalCorner == pathCorner &&
									 corner.travelType != NAVTRAVEL_WALK );
		const int traversalMsec = gameLocal.time - traversalStartTime;
		const bool transportInProgress = actionCurrent &&
			( ( corner.travelType == NAVTRAVEL_JUMPPAD && !self->pfl.onGround ) ||
			  ( corner.travelType == NAVTRAVEL_TELEPORT && self->IsInTeleport() ) );
		const bool traversalTimedOut = actionCurrent &&
			traversalMsec > ( ( corner.travelType == NAVTRAVEL_JUMPPAD ) ? BOT_PAD_TIMEOUT_MSEC : BOT_TRAVEL_TIMEOUT_MSEC );
		const bool traversalGrace = committedTravel &&
								   ( traversalCorner == pathCorner ||
									 ( traversalEntered && traversalCorner == pathCorner + 1 ) ) &&
								   traversalMsec <= BOT_TRAVEL_COMMIT_MSEC;
		idVec3 cornerDelta = corner.origin - origin;
		cornerDelta.z = 0.0f;
		const float cornerDistance = cornerDelta.Length();
		const bool routeProgress = pathCorner > stuckPathCorner ||
			( pathCorner == stuckPathCorner &&
			  cornerDistance + BOT_GOAL_PROGRESS_EPSILON < stuckCornerDistance );

		if ( traversalTimedOut ) {
			if ( bot_debug.GetBool() ) {
				gameLocal.Printf( "bot %s: %s traversal timed out at %s toward %s, %s\n",
								  name.c_str(), BotTravelName( corner.travelType ), origin.ToString( 0 ),
								  corner.origin.ToString( 0 ), self->pfl.onGround ? "on the ground" : "airborne" );
			}

			path.Clear();
			pathTime = 0;
			ResetTraversal();
			stuckChecks = 0;
		} else if ( transportInProgress || traversalGrace || combatSteeringTime > stuckTime ) {
			// A window in which combat footwork owned the feet - holding range,
			// strafing, dodging - says nothing about the route.  Counting it
			// threw away good routes mid-fight and forced a repath every second
			// and a half of every engagement.
			stuckChecks = 0;
		} else if ( !routeProgress ) {
			stuckChecks++;
			unstickUntil	= gameLocal.time + BOT_UNSTICK_MSEC;
			unstickSide		= ( gameLocal.random.RandomFloat() < 0.5f ) ? -1.0f : 1.0f;

			// Two failures in a row means the route itself is bad.
			if ( stuckChecks >= 2 && path.Num() > 0 ) {
				if ( bot_debug.GetBool() ) {
					static const char *travelNames[] = { "walk", "drop", "jump", "jumppad", "teleport" };
					// The leg the bot could not finish, and whether the navmesh
					// would still accept it as a walk: a leg it still accepts
					// is a shortcut proof at fault, one it refuses came from
					// somewhere else - a graph link, or ground that has moved.
					const idVec3 &legStart = ( pathCorner > 0 ) ? path[pathCorner - 1].origin : path.RouteStart();
					const char *verdict = "-";
					if ( corner.travelType == NAVTRAVEL_WALK ) {
						verdict = "walkable";
						navMesh.WalkLegAccepted( legStart, corner.origin, &verdict );
					}
					gameLocal.Printf( "bot %s: stuck at %s corner %d/%d %s at %s (%.0f away, %.0f up) goal %d, leg from %s %s\n",
									  name.c_str(), origin.ToString( 0 ), pathCorner, path.Num(),
									  travelNames[idMath::ClampInt( 0, NAVTRAVEL_NUM - 1, corner.travelType )],
									  corner.origin.ToString( 0 ), cornerDistance, corner.origin.z - origin.z,
									  goalType, legStart.ToString( 0 ), verdict );
				}

				path.Clear();
				pathTime = 0;
				ResetTraversal();
				stuckChecks = 0;
			}
		} else {
			stuckChecks = 0;
		}

		stuckTime	= gameLocal.time;
		stuckPathCorner = pathCorner;
		stuckCornerDistance = cornerDistance;
	}
}

/*
================
rvBot::ProjectileSpeed

Forward launch speed of whatever is in the bot's hands, including arcing
projectiles; UpdateAim applies their gravity separately.
================
*/
float rvBot::ProjectileSpeed( idPlayer *self ) const {
	if ( !self->weapon ) {
		return 0.0f;
	}

	// The test is the presence of def_projectile, NOT rvWeapon::wfl.
	// attackHitscan.  The gauntlet and the lightning gun set neither
	// def_projectile nor def_hitscan and trace for themselves, so attackHitscan
	// is false for both even though both are instant hit - a bot that led with
	// a lightning gun would miss with it every time.  rvWeapon::spawnArgs
	// rather than the weapon decl, because that is the only view of the keys
	// with the weapon mods applied.
	const char *projName = self->weapon->spawnArgs.GetString( "def_projectile", "" );
	if ( !projName[0] ) {
		return 0.0f;
	}

	// Resolved through gameLocal so the multiplayer retune applies:
	// idGameLocal::FindEntityDef silently prefers <name>_mp in multiplayer and
	// every MP projectile is faster than its single player original - rocket
	// 935 not 900, nail 1400 not 1200.  A hardcoded table would lead short on
	// every shot.
	const idDict *dict = gameLocal.FindEntityDefDict( projName, false );
	if ( !dict ) {
		return 0.0f;
	}

	// Multiplayer forbids speed ramps outright - idProjectile::Launch asserts
	// !gameLocal.isServer when speed_end is present - so this single constant
	// is the whole flight model.
	return idProjectile::GetVelocity( dict ).x;
}

/*
================
rvBot::ProjectileGravity
================
*/
idVec3 rvBot::ProjectileGravity( idPlayer *self ) const {
	if ( !self->weapon ) {
		return vec3_zero;
	}

	const char *projName = self->weapon->spawnArgs.GetString( "def_projectile", "" );
	if ( !projName[0] ) {
		return vec3_zero;
	}
	const idDict *dict = gameLocal.FindEntityDefDict( projName, false );
	return dict ? idProjectile::GetGravity( dict ) : vec3_zero;
}

/*
================
BotFeetAimPoint

Where a splash weapon should be pointed at a grounded opponent: the floor at
their feet.  Refused when the opponent is airborne (the blast would miss under
them), when there is no floor close beneath them, when the point is hidden
from the shooter, or when it is close enough for the blast to come back.
================
*/
static bool BotFeetAimPoint( idPlayer *self, idPlayer *foe, const idVec3 &eye, idVec3 &feet ) {
	const float radius = BotWeaponSplashRadius( self );
	if ( radius <= 0.0f || !foe->pfl.onGround ) {
		return false;
	}

	const idVec3 foeOrigin = foe->GetPhysics()->GetOrigin();
	if ( ( foeOrigin - eye ).LengthSqr() < Square( radius * 1.25f + 48.0f ) ) {
		return false;
	}

	trace_t floor;
	gameLocal.TracePoint( self, floor, foeOrigin + idVec3( 0.0f, 0.0f, 8.0f ),
						  foeOrigin - idVec3( 0.0f, 0.0f, 24.0f ), MASK_SOLID, foe );
	if ( floor.fraction >= 1.0f || floor.c.normal.z < 0.7f ) {
		return false;
	}

	feet = floor.endpos + idVec3( 0.0f, 0.0f, 2.0f );

	trace_t view;
	gameLocal.TracePoint( self, view, eye, feet, MASK_SHOT_BOUNDINGBOX, self );
	if ( view.fraction >= 0.98f ) {
		return true;
	}
	if ( gameLocal.GetTraceEntity( view ) == foe ) {
		return true;
	}

	return ( view.endpos - feet ).LengthSqr() <= Square( 16.0f );
}

/*
================
rvBot::IdleAimPoint

With nothing to shoot at, a player looks where they are going, not at their
feet.  The next corner can be a step away and well under the eye line, and a
bot that aimed at it walked every tight route staring at the floor.  The view
rests a good way along the route instead, at eye height over the floor there,
so stairs and ramps tilt it and a corridor keeps it level.  Standing watch -
a patient bot on a finished wander, or one waiting for a timed item - it
sweeps the room either side of the facing it stopped with, which is also what
lets an ambusher notice someone coming in from the flank.  Movement is never
affected: ApplyMove resolves a world-space direction against whatever the
view yaw is.
================
*/
idVec3 rvBot::IdleAimPoint( idPlayer *self, const idVec3 &eye ) {
	const idVec3 origin = self->GetPhysics()->GetOrigin();
	const float eyeHeight = eye.z - origin.z;

	if ( pathCorner >= 0 && pathCorner < path.Num() ) {
		scanStartTime = 0;

		idVec3 from = origin;
		float remaining = BOT_LOOK_AHEAD;
		for ( int i = pathCorner; i < path.Num(); i++ ) {
			const idVec3 &to = path[i].origin;
			idVec3 leg = to - from;
			leg.z = 0.0f;
			const float length = leg.Length();
			if ( length > 0.0f && length >= remaining ) {
				idVec3 point = from + ( to - from ) * ( remaining / length );
				point.z += eyeHeight;
				return point;
			}
			remaining -= length;
			from = to;
		}

		// A short route: watch where it ends.
		idVec3 point = path[path.Num() - 1].origin;
		point.z += eyeHeight;
		return point;
	}

	if ( !idleScan ) {
		scanStartTime = 0;
		return eye + aimAngles.ToForward() * 256.0f;
	}

	// The sweep starts from the current facing so the view never jumps, and
	// neighbouring bots start out opposite ways so a pair standing together
	// does not turn in step.
	if ( !scanStartTime ) {
		scanStartTime	= gameLocal.time;
		scanBaseYaw		= aimAngles.yaw;
	}
	const float sweep	= idMath::TWO_PI * MS2SEC( gameLocal.time - scanStartTime ) / BOT_SCAN_PERIOD_SEC;
	const float side	= ( clientNum & 1 ) ? -1.0f : 1.0f;
	const idAngles look( 0.0f, scanBaseYaw + side * BOT_SCAN_HALF_ANGLE * idMath::Sin( sweep ), 0.0f );

	return eye + look.ToForward() * 256.0f;
}

/*
================
rvBot::UpdateAim

The bot aims at where it BELIEVES the target is, which trails the truth; the
belief is led for projectile flight, offset by the target's angular rate, and
shaken by a continuous tremor; and the view chases that with a damped
second-order slew rather than a straight clamp, which is what produces the
overshoot and hunting at low skill and the clean settle at high skill.
================
*/
void rvBot::UpdateAim( idPlayer *self, usercmd_t &cmd ) {
	idVec3	eye;
	idMat3	axis;

	self->GetViewPos( eye, axis );

	// A frame hitch must not teleport the view: everything below integrates.
	const float	dt	= idMath::ClampFloat( 0.001f, 0.1f, MS2SEC( gameLocal.GetMSec() ) );
	idPlayer *	foe	= enemy.GetEntity();

	aimPointValid = false;

	if ( foe ) {
		const idBounds &bounds = foe->GetPhysics()->GetAbsBounds();

		idVec3 truePos = bounds.GetCenter();
		const float heightBias = BOT_AIM_HEIGHT_BIAS * ( traits.weaponSkill * 2.0f - 1.0f ) + traits.aimHeight;

		truePos.z = idMath::ClampFloat( bounds[0].z + 1.0f, bounds[1].z - 1.0f,
									   truePos.z + ( bounds[1].z - bounds[0].z ) * heightBias );

		// The belief only follows what the bot can actually see.  An enemy is
		// remembered for a while after it breaks line of sight, and a belief
		// that kept updating through the wall would have the bot tracking you
		// perfectly while you were behind it and already on target the instant
		// you stepped out.  Out of sight, the aim coasts on the last position
		// it had - which is exactly what the word believed means.
		const bool foeVisible = ( gameLocal.time == enemyLastSeenTime );
		if ( foeVisible ) {
			// Use the exposed chest/head/pelvis sample found by perception rather
			// than aiming back into the covered centre of the model.  Keep a
			// reduced character/skill height bias within the exposed body.
			truePos = enemyVisiblePoint;
			truePos.z = idMath::ClampFloat( bounds[0].z + 1.0f, bounds[1].z - 1.0f,
				truePos.z + ( bounds[1].z - bounds[0].z ) * heightBias * 0.35f );

			// Splash weapons are aimed at the floor under a grounded target: a
			// near miss still lands in the blast instead of sailing past.  Only
			// when the impact point itself is in view and far enough away that
			// the blast cannot reach the shooter.
			idVec3 feet;
			if ( aimAtFeet && BotFeetAimPoint( self, foe, eye, feet ) ) {
				truePos = feet;
			}
		}

		// First order lag.  aimTrackTimeConst seconds after the target changes
		// direction the bot is still aiming most of the way back where it was.
		if ( !aimBeliefValid ) {
			if ( foeVisible ) {
				aimBelief = truePos;
			} else {
				// A contact known only by sound starts from where it was heard,
				// never from its live position behind the wall.
				aimBelief = enemyLastSeenOrigin;
				aimBelief.z += ( bounds[1].z - bounds[0].z ) * BOT_AIM_CHEST_FRACTION;
			}
			// Where someone is registers on sight; how they are moving takes a
			// moment longer to read.
			aimPerceivedVelocity.Zero();
			aimBeliefValid	= true;
		} else if ( foeVisible && !MistakeActive( BOTMISTAKE_LOSETRACK ) ) {
			// A lag on its own trails a steady runner by speed times the time
			// constant for as long as the run lasts - which put even skill 5
			// off the hull of anyone moving.  The motion the bot has registered
			// carries the belief forward first, by aimAnticipation, and the lag
			// then corrects whatever that got wrong.  The registered velocity
			// catches up with a change of direction in about half a reaction,
			// so a target that reverses is trailed for a moment at every skill:
			// that is the miss a juking player earns.
			const float perceive = Max( MS2SEC( traits.reactionMsec ) * BOT_AIM_PERCEIVE_FRACTION, 0.01f );
			aimPerceivedVelocity += ( foe->GetPhysics()->GetLinearVelocity() - aimPerceivedVelocity ) *
									( 1.0f - idMath::Exp( -dt / perceive ) );
			aimBelief += aimPerceivedVelocity * ( traits.aimAnticipation * dt );

			const float alpha = 1.0f - idMath::Exp( -dt / Max( traits.aimTrackTimeConst, 0.01f ) );

			aimBelief += ( truePos - aimBelief ) * alpha;
		} else if ( !MistakeActive( BOTMISTAKE_LOSETRACK ) ) {
			// Briefly extrapolate the last observed velocity, with confidence
			// fading to zero.  This predicts a doorway exit without tracking live
			// physics through the wall.  pursuit runs to 2 for how long a chase
			// lasts; as a gain on the target's speed it stops at 1, or a hunter
			// led a door exit at 1.4 times the speed it saw.
			const float unseen = MS2SEC( gameLocal.time - enemyLastSeenTime );
			const float confidence = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - unseen / 0.75f );
			aimBelief += enemyLastSeenVelocity * ( dt * Min( 1.0f, traits.pursuit ) * confidence );
		}

		aimPoint = aimBelief;

		const float speed = ProjectileSpeed( self );

		if ( speed > 0.0f ) {
			// Led with the motion the bot has registered, not the live one, so a
			// rocket fired just after a reversal goes where the target was going.
			const idVec3 foeVel = foeVisible ? aimPerceivedVelocity : enemyLastSeenVelocity;
			const idVec3 ownVel = self->GetPhysics()->GetLinearVelocity();
			const idVec3 gravity = ProjectileGravity( self );

			// Compensating for its own motion is the same skill as leading the
			// target, so it rides the same knob rather than a second one.
			const idVec3 relVel = foeVel - ownVel * ( 1.0f - idMath::ClampFloat( 0.0f, 1.0f, traits.aimLead ) );

			// Two fixed point iterations is plenty: the flight time changes
			// very little once the lead is roughly right.
			idVec3	lead;
			idVec3	gravityCompensation;
			float	flight = ( aimBelief - eye ).Length() / speed;

			// Drop is compensated in full at every skill: holding a grenade
			// launcher level is not a subtle skill, and under-compensating
			// landed a novice's grenades so short that the usefulness proof
			// refused nearly every one.  Skill shows in the lead instead.
			for ( int i = 0; i < 3; i++ ) {
				lead	= relVel * ( flight * traits.aimLead * aimLeadRoll );
				gravityCompensation = gravity * ( -0.5f * flight * flight );
				flight	= ( aimBelief + lead + gravityCompensation - eye ).Length() / speed;
			}

			aimPoint = aimBelief + lead + gravityCompensation;
		}

		aimPointValid = true;
	} else {
		aimPoint = IdleAimPoint( self, eye );
	}

	idVec3 dir = aimPoint - eye;
	if ( dir.Normalize() <= 0.0f ) {
		// Leave the view where it is.  rvBot::Think carries last frame's
		// cmd.angles across its memset for exactly this case, and the player
		// re-latches its delta from them, so the view holds instead of snapping.
		return;
	}

	idAngles pointAngles = dir.ToAngles();
	pointAngles.roll = 0.0f;

	// Tracking error: a lag ACROSS the view rather than noise around it.  The
	// faster the target crosses, the further behind the bot's aim sits, which
	// is why a strafing player is harder for a bot to hit than a charging one.
	float errPitch	= 0.0f;
	float errYaw	= 0.0f;

	if ( aimTrackValid ) {
		const float dp	= idMath::AngleDelta( pointAngles.pitch, aimTrackAngles.pitch );
		const float dy	= idMath::AngleDelta( pointAngles.yaw, aimTrackAngles.yaw );
		const float mag	= idMath::Sqrt( dp * dp + dy * dy );

		aimAngVel += ( mag / dt - aimAngVel ) * BOT_ANGVEL_FILTER;

		// What the slew pursues.  A swing past BOT_AIM_RATE_JUMP in one frame is
		// a new aim point, not motion, and pursuing it would fling the view.
		if ( mag / dt > BOT_AIM_RATE_JUMP ) {
			aimTargetRate.Zero();
		} else {
			aimTargetRate.pitch	+= ( dp / dt - aimTargetRate.pitch ) * BOT_ANGVEL_FILTER;
			aimTargetRate.yaw	+= ( dy / dt - aimTargetRate.yaw ) * BOT_ANGVEL_FILTER;
		}

		if ( foe && mag > idMath::FLOAT_EPSILON ) {
			const float lag = traits.aimTrackError * aimAngVel * 0.01f;

			errPitch	= -( dp / mag ) * lag;
			errYaw		= -( dy / mag ) * lag;
		}
	} else {
		aimTargetRate.Zero();
	}

	aimTrackAngles	= pointAngles;
	aimTrackValid	= true;

	// Tremor: continuous and smooth, so it reads as a hand rather than as the
	// white noise a re-rolled step offset produced.  Two octaves, four phases,
	// all seeded from the client number.
	const float t			= MS2SEC( gameLocal.time );
	const float w			= idMath::TWO_PI * traits.aimTremorRateHz;
	const float tremorPitch	= traits.aimTremorDeg * ( 0.65f * idMath::Sin( w * t + tremorPhase[0] ) +
													  0.35f * idMath::Sin( BOT_TREMOR_OCTAVE * w * t + tremorPhase[1] ) );
	const float tremorYaw	= traits.aimTremorDeg * ( 0.65f * idMath::Sin( w * t + tremorPhase[2] ) +
													  0.35f * idMath::Sin( BOT_TREMOR_OCTAVE * w * t + tremorPhase[3] ) );

	idAngles desired = pointAngles;

	desired.pitch	+= errPitch + tremorPitch;
	desired.yaw		+= errYaw + tremorYaw;

	// Damped second order slew.  turnAccel over turnSpeed is the natural
	// frequency, which keeps the two knobs consistent: raising the cap without
	// raising the acceleration makes a bot that is quick but sloppy.
	float wn = traits.turnAccel / Max( traits.turnSpeed, 1.0f );

	// See BOT_TURN_STABLE_WN: content can ask for a frequency this explicit
	// integrator cannot represent, and the failure mode is a spinning view.
	wn = Min( wn, BOT_TURN_STABLE_WN / dt );

	// In a fight the damping acts on the view's speed relative to the aim
	// point's, not on its speed at all.  Damped against standing still, the
	// slew trailed anything crossing the view by 2 x damping / wn seconds of
	// its motion - a fifth of a second at skill 5, two body widths on a
	// strafer at 600 units - and the cone gate refused every shot it would
	// have missed.  Following the point is smooth pursuit; skill is in how well
	// the point itself is placed, and in the overshoot of the corrections.
	const idAngles pursuitRate = foe ? aimTargetRate : ang_zero;

	for ( int i = 0; i < 2; i++ ) {
		const float err		= idMath::AngleDelta( desired[i], aimAngles[i] );
		const float accel	= wn * wn * err - 2.0f * traits.turnDamping * wn * ( aimRate[i] - pursuitRate[i] );

		aimRate[i]		= idMath::ClampFloat( -traits.turnSpeed, traits.turnSpeed, aimRate[i] + accel * dt );
		aimAngles[i]	= idMath::AngleNormalize180( aimAngles[i] + aimRate[i] * dt );
	}

	// The pitch stop has to kill the rate as well, or the slew winds up against
	// it and then unwinds all at once when the target moves back into range.
	if ( aimAngles.pitch > 85.0f || aimAngles.pitch < -85.0f ) {
		aimAngles.pitch	= idMath::ClampFloat( -85.0f, 85.0f, aimAngles.pitch );
		aimRate.pitch	= 0.0f;
	}

	aimAngles.roll	= 0.0f;
	aimRate.roll	= 0.0f;

	// The player rebuilds its view as SHORT2ANGLE( cmd.angles ) + deltaViewAngles,
	// so the delta has to come back out here.
	for ( int i = 0; i < 3; i++ ) {
		cmd.angles[i] = ANGLE2SHORT( aimAngles[i] - self->GetDeltaViewAngles()[i] );
	}
}

/*
================
rvBot::UpdateBuying

With buying enabled the map's weapon, ammunition and armour pickups are
removed, so a bot that never spent its credits played every life with the
spawn loadout.  It shops the way a sensible player does, through the same
server function the buy impulse reaches and with the same price and
availability checks: the weapon it likes best and can afford when it has
nothing real, armour when it is thin, a refill when a gun runs dry, and with
credits to spare a second weapon or, in a team game, a team powerup.
================
*/
void rvBot::UpdateBuying( idPlayer *self ) {
	if ( gameLocal.time < nextBuyTime ) {
		return;
	}
	// A purchase a second at most, staggered by client, and checked far less
	// often when the mode has no buying at all.
	nextBuyTime = gameLocal.time + 900 + ( clientNum & 7 ) * 37;

	if ( !self->CanBuy() ) {
		nextBuyTime += 4000;
		return;
	}

	// The best weapon this bot does not have and can afford, by its fight
	// value and the personality's opinion of it.
	const char *bestWeapon = NULL;
	float bestWeaponScore = 0.0f;
	int bestWeaponCost = 0;
	for ( int i = 0; i < BOT_NUM_WEAPON_RANGES; i++ ) {
		const char *weaponName = botWeaponRanges[i].name;
		if ( self->ItemBuyStatus( weaponName ) != IBS_CAN_BUY ) {
			continue;
		}
		const float score = BotWeaponPower( weaponName ) *
			rvBotCharacterManager::WeaponBias( traits, weaponName );
		if ( score > bestWeaponScore ) {
			bestWeaponScore = score;
			bestWeapon = weaponName;
			bestWeaponCost = self->GetItemCost( weaponName );
		}
	}

	const float cash = self->buyMenuCash;
	const bool underArmed = BestWeaponPower( self ) <= 0.55f;

	if ( bestWeapon && underArmed ) {
		self->AttemptToBuyItem( bestWeapon );
		return;
	}

	if ( self->inventory.armor < 50 ) {
		if ( self->ItemBuyStatus( "item_armor_large" ) == IBS_CAN_BUY ) {
			self->AttemptToBuyItem( "item_armor_large" );
			return;
		}
		if ( self->ItemBuyStatus( "item_armor_small" ) == IBS_CAN_BUY ) {
			self->AttemptToBuyItem( "item_armor_small" );
			return;
		}
	}

	// A refill when the weapon in hand is nearly dry.
	if ( self->weapon ) {
		const char *current = self->spawnArgs.GetString( va( "def_weapon%d", self->GetCurrentWeapon() ), "" );
		const int ammoIndex = self->inventory.AmmoIndexForWeaponClass( current );
		const int ammo = ammoIndex > 0 ? self->inventory.ammo[ammoIndex] : -1;
		const int maxAmmo = ammoIndex > 0 ?
			self->inventory.MaxAmmoForAmmoClass( self, rvWeapon::GetAmmoNameForIndex( ammoIndex ) ) : 0;
		if ( maxAmmo > 0 && ammo >= 0 && ammo * 4 < maxAmmo &&
			 self->ItemBuyStatus( "ammorefill" ) == IBS_CAN_BUY ) {
			self->AttemptToBuyItem( "ammorefill" );
			return;
		}
	}

	// Spare credits buy a second gun, keeping a reserve for armour next life.
	if ( bestWeapon && cash - (float)bestWeaponCost >= 900.0f ) {
		self->AttemptToBuyItem( bestWeapon );
		return;
	}

	// In a team game a rich bot buys for everyone, as a good team mate does.
	if ( gameLocal.IsTeamGame() && cash >= 5000.0f ) {
		static const struct {
			const char *	item;
			int				powerup;
		} teamPowerups[] = {
			{ "damage_boost",	POWERUP_TEAM_DAMAGE_MOD },
			{ "health_regen",	POWERUP_REGENERATION },
			{ "ammo_regen",		POWERUP_AMMOREGEN }
		};
		for ( int i = 0; i < (int)( sizeof( teamPowerups ) / sizeof( teamPowerups[0] ) ); i++ ) {
			// The team already has it running; buying again only burns credits.
			if ( self->PowerUpActive( teamPowerups[i].powerup ) ) {
				continue;
			}
			if ( self->ItemBuyStatus( teamPowerups[i].item ) == IBS_CAN_BUY ) {
				self->AttemptToBuyItem( teamPowerups[i].item );
				return;
			}
		}
	}
}

/*
================
rvBot::UpdateWeapon
================
*/
void rvBot::UpdateWeapon( idPlayer *self ) {
	if ( gameLocal.time < nextWeaponTime ) {
		return;
	}
	nextWeaponTime = gameLocal.time + Max( 1, (int)traits.weaponSwitchMsec );

	// A wrong-weapon mistake is not a wrong choice, it is no choice: the bot
	// keeps whatever is already in its hands until it notices.
	if ( MistakeActive( BOTMISTAKE_WRONGWEAPON ) ) {
		return;
	}

	idPlayer *	foe		= enemy.GetEntity();

	// With nobody in mind, carry the gun for the fight this personality
	// expects rather than for a thousand units of open ground.  Crossing a
	// map with the rail gun out meant switching when the fight started at
	// close range and losing the first half second of it; a sniper still
	// keeps the rail, a rusher the shotgun, and a baseline bot the rocket
	// launcher or the lightning gun, which is what a player carries.
	float		range	= traits.combatRange;

	if ( foe ) {
		range = ( enemyLastSeenOrigin - self->GetPhysics()->GetOrigin() ).Length();
	}

	// weaponSkill is the chance the range-correct choice gets made at all.  A
	// poor bot takes the first thing it can shoot with, which is exactly what
	// the ordered profile table gives it.
	const bool careful = ( gameLocal.random.RandomFloat() < traits.weaponSkill );

	int		bestSlot	= -1;
	float	bestScore	= 0.0f;
	int		firstSlot	= -1;
	float	currentScore = 0.0f;
	const int currentSlot = self->GetCurrentWeapon();

	float rangeBlend = idMath::ClampFloat( 0.0f, 1.0f,
		( range - BOT_WEAPON_RANGE_SPLIT * 0.5f ) / ( BOT_WEAPON_RANGE_SPLIT * 1.5f ) );
	rangeBlend = rangeBlend * rangeBlend * ( 3.0f - 2.0f * rangeBlend );

	for ( int i = 0; i < BOT_NUM_WEAPON_RANGES; i++ ) {
		const char *weaponName = botWeaponRanges[i].name;
		const int slot = self->GetWeaponIndex( weaponName );

		// GetWeaponIndex answers 0 for anything it does not know, so confirm
		// the slot really is the weapon that was asked for.
		if ( idStr::Icmp( self->spawnArgs.GetString( va( "def_weapon%d", slot ), "" ), weaponName ) != 0 ) {
			continue;
		}
		if ( !( self->inventory.weapons & ( 1 << slot ) ) ) {
			continue;
		}
		if ( !self->inventory.HasAmmo( weaponName ) ) {
			continue;
		}

		if ( firstSlot == -1 ) {
			firstSlot = slot;
		}

		float suitability = botWeaponRanges[i].closeScore +
			( botWeaponRanges[i].farScore - botWeaponRanges[i].closeScore ) * rangeBlend;

		// A weapon cannot be chosen for a fight it cannot reach: a lightning gun
		// at 1000 units spends its cells on empty air.
		const botWeaponInfo_t &info = BotWeaponInfo( i );
		if ( info.reach > 0.0f && range > info.reach ) {
			suitability *= ( info.reach < 128.0f ) ? 0.05f : 0.15f;
		}

		// Splash inside its own blast radius is a liability; the final fire
		// trace is a second line of defence, but choosing a safer gun is better
		// than holding a rocket and declining every shot.  The radius is the
		// weapon's own, so the Dark Matter Gun's huge blast keeps it for range.
		if ( info.splashRadius > 0.0f && range < info.splashRadius * 1.25f ) {
			suitability *= 0.2f;
		}

		// The gauntlet is the last resort: worth drawing only in reach and
		// still worse than a loaded shotgun there.
		if ( info.reach > 0.0f && info.reach < 128.0f && range <= info.reach ) {
			suitability = Max( suitability, 6.0f );
		}

		const float score = suitability * rvBotCharacterManager::WeaponBias( traits, weaponName );
		if ( slot == currentSlot ) {
			currentScore = score;
		}

		if ( score > bestScore ) {
			bestScore	= score;
			bestSlot	= slot;
		}
	}

	int want = bestSlot;
	if ( !careful && currentScore > 0.0f ) {
		// The mistake is inertia, not a deterministic preference for whichever
		// weapon happens to be first in the table.
		want = currentSlot;
	} else if ( want == -1 ) {
		want = firstSlot;
	}
	if ( want == -1 ) {
		return;
	}

	if ( want != currentSlot ) {
		// Minimum dwell and a score margin stop range-boundary oscillation.  An
		// empty/invalid current weapon has currentScore zero and switches at once.
		if ( currentScore > 0.0f &&
			 ( gameLocal.time - lastWeaponSwitchTime < 850 || bestScore < currentScore * 1.18f ) ) {
			return;
		}
		self->SelectWeapon( want, false );
		lastWeaponSwitchTime = gameLocal.time;
	}
}

/*
================
rvBot::BurstAllows

Trigger discipline for automatic weapons: a burst, then a pause long enough for
the aim to re-settle.  Low skill holds the trigger down for over a second at a
time and hits nothing with most of it; high skill taps.
================
*/
bool rvBot::BurstAllows( idPlayer *self ) {
	// Anything that fires one shot per press has its own rhythm already and a
	// burst window would only stutter it.  For a rail gun the settle time is
	// the whole of the discipline.
	const float fireRate = self->weapon ? self->weapon->spawnArgs.GetFloat( "fireRate", "0" ) : 0.0f;

	if ( fireRate <= 0.0f || fireRate > BOT_AUTO_FIRE_RATE ) {
		return true;
	}

	// Spin-up barrels, beams and the gauntlet are held, not tapped.
	if ( BotCurrentWeaponInfo( self ).sustained ) {
		return true;
	}

	if ( gameLocal.time < burstEndTime ) {
		return true;
	}
	if ( gameLocal.time < burstRestTime ) {
		onTargetTime = 0;
		return false;
	}

	const int spread	= Max( 1, (int)( traits.burstMaxMsec - traits.burstMinMsec ) );
	const int length	= (int)traits.burstMinMsec + gameLocal.random.RandomInt( spread );

	burstEndTime	= gameLocal.time + length;
	burstRestTime	= burstEndTime + (int)traits.burstPauseMsec;

	// The lead error is rolled per burst, not per frame.  A shot that missed in
	// front should keep missing in front until the bot lets go of the trigger
	// and takes another look; re-rolling every frame would average out to a
	// perfect lead over any burst long enough to matter.
	aimLeadRoll = 1.0f + gameLocal.random.CRandomFloat() * traits.aimLeadError;

	return true;
}

/*
================
BotWeaponSplashRadius

Reads the live, MP-resolved projectile and damage definitions.  Weapon mods and
the shipped multiplayer retune therefore feed the same safety radius used by
the actual explosion.
================
*/
static float BotWeaponSplashRadius( idPlayer *self ) {
	if ( !self || !self->weapon ) {
		return 0.0f;
	}

	const char *projectileName = self->weapon->spawnArgs.GetString( "def_projectile", "" );
	const idDict *projectile = projectileName[0] ? gameLocal.FindEntityDefDict( projectileName, false ) : NULL;
	if ( !projectile ) {
		return 0.0f;
	}

	const char *splashName = projectile->GetString( "def_splash_damage", "" );
	const idDict *splash = splashName[0] ? gameLocal.FindEntityDefDict( splashName, false ) : NULL;
	return splash ? Max( 0.0f, splash->GetFloat( "radius", "0" ) ) : 0.0f;
}

/*
================
rvBot::UpdateFire
================
*/
void rvBot::UpdateFire( idPlayer *self, usercmd_t &cmd ) {
	idPlayer *foe = enemy.GetEntity();

	if ( !foe || !aimPointValid || !IsEnemy( self, foe ) ) {
		onTargetTime	= 0;
		wasFiring		= false;
		return;
	}

	// The rules hold every trigger while a round is set up or just decided.
	// Waiting for the release must not bank the reaction and settle time for
	// a free shot on the first unlocked frame.
	rvGameState *state = gameLocal.mpGame.GetGameState();
	if ( state && state->WeaponsLocked() ) {
		enemyFireTime	= Max( enemyFireTime, gameLocal.time +
			(int)( traits.reactionMsec * traits.reacquireFraction ) );
		onTargetTime	= 0;
		wasFiring		= false;
		return;
	}

	// The reaction deadline was computed once, when the target registered.
	if ( gameLocal.time < enemyFireTime ) {
		onTargetTime	= 0;
		wasFiring		= false;
		return;
	}

	// Most personalities only shoot at what they can see.  A suppressive one
	// keeps the trigger down briefly at the last believed position, putting
	// fire through the doorway instead of becoming silent the instant contact
	// crosses it.
	const int unseenMsec = gameLocal.time - enemyLastSeenTime;
	const bool suppressing = enemyLastSeenTime > 0 &&
							 unseenMsec > 0 &&
							 unseenMsec <= (int)traits.suppressionMsec;

	if ( gameLocal.time != enemyLastSeenTime && !suppressing ) {
		onTargetTime	= 0;
		wasFiring		= false;
		return;
	}
	if ( !self->weapon || self->weapon->IsReloading() || self->weapon->IsHolstered() ) {
		onTargetTime = 0;
		wasFiring = false;
		return;
	}

	idVec3	eye;
	idMat3	axis;

	self->GetViewPos( eye, axis );

	// Measured against the aim POINT, not the enemy's centre.  If the two
	// disagreed the bot would aim at where it is leading and then refuse to
	// pull the trigger because the centre of the target was outside the cone.
	idVec3 dir = aimPoint - eye;
	const float shotRange = dir.Normalize();
	if ( shotRange <= 0.0f ) {
		return;
	}

	const float angle			= RAD2DEG( idMath::ACos( idMath::ClampFloat( -1.0f, 1.0f, dir * aimAngles.ToForward() ) ) );
	// The slew that matters is the view's speed relative to the aim point's.
	// A view tracking a strafer at close range turns faster than the hold rate
	// while sitting on it - 230 degrees a second at 80 units - and an absolute
	// gate refused exactly the shots a tracking player takes.
	const float slewPitch		= aimRate.pitch - aimTargetRate.pitch;
	const float slewYaw			= aimRate.yaw - aimTargetRate.yaw;
	const float turnRate		= idMath::Sqrt( slewPitch * slewPitch + slewYaw * slewYaw );
	const float fireCone		= traits.fireConeDeg * ( 0.5f + traits.initiative );
	const float holdTurnRate	= traits.holdFireTurnRate * ( 0.5f + traits.initiative );
	// The settle shrinks with initiative but never to nothing: at 1.0 the old
	// factor reached zero and a rusher fired the frame the cone was entered,
	// whatever its skill.  A quarter of the authored settle is the floor.
	const int settleMsec		= (int)( traits.aimSettleMsec * Max( 0.25f, 2.0f - traits.initiative * 2.0f ) );

	// Two gates, and the second one matters as much as the first: a view still
	// swinging hard is not aimed at anything, it is passing over it.
	if ( angle > fireCone || turnRate > holdTurnRate ) {
		onTargetTime	= 0;
		wasFiring		= false;
		return;
	}

	onTargetTime += gameLocal.GetMSec();

	// A mistimed shot is the bot pulling the trigger before the sight has
	// stopped moving, which is what a rattled player does.
	const bool settled = ( onTargetTime >= settleMsec ) || MistakeActive( BOTMISTAKE_MISTIMEDSHOT );

	if ( !settled ) {
		wasFiring = false;
		return;
	}

	// Nothing past a weapon's reach is worth a round.  rvWeapon::spawnArgs
	// carries the live, mod-resolved value; a gauntlet's is measured from the
	// eye, so the target's half hull is allowed on top.
	const float weaponRange = self->weapon->spawnArgs.GetFloat( "range", "0" );
	if ( weaponRange > 0.0f && weaponRange < BOT_WEAPON_REACH_LIMIT &&
		 shotRange > weaponRange + pm_bboxwidth.GetFloat() * 0.5f ) {
		onTargetTime = 0;
		wasFiring = false;
		return;
	}

	const float fireRate = self->weapon->spawnArgs.GetFloat( "fireRate", "0" );
	const bool sustained = BotCurrentWeaponInfo( self ).sustained;
	const bool singleShot = !sustained && ( fireRate <= 0.0f || fireRate > BOT_AUTO_FIRE_RATE );
	if ( singleShot && ( gameLocal.time < nextSingleShotTime || !self->weapon->IsReady() ) ) {
		wasFiring = false;
		return;
	}

	// Proven along the vector the weapon will actually fire down, which is the
	// view axis - rvWeapon takes its muzzle axis from the player's view - and
	// not along the direction to aimPoint.  The cone gate above deliberately
	// lets those two differ by up to the whole effective fire cone, 18 degrees
	// at skill 1 and 7 at skill 3, so clearing the aim ray says very little
	// about where the round goes: at 400 units, 7 degrees is 49 units, enough
	// to put a teammate squarely on the real trajectory and nowhere near the
	// one that was tested.  aimPoint stays the cone and settle reference above,
	// which is correct and is what makes the bot shoot where it is looking.
	const idVec3 firePoint = eye + aimAngles.ToForward() * shotRange;

	const float splashRadius = BotWeaponSplashRadius( self );
	if ( !BotCombatLineOfFireIsSafe( self, foe, eye, firePoint, splashRadius,
										 !suppressing ) ) {
		wasFiring = false;
		return;
	}

	// Asked last, because BurstAllows is not a query: it opens the burst window,
	// schedules the pause that follows it and rolls this burst's lead error.
	// Asking before the shot has cleared every other gate spends a burst on
	// rounds that are never fired - the window is wall clock, so it drains while
	// the bot is refusing - and leaves the bot owing a pause and a full
	// re-settle for an obstruction that may have lasted half a second.
	if ( !BurstAllows( self ) ) {
		wasFiring = false;
		return;
	}

	cmd.buttons |= BUTTON_ATTACK;

	if ( singleShot ) {
		const int cadence = Max( gameLocal.GetMSec(), (int)( fireRate * 1000.0f * 0.85f ) );
		nextSingleShotTime = gameLocal.time + cadence;
	}

	if ( bot_debugAim.GetInteger() >= 1 && ( !wasFiring || bot_debugAim.GetInteger() >= 2 ) ) {
		if ( bot_debugAim.GetInteger() < 2 || gameLocal.time >= nextAimDebugTime ) {
			nextAimDebugTime = gameLocal.time + 250;

			gameLocal.Printf( "bot %s: %s at %s, cone %.1f/%.1f deg, slew %.0f/%.0f deg/s, settled %d/%d ms, lead x%.2f\n",
							  name.c_str(), suppressing ? "suppress" : "fire",
							  BotDisplayName( foe ),
							  angle, fireCone,
							  turnRate, holdTurnRate,
							  onTargetTime, settleMsec,
							  aimLeadRoll );
		}
	}
	if ( singleShot ) {
		onTargetTime = 0;
		aimLeadRoll = 1.0f + gameLocal.random.CRandomFloat() * traits.aimLeadError;
	}

	wasFiring = true;
}

/*
================
rvBot::QueueChat

Chat is queued, never sent from the event.  A bot that answers a frag on the
frame it landed reads as a script, and a worse player takes longer to type.
================
*/
void rvBot::QueueChat( rvBotChatEvent event, const char *other, const char *weapon, const char *item ) {
	if ( !active || !character ) {
		return;
	}
	if ( !gameLocal.isMultiplayer || gameLocal.isClient || !gameLocal.isServer ) {
		return;
	}
	if ( bot_chat.GetInteger() <= 0 || !bot_characters.GetBool() ) {
		return;
	}
	// A line already being typed is not replaced.  At bot_chat 2 the per-bot
	// throttle (3 s) is shorter than the longest typing delay (5 s), so a
	// frag a few seconds after a death used to overwrite the death line after
	// its throttle stamp had already been spent: one reservation, no line.
	if ( !chatPending.IsEmpty() ) {
		return;
	}

	float chatiness = traits.chatiness;

	if ( bot_chat.GetInteger() >= 2 ) {
		chatiness += BOT_CHAT_CHATTY_BONUS;
	}
	if ( gameLocal.random.RandomFloat() >= chatiness ) {
		return;
	}

	botChatTokens_t tokens;

	idStr mapName = gameLocal.GetMapName();
	mapName.StripPath();
	mapName.StripFileExtension();

	tokens.self		= name;
	tokens.other	= ( other && other[0] ) ? other : "";
	tokens.weapon	= ( weapon && weapon[0] ) ? weapon : "";
	tokens.map		= mapName;
	tokens.item		= ( item && item[0] ) ? item : "";

	idStr line;

	// Silence is a valid answer: a character with nothing to say about an event
	// says nothing, and no fallback line is invented for it.  Asked before the
	// throttle, because AllowChat stamps as it answers and a line that was
	// never going to exist must not spend the bot's next six seconds of voice.
	if ( !botCharacterManager.ChatLine( character, event, tokens, line ) ) {
		return;
	}

	// The engine has no chat flood protection anywhere, and a client whose
	// reliable message queue overflows is dropped by the server - so unbounded
	// bot chatter can kick real players off.  The throttle is the manager's
	// because half of it is server wide.
	if ( !botCharacterManager.AllowChat( clientNum ) ) {
		return;
	}

	chatPending		= line;
	chatTeamOnly	= false;
	chatPendingIsReply = false;
	chatSendTime	= BotChatSendTime( line, traits.chatDelayScale );
}

/*
================
rvBot::QueueOrderAck
================
*/
void rvBot::QueueOrderAck( const char *commander ) {
	if ( !active || !character ) {
		return;
	}
	if ( !gameLocal.isMultiplayer || gameLocal.isClient || !gameLocal.isServer ) {
		return;
	}
	if ( bot_chat.GetInteger() <= 0 || !bot_characters.GetBool() ) {
		return;
	}
	if ( !chatPending.IsEmpty() ) {
		return;		// still typing something else; the order itself is already in force
	}

	botChatTokens_t tokens;
	idStr mapName = gameLocal.GetMapName();
	mapName.StripPath();
	mapName.StripFileExtension();
	tokens.self		= name;
	tokens.other	= ( commander && commander[0] ) ? commander : "";
	tokens.weapon	= "";
	tokens.map		= mapName;
	tokens.item		= "";

	idStr line;
	if ( !botCharacterManager.ChatLine( character, BOTCHAT_ORDER, tokens, line ) ||
		 !botCharacterManager.AllowChat( clientNum ) ) {
		return;
	}

	// Team chat, and marked as a reply so no other bot answers it.
	chatPending		= line;
	chatTeamOnly	= true;
	chatPendingIsReply = true;
	chatSendTime	= BotChatSendTime( line, traits.chatDelayScale );
}

/*
================
rvBot::TryQueueReply

Conversational replies share the event-chat delivery path, but never replace a
line the bot has already started "typing".  Returning success lets the manager
stamp the source player's cooldown only when a real line was reserved.
================
*/
bool rvBot::TryQueueReply( const idStr &normalizedText, bool sourceIsBot, bool addressed,
						   const char *other, bool teamOnly ) {
	if ( !active || !character || !chatPending.IsEmpty() ) {
		return false;
	}
	if ( !gameLocal.isMultiplayer || gameLocal.isClient || !gameLocal.isServer ) {
		return false;
	}
	if ( bot_chat.GetInteger() <= 0 || !bot_characters.GetBool() ) {
		return false;
	}

	float chatiness = traits.chatiness;
	if ( bot_chat.GetInteger() >= 2 ) {
		chatiness += BOT_CHAT_CHATTY_BONUS;
	}
	if ( gameLocal.random.RandomFloat() >= chatiness ) {
		return false;
	}

	botChatTokens_t tokens;
	idStr mapName = gameLocal.GetMapName();
	mapName.StripPath();
	mapName.StripFileExtension();

	tokens.self = name;
	tokens.other = ( other && other[0] ) ? other : "";
	tokens.map = mapName;

	idStr line;
	if ( !botCharacterManager.ReplyLine( character, normalizedText, sourceIsBot, addressed, tokens, line ) ) {
		return false;
	}
	if ( !botCharacterManager.AllowChat( clientNum ) ) {
		return false;
	}

	chatPending = line;
	chatTeamOnly = teamOnly;
	chatPendingIsReply = true;
	chatSendTime = BotChatSendTime( line, traits.chatDelayScale );

	return true;
}

/*
================
rvBot::UpdateChat
================
*/
void rvBot::UpdateChat( idPlayer *self ) {
	// Quake 4 already snapshots isChatting and draws mtr_icon_chatting above
	// living remote players.  Drive that stock path for the whole CPM delay so
	// clients see the same feedback they see while a person composes a line.
	if ( self ) {
		self->isChatting = active && !chatPending.IsEmpty() && gameLocal.time < chatSendTime;
	}

	if ( chatPending.IsEmpty() || gameLocal.time < chatSendTime ) {
		return;
	}

	const idStr line = chatPending;
	const bool pendingTeamOnly = chatTeamOnly;
	const bool wasReply = chatPendingIsReply;

	chatPending.Clear();
	chatTeamOnly = false;
	chatPendingIsReply = false;

	// The bot left, or its slot went away, between queuing the line and saying
	// it.  ProcessChatMessage would drop it anyway; dropping it here says why.
	if ( !self || !active ) {
		return;
	}

	// Event chatter is global, while a reply mirrors the chat it answers.  Keep
	// the game type test because team = true in a free-for-all colours every
	// speaker as a Marine and hides the line from spectators.
	const bool teamOnly = ( pendingTeamOnly && gameLocal.IsTeamGame() );

	gameLocal.mpGame.ProcessChatMessage( clientNum, teamOnly, BotDisplayName( self ), line.c_str(), NULL, !wasReply );
}

/*
================
rvBot::SayFarewell

The one line that cannot wait: the slot is about to be kicked, so a queued
farewell would be dropped with the bot that owed it.
================
*/
void rvBot::SayFarewell( void ) {
	idPlayer *self = GetPlayer();

	if ( !self ) {
		return;
	}

	// Anything still queued is about to be lost with the slot anyway, and
	// forcing it out now would have the bot answer a two second old frag on its
	// way out of the door.  The farewell replaces it rather than queue-jumping.
	chatPending.Clear();
	chatTeamOnly = false;
	chatPendingIsReply = false;

	QueueChat( BOTCHAT_FAREWELL, NULL, NULL, NULL );

	chatSendTime = gameLocal.time;
	UpdateChat( self );
}

/*
================
rvBot::OnKilledEnemy

methodOfDeath below MAX_WEAPONS is a weapon slot on the KILLER, which is this
bot, so its own spawnArgs are what name the weapon.
================
*/
void rvBot::OnKilledEnemy( idPlayer *victim, int methodOfDeath ) {
	idPlayer *self = GetPlayer();

	killStreak++;

	idStr	weaponName;
	bool	gauntlet = false;

	if ( self && methodOfDeath >= 0 && methodOfDeath < MAX_WEAPONS ) {
		const char *weaponClass = self->spawnArgs.GetString( va( "def_weapon%d", methodOfDeath ), "" );

		if ( weaponClass[0] ) {
			gauntlet	= ( idStr::Icmp( weaponClass, "weapon_gauntlet" ) == 0 );
			weaponName	= weaponClass;
			weaponName.StripLeadingOnce( "weapon_" );
		}
	}

	// One line per death, most specific first: a revenge kill is more
	// interesting than a humiliation, and both are more interesting than the
	// third frag in a row.
	rvBotChatEvent event = BOTCHAT_KILL;

	if ( victim && victim->entityNumber == revengeTarget ) {
		event			= BOTCHAT_KILL_REVENGE;
		revengeTarget	= -1;
	} else if ( gauntlet ) {
		event = BOTCHAT_KILL_GAUNTLET;
	} else if ( killStreak >= BOT_KILL_STREAK ) {
		event = BOTCHAT_KILL_STREAK;
	}

	QueueChat( event, BotDisplayName( victim ), weaponName.c_str(), NULL );
}

/*
================
rvBot::OnKilledBy

idPlayer::lastKiller would answer "who killed me" without a hook, but it is
never cleared on a normal respawn, so a bot reading it would owe the same
player a debt forever.  The debt is tracked here instead, and paid off the
moment it is collected.
================
*/
void rvBot::OnKilledBy( idPlayer *killer, int methodOfDeath ) {
	idPlayer *self = GetPlayer();

	killStreak = 0;

	if ( !killer || killer == self ) {
		// Nobody to blame: the map, a fall, or its own splash damage.
		revengeTarget = -1;
		QueueChat( BOTCHAT_DEATH_ACCIDENT, NULL, NULL, NULL );
		return;
	}

	idStr weaponName;

	if ( methodOfDeath >= 0 && methodOfDeath < MAX_WEAPONS ) {
		const char *weaponClass = killer->spawnArgs.GetString( va( "def_weapon%d", methodOfDeath ), "" );

		if ( weaponClass[0] ) {
			weaponName = weaponClass;
			weaponName.StripLeadingOnce( "weapon_" );
		}
	}

	revengeTarget = killer->entityNumber;

	QueueChat( BOTCHAT_DEATH, BotDisplayName( killer ), weaponName.c_str(), NULL );
}

/*
================
rvBot::OnDamaged

Damage is a perception event: it identifies the actual threat without granting
line-of-sight through a wall, and schedules one dodge that sustained fire may
not postpone.
================
*/
void rvBot::OnDamaged( idPlayer *attacker, int damage, const idVec3 &dir ) {
	idPlayer *self = GetPlayer();
	if ( !self || damage <= 0 ) {
		return;
	}

	damageStamp = self->lastDmgTime;
	ScheduleDodge( dir );

	if ( bot_debug.GetInteger() >= 2 ) {
		gameLocal.Printf( "bot %s: hit by %s for %d at %s velocity %s direction %s (time %d)\n", name.c_str(),
			attacker ? BotDisplayName( attacker ) : "world", damage, self->GetPhysics()->GetOrigin().ToString( 0 ),
			self->GetPhysics()->GetLinearVelocity().ToString( 0 ), dir.ToString( 0 ), gameLocal.time );
	}
	if ( !IsEnemy( self, attacker ) ) {
		return;
	}

	lastAttacker = attacker;
	lastAttackerTime = gameLocal.time;

	// Already trading shots with someone in view: the attacker is weighed by
	// the next perception pass, which ignores the view cone for it and favours
	// it, instead of being forced on the bot here.  Forcing it reset the
	// reaction deadline on every hit, so two enemies alternating machinegun
	// hits could keep a bot from ever pulling the trigger.
	idPlayer *current = enemy.GetEntity();
	if ( current && current != attacker && IsEnemy( self, current ) &&
		 gameLocal.time - enemyLastSeenTime <= BOT_CONTINUOUS_MSEC ) {
		return;
	}

	idVec3 visiblePoint;
	if ( CanSee( self, attacker, &visiblePoint ) ) {
		if ( current != attacker ) {
			AcquireEnemy( self, attacker, true );
		}
		enemyLastSeenTime = gameLocal.time;
		enemyLastSeenOrigin = attacker->GetPhysics()->GetOrigin();
		enemyLastSeenVelocity = attacker->GetPhysics()->GetLinearVelocity();
		enemyVisiblePoint = visiblePoint;
	}
}

/*
================
rvBot::OnMatchStart
================
*/
void rvBot::OnMatchStart( void ) {
	killStreak		= 0;
	revengeTarget	= -1;

	QueueChat( BOTCHAT_LEVELSTART, NULL, NULL, NULL );
}

/*
================
rvBot::OnMatchEnd
================
*/
void rvBot::OnMatchEnd( bool won ) {
	QueueChat( won ? BOTCHAT_MATCH_WIN : BOTCHAT_MATCH_LOSE, NULL, NULL, NULL );
}

/*
================
rvBot::ShiftMatchTime

A competitive pause freezes rvBotManager::Think but not gameLocal.time, so every
absolute deadline a bot holds would come back from a thirty second timeout
thirty seconds overdue: the reaction it had not finished paying would be
forgiven, the goal it was walking to would be abandoned and blacklisted, and the
stuck detector would fire on a sample taken before the pause.  The rest of the
game answers this by shifting its deadlines forward for every frozen frame -
idPlayer, idInventory, rvWeapon, the game states and the movers all do it - and
so must the bots.

Only absolute stamps move.  onTargetTime is an accumulated duration and
goalBestDistance is a distance, so both are deliberately absent.
================
*/
void rvBot::ShiftMatchTime( int deltaMsec ) {
	if ( deltaMsec <= 0 ) {
		return;
	}

	const int maxInt = 0x7fffffff;
#define SHIFT_BOT_TIME( value ) \
	do { if ( ( value ) > 0 ) { ( value ) = ( value ) > maxInt - deltaMsec ? maxInt : ( value ) + deltaMsec; } } while ( 0 )

	// -- navigation --
	SHIFT_BOT_TIME( pathTime );
	SHIFT_BOT_TIME( goalGiveUpTime );
	SHIFT_BOT_TIME( goalCommitUntil );
	SHIFT_BOT_TIME( goalProgressTime );
	SHIFT_BOT_TIME( enemyPathTime );
	SHIFT_BOT_TIME( nextGoalSelectTime );
	SHIFT_BOT_TIME( holdUntil );
	SHIFT_BOT_TIME( noRouteSince );
	SHIFT_BOT_TIME( traversalStartTime );
	SHIFT_BOT_TIME( dodgeHopStartTime );
	SHIFT_BOT_TIME( dodgeHopCheckTime );
	SHIFT_BOT_TIME( recoverSearchTime );
	SHIFT_BOT_TIME( recoverWanderUntil );

	for ( int i = 0; i < MAX_AVOID_GOALS; i++ ) {
		SHIFT_BOT_TIME( avoidGoals[i].until );
	}

	// -- stuck detection --
	SHIFT_BOT_TIME( stuckTime );
	SHIFT_BOT_TIME( unstickUntil );
	SHIFT_BOT_TIME( offRouteProofTime );
	SHIFT_BOT_TIME( combatSteeringTime );
	SHIFT_BOT_TIME( jumpRetryPathTime );
	SHIFT_BOT_TIME( climbCheckTime );
	SHIFT_BOT_TIME( padHoldSince );
	SHIFT_BOT_TIME( padApproachSince );
	SHIFT_BOT_TIME( padApproachTime );
	SHIFT_BOT_TIME( airRecoverLanded );
	SHIFT_BOT_TIME( knockbackTime );
	SHIFT_BOT_TIME( airborneSince );
	SHIFT_BOT_TIME( jumpPressTime );
	SHIFT_BOT_TIME( airRecoverPlanTime );

	SHIFT_BOT_TIME( goalSelectedTime );
	for ( int i = 0; i < ROAM_HISTORY; i++ ) {
		SHIFT_BOT_TIME( roamHistoryTime[i] );
	}

	// -- combat, aim and trigger --
	SHIFT_BOT_TIME( enemyAcquiredTime );
	SHIFT_BOT_TIME( enemyLastSeenTime );
	SHIFT_BOT_TIME( enemyLastHeardTime );
	SHIFT_BOT_TIME( nextHearingTime );
	SHIFT_BOT_TIME( lastAttackerTime );
	SHIFT_BOT_TIME( enemyFireTime );
	SHIFT_BOT_TIME( burstEndTime );
	SHIFT_BOT_TIME( burstRestTime );
	SHIFT_BOT_TIME( nextSingleShotTime );
	SHIFT_BOT_TIME( mistakeEndTime );
	SHIFT_BOT_TIME( nextMistakeTime );
	SHIFT_BOT_TIME( scanStartTime );

	// -- dodging --
	// damageStamp mirrors idPlayer::lastDmgTime, which idPlayer shifts for the
	// same pause; leaving it behind would make the bot react a second time to a
	// hit it has already answered.
	SHIFT_BOT_TIME( damageStamp );
	SHIFT_BOT_TIME( dodgeStartTime );
	SHIFT_BOT_TIME( dodgeEndTime );
	SHIFT_BOT_TIME( nextThreatScanTime );
	SHIFT_BOT_TIME( strafeFlipTime );

	// -- weapon, chat and throttles --
	SHIFT_BOT_TIME( chatSendTime );
	SHIFT_BOT_TIME( nextWeaponTime );
	SHIFT_BOT_TIME( lastWeaponSwitchTime );
	SHIFT_BOT_TIME( nextBuyTime );
	SHIFT_BOT_TIME( nextRejoinTime );
	SHIFT_BOT_TIME( nextStatusTime );
	SHIFT_BOT_TIME( nextAimDebugTime );

#undef SHIFT_BOT_TIME
}

/*
================
rvBot::Think
================
*/
void rvBot::Think( usercmd_t &cmd ) {
	idPlayer *self = GetPlayer();

	// Start from a clean command every frame; anything not set below is
	// deliberately "not pressed".
	const int	previousAngles0 = cmd.angles[0];
	const int	previousAngles1 = cmd.angles[1];
	const int	previousAngles2 = cmd.angles[2];

	memset( &cmd, 0, sizeof( cmd ) );

	cmd.gameFrame	= gameLocal.framenum;
	cmd.gameTime	= gameLocal.time;
	cmd.angles[0]	= previousAngles0;
	cmd.angles[1]	= previousAngles1;
	cmd.angles[2]	= previousAngles2;

	// Talking is not an input and does not stop for bot_pause, for death or for
	// spectating: a bot that was fragged one second after its own kill should
	// still get the line out.
	UpdateChat( self );

	if ( !self || bot_pause.GetBool() ) {
		return;
	}

	if ( self->health <= 0 || self->spectating ) {
		ResetDodgeJump();
		if ( self->health <= 0 ) {
			// idPlayer::EvaluateControls takes the attack button as a level, not
			// an edge, so holding it is enough to claim the respawn.
			cmd.buttons |= BUTTON_ATTACK;
		} else if ( gameLocal.time > nextRejoinTime && !gameLocal.mpGame.IsManagedMatch() ) {
			// Attack does not rejoin from spectate - it cycles the spectator's
			// view target.  Say the bot wants to play the way a human leaving
			// the spectate menu does, and let the game type decide when.
			//
			// A managed match owns its roster: its session admits registered
			// people only and keeps everyone else in spectator.  Forcing a
			// respawn there put bots the session never registered into a
			// competitive match, so a bot left over from before stays watching.
			nextRejoinTime = gameLocal.time + 1000;

			self->wantSpectate	= false;
			self->forceRespawn	= true;
		}

		if ( goalType != BOTGOAL_NONE ) {
			// Where and doing what a bot went down, so a map's deaths to the
			// world - a void, a pit, a crusher - can be traced to the route.
			if ( bot_debug.GetInteger() >= 1 && self->health <= 0 ) {
				const bool onRoute = pathCorner >= 0 && pathCorner < path.Num();
				const bool attacked = lastAttacker.GetEntity() && gameLocal.time - lastAttackerTime < 3000;
				gameLocal.Printf( "bot %s: down at %s%s, corner %d/%d %s at %s, goal %d\n", name.c_str(),
								  self->GetPhysics()->GetOrigin().ToString( 0 ), attacked ? "" : " with no attacker",
								  pathCorner, path.Num(),
								  onRoute ? BotTravelName( path[pathCorner].travelType ) : "-",
								  onRoute ? path[pathCorner].origin.ToString( 0 ) : "-", goalType );
			}
			OnSpawn();
			goalType = BOTGOAL_NONE;
		}
		return;
	}

	// OnSpawn is where the personality is re-resolved, so a mid-match change to
	// bot_skill, bot_skillVariance or bot_characters lands here - on the next
	// life, not in the middle of the fight the bot is already in.
	if ( goalType == BOTGOAL_NONE ) {
		OnSpawn();
		goalType = BOTGOAL_ROAM;
	}

	if ( probeAirActive ) {
		// Aim still writes the view the input is resolved against.
		UpdateAim( self, cmd );
		ProbeAirControl( self, cmd );
		ReportProbeGrounding( self );
		return;
	}
	UpdateEnemy( self );
	AdoptPadLaunch( self );
	// When this flight began, for telling a knock's throw from the bot's own jump.
	if ( self->pfl.onGround ) {
		airborneSince = 0;
	} else if ( !airborneSince ) {
		airborneSince = gameLocal.time;
		airborneVoluntary = jumpPressTime && gameLocal.time - jumpPressTime <= BOT_AIR_RECOVER_PRESS_MSEC;
	}
	UpdateGoal( self );
	UpdateAim( self, cmd );
	// Movement is resolved against the view angles in this same command.  Aim
	// first so a sharp turn cannot rotate the world-space route input away from
	// a narrow takeoff or landing during the frame it matters most.
	UpdateMovement( self, cmd );
	UpdateBuying( self );
	UpdateWeapon( self );
	UpdateFire( self, cmd );
	// Level four also records quiet grounded approaches, so an unintended
	// transport entry can be reconstructed before its first airborne frame.
	if ( bot_debug.GetInteger() >= 3 && ( bot_debug.GetInteger() >= 4 || !self->pfl.onGround || airRecovering ||
		( knockbackFrameTime >= 0 && gameLocal.time - knockbackFrameTime <= 500 ) ) ) {
		const idPhysics_Player *physics = static_cast<const idPhysics_Player *>( self->GetPlayerPhysics() );
		gameLocal.Printf( "BOT_CONTROL client=%d time=%d origin=%s velocity=%s health=%d grounded=%d crouched=%d speed=%.3f modifier=%.3f knock=%d strong=%d airborne=%d voluntary=%d recover=%d sliding=%d landed=%d wish=%s cmd=%d,%d,%d buttons=%d yaw=%.3f commandYaw=%.3f corner=%d/%d type=%d started=%d target=%s\n",
			clientNum, gameLocal.time, physics->GetOrigin().ToString( 3 ), physics->GetLinearVelocity().ToString( 3 ),
			self->health, self->pfl.onGround ? 1 : 0, physics->IsCrouching() ? 1 : 0, physics->GetMoveSpeed(),
			self->PowerUpModifier( PMOD_SPEED ), physics->GetKnockBack(), knockbackTime, airborneSince, airborneVoluntary ? 1 : 0,
			airRecovering ? 1 : 0, airRecoverSliding ? 1 : 0, airRecoverLanded, airRecoverWish.ToString( 3 ),
			cmd.forwardmove, cmd.rightmove, cmd.upmove, cmd.buttons, aimAngles.yaw,
			SHORT2ANGLE( cmd.angles[1] ) + self->GetDeltaViewAngles()[1], pathCorner, path.Num(),
			pathCorner >= 0 && pathCorner < path.Num() ? path[pathCorner].travelType : -1,
			traversalStarted ? 1 : 0, pathCorner >= 0 && pathCorner < path.Num() ? path[pathCorner].origin.ToString( 3 ) : "-" );
	}
	if ( self->pfl.onGround && cmd.upmove > 0 ) {
		jumpPressTime = gameLocal.time;
	}
	ReportProbeGrounding( self );
	if ( bot_debug.GetInteger() >= 3 && pathCorner >= 0 && pathCorner < path.Num() &&
		( path[pathCorner].travelType != NAVTRAVEL_WALK || bot_debug.GetInteger() >= 4 ) ) {
		gameLocal.Printf( "BOT_TRAVEL client=%d time=%d origin=%s velocity=%s grounded=%d type=%d corner=%d/%d started=%d target=%s cmd=%d,%d,%d yaw=%.3f\n",
			self->entityNumber, gameLocal.time, self->GetPhysics()->GetOrigin().ToString( 3 ),
			self->GetPhysics()->GetLinearVelocity().ToString( 3 ), self->pfl.onGround ? 1 : 0,
			path[pathCorner].travelType, pathCorner, path.Num(), traversalStarted ? 1 : 0,
			path[pathCorner].origin.ToString( 3 ), cmd.forwardmove, cmd.rightmove, cmd.upmove, aimAngles.yaw );
	}

	if ( bot_debug.GetInteger() >= 2 && gameLocal.time >= nextStatusTime ) {
		static const char *goalNames[] = { "none", "roam", "item", "enemy", "objective" };
		static const char *objectiveNames[] = { "none", "capture", "intercept", "return", "escort", "fetch", "rescue", "control", "defend" };
		static const char *mistakeNames[] = { "-", "losetrack", "mistimed", "wrongweapon", "latedodge" };

		nextStatusTime = gameLocal.time + 2000;

		idPlayer *foe = enemy.GetEntity();
		idStr goalLabel = goalNames[idMath::ClampInt( 0, 4, goalType )];
		if ( goalType == BOTGOAL_OBJECTIVE ) {
			goalLabel += ":";
			goalLabel += objectiveNames[idMath::ClampInt( 0, 8, objectiveKind )];
		}

		gameLocal.Printf( "bot %s [%s/%s skill %.1f]: at %s hp %d armor %d goal %s corner %d/%d remain %.0f enemy %s view %.0f/%.0f%s mistake %s%s\n",
						  name.c_str(),
						  character ? character->GetName() : "-",
						  character ? character->GetStyleName() : "-",
						  effectiveSkill,
						  self->GetPhysics()->GetOrigin().ToString( 0 ),
						  self->health,
						  self->inventory.armor,
						  goalLabel.c_str(),
						  pathCorner, path.Num(),
						  PathDistanceRemaining( self->GetPhysics()->GetOrigin() ),
						  foe ? BotDisplayName( foe ) : "-",
						  aimAngles.pitch, aimAngles.yaw, scanStartTime ? " scanning" : "",
						  mistakeNames[idMath::ClampInt( 0, BOTMISTAKE_NUM - 1, ( gameLocal.time < mistakeEndTime ) ? mistake : BOTMISTAKE_NONE )],
						  ( cmd.buttons & BUTTON_ATTACK ) ? " FIRING" : "" );
	}

	if ( bot_debugNav.GetInteger() >= 2 ) {
		for ( int i = idMath::ClampInt( 0, path.Num() - 1, pathCorner ); i < path.Num() - 1; i++ ) {
			gameRenderWorld->DebugArrow( colorRed, path[i].origin + idVec3( 0.0f, 0.0f, 8.0f ),
										 path[i + 1].origin + idVec3( 0.0f, 0.0f, 8.0f ), 4, 0 );
		}
	}
}

/*
===============================================================================

	rvBotManager

===============================================================================
*/

/*
================
rvBotManager::rvBotManager
================
*/
rvBotManager::rvBotManager( void ) {
	probeHealth		= 0;
	probeKnockback	= 0;
	probeArmor		= -1;
	probeKick.Zero();
	navBuilt			= false;
	navFailed			= false;
	nextMinPlayerCheck	= 0;
	profileThinkTicks	= 0.0;
	profileFrameTicks	= 0.0;
	profileFrameNumber	= -1;
	profileMaxTicks		= 0.0;
	profileFrames		= 0;
	nextProfileTime		= 0;
	decisionFrame		= -1;
	decisionsThisFrame	= 0;
	ResetAwareness();
	pendingSkill		= -1;
	pendingExactCharacter = false;
	leaderClientNum		= -1;
	nextLeaderCheck		= 0;
	ResetReplyCooldowns();
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		ClearOrder( i );
		clientNode[i] = -1;
		clientNodeTime[i] = 0;
		clientNodeOrigin[i].Zero();
	}
}

/*
================
rvBotManager::Init
================
*/
void rvBotManager::Init( void ) {
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		bots[i].Shutdown();
	}

	// Every bot released its own character above; this is the belt and braces
	// for a roster that was reloaded out from under them.
	botCharacterManager.ReleaseAllCharacters();

	navBuilt			= false;
	navFailed			= false;
	nextMinPlayerCheck	= 0;
	pendingSkill		= -1;
	pendingExactCharacter = false;
	leaderClientNum		= -1;
	nextLeaderCheck		= 0;
	items.Clear();
	profileThinkTicks	= 0.0;
	profileFrameTicks	= 0.0;
	profileFrameNumber	= -1;
	profileMaxTicks		= 0.0;
	profileFrames		= 0;
	nextProfileTime		= 0;
	ResetAwareness();
	profileDistribution.Clear();
	BotResetWeaponInfo();
	ResetReplyCooldowns();
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		ClearOrder( i );
		clientNode[i] = -1;
		clientNodeTime[i] = 0;
		clientNodeOrigin[i].Zero();
	}
}

/*
================
rvBotManager::Shutdown
================
*/
void rvBotManager::Shutdown( void ) {
	Init();
	navMesh.Clear();
}

/*
================
rvBotManager::OnMapShutdown
================
*/
void rvBotManager::OnMapShutdown( void ) {
	navMesh.Clear();
	navBuilt	= false;
	navFailed	= false;

	// Bots keep their client slots and their characters across a map change;
	// only the map-shaped state goes.
	leaderClientNum	= -1;
	nextLeaderCheck	= 0;
	ResetReplyCooldowns();

	// Every remaining deadline here is an absolute gameLocal.time stamp, and
	// idGameLocal::LoadMap sets that clock back to zero.  Anything left over is
	// therefore unreachable for roughly as long as the previous map ran, not
	// for the throttle it was written as.  The autofill check is the expensive
	// one to lose - bot_minPlayers simply stops topping the match up - and the
	// chat throttle otherwise gags every bot until the GAMEON transition.
	nextMinPlayerCheck = 0;
	nextProfileTime = 0;
	items.Clear();
	ResetAwareness();
	BotResetWeaponInfo();
	botCharacterManager.ResetChatThrottle();

	// An order names places and people on the old map, and a cached node
	// belongs to the old navmesh.
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		ClearOrder( i );
		clientNode[i] = -1;
		clientNodeTime[i] = 0;
	}

	// Surviving bots re-arm.  Empty slots are skipped now that OnSpawn resolves
	// a personality: an inactive slot has clientNum -1, which is not a seed.
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !bots[i].IsActive() ) {
			continue;
		}

		// A queued line deliberately outlives a respawn, so OnSpawn keeps it.
		// It must not outlive the map: its send time is on the old clock, which
		// would pin the composing icon on and block every reply for the rest of
		// the match, and the line itself is about a fight nobody is still in.
		bots[i].chatPending.Clear();
		bots[i].chatTeamOnly		= false;
		bots[i].chatPendingIsReply	= false;
		bots[i].chatSendTime		= 0;

		bots[i].OnSpawn();
	}
}

/*
================
rvBotManager::IsBot
================
*/
bool rvBotManager::IsBot( int clientNum ) const {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return false;
	}

	return bots[clientNum].IsActive();
}

/*
================
rvBotManager::PadApproachOf

A bot publishes its approach only from the frame it last steered for the pad;
one that died, changed route or stopped thinking stops holding the corridor
within a few frames instead of reserving it forever.
================
*/
bool rvBotManager::PadApproachOf( int clientNum, int &padEntityNum, int &since,
								  idVec3 &launch, idVec3 &landing ) const {
	if ( !IsBot( clientNum ) ) {
		return false;
	}
	const rvBot &bot = bots[clientNum];
	if ( bot.padApproachEntity < 0 || gameLocal.time - bot.padApproachTime > BOT_PAD_APPROACH_STALE_MSEC ) {
		return false;
	}
	padEntityNum = bot.padApproachEntity;
	since = bot.padApproachSince;
	launch = bot.padApproachLaunch;
	landing = bot.padApproachLanding;
	return true;
}

/*
================
rvBotManager::ResetReplyCooldowns
================
*/
void rvBotManager::ResetReplyCooldowns( void ) {
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		nextReplySourceTime[i] = 0;
	}
}

/*
================
rvBotManager::OnChatMessage

One accepted typed-chat line gets at most one prospective responder.  Addressed
bots are preferred, team chat is visible only to team-mates, and the packet's
claimed name is never used: clientNum and server-owned userinfo are the
identity.  The selected character can still decline through source/address
rules, chatiness, a pending line or the shared flood throttle.
================
*/
void rvBotManager::OnChatMessage( int sourceClientNum, bool teamOnly, const char *visibleText ) {
	if ( sourceClientNum < 0 || sourceClientNum >= MAX_CLIENTS ) {
		return;
	}

	idEntity *sourceEntity = gameLocal.entities[sourceClientNum];
	if ( !sourceEntity || !sourceEntity->IsType( idPlayer::GetClassType() ) ) {
		return;
	}

	idPlayer *sourcePlayer = static_cast<idPlayer *>( sourceEntity );
	idStr normalizedText;
	botCharacterManager.NormalizeReplyText( visibleText, normalizedText );

	if ( normalizedText.IsEmpty() ) {
		return;
	}

	// An order is gameplay and is honoured with bot chat off; it is not also
	// small talk for some other bot to answer.
	if ( HandleTeamOrder( sourceClientNum, teamOnly, normalizedText ) ) {
		return;
	}

	if ( bot_chat.GetInteger() <= 0 || !bot_characters.GetBool() ) {
		return;
	}
	if ( gameLocal.time < nextReplySourceTime[sourceClientNum] ) {
		return;
	}

	const bool sourceIsBot = IsBot( sourceClientNum );
	idList<int> addressedCandidates;
	idList<int> generalCandidates;

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		if ( i == sourceClientNum || !bots[i].IsActive() ) {
			continue;
		}

		idEntity *candidateEntity = gameLocal.entities[i];
		if ( !candidateEntity || !candidateEntity->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *candidatePlayer = static_cast<idPlayer *>( candidateEntity );
		if ( candidatePlayer->spectating ) {
			continue;
		}
		if ( teamOnly &&
			 ( !gameLocal.IsTeamGame() || candidatePlayer->team != sourcePlayer->team ) ) {
			continue;
		}
		if ( candidatePlayer->IsPlayerMuted( sourcePlayer ) ) {
			continue;
		}

		const rvBotCharacter *candidateCharacter = bots[i].GetCharacter();
		const char *candidateName = gameLocal.userInfo[i].GetString( "ui_name", bots[i].GetName() );
		const bool addressed = botCharacterManager.ReplyNameMatches( candidateName, normalizedText );

		if ( !botCharacterManager.HasReply( candidateCharacter, normalizedText, sourceIsBot, addressed ) ) {
			continue;
		}

		if ( addressed ) {
			addressedCandidates.Append( i );
		} else {
			generalCandidates.Append( i );
		}
	}

	const idList<int> *candidates = addressedCandidates.Num() ? &addressedCandidates : &generalCandidates;
	if ( !candidates->Num() ) {
		return;
	}

	const bool addressed = addressedCandidates.Num() > 0;
	float replyChance;

	if ( sourceIsBot ) {
		replyChance = addressed ? BOT_REPLY_BOT_ADDRESSED_CHANCE : BOT_REPLY_BOT_CHANCE;
	} else {
		replyChance = addressed ? BOT_REPLY_PLAYER_ADDRESSED_CHANCE : BOT_REPLY_PLAYER_CHANCE;
	}

	if ( gameLocal.random.RandomFloat() >= replyChance ) {
		return;
	}

	const int responder = ( *candidates )[gameLocal.random.RandomInt( candidates->Num() )];
	const char *sourceName = gameLocal.userInfo[sourceClientNum].GetString( "ui_name", "player" );

	if ( !bots[responder].TryQueueReply( normalizedText, sourceIsBot, addressed, sourceName, teamOnly ) ) {
		return;
	}

	int cooldown = BOT_REPLY_SOURCE_THROTTLE_MSEC;
	if ( bot_chat.GetInteger() >= 2 ) {
		cooldown /= 2;
	}
	nextReplySourceTime[sourceClientNum] = gameLocal.time + cooldown;
}

/*
================
BotOrderWordsAt

End of phrase in text when the phrase starts exactly at pos and ends on a word
boundary, else -1.  Both strings are NormalizeReplyText output: lower case
words, one space apart.
================
*/
static int BotOrderWordsAt( const char *text, int pos, const char *phrase ) {
	int i = 0;
	while ( phrase[i] ) {
		if ( text[pos + i] != phrase[i] ) {
			return -1;
		}
		i++;
	}
	const char next = text[pos + i];
	return ( next == '\0' || next == ' ' ) ? pos + i : -1;
}

// Words that address every bot on the speaker's team.
static const char *botOrderGroupWords[] = {
	"everyone", "everybody", "all", "team", "bots", "guys", "squad", NULL
};

typedef struct botOrderPhrase_s {
	const char *	phrase;
	int				kind;		// botOrderKind_t
	bool			here;		// defend where the speaker is standing
} botOrderPhrase_t;

// The longest phrase that matches wins, so "defend here" beats "defend".
static const botOrderPhrase_t botOrderPhrases[] = {
	{ "follow me", BOTORDER_FOLLOW, false },
	{ "follow", BOTORDER_FOLLOW, false },
	{ "escort me", BOTORDER_FOLLOW, false },
	{ "cover me", BOTORDER_FOLLOW, false },
	{ "come with me", BOTORDER_FOLLOW, false },
	{ "with me", BOTORDER_FOLLOW, false },
	{ "on me", BOTORDER_FOLLOW, false },
	{ "regroup", BOTORDER_FOLLOW, false },
	{ "defend", BOTORDER_DEFEND, false },
	{ "defend the base", BOTORDER_DEFEND, false },
	{ "defend base", BOTORDER_DEFEND, false },
	{ "defense", BOTORDER_DEFEND, false },
	{ "defence", BOTORDER_DEFEND, false },
	{ "guard", BOTORDER_DEFEND, false },
	{ "guard the base", BOTORDER_DEFEND, false },
	{ "protect the base", BOTORDER_DEFEND, false },
	{ "hold the base", BOTORDER_DEFEND, false },
	{ "stay home", BOTORDER_DEFEND, false },
	{ "defend here", BOTORDER_DEFEND, true },
	{ "defend this", BOTORDER_DEFEND, true },
	{ "guard here", BOTORDER_DEFEND, true },
	{ "guard this", BOTORDER_DEFEND, true },
	{ "hold here", BOTORDER_DEFEND, true },
	{ "hold this", BOTORDER_DEFEND, true },
	{ "hold this position", BOTORDER_DEFEND, true },
	{ "stay here", BOTORDER_DEFEND, true },
	{ "camp here", BOTORDER_DEFEND, true },
	{ "attack", BOTORDER_ATTACK, false },
	{ "offense", BOTORDER_ATTACK, false },
	{ "offence", BOTORDER_ATTACK, false },
	{ "get the flag", BOTORDER_ATTACK, false },
	{ "get flag", BOTORDER_ATTACK, false },
	{ "grab the flag", BOTORDER_ATTACK, false },
	{ "take the flag", BOTORDER_ATTACK, false },
	{ "capture the flag", BOTORDER_ATTACK, false },
	{ "go get the flag", BOTORDER_ATTACK, false },
	{ "push", BOTORDER_ATTACK, false },
	{ "free", BOTORDER_NONE, false },
	{ "freelance", BOTORDER_NONE, false },
	{ "dismissed", BOTORDER_NONE, false },
	{ "roam", BOTORDER_NONE, false },
	{ "carry on", BOTORDER_NONE, false },
	{ "as you were", BOTORDER_NONE, false },
	{ "stop following", BOTORDER_NONE, false },
	{ "do your thing", BOTORDER_NONE, false },
	{ NULL, BOTORDER_NONE, false }
};

/*
================
BotParseTeamOrder

<addressees> <order>: one or more team-mates' names or a group word, joined by
"and" if the speaker likes, then an order phrase straight after - "kane
defend", "everyone follow me", "sorg and bagby hold here please".  Anchoring
the phrase to the names keeps ordinary team talk ("i will defend", "kane is
following me") from turning into orders.  names holds each candidate bot's
normalized name; addressed is set per name.  Returns the order kind, with
BOTORDER_NONE meaning "back to free play", or -1 when the line is no order.
================
*/
static int BotParseTeamOrder( const char *text, const char *const *names, int numNames,
							  bool *addressed, bool &everyone, bool &here ) {
	everyone = false;
	here = false;
	for ( int i = 0; i < numNames; i++ ) {
		addressed[i] = false;
	}

	int pos = 0;
	bool any = false;
	while ( text[pos] ) {
		int end = -1;
		if ( any && ( end = BotOrderWordsAt( text, pos, "and" ) ) >= 0 ) {
			pos = ( text[end] == ' ' ) ? end + 1 : end;
			continue;
		}

		bool matched = false;
		for ( int g = 0; botOrderGroupWords[g] && !matched; g++ ) {
			if ( ( end = BotOrderWordsAt( text, pos, botOrderGroupWords[g] ) ) >= 0 ) {
				everyone = true;
				matched = true;
			}
		}
		// The longest name that fits, so one bot's name being the start of
		// another's addresses the right one.
		int bestName = -1;
		int bestEnd = -1;
		for ( int i = 0; i < numNames && !matched; i++ ) {
			const int nameEnd = ( names[i] && names[i][0] ) ? BotOrderWordsAt( text, pos, names[i] ) : -1;
			if ( nameEnd > bestEnd ) {
				bestEnd = nameEnd;
				bestName = i;
			}
		}
		if ( !matched && bestName >= 0 ) {
			addressed[bestName] = true;
			end = bestEnd;
			matched = true;
		}
		if ( !matched ) {
			break;
		}
		any = true;
		pos = ( text[end] == ' ' ) ? end + 1 : end;
	}
	if ( !any ) {
		return -1;
	}

	int end = BotOrderWordsAt( text, pos, "please" );
	if ( end >= 0 ) {
		pos = ( text[end] == ' ' ) ? end + 1 : end;
	}

	int best = -1;
	int bestEnd = -1;
	for ( int i = 0; botOrderPhrases[i].phrase; i++ ) {
		end = BotOrderWordsAt( text, pos, botOrderPhrases[i].phrase );
		if ( end > bestEnd ) {
			bestEnd = end;
			best = i;
		}
	}
	if ( best < 0 ) {
		return -1;
	}

	here = botOrderPhrases[best].here;
	return botOrderPhrases[best].kind;
}

/*
================
BotOrderAnchor

A spot defence holds around the nearest fixed point to where the speaker
stands - a spawn point or a pickup on the same floor - because the hold logic
measures from an entity.  NULL when nothing fixed is close enough.
================
*/
static idEntity *BotOrderAnchor( const idPlayer *speaker ) {
	static const float BOT_ORDER_ANCHOR_RANGE = 512.0f;
	const idVec3 &origin = speaker->GetPhysics()->GetOrigin();
	idEntity *best = NULL;
	float bestDistanceSqr = Square( BOT_ORDER_ANCHOR_RANGE );

	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		const bool fixedPoint = ent->IsType( idPlayerStart::GetClassType() ) ||
			( ent->IsType( idItem::GetClassType() ) && !ent->spawnArgs.GetBool( "dropped" ) &&
			  !ent->IsType( rvItemCTFFlag::GetClassType() ) );
		if ( !fixedPoint || !ent->GetPhysics() || ent->GetInstance() != speaker->GetInstance() ) {
			continue;
		}
		const idVec3 delta = ent->GetPhysics()->GetOrigin() - origin;
		if ( idMath::Fabs( delta.z ) > 128.0f ) {
			continue;		// another floor
		}
		const float distanceSqr = delta.LengthSqr();
		if ( distanceSqr < bestDistanceSqr ) {
			bestDistanceSqr = distanceSqr;
			best = ent;
		}
	}

	return best;
}

/*
================
rvBotManager::HandleTeamOrder

Only a human may give orders, only in team chat and only to bots on the
speaker's own team.  Up to two of the bots ordered say they heard it.
================
*/
bool rvBotManager::HandleTeamOrder( int sourceClientNum, bool teamOnly, const idStr &normalizedText ) {
	if ( !teamOnly || !gameLocal.IsTeamGame() || IsBot( sourceClientNum ) ) {
		return false;
	}

	idEntity *sourceEntity = gameLocal.entities[sourceClientNum];
	if ( !sourceEntity || !sourceEntity->IsType( idPlayer::GetClassType() ) ) {
		return false;
	}
	idPlayer *speaker = static_cast<idPlayer *>( sourceEntity );
	if ( speaker->spectating ) {
		return false;
	}

	idStr normalizedNames[MAX_CLIENTS];
	const char *names[MAX_CLIENTS];
	int clients[MAX_CLIENTS];
	int numNames = 0;
	for ( int i = 0; i < gameLocal.numClients && numNames < MAX_CLIENTS; i++ ) {
		idPlayer *bot = gameLocal.GetClientByNum( i );
		if ( !bots[i].IsActive() || !bot || bot->spectating || bot->team != speaker->team ||
			 bot->GetInstance() != speaker->GetInstance() ) {
			continue;
		}
		botCharacterManager.NormalizeReplyText(
			gameLocal.userInfo[i].GetString( "ui_name", bots[i].GetName() ), normalizedNames[numNames] );
		names[numNames] = normalizedNames[numNames].c_str();
		clients[numNames] = i;
		numNames++;
	}
	if ( !numNames ) {
		return false;
	}

	bool addressed[MAX_CLIENTS];
	bool everyone = false;
	bool here = false;
	const int kind = BotParseTeamOrder( normalizedText.c_str(), names, numNames, addressed, everyone, here );
	if ( kind < 0 ) {
		return false;
	}

	// A defence "here" - or any defence outside CTF, which has no base - holds
	// around the nearest fixed point; with none close enough the bot stays
	// with the speaker instead, which is the nearest thing to what was asked.
	int resolvedKind = kind;
	idEntity *anchor = NULL;
	const bool baseMode = MPGameTypeHasAny( gameLocal.gameType, GTF_FLAG );
	if ( kind == BOTORDER_DEFEND && ( here || !baseMode ) ) {
		anchor = BotOrderAnchor( speaker );
		if ( !anchor ) {
			resolvedKind = BOTORDER_FOLLOW;
		}
	}

	int acknowledged = 0;
	for ( int n = 0; n < numNames; n++ ) {
		if ( !everyone && !addressed[n] ) {
			continue;
		}

		const int clientNum = clients[n];
		botOrder_t &order = orders[clientNum];
		order.kind				= resolvedKind;
		order.commander			= sourceClientNum;
		order.commanderSpawnId	= gameLocal.spawnIds[sourceClientNum];
		order.team				= speaker->team;
		order.anchor			= anchor;
		order.issuedTime		= gameLocal.time;
		if ( resolvedKind == BOTORDER_NONE ) {
			ClearOrder( clientNum );
		}

		if ( bot_debug.GetBool() ) {
			static const char *kindNames[] = { "free play", "attack", "defend", "follow" };
			gameLocal.Printf( "bot %s: ordered to %s by %s%s\n", bots[clientNum].GetName(),
							  kindNames[idMath::ClampInt( 0, 3, resolvedKind )],
							  gameLocal.userInfo[sourceClientNum].GetString( "ui_name", "player" ),
							  anchor ? va( " at %s", anchor->GetName() ) : "" );
		}

		if ( acknowledged < 2 ) {
			bots[clientNum].QueueOrderAck( gameLocal.userInfo[sourceClientNum].GetString( "ui_name", "" ) );
			acknowledged++;
		}
	}

	return true;
}

/*
================
rvBotManager::GetOrder
================
*/
const botOrder_t *rvBotManager::GetOrder( int clientNum ) const {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !bots[clientNum].IsActive() ) {
		return NULL;
	}

	const botOrder_t &order = orders[clientNum];
	if ( order.kind == BOTORDER_NONE ) {
		return NULL;
	}

	idPlayer *bot = gameLocal.GetClientByNum( clientNum );
	if ( !bot || bot->team != order.team ) {
		return NULL;		// autobalance or a team switch: a new side gives its own orders
	}

	if ( order.kind == BOTORDER_FOLLOW ) {
		const int commander = order.commander;
		if ( commander < 0 || commander >= MAX_CLIENTS ||
			 gameLocal.spawnIds[commander] != order.commanderSpawnId ) {
			return NULL;
		}
		idPlayer *leader = gameLocal.GetClientByNum( commander );
		if ( !leader || leader->spectating || leader->team != bot->team ) {
			return NULL;
		}
	}

	return &order;
}

/*
================
rvBotManager::ClientNavNode
================
*/
int rvBotManager::ClientNavNode( int clientNum ) const {
	static const float BOT_CLIENT_NODE_SLACK = 32.0f;
	static const int BOT_CLIENT_NODE_MSEC = 1000;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !navMesh.IsValid() ) {
		return -1;
	}
	idPlayer *player = gameLocal.GetClientByNum( clientNum );
	if ( !player || !player->GetPhysics() ) {
		return -1;
	}

	const idVec3 &origin = player->GetPhysics()->GetOrigin();
	if ( clientNodeTime[clientNum] && gameLocal.time >= clientNodeTime[clientNum] &&
		 gameLocal.time - clientNodeTime[clientNum] < BOT_CLIENT_NODE_MSEC &&
		 ( origin - clientNodeOrigin[clientNum] ).LengthSqr() < Square( BOT_CLIENT_NODE_SLACK ) ) {
		return clientNode[clientNum];
	}

	clientNode[clientNum] = navMesh.SnapNode( origin );
	clientNodeOrigin[clientNum] = origin;
	clientNodeTime[clientNum] = Max( 1, gameLocal.time );
	return clientNode[clientNum];
}

/*
================
rvBotManager::ClearOrder
================
*/
void rvBotManager::ClearOrder( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	botOrder_t &order = orders[clientNum];
	order.kind				= BOTORDER_NONE;
	order.commander			= -1;
	order.commanderSpawnId	= -1;
	order.team				= -1;
	order.anchor			= NULL;
	order.issuedTime		= 0;
}

/*
================
rvBotManager::NumBots
================
*/
int rvBotManager::NumBots( void ) const {
	int count = 0;

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( bots[i].IsActive() ) {
			count++;
		}
	}

	return count;
}

/*
================
rvBotManager::GoalClaimCount

Lightweight team reservation.  Callers turn this count into a utility penalty,
so a second bot may still help with a major objective but ordinary pickups are
naturally spread across the team.
================
*/
int rvBotManager::GoalClaimCount( const rvBot *requester, const idEntity *goal ) const {
	if ( !requester || !goal ) {
		return 0;
	}

	const int requesterNum = requester->GetClientNum();
	idPlayer *requesterPlayer = ( requesterNum >= 0 && requesterNum < gameLocal.numClients &&
		gameLocal.entities[requesterNum] && gameLocal.entities[requesterNum]->IsType( idPlayer::GetClassType() ) ) ?
		static_cast<idPlayer *>( gameLocal.entities[requesterNum] ) : NULL;
	if ( !requesterPlayer ) {
		return 0;
	}
	if ( !gameLocal.IsTeamGame() ) {
		return 0;
	}

	int claims = 0;
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( &bots[i] == requester || !bots[i].IsActive() || bots[i].GetGoalEntity() != goal ) {
			continue;
		}

		idEntity *ent = gameLocal.entities[i];
		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		idPlayer *player = static_cast<idPlayer *>( ent );
		if ( player->GetInstance() != requesterPlayer->GetInstance() ) {
			continue;
		}
		if ( player->team != requesterPlayer->team ) {
			continue;
		}

		claims++;
	}

	return claims;
}

/*
================
rvBotManager::ClaimDecision
================
*/
bool rvBotManager::ClaimDecision( void ) {
	if ( decisionFrame != gameLocal.framenum ) {
		decisionFrame = gameLocal.framenum;
		decisionsThisFrame = 0;
	}
	if ( decisionsThisFrame >= BOT_DECISIONS_PER_FRAME ) {
		return false;
	}
	decisionsThisFrame++;
	return true;
}

/*
================
rvBotManager::ObjectiveKindOf
================
*/
int rvBotManager::ObjectiveKindOf( int clientNum ) const {
	if ( !IsBot( clientNum ) || bots[clientNum].goalType != BOTGOAL_OBJECTIVE ) {
		return BOTOBJ_NONE;
	}
	return bots[clientNum].objectiveKind;
}

/*
================
rvBotManager::ObjectiveEntityOf
================
*/
const idEntity *rvBotManager::ObjectiveEntityOf( int clientNum ) const {
	if ( ObjectiveKindOf( clientNum ) == BOTOBJ_NONE ) {
		return NULL;
	}
	return bots[clientNum].goalEntity.GetEntity();
}

/*
================
rvBotManager::BalancedTeamForBot

Bot allocation is synchronous enough for the rvBot to exist, but not for
gameLocal.numClients or even the idPlayer entity to be current.  Count the full
client-slot range and include the explicit reservations made by earlier bot
allocations.  Without that second source, several addbot commands buffered in
one frame all see the same population and choose the same side.
================
*/
int rvBotManager::BalancedTeamForBot( int clientNum, int &balanceInstance ) const {
	balanceInstance = 0;

	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		idPlayer *botPlayer = bots[clientNum].GetPlayer();

		if ( botPlayer ) {
			balanceInstance = botPlayer->GetInstance();
		} else {
			idPlayer *localPlayer = gameLocal.GetLocalPlayer();

			// A console-created bot on a listen server joins the host's gameplay
			// instance.  Dedicated/team maps use instance zero.
			if ( localPlayer ) {
				balanceInstance = localPlayer->GetInstance();
			}
		}
	}

	int teamCount[TEAM_MAX];
	memset( teamCount, 0, sizeof( teamCount ) );

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( i == clientNum ) {
			continue;
		}

		idEntity *ent = gameLocal.entities[i];
		idPlayer *player = ( ent && ent->IsType( idPlayer::GetClassType() ) ) ?
			static_cast<idPlayer *>( ent ) : NULL;

		if ( player ) {
			if ( player->GetInstance() != balanceInstance ) {
				continue;
			}
			if ( player->spectating && player->wantSpectate ) {
				continue;
			}

			int assignedTeam = player->team;

			// Active bots and players waiting for their first/round spawn have
			// an authoritative intended team before idPlayer::team is latched.
			// Prefer that reservation during this narrow window.
			if ( bots[i].active || player->spectating ) {
				const char *intendedName = gameLocal.userInfo[i].GetString( "ui_team" );

				for ( int team = 0; team < TEAM_MAX; team++ ) {
					if ( !idStr::Icmp( intendedName, idMultiplayerGame::teamNames[team] ) ) {
						assignedTeam = team;
						break;
					}
				}
			}

			if ( assignedTeam >= 0 && assignedTeam < TEAM_MAX ) {
				teamCount[assignedTeam]++;
			}
			continue;
		}

		// AllocateClientSlotForBot can register rvBot before SpawnPlayer has
		// produced its idPlayer.  Its cached assignment is still a real place
		// on this instance and must influence the next buffered addbot.
		if ( bots[i].active && bots[i].teamAssignmentInstance == balanceInstance &&
			 bots[i].teamAssignment >= 0 && bots[i].teamAssignment < TEAM_MAX ) {
			teamCount[bots[i].teamAssignment]++;
		}
	}

	return ( teamCount[TEAM_STROGG] < teamCount[TEAM_MARINE] ) ? TEAM_STROGG : TEAM_MARINE;
}

/*
================
rvBotManager::FillUserInfo
================
*/
void rvBotManager::FillUserInfo( int clientNum, idDict &info ) {
	if ( !IsBot( clientNum ) ) {
		return;
	}

	bots[clientNum].FillUserInfo( info );
}

/*
================
rvBotManager::EnsureNavMesh
================
*/
bool rvBotManager::EnsureNavMesh( void ) {
	// The mesh itself is the authority, not the flag.  `navmesh build` calls
	// rvNavMesh::Build directly - it is a deliberate forced rebuild, so it
	// cannot go through here - and rvNavMesh::Build opens with an unconditional
	// Clear().  Gating on the flag alone would therefore throw away a perfectly
	// good hand-built mesh and re-run the whole collision sweep on the next
	// addbot, stalling the server thread for seconds to arrive back where it
	// already was.
	if ( navMesh.IsValid() ) {
		navBuilt = true;
		return true;
	}
	if ( navFailed ) {
		return false;
	}

	navBuilt = true;

	if ( !navMesh.Build() ) {
		navFailed = true;
		return false;
	}

	return true;
}

/*
================
rvBotManager::PickBotName

First unused name from the table, so a full server does not end up with eight
players called the same thing and idGameLocal::SetUserInfo suffixing them all.

Only used when there is no roster to pick from: with characters loaded the name
comes from the character, so the scoreboard and the personality agree.
================
*/
const char *rvBotManager::PickBotName( void ) const {
	for ( int i = 0; botNames[i]; i++ ) {
		bool taken = false;

		for ( int j = 0; j < MAX_CLIENTS && !taken; j++ ) {
			if ( bots[j].IsActive() && idStr::Icmp( bots[j].GetName(), botNames[i] ) == 0 ) {
				taken = true;
			}
		}

		if ( !taken ) {
			return botNames[i];
		}
	}

	return botNames[0];
}

/*
================
rvBotManager::AddBot
================
*/
bool rvBotManager::AddBot( const char *name, int skillLevel, bool requireExactCharacter ) {
	if ( !gameLocal.isMultiplayer ) {
		gameLocal.Printf( "addbot: bots are multiplayer only\n" );
		return false;
	}
	if ( gameLocal.isClient || !gameLocal.isServer ) {
		gameLocal.Printf( "addbot: only the server can add bots\n" );
		return false;
	}
	if ( !bot_enable.GetBool() ) {
		gameLocal.Printf( "addbot: bots are disabled (bot_enable 0)\n" );
		return false;
	}
	// The managed-match session registers people only and holds everyone else
	// in spectator; a bot added there could never play its match.
	if ( gameLocal.mpGame.IsManagedMatch() ) {
		gameLocal.Printf( "addbot: a managed match admits registered players only\n" );
		return false;
	}

	// -1 is "follow bot_skill".  Anything else has to be a real level, or an
	// operator's typo would quietly become one.
	if ( skillLevel != -1 && ( skillLevel < 1 || skillLevel > BOT_SKILL_LEVELS ) ) {
		gameLocal.Printf( "addbot: skill must be 1 to %d\n", BOT_SKILL_LEVELS );
		return false;
	}
	if ( requireExactCharacter ) {
		if ( !name || !name[0] ) {
			gameLocal.Printf( "addbot: exact character mode requires a name\n" );
			return false;
		}
		if ( !bot_characters.GetBool() ) {
			gameLocal.Printf( "addbot: exact character '%s' requires bot_characters 1\n", name );
			return false;
		}

		const rvBotCharacter *requestedCharacter = botCharacterManager.FindCharacter( name );
		if ( !requestedCharacter ) {
			gameLocal.Printf( "addbot: no character called '%s' is loaded\n", name );
			return false;
		}
		if ( requestedCharacter->IsInUse() ) {
			gameLocal.Printf( "addbot: character '%s' is already in use\n", name );
			return false;
		}
	}

	if ( !EnsureNavMesh() ) {
		gameLocal.Printf( "addbot: no navigation could be generated for this map\n" );
		return false;
	}

	const int level = idMath::ClampInt( 1, BOT_SKILL_LEVELS, ( skillLevel > 0 ) ? skillLevel : bot_skill.GetInteger() );

	// The engine wants a name for the slot before the bot exists.  Peek at the
	// roster for one rather than inventing it, so the slot, the scoreboard and
	// the personality all agree.  The peek and the claim that follows it can
	// only disagree if the roster changed in between, and FillUserInfo
	// republishes ui_name from the character anyway, so the worst case is that
	// the engine's own record of the slot name is cosmetically stale.
	const char *botName = ( name && name[0] ) ? name : NULL;

	if ( !botName && bot_characters.GetBool() ) {
		const rvBotCharacter *peek = botCharacterManager.PickCharacter( level, false );

		if ( peek ) {
			botName = peek->GetName();
		}
	}
	if ( !botName ) {
		botName = PickBotName();
	}

	int maxPlayers = gameLocal.serverInfo.GetInt( "si_maxPlayers" );
	if ( maxPlayers <= 0 ) {
		maxPlayers = MAX_CLIENTS;
	}

	// The engine allocates the slot, then calls back into ServerClientBegin ->
	// SpawnPlayer, which is where OnSpawnPlayer registers the bot.  A per-bot
	// skill has nowhere to travel through that call, so it is parked here for
	// the length of it.
	pendingSkill = skillLevel;
	pendingExactCharacter = requireExactCharacter;

	const int clientNum = networkSystem->AllocateClientSlotForBot( botName, maxPlayers );

	pendingSkill = -1;
	pendingExactCharacter = false;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		gameLocal.Printf( "addbot: no free client slot\n" );
		return false;
	}

	if ( !bots[clientNum].IsActive() ) {
		gameLocal.Warning( "addbot: client %d was allocated but never spawned a bot", clientNum );
		return false;
	}

	gameLocal.Printf( "addbot: '%s' joined as client %d at skill %.1f\n",
					  bots[clientNum].GetName(), clientNum, bots[clientNum].GetEffectiveSkill() );

	return true;
}

/*
================
rvBotManager::RemoveBot
================
*/
bool rvBotManager::RemoveBot( const char *name ) {
	int target = -1;

	for ( int i = MAX_CLIENTS - 1; i >= 0; i-- ) {
		if ( !bots[i].IsActive() ) {
			continue;
		}
		if ( name && name[0] && idStr::Icmp( bots[i].GetName(), name ) != 0 ) {
			continue;
		}

		target = i;
		break;
	}

	return RemoveBotAt( target );
}

/*
================
rvBotManager::RemoveBotAt
================
*/
bool rvBotManager::RemoveBotAt( int clientNum ) {
	if ( !IsBot( clientNum ) ) {
		return false;
	}

	// Said before the kick, and said immediately: a queued line would be
	// dropped along with the slot that owes it.
	bots[clientNum].SayFarewell();

	// Finish the server's normal disconnect before the next buffered command.
	// Appending behind an exec loop deferred the kick until after addbot and
	// left the character claimed throughout a replay or roster replacement.
	cmdSystem->BufferCommandText( CMD_EXEC_INSERT, va( "kick %d\n", clientNum ) );

	return true;
}

/*
================
rvBotManager::RemoveAll
================
*/
void rvBotManager::RemoveAll( void ) {
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( bots[i].IsActive() ) {
			cmdSystem->BufferCommandText( CMD_EXEC_INSERT, va( "kick %d\n", i ) );
		}
	}
}

/*
================
rvBotManager::RebindCharacters
================
*/
void rvBotManager::RebindCharacters( void ) {
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( bots[i].IsActive() ) {
			bots[i].RebindCharacter();
		}
	}
}

/*
================
rvBotManager::ProbeTraversal
================
*/
bool rvBotManager::ProbeTraversal( int clientNum, const idVec3 &origin, const idVec3 &velocity,
								 const idVec3 &landing, navTravelType_t travel, idEntity *pad, bool dodge,
								 const idVec3 *airDirection, const idVec3 *entryOrigin,
								 const idVec3 *nextJump, bool naturalWalk, bool padTraffic, bool recover, bool climb ) {
	if ( ( travel != NAVTRAVEL_WALK && travel != NAVTRAVEL_DROP && travel != NAVTRAVEL_JUMP && travel != NAVTRAVEL_JUMPPAD ) ||
		( ( travel == NAVTRAVEL_JUMPPAD ) != ( pad != NULL ) ) || ( dodge && travel != NAVTRAVEL_WALK ) ||
		( airDirection && ( travel != NAVTRAVEL_WALK || dodge ) ) ||
		( entryOrigin && travel != NAVTRAVEL_DROP && travel != NAVTRAVEL_JUMPPAD ) || ( padTraffic && !pad ) ||
		( naturalWalk && ( travel != NAVTRAVEL_WALK || dodge || airDirection ) ) || ( nextJump && !naturalWalk ) ||
		( recover && ( !naturalWalk || nextJump || padTraffic ) ) ||
		( climb && ( !naturalWalk || recover || nextJump || padTraffic ) ) ) {
		return false;
	}
	if ( !gameLocal.isServer || gameLocal.isClient || !gameLocal.GetLocalPlayer() ||
		gameLocal.numClients != ( padTraffic ? 3 : 2 ) || NumBots() != ( padTraffic ? 2 : 1 ) || !IsBot( clientNum ) ||
		gameLocal.mpGame.IsManagedMatch() || !gameLocal.CheatsOk( false ) || !navMesh.IsValid() ) {
		return false;
	}
	idPlayer *self = bots[clientNum].GetPlayer();
	if ( !self || self == gameLocal.GetLocalPlayer() || self->health <= 0 || self->spectating ||
		 self->GetInstance() != gameLocal.GetLocalPlayer()->GetInstance() ) {
		return false;
	}
	idVec3 naturalLanding;
	if ( pad && ( !pad->IsType( rvJumpPad::GetClassType() ) ||
		 !navMesh.NaturalPadLanding( pad->entityNumber, naturalLanding ) ) ) {
		return false;
	}
	// Do not let this diagnostic's teleport displace or telefrag the observer.
	idBounds destination = self->GetPhysics()->GetBounds();
	destination.TranslateSelf( origin );
	if ( destination.IntersectsBounds( gameLocal.GetLocalPlayer()->GetPhysics()->GetAbsBounds() ) ) {
		return false;
	}
	rvBot &bot = bots[clientNum];
	bot.OnSpawn();
	const idAngles facing( 0.0f, ( landing - origin ).ToYaw(), 0.0f );
	self->Teleport( origin - idVec3( 0.0f, 0.0f, CM_CLIP_EPSILON ), facing, NULL );
	self->GetPhysics()->SetLinearVelocity( velocity );
	if ( padTraffic ) {
		self->fl.notarget = true;	// isolate physical traffic from weapon impulses
	}
	bot.aimAngles = facing;
	bot.goalType = BOTGOAL_ROAM;
	bot.goalOrigin = nextJump ? *nextJump : landing;
	if ( travel == NAVTRAVEL_WALK || ( pad && entryOrigin ) ) {
		// A walking probe has no committed jump action to defer goal selection.
		// Keep its supplied leg long enough to observe the impulse response.
		bot.nextGoalSelectTime = gameLocal.time + BOT_TRAVEL_TIMEOUT_MSEC;
	}
	bot.path.Clear();
	bot.path.Append( entryOrigin ? *entryOrigin : origin, NAVTRAVEL_WALK );
	bot.path.Append( landing, travel, pad ? pad->entityNumber : -1 );
	if ( nextJump ) {
		bot.path.Append( *nextJump, NAVTRAVEL_JUMP );
	}
	bot.pathCorner = 1;
	bot.pathTime = gameLocal.time;
	bot.traversalCorner = 1;
	if ( climb ) {
		// Restore a pending ordinary climb attempt, without forcing a jump or
		// bypassing movement admission. Landing can retain an old climb window.
		bot.climbCheckTarget = landing;
		bot.climbCheckDistance = idVec3( landing.x - origin.x, landing.y - origin.y, 0.0f ).Length();
		bot.climbCheckTime = gameLocal.time - BOT_CLIMB_HOP_MSEC;
	}
	bot.traversalStartOrigin = origin;
	bot.traversalStartTime = gameLocal.time;
	bot.traversalEntered = bot.traversalStarted = ( pad != NULL );
	if ( pad && entryOrigin ) {
		bot.pathCorner = 0;
		bot.ResetTraversal();
	}
	if ( dodge ) {
		// Use normal no-route dodge admission and HoldDodgeJump, including
		// the fresh-press pulse and player physics. This never bypasses proof.
		bot.path.Clear();
		bot.pathCorner = 0;
		bot.ResetTraversal();
		bot.dodgeDirection = landing - origin;
		bot.dodgeDirection.z = 0.0f;
		bot.dodgeDirection.Normalize();
		bot.dodgeStartTime = gameLocal.time;
		bot.dodgeEndTime = gameLocal.time + BOT_DODGE_MSEC;
		bot.dodgeJump = true;
	}
	if ( airDirection ) {
		// The fixed-input bound: no route, no controller, only player physics.
		bot.path.Clear();
		bot.pathCorner = 0;
		bot.ResetTraversal();
		bot.probeAirActive = true;
		bot.probeAirDirection.Set( airDirection->x, airDirection->y, 0.0f );
		bot.probeAirDirection.Normalize();
	}
	if ( recover ) {
		// Exercise normal off-route movement with the supplied nearest-node
		// answer. Keep it only for this bounded diagnostic; physics and all
		// movement guards still run, and later goal selection resumes normally.
		bot.path.Clear();
		bot.pathCorner = 0;
		bot.ResetTraversal();
		bot.repathFailures = 2;
		bot.recoverValid = true;
		bot.recoverTarget = landing;
		// The replay observes up to thirty seconds. Do not replace its
		// supplied recovery goal with unrelated roaming after ten seconds.
		bot.nextGoalSelectTime = gameLocal.time + 3 * BOT_TRAVEL_TIMEOUT_MSEC;
		bot.recoverSearchTime = bot.nextGoalSelectTime;
	}
	if ( travel == NAVTRAVEL_WALK && !dodge && !airDirection && !naturalWalk ) {
		// A walking probe replays a post-impulse state: the thrown bot's own
		// recovery is what it measures.
		bot.knockbackTime = gameLocal.time;
		bot.airborneSince = gameLocal.time;
	}
	if ( probeHealth > 0 ) {
		// A replayed impulse is survived or not at the health it left, and
		// under the knockback time it started: no ground friction, and only
		// air acceleration even on the ground, as idPhysics_Player applies.
		self->health = probeHealth;
		if ( probeArmor >= 0 ) {
			self->inventory.armor = probeArmor;
		}
		probeArmor = -1;
		probeHealth = 0;
		static_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->RestoreKnockBack( probeKnockback );
		probeKnockback = 0;
		OnPlayerKnockback( self, probeKick );
		probeKick.Zero();
	}
	bot.probeWatch = true;
	bot.probeStartTime = gameLocal.time;
	bot.probeAirborneSeen = false;
	gameLocal.Printf( "BOT_%s_PROBE client=%d time=%d origin=%s velocity=%s landing=%s gravity=%.3f\n",
		airDirection ? "AIR" : ( dodge ? "DODGE" : ( pad ? "PAD" : ( travel == NAVTRAVEL_WALK ? "MOVE" : ( travel == NAVTRAVEL_DROP ? "DROP" : "JUMP" ) ) ) ),
		clientNum, gameLocal.time, origin.ToString( 3 ), velocity.ToString( 3 ), landing.ToString( 3 ),
		gameLocal.GetCurrentGravity( self ).Length() );
	return true;
}

/*
================
rvBotManager::SetProbeHealth
================
*/
bool rvBotManager::SetProbeHealth( int clientNum, int health, int knockbackMsec, const idVec3 &kick, int armor ) {
	if ( !gameLocal.isServer || gameLocal.isClient || !gameLocal.GetLocalPlayer() || gameLocal.numClients != 2 ||
		 NumBots() != 1 || !IsBot( clientNum ) || gameLocal.mpGame.IsManagedMatch() || !gameLocal.CheatsOk( false ) ||
		 health < 1 || health > 1000 || knockbackMsec < 0 || knockbackMsec > 1000 || armor < -1 || armor > 1000 ||
		 !( kick.LengthSqr() <= Square( 10000.0f ) ) ) {
		return false;
	}
	probeHealth = health;
	probeKnockback = knockbackMsec;
	probeArmor = armor;
	probeKick = kick;
	return true;
}

/*
================
rvBotManager::ListBots
================
*/
void rvBotManager::ListBots( bool weaponDetail ) const {
	int count = 0;

	// The match's own statistics, the ones the end-of-game screen shows, so
	// how well each bot fights can be read off a running server: every round
	// it fired, every one that hit, and the damage that did.
	gameLocal.Printf( "  cl  name             character        style        skill  kills deaths   hits/shots  acc  damage\n" );

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !bots[i].IsActive() ) {
			continue;
		}

		const rvBotCharacter *character = bots[i].GetCharacter();
		const rvPlayerStat *stat = statManager ? statManager->GetPlayerStat( i ) : NULL;
		int shots = 0;
		int hits = 0;
		if ( stat ) {
			for ( int w = 0; w < MAX_WEAPONS; w++ ) {
				shots += stat->weaponShots[w];
				hits += stat->weaponHits[w];
			}
		}

		gameLocal.Printf( "  %2d  %-16s %-16s %-12s %.1f  %5d %6d %6d/%-6d %3d%% %7d\n",
						  i,
						  bots[i].GetName(),
						  character ? character->GetName() : "-",
						  character ? character->GetStyleName() : "-",
						  bots[i].GetEffectiveSkill(),
						  stat ? stat->kills : 0,
						  stat ? stat->deaths : 0,
						  hits, shots,
						  shots > 0 ? ( hits * 100 ) / shots : 0,
						  stat ? stat->damageGiven : 0 );

		// Which guns the skill went into: a shotgun counts every pellet and a
		// lightning gun every tick, so the totals above mix very different rates.
		const idEntity *ent = gameLocal.entities[i];
		if ( weaponDetail && stat && ent && ent->IsType( idPlayer::GetClassType() ) ) {
			for ( int w = 0; w < MAX_WEAPONS; w++ ) {
				if ( stat->weaponShots[w] <= 0 && stat->weaponKills[w] <= 0 ) {
					continue;
				}
				gameLocal.Printf( "      weapon %-24s %6d/%-6d %3d%% %5d kills\n",
								  ent->spawnArgs.GetString( va( "def_weapon%d", w ), "?" ),
								  stat->weaponHits[w], stat->weaponShots[w],
								  stat->weaponShots[w] > 0 ? ( stat->weaponHits[w] * 100 ) / stat->weaponShots[w] : 0,
								  stat->weaponKills[w] );
			}
		}
		count++;
	}

	gameLocal.Printf( "%d bot%s, %d character%s loaded, navmesh %s (%d nodes, %d links, built in %d ms)\n",
					  count, count == 1 ? "" : "s",
					  botCharacterManager.NumCharacters(), botCharacterManager.NumCharacters() == 1 ? "" : "s",
					  navMesh.IsValid() ? "ready" : "not built",
					  navMesh.NumNodes(), navMesh.NumLinks(), navMesh.GetBuildMilliseconds() );
}

/*
================
rvBotManager::OnSpawnPlayer
================
*/
void rvBotManager::OnSpawnPlayer( int clientNum, bool isBot, const char *botName ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	if ( isBot ) {
		// A bot re-begun after a map change never goes through AddBot, so this
		// is the earliest point the new map's navmesh can be built.
		EnsureNavMesh();
		bots[clientNum].Init( clientNum, botName, pendingSkill, pendingExactCharacter );
		return;
	}

	// A human took a slot a bot used to hold.
	if ( bots[clientNum].IsActive() ) {
		bots[clientNum].Shutdown();
	}
}

/*
================
rvBotManager::OnPlayerRespawned
================
*/
void rvBotManager::OnPlayerRespawned( int clientNum ) {
	if ( !IsBot( clientNum ) ) {
		return;
	}

	bots[clientNum].OnSpawn();
}

/*
================
rvBotManager::OnClientDisconnect
================
*/
void rvBotManager::OnClientDisconnect( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	if ( leaderClientNum == clientNum ) {
		leaderClientNum = -1;
	}
	nextReplySourceTime[clientNum] = 0;
	ClearOrder( clientNum );

	// openQ4: a grudge is kept as a client slot. Left alone, it passed to
	// whoever joined into the slot next, who was hunted and taunted for a
	// kill they never made.
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( bots[i].revengeTarget == clientNum ) {
			bots[i].revengeTarget = -1;
		}
	}

	bots[clientNum].Shutdown();
}

/*
================
rvBotManager::OnMatchStart
================
*/
void rvBotManager::OnMatchStart( void ) {
	if ( !gameLocal.isMultiplayer || gameLocal.isClient ) {
		return;
	}

	// Scores are zeroed on the way into GAMEON, so nobody leads yet.
	leaderClientNum	= -1;
	nextLeaderCheck	= 0;

	botCharacterManager.ResetChatThrottle();
	ResetReplyCooldowns();

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( bots[i].IsActive() ) {
			bots[i].OnMatchStart();
		}
	}
}

/*
================
rvBotManager::OnMatchEnd
================
*/
void rvBotManager::OnMatchEnd( void ) {
	if ( !gameLocal.isMultiplayer || gameLocal.isClient ) {
		return;
	}

	// Red Rover uses teams for combat rules but ranks the match individually;
	// using teamScore there makes every bot on the final team claim a win.
	const bool useTeamScore = gameLocal.IsTeamGame() && gameLocal.gameType != GAME_REDROVER;
	const int forfeitWinner = useTeamScore ? gameLocal.mpGame.ForfeitTeam() : -1;
	bool topScoreFound = false;
	int topScore = 0;
	int topScoreCount = 0;

	if ( useTeamScore ) {
		for ( int i = 0; i < TEAM_MAX; i++ ) {
			const int score = gameLocal.mpGame.GetScoreForTeam( i );

			if ( !topScoreFound || score > topScore ) {
				topScore = score;
				topScoreFound = true;
				topScoreCount = 1;
			} else if ( score == topScore ) {
				topScoreCount++;
			}
		}
	} else {
		for ( int i = 0; i < gameLocal.numClients; i++ ) {
			idEntity *ent = gameLocal.entities[i];

			if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
				continue;
			}

			idPlayer *player = static_cast<idPlayer *>( ent );
			if ( !gameLocal.mpGame.CanPlay( player ) ||
				 ( gameLocal.gameType == GAME_DUEL && player->spectating ) ) {
				continue;
			}

			const int score = gameLocal.mpGame.GetScore( i );

			if ( !topScoreFound || score > topScore ) {
				topScore = score;
				topScoreFound = true;
				topScoreCount = 1;
			} else if ( score == topScore ) {
				topScoreCount++;
			}
		}
	}

	// Equal points are not a draw when one side has forfeited.  In that case the
	// surviving team is the authoritative winner even on a 0-0 board.
	const bool matchTied = topScoreFound && topScoreCount > 1 && forfeitWinner < 0;

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !bots[i].IsActive() ) {
			continue;
		}
		if ( matchTied ) {
			// There is no stock draw-chat category.  Silence is preferable to
			// every tied bot claiming a win or half of them conceding a loss.
			continue;
		}

		idEntity *ent = gameLocal.entities[i];
		idPlayer *player = ( ent && ent->IsType( idPlayer::GetClassType() ) ) ? static_cast<idPlayer *>( ent ) : NULL;
		if ( !player || !gameLocal.mpGame.CanPlay( player ) ||
			 ( gameLocal.gameType == GAME_DUEL && player->spectating ) ) {
			// Duel's waiting line did not contest this match and must not announce
			// a loss merely because it watched from spectator.
			continue;
		}

		bool won = false;

		// Zero and negative scoreboards are valid too, so no positive-score
		// sentinel is used.
		const bool validTeam = player->team >= 0 && player->team < TEAM_MAX;
		if ( useTeamScore && forfeitWinner >= 0 ) {
			won = validTeam && player->team == forfeitWinner;
		} else {
			const int score = ( useTeamScore && validTeam ) ?
				gameLocal.mpGame.GetScoreForTeam( player->team ) :
				gameLocal.mpGame.GetScore( i );

			won = topScoreFound && ( !useTeamScore || validTeam ) && score >= topScore;
		}

		bots[i].OnMatchEnd( won );
	}

	leaderClientNum = -1;
}

/*
================
rvBotManager::ShiftMatchTime

Every bot deadline moved forward by one frozen frame.  Hooked from
idMultiplayerGame::RebaseCompetitivePauseFrame, beside the game state's own
shift, because idGameLocal::RunFrame keeps advancing gameLocal.time through a
competitive pause while it skips rvBotManager::Think.
================
*/
void rvBotManager::ShiftMatchTime( int deltaMsec ) {
	if ( deltaMsec <= 0 ) {
		return;
	}

	const int maxInt = 0x7fffffff;
#define SHIFT_BOT_MANAGER_TIME( value ) \
	do { if ( ( value ) > 0 ) { ( value ) = ( value ) > maxInt - deltaMsec ? maxInt : ( value ) + deltaMsec; } } while ( 0 )

	SHIFT_BOT_MANAGER_TIME( nextMinPlayerCheck );
	SHIFT_BOT_MANAGER_TIME( nextLeaderCheck );
	SHIFT_BOT_MANAGER_TIME( nextProfileTime );

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		SHIFT_BOT_MANAGER_TIME( nextReplySourceTime[i] );
		SHIFT_BOT_MANAGER_TIME( noises[i].time );
	}

	// Item timers follow the items' own respawn events, which a pause carries
	// forward as well.
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		SHIFT_BOT_MANAGER_TIME( itemPickupAt[i] );
		SHIFT_BOT_MANAGER_TIME( itemRespawnAt[i] );
	}

#undef SHIFT_BOT_MANAGER_TIME

	botCharacterManager.ShiftChatThrottle( deltaMsec );

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( bots[i].IsActive() ) {
			bots[i].ShiftMatchTime( deltaMsec );
		}
	}
}

/*
================
rvBotManager::OnPlayerDeath

Hooked from idMultiplayerGame::PlayerDeath, after the scores have been
committed, so a bot reacting here already sees the post-frag board.
================
*/
void rvBotManager::OnPlayerDeath( idPlayer *dead, idPlayer *killer, int methodOfDeath ) {
	if ( !gameLocal.isMultiplayer || gameLocal.isClient || !dead ) {
		return;
	}

	const int deadNum	= dead->entityNumber;
	const int killerNum	= killer ? killer->entityNumber : -1;

	if ( IsBot( deadNum ) ) {
		bots[deadNum].OnKilledBy( killer, methodOfDeath );
	}

	// A suicide is a death and nothing else: nobody gets a kill line for it.
	if ( killer && killer != dead && IsBot( killerNum ) ) {
		bots[killerNum].OnKilledEnemy( dead, methodOfDeath );
	}
}

/*
================
rvBotManager::OnPlayerDamaged
================
*/
void rvBotManager::OnPlayerDamaged( idPlayer *victim, idEntity *attacker, int damage, const idVec3 &dir ) {
	if ( !gameLocal.isMultiplayer || gameLocal.isClient || !victim || !IsBot( victim->entityNumber ) ) {
		return;
	}

	idPlayer *source = NULL;
	if ( attacker && attacker->IsType( idPlayer::GetClassType() ) ) {
		source = static_cast<idPlayer *>( attacker );
	} else if ( attacker && attacker->IsType( idProjectile::GetClassType() ) ) {
		idEntity *owner = static_cast<idProjectile *>( attacker )->GetOwner();
		if ( owner && owner->IsType( idPlayer::GetClassType() ) ) {
			source = static_cast<idPlayer *>( owner );
		}
	}

	bots[victim->entityNumber].OnDamaged( source, damage, dir );
}

void rvBotManager::OnPlayerKnockback( idPlayer *victim, const idVec3 &kick ) {
	if ( !gameLocal.isMultiplayer || gameLocal.isClient || !victim || !IsBot( victim->entityNumber ) ) {
		return;
	}
	bots[victim->entityNumber].OnKnockback( kick );
}

void rvBot::OnKnockback( const idVec3 &kick ) {
	if ( kick.LengthSqr() <= 0.0f ) {
		return;
	}
	// Even a small nail or machine-gun impulse can spoil a ledge approach or
	// leave a committed jump unaffordable. Let the live landing proof decide
	// whether recovery is needed; kick magnitude alone cannot decide safety.
	if ( knockbackFrameTime != gameLocal.time ) {
		knockbackFrameTime = gameLocal.time;
		knockbackFrameKick.Zero();
	}
	knockbackFrameKick += kick;
	knockbackTime = gameLocal.time;
	airRecoverPlanTime = gameLocal.time - BOT_AIR_RECOVER_REPLAN_MSEC;
	airRecoverLanded = 0;
}

void rvBot::RefreshKnockbackMove( usercmd_t &cmd ) {
	idPlayer *self = GetPlayer();
	if ( !self || self->health <= 0 || self->spectating || probeAirActive ||
		 knockbackFrameTime != gameLocal.time || knockbackTime != gameLocal.time ) {
		return;
	}
	// Bot Think precedes entity Think. A weapon can hit this player between
	// command generation and its physics step. Recheck the changed flight
	// before consuming the old command, with the hit's final health/timer.
	// Keep the existing view, weapons and buttons; only recovery movement owns
	// this refresh. In particular, a safe pad flight retains its controller.
	if ( dodgeHopActive && knockbackTime >= dodgeHopStartTime ) {
		ResetDodgeJump();
	}
	if ( !self->pfl.onGround && !airborneSince ) {
		airborneSince = gameLocal.time;
		airborneVoluntary = false;
	}
	RecoverInAir( self, cmd );
}

void rvBotManager::RefreshKnockbackMove( idPlayer *player, usercmd_t &cmd ) {
	if ( gameLocal.isMultiplayer && !gameLocal.isClient && player && IsBot( player->entityNumber ) &&
		 !bot_pause.GetBool() && bots[player->entityNumber].knockbackFrameTime == gameLocal.time ) {
		const double start = idLib::sys->GetClockTicks();
		bots[player->entityNumber].RefreshKnockbackMove( cmd );
		profileFrameTicks += idLib::sys->GetClockTicks() - start;
	}
}

/*
================
rvBotManager::UpdateLeaderChat

idMultiplayerGame::UpdateLeader is declared and never defined, and the lead
announcements in CommonRun are written around gameLocal.GetLocalPlayer(), so
they never fire on a dedicated server and never fire for anyone but the local
client on a listen server.  The rank the bots react to is tracked here instead.
================
*/
void rvBotManager::UpdateLeaderChat( void ) {
	if ( gameLocal.time < nextLeaderCheck ) {
		return;
	}
	nextLeaderCheck = gameLocal.time + BOT_LEADER_CHECK_MSEC;

	if ( bot_chat.GetInteger() <= 0 ) {
		return;
	}

	int leader	= -1;
	int best	= 0;
	int shared	= 0;

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[i];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		if ( static_cast<idPlayer *>( ent )->spectating ) {
			continue;
		}

		const int score = gameLocal.mpGame.GetScore( i );

		if ( leader == -1 || score > best ) {
			best	= score;
			leader	= i;
			shared	= 1;
		} else if ( score == best ) {
			shared++;
		}
	}

	// Nobody leads on nothing, and a tie is not a lead change - without both
	// tests two bots trade "first place" lines every time either of them scores.
	if ( best <= 0 || shared > 1 ) {
		leader = -1;
	}

	if ( leader == leaderClientNum ) {
		return;
	}

	const int previous = leaderClientNum;

	leaderClientNum = leader;

	if ( previous >= 0 && IsBot( previous ) ) {
		idEntity *ent = ( leader >= 0 ) ? gameLocal.entities[leader] : NULL;
		idPlayer *player = ( ent && ent->IsType( idPlayer::GetClassType() ) ) ? static_cast<idPlayer *>( ent ) : NULL;

		bots[previous].QueueChat( BOTCHAT_LEAD_LOST, BotDisplayName( player ), NULL, NULL );
	}

	if ( leader >= 0 && IsBot( leader ) ) {
		idEntity *ent = ( previous >= 0 ) ? gameLocal.entities[previous] : NULL;
		idPlayer *player = ( ent && ent->IsType( idPlayer::GetClassType() ) ) ? static_cast<idPlayer *>( ent ) : NULL;

		bots[leader].QueueChat( BOTCHAT_LEAD_TAKEN, BotDisplayName( player ), NULL, NULL );
	}
}

/*
================
rvBotManager::CheckMinPlayers
================
*/
void rvBotManager::CheckMinPlayers( void ) {
	if ( gameLocal.time < nextMinPlayerCheck ) {
		return;
	}
	nextMinPlayerCheck = gameLocal.time + 2000;

	const int wanted = bot_minPlayers.GetInteger();
	if ( wanted <= 0 || !bot_enable.GetBool() ) {
		return;
	}

	// Only people who want to play count toward the target: a human watching
	// from spectator is not a player, and must not push a bot out of a match it
	// is filling.  Every connected client still occupies a slot, though.
	int players		= 0;
	int connected	= 0;
	int numBots		= 0;

	for ( int i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		connected++;
		if ( bots[i].IsActive() ) {
			numBots++;
			players++;
		} else if ( !static_cast<idPlayer *>( ent )->wantSpectate ) {
			players++;
		}
	}

	// Never fill the last public slot.  A server whose bots took every slot
	// answered "server full" to the very people the bots were keeping it warm
	// for; with one slot always open, the newcomer joins and a bot then makes
	// room on the next check.
	int maxPlayers = gameLocal.serverInfo.GetInt( "si_maxPlayers" );
	if ( maxPlayers <= 0 ) {
		maxPlayers = MAX_CLIENTS;
	}
	const int publicSlots = Max( 1, maxPlayers - Max( 0, gameLocal.serverInfo.GetInt( "si_privatePlayers" ) ) );

	if ( players < wanted && connected < publicSlots - 1 ) {
		// A refusal here is normally permanent for this map - no navigation
		// could be generated, the slots are full, bots are disabled - and
		// AddBot prints the reason itself.  Retrying on the ordinary two second
		// tick would repeat that line for the rest of the match, so an autofill
		// that is being refused backs off instead.
		if ( !AddBot( NULL ) ) {
			nextMinPlayerCheck = gameLocal.time + BOT_MINPLAYERS_BACKOFF_MSEC;
		}
	} else if ( numBots > 0 && ( players > wanted || connected >= publicSlots ) ) {
		RemoveBotAt( PickBotToRemove() );
	}
}

/*
================
rvBotManager::PickBotToRemove
================
*/
int rvBotManager::PickBotToRemove( void ) const {
	int teamCount[TEAM_MAX];
	memset( teamCount, 0, sizeof( teamCount ) );

	const bool teamGame = gameLocal.IsTeamGame();
	if ( teamGame ) {
		for ( int i = 0; i < gameLocal.numClients; i++ ) {
			idEntity *ent = gameLocal.entities[i];
			idPlayer *player = ( ent && ent->IsType( idPlayer::GetClassType() ) ) ? static_cast<idPlayer *>( ent ) : NULL;
			if ( player && !player->wantSpectate && player->team >= 0 && player->team < TEAM_MAX ) {
				teamCount[player->team]++;
			}
		}
	}

	rvGameState *state = gameLocal.mpGame.GetGameState();
	const mpGameState_t matchState = state ? state->GetMPGameState() : INACTIVE;
	const bool matchLive = matchState == COUNTDOWN || matchState == GAMEON || matchState == SUDDENDEATH;
	const bool seatedMode = gameLocal.gameType == GAME_DUEL || gameLocal.gameType == GAME_TOURNEY;
	const bool roundLive = state && MPGameTypeHasAny( gameLocal.gameType, GTF_ROUND ) &&
		state->IsType( rvRoundGameState::GetClassType() ) &&
		static_cast<rvRoundGameState *>( state )->RoundIsLive();

	int best = -1;
	int bestScore = 0;
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !bots[i].IsActive() ) {
			continue;
		}

		int score = i;		// later slots first, deterministically
		idPlayer *player = bots[i].GetPlayer();
		if ( !player ) {
			score += 1000;	// not even spawned: free to go
		} else {
			const bool playing = !player->spectating && player->health > 0;

			// Removing someone mid-duel forfeits it, and pulling a living
			// player out of a live round changes who wins it; those wait.
			if ( playing && matchLive && seatedMode ) {
				continue;
			}
			if ( playing && roundLive ) {
				continue;
			}

			if ( !playing ) {
				score += 400;	// dead or queued: nothing is lost
			}
			if ( teamGame && player->team >= 0 && player->team < TEAM_MAX ) {
				const int other = gameLocal.mpGame.OpposingTeam( player->team );
				if ( other >= 0 && other < TEAM_MAX ) {
					// The larger team gives up a player; the smaller never does.
					score += ( teamCount[player->team] - teamCount[other] ) * 300;
				}
			}
		}

		if ( best < 0 || score > bestScore ) {
			best = i;
			bestScore = score;
		}
	}

	return best;
}

/*
================
rvBotManager::RefreshWorldCache
================
*/
void rvBotManager::RefreshWorldCache( void ) {
	items.SetNum( 0, false );

	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( ent->IsType( idItem::GetClassType() ) && ent->GetPhysics() ) {
			items.Append( static_cast<idItem *>( ent ) );
		}
	}

	// This frame's noises: weapon fire and running footsteps.  Pickups are
	// recorded as they happen by OnItemPickup.
	for ( int i = 0; i < gameLocal.numClients && i < MAX_CLIENTS; i++ ) {
		idEntity *ent = gameLocal.entities[i];
		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		idPlayer *player = static_cast<idPlayer *>( ent );
		if ( player->spectating || player->health <= 0 || !gameLocal.mpGame.CanPlay( player ) ) {
			continue;
		}

		const idVec3 &origin = player->GetPhysics()->GetOrigin();
		if ( gameLocal.usercmds && ( gameLocal.usercmds[i].buttons & BUTTON_ATTACK ) &&
			 player->weapon && !player->weapon->IsHolstered() ) {
			const char *weaponClass = player->spawnArgs.GetString( va( "def_weapon%d", player->GetCurrentWeapon() ), "" );
			const bool quiet = !idStr::Icmp( weaponClass, "weapon_gauntlet" );
			RecordNoise( i, origin, quiet ? BOT_NOISE_QUIET_FIRE_RANGE : BOT_NOISE_FIRE_RANGE );
			continue;
		}

		idVec3 velocity = player->GetPhysics()->GetLinearVelocity();
		velocity.z = 0.0f;
		if ( player->pfl.onGround && velocity.LengthSqr() > Square( BOT_NOISE_STEP_SPEED ) ) {
			RecordNoise( i, origin, BOT_NOISE_STEP_RANGE );
		}
	}
}

/*
================
rvBotManager::RecordNoise

Keeps the loudest noise of the current moment per player.  A quieter noise
does not overwrite a louder one that is still fresh, so footsteps between two
shots do not shrink the gunfire's reach.
================
*/
void rvBotManager::RecordNoise( int clientNum, const idVec3 &origin, float range ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	botNoise_t &noise = noises[clientNum];
	if ( noise.time > 0 && gameLocal.time - noise.time < BOT_NOISE_MEMORY_MSEC && noise.range > range ) {
		noise.origin = origin;		// the louder noise moves with its maker
		return;
	}

	noise.origin = origin;
	noise.range = range;
	noise.time = gameLocal.time;
}

/*
================
rvBotManager::ResetAwareness
================
*/
void rvBotManager::ResetAwareness( void ) {
	for ( int i = 0; i < MAX_GENTITIES; i++ ) {
		itemTimerSpawnId[i] = -1;
		itemPickupAt[i] = 0;
		itemRespawnAt[i] = 0;
		itemTimerKnownBy[i] = 0;
	}
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		noises[i].origin.Zero();
		noises[i].range = 0.0f;
		noises[i].time = 0;
	}
}

/*
================
rvBotManager::OnItemPickup

Called from idItem::Pickup for every server-side pickup.  respawnSeconds is
zero when the item will not come back.  The timer is learned only by bots that
could have noticed: the taker, anyone within earshot of the pickup, and
everyone for an item whose acquire sound is played to the whole server.  Each
witness then keeps it or not by its own itemTiming - a novice does not count.
================
*/
void rvBotManager::OnItemPickup( idItem *item, idPlayer *player, float respawnSeconds ) {
	if ( !item || !gameLocal.isMultiplayer || gameLocal.isClient ||
		 item->entityNumber < 0 || item->entityNumber >= MAX_GENTITIES ) {
		return;
	}

	const int number = item->entityNumber;
	itemTimerSpawnId[number] = gameLocal.spawnIds[number];
	itemPickupAt[number] = gameLocal.time;
	itemRespawnAt[number] = ( respawnSeconds > 0.0f ) ? gameLocal.time + SEC2MS( respawnSeconds ) : 0;
	itemTimerKnownBy[number] = 0;

	const idVec3 origin = item->GetPhysics() ? item->GetPhysics()->GetOrigin() : vec3_zero;
	if ( player && player->entityNumber >= 0 && player->entityNumber < MAX_CLIENTS ) {
		RecordNoise( player->entityNumber, origin, BOT_NOISE_PICKUP_RANGE );
	}

	if ( !itemRespawnAt[number] ) {
		return;
	}

	const bool announced = item->spawnArgs.GetBool( "globalAcquireSound", "0" );
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !bots[i].IsActive() ) {
			continue;
		}
		idPlayer *witness = bots[i].GetPlayer();
		if ( !witness || witness->spectating || witness->GetInstance() != item->GetInstance() ) {
			continue;
		}

		const bool noticed = announced || witness == player ||
			( witness->GetPhysics()->GetOrigin() - origin ).LengthSqr() <=
				Square( BOT_PICKUP_WITNESS_RANGE * bots[i].traits.hearing );
		if ( noticed && gameLocal.random.RandomFloat() < bots[i].traits.itemTiming ) {
			itemTimerKnownBy[number] |= 1u << i;
		}
	}
}

/*
================
rvBotManager::KnownItemRespawn

The respawn time this bot believes, with a personal error that shrinks with
itemTiming and stays the same for the same bot and item, so a belief does not
jitter from one decision tick to the next.
================
*/
int rvBotManager::KnownItemRespawn( const idItem *item, int clientNum ) const {
	if ( !item || clientNum < 0 || clientNum >= MAX_CLIENTS ||
		 item->entityNumber < 0 || item->entityNumber >= MAX_GENTITIES ) {
		return 0;
	}

	const int number = item->entityNumber;
	if ( itemTimerSpawnId[number] != gameLocal.spawnIds[number] || itemRespawnAt[number] <= 0 ||
		 !( itemTimerKnownBy[number] & ( 1u << clientNum ) ) ) {
		return 0;
	}

	const unsigned int hash = (unsigned int)( number * 2654435761u ) ^ (unsigned int)( clientNum * 40503u );
	const float unit = (float)( hash % 2001u ) / 1000.0f - 1.0f;		// -1 .. 1
	const float error = unit * 3000.0f * ( 1.0f - idMath::ClampFloat( 0.0f, 1.0f, bots[clientNum].traits.itemTiming ) );

	return Max( 1, itemRespawnAt[number] + (int)error );
}

/*
================
rvBotManager::ItemPickupTime
================
*/
int rvBotManager::ItemPickupTime( const idItem *item ) const {
	if ( !item || item->entityNumber < 0 || item->entityNumber >= MAX_GENTITIES ||
		 itemTimerSpawnId[item->entityNumber] != gameLocal.spawnIds[item->entityNumber] ) {
		return -1;
	}

	return itemPickupAt[item->entityNumber];
}

/*
================
rvBotManager::UpdateProfile

bot_profile <seconds> prints what the bots cost the server frame and what the
navigation queries behind that cost were, then starts a fresh window.  It is
the measurement every routing or perception change is judged by.
================
*/
void rvBotManager::UpdateProfile( double thinkTicks ) {
	profileThinkTicks += thinkTicks;
	profileMaxTicks = Max( profileMaxTicks, thinkTicks );
	profileFrames++;

	const int period = bot_profile.GetInteger();
	if ( period <= 0 ) {
		profileThinkTicks = 0.0;
		profileMaxTicks = 0.0;
		profileFrames = 0;
		nextProfileTime = 0;
		profileDistribution.Clear();
		navMesh.ResetStats();
		return;
	}
	const double msecPerTick = 1000.0 / idLib::sys->ClockTicksPerSecond();
	profileDistribution.Add( thinkTicks * msecPerTick );
	if ( !nextProfileTime ) {
		nextProfileTime = gameLocal.time + SEC2MS( period );
		return;
	}
	if ( gameLocal.time < nextProfileTime ) {
		return;
	}

	const navStats_t &nav = navMesh.GetStats();
	const int frames = Max( 1, profileFrames );

	gameLocal.Printf( "botprofile: %d bots %d frames think avg %.3f ms max %.3f ms | "
					  "paths %d fail %d quick %d repair %d expand %.0f/search | "
					  "fields %d settle %.0f/field | nav %.3f ms/frame (fields %.3f) | "
					  "fail snap %d search %d leg %d\n",
					  NumBots(), profileFrames,
					  profileThinkTicks * msecPerTick / frames, profileMaxTicks * msecPerTick,
					  nav.pathSearches, nav.pathFailures, nav.quickRejects, nav.repairSearches,
					  (double)nav.nodesExpanded / Max( 1, nav.pathSearches - nav.quickRejects ),
					  nav.fieldSearches,
					  (double)nav.fieldNodes / Max( 1, nav.fieldSearches ),
					  nav.searchTicks * msecPerTick / frames, nav.fieldTicks * msecPerTick / frames,
					  nav.snapFailures, nav.searchFailures, nav.legFailures );
	gameLocal.Printf( "botprofile-percentiles: %u frames p50_upper %.3f ms p95_upper %.3f ms p99_upper %.3f ms\n",
		profileDistribution.Samples(), profileDistribution.PercentileUpperMS( 50 ),
		profileDistribution.PercentileUpperMS( 95 ), profileDistribution.PercentileUpperMS( 99 ) );

	profileThinkTicks = 0.0;
	profileMaxTicks = 0.0;
	profileFrames = 0;
	profileDistribution.Clear();
	nextProfileTime = gameLocal.time + SEC2MS( period );
	navMesh.ResetStats();
}

/*
================
rvBotManager::Think
================
*/
void rvBotManager::Think( void ) {
	profileFrameNumber = -1;
	profileFrameTicks = 0.0;
	if ( !gameLocal.isMultiplayer || gameLocal.isClient || !gameLocal.isServer ) {
		return;
	}

	CheckMinPlayers();
	UpdateLeaderChat();

	if ( !gameLocal.usercmds ) {
		return;
	}

	// Bots are client slots and survive a map change; the navmesh describes the
	// map and does not.  Rebuild it here rather than only in AddBot, or bots
	// carried into the next map would stand still for the rest of it.
	if ( !navMesh.IsValid() && NumBots() > 0 ) {
		if ( !EnsureNavMesh() ) {
			return;
		}
	}

	const double thinkStart = idLib::sys->GetClockTicks();

	// openQ4: only bots read this cache, and it was rebuilt every frame on
	// servers that have none: a walk over every spawned entity, and a weapon
	// definition lookup for each player firing.
	if ( NumBots() > 0 ) {
		RefreshWorldCache();
	}

	// The engine hands the game a const view of its own user command array for
	// this frame; a bot's slot is ours to fill in, exactly where a remote
	// player's packet would have landed.
	usercmd_t *cmds = const_cast<usercmd_t *>( gameLocal.usercmds );

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		if ( bots[i].IsActive() ) {
			bots[i].Think( cmds[i] );
		}
	}

	profileFrameTicks += idLib::sys->GetClockTicks() - thinkStart;
	profileFrameNumber = gameLocal.framenum;

	if ( bot_debugNav.GetInteger() >= 1 && navMesh.IsValid() ) {
		idPlayer *viewer = gameLocal.GetLocalPlayer();
		if ( viewer ) {
			navMesh.DebugDraw( viewer->GetPhysics()->GetOrigin(), 1024.0f );
		}
	}
}

void rvBotManager::FinishFrameProfile( void ) {
	// Include movement refreshed between entity thinks in this same frame's
	// mean, maximum and distribution. Paused/skipped bot frames add no sample.
	if ( profileFrameNumber == gameLocal.framenum ) {
		UpdateProfile( profileFrameTicks );
		profileFrameNumber = -1;
	}
}
