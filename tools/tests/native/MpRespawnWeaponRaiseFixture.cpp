// Executes a multiplayer client's weapon bookkeeping across its owner's respawn: the
// snapshot reader's hitch test, its weapon-change and weapon-entity blocks and its respawn
// branch, idPlayer::UpdateWeapon's weapon creation, and rvWeapon::PlayAnim with the raise
// start it takes from the server.  tools/tests/mp_predicted_hitscan_state.py splices them in
// verbatim from src/mpgame/Player.cpp and src/mpgame/Weapon.cpp.
//
// The server makes a player's weapon for a new life on the frame after the respawn and
// raises it at once.  The client must raise its own from that game time, not catch it
// straight up to ready (it then fired shots the server never fired) and not start it when
// the snapshot arrives (it would then be ready, and fire, late).  Only a respawn seen across
// a genuine state hitch, or a weapon swap the client missed, still catches up.

#include <cassert>
#include <cstdio>
#include <cstring>

template<class T> static T Min( T a, T b ) { return a < b ? a : b; }
static int FRAME2MS( int framenum ) { return ( framenum * 1000 ) / 60; }

static const int ANIMCHANNEL_ALL = 0;
static const int BUTTON_ATTACK = 1;
static const int RAISE_MSEC = 500;
static const int PREDICTION_LEAD_MSEC = 34;
// PRODUCTION_CONSTANTS

enum weaponStatus_t { WP_READY, WP_OUTOFAMMO, WP_RELOAD, WP_HOLSTERED, WP_RISING, WP_LOWERING };

struct idCVarStub {
	bool GetBool( void ) const { return false; }
};
static idCVarStub net_mpHitscanDebug;

struct idGameLocalStub {
	bool	isClient = true;
	bool	isMultiplayer = true;
	int		time = 0;
	int		localClientNum = 0;
	int		clientAckSequence = 0;
	int		GetMSec( void ) const { return 16; }
	void	Printf( const char *, ... ) {}
	void	Warning( const char *, ... ) {}
};
static idGameLocalStub gameLocal;

struct idCommonStub {
	void Warning( const char *, ... ) {}
};
static idCommonStub commonStub;
static idCommonStub *common = &commonStub;

struct AnimBlend {
	int start = 0;
	int end = 0;
	int GetEndTime( void ) const { return end; }
};

struct Animator {
	AnimBlend	blends[ 4 ];
	int			GetAnim( const char * ) { return 1; }
	void		Clear( int, int, int ) {}
	void		PlayAnim( int channel, int, int startTime, int ) { blends[ channel ].start = startTime; blends[ channel ].end = startTime + RAISE_MSEC; }
	AnimBlend *	CurrentAnim( int channel ) { return &blends[ channel ]; }
};

struct ViewModel {
	void			Show( void ) {}
	const char *	GetName( void ) const { return "view"; }
	const char *	GetEntityDefName( void ) const { return "weapon_machinegun"; }
};

class idPlayer;

class rvWeapon {
public:
	idPlayer *		owner = nullptr;
	ViewModel		view;
	ViewModel *		viewModel = &view;
	Animator		viewAnim;
	Animator		worldAnim;
	Animator *		viewAnimator = &viewAnim;
	Animator *		worldAnimator = &worldAnim;
	weaponStatus_t	status = WP_HOLSTERED;
	int				netRaiseStartTime = -1;
	int				animDoneTime[ 4 ] = {};
	int				catchups = 0;

	void	PlayAnim( int channel, const char *animname, int blendFrames );
	void	SetNetRaiseStartTime( int time );
	void	NetCatchup( void );
	void	ExecuteState( const char * ) { catchups++; status = WP_READY; }
	void	Reset( void ) { viewAnim = Animator(); worldAnim = Animator(); status = WP_HOLSTERED; netRaiseStartTime = -1; catchups = 0; }
	// What every raise state does on its first frame.
	void	Raise( void ) { status = WP_RISING; PlayAnim( ANIMCHANNEL_ALL, "raise", 0 ); }
};

struct EntityPtr {
	int		spawnId = 7;
	int		GetSpawnId( void ) const { return spawnId; }
	bool	SetSpawnId( int id ) { if ( id == spawnId ) { return false; } spawnId = id; return true; }
	bool	IsValid( void ) const { return true; }
	bool	operator!( void ) const { return false; }
};

struct playerPState_t {
	int movementTime = 0;
	int movementFlags = 0;
};

struct Usercmd {
	int buttons = 0;
};

// The respawn branch latches the weapon mods the snapshot carried across Init().
static const int MAX_WEAPONS = 16;

struct Inventory {
	int powerups = 0;
	int weaponMods[ MAX_WEAPONS ] = {};
};

struct Snapshot {
	int		sequence;
	int		time;
	int		health;
	int		idealWeapon;
	bool	hasWeaponState;
	int		viewSpawnId = 7;
	int		worldSpawnId = 9;
	int		knockBack = 0;		// what is left of the spawn's knockback
};

class idPlayer {
public:
	int				entityNumber = 0;
	int				health = 100;
	int				idealWeapon = 1;
	int				currentWeapon = -1;
	int				clientIdealWeaponPredictFrame = -1;
	int				snapshotSequence = 0;
	unsigned int	lastSnapshotSequence = 0;
	bool			weaponCatchup = false;
	int				netWeaponRaiseTime = -1;
	bool			spectating = false;
	int				creations = 0;
	Usercmd			usercmd;
	Inventory		inventory;
	EntityPtr		weaponViewModel;
	EntityPtr		weaponWorldModel;
	struct {
		int knockBack = 0;
		void EnableClip( void ) {}
		int GetKnockBack( void ) const { return knockBack; }
	} physicsObj;
	rvWeapon		storage;
	rvWeapon *		weapon = nullptr;

	idPlayer() { weaponWorldModel.spawnId = 9; }

	void	SetWeapon( int index ) {
		if ( weapon && index == currentWeapon ) {
			return;
		}
		weapon = nullptr;
		currentWeapon = index;
		if ( index < 0 ) {
			return;
		}
		storage.Reset();
		storage.owner = this;
		weapon = &storage;
		creations++;
	}
	// The parts of idPlayer::Init the snapshot bookkeeping depends on.
	void	Init( void ) { currentWeapon = -1; idealWeapon = -1; lastSnapshotSequence = 0; netWeaponRaiseTime = -1; }
	void	StopFiring( void ) {}
	void	UpdateHudWeapon( void ) {}
	void	StopRagdoll( void ) {}
	void	SetPhysics( void * ) {}
	void	SetCombatContents( bool ) {}

	void	ReadFromSnapshot( const Snapshot &snapshot );
	void	UpdateWeapon( void );
	void	EnterSpectate( void );
	// A client frame after the snapshot: the weapon is made, and on the newest predicted
	// frame a weapon still holstered starts its raise.
	void	Think( void ) {
		gameLocal.time += PREDICTION_LEAD_MSEC;
		if ( spectating ) {
			return;
		}
		UpdateWeapon();
		if ( weapon && weapon->status == WP_HOLSTERED ) {
			weapon->Raise();
		}
	}
};

void idPlayer::ReadFromSnapshot( const Snapshot &snapshot ) {
	bool stateHitch;
	int oldHealth;
	const int newIdealWeapon = snapshot.idealWeapon;
	const int newWeaponSpawnId = snapshot.viewSpawnId;
	const int newWeaponWorldSpawnId = snapshot.worldSpawnId;
	const bool hasWeaponState = snapshot.hasWeaponState;
	const playerPState_t decodedPhysics;
	(void)decodedPhysics;
	gameLocal.time = snapshot.time;
	snapshotSequence = snapshot.sequence;
	// ApplySnapshotState, ahead of the weapon blocks.
	physicsObj.knockBack = snapshot.knockBack;

	// PRODUCTION_HITCH

	lastSnapshotSequence = snapshotSequence;
	health = snapshot.health;

	// PRODUCTION_WEAPON_CHANGE

	if ( oldHealth > 0 && health <= 0 ) {
		// The death branch deletes the weapon.
		weapon = nullptr;
		currentWeapon = -1;
	}
	// PRODUCTION_RESPAWN
}

void idPlayer::UpdateWeapon( void ) {
	// PRODUCTION_UPDATE_WEAPON
}

// What idPlayer::Spectate( true ) does to the snapshot bookkeeping; the weapon goes with it.
void idPlayer::EnterSpectate( void ) {
	// PRODUCTION_SPECTATE
	weapon = nullptr;
	currentWeapon = -1;
	spectating = true;
}

// PRODUCTION_WEAPON

static int failures = 0;

static void Check( bool condition, const char *what ) {
	if ( !condition ) {
		std::printf( "FAIL: %s\n", what );
		failures++;
	}
}

// Alive with the machinegun at sequence 10, dead from 11 to 19, then the respawn snapshot.
static void Live( idPlayer &player, int respawnSequence, bool serverWeapon, int knockBack = 0 ) {
	player.ReadFromSnapshot( { 10, 1000, 100, 1, true } );
	player.Think();
	Check( player.weapon != nullptr, "the first weapon is made" );
	for ( int sequence = 11; sequence < 20; sequence++ ) {
		player.ReadFromSnapshot( { sequence, 1000 + ( sequence - 10 ) * 33, 0, 1, false } );
		player.Think();
	}
	Check( player.weapon == nullptr, "the dead player has no weapon" );
	Snapshot respawn = { respawnSequence, 2000, 100, 1, serverWeapon };
	respawn.knockBack = knockBack;
	player.ReadFromSnapshot( respawn );
	player.Think();
	player.ReadFromSnapshot( { respawnSequence + 1, 2033, 100, 1, true } );
	player.Think();
}

int main( void ) {
	// The respawn snapshot shows the player alive before the server's weapon exists: that
	// weapon is made, and starts its raise, on the server's next frame.
	{
		idPlayer player;
		Live( player, 20, false );
		Check( player.weapon != nullptr, "the weapon for the new life is made" );
		Check( player.weapon && player.weapon->catchups == 0, "a respawn without a hitch does not catch the weapon up" );
		Check( player.weapon && player.weapon->viewAnim.blends[ ANIMCHANNEL_ALL ].start == 2016,
			"the raise starts on the server's next frame" );
		Check( player.weapon && player.weapon->animDoneTime[ ANIMCHANNEL_ALL ] == 2016 + RAISE_MSEC,
			"the raise ends when the server's does" );
		Check( player.weapon && player.weapon->worldAnim.blends[ ANIMCHANNEL_ALL ].start == 2016,
			"the world model raises with the view model" );
		Check( player.netWeaponRaiseTime == -1 && !player.weaponCatchup, "the respawn's raise time is spent" );
		Check( player.lastSnapshotSequence == 21, "the snapshot sequence runs on through the respawn" );

		// A later raise (a weapon swap) plays from its own frame.
		player.weapon->status = WP_HOLSTERED;
		gameLocal.time = 4000;
		player.weapon->Raise();
		Check( player.weapon->viewAnim.blends[ ANIMCHANNEL_ALL ].start == 4000, "a later raise plays from now" );
	}

	// A snapshot without the server's weapon ignores the spawn clock: the weapon is made next.
	{
		idPlayer player;
		Live( player, 20, false, 68 );
		Check( player.weapon && player.weapon->viewAnim.blends[ ANIMCHANNEL_ALL ].start == 2016,
			"without the server's weapon the raise starts next frame whatever the clock says" );
	}

	// The respawn snapshot already carries the server's weapon. The spawn's knockback, run
	// down a frame at a time from the spawn's own frame, says which frame made it; a clock
	// that does not read as one, or has run out, leaves the snapshot's own frame.
	const struct { int knockBack; int start; const char *what; } clocks[] = {
		{ 84, 2000, "a clock one frame down places the spawn on the snapshot's frame" },
		{ 68, 1984, "a clock two frames down places the spawn a frame before the snapshot" },
		{ 52, 1968, "a clock three frames down places the spawn two frames before the snapshot" },
		{ 0, 2000, "a finished clock leaves the snapshot's frame" },
		{ 74, 2000, "a knockback that is not the spawn's clock leaves the snapshot's frame" },
		{ 100, 2000, "a clock that has not run leaves the snapshot's frame" },
	};
	for ( const auto &clock : clocks ) {
		idPlayer player;
		Live( player, 20, true, clock.knockBack );
		Check( player.weapon && player.weapon->catchups == 0, "a respawn with the server's weapon does not catch up" );
		Check( player.weapon && player.weapon->viewAnim.blends[ ANIMCHANNEL_ALL ].start == clock.start, clock.what );
	}

	// A join from spectating brings new weapon entities: the weapon for them raises with the
	// server's, unless they arrive across a hitch.
	for ( int hitch = 0; hitch != 2; hitch++ ) {
		idPlayer player;
		player.ReadFromSnapshot( { 40, 4000, 100, 1, true } );
		player.Think();
		Snapshot join = { hitch ? 43 : 41, 4033, 100, 1, false };
		join.viewSpawnId = 17;
		join.worldSpawnId = 19;
		player.ReadFromSnapshot( join );
		player.Think();
		Check( player.weapon && player.weapon->catchups == hitch, "new weapon entities catch up only across a hitch" );
		if ( !hitch ) {
			Check( player.weapon && player.weapon->viewAnim.blends[ ANIMCHANNEL_ALL ].start == 4049,
				"the weapon for new entities raises with the server's" );
		}
	}

	// Spectating runs Init() too. Its next snapshots, which carry the server's ideal weapon,
	// must not read as a hitch, and a catch-up still waiting for the last life's weapon must
	// not outlive it: the weapon for the next join raises with the server's.
	for ( int pending = 0; pending != 2; pending++ ) {
		idPlayer player;
		player.ReadFromSnapshot( { 70, 7000, 100, 1, true } );
		player.Think();
		if ( pending ) {
			// A weapon swap missed across a hitch, then straight to spectating.
			player.ReadFromSnapshot( { 73, 7100, 100, 2, true } );
			Check( player.weaponCatchup, "the missed swap asks for a catch-up" );
		}
		player.EnterSpectate();
		const int last = pending ? 73 : 70;
		for ( int sequence = last + 1; sequence <= last + 6; sequence++ ) {
			player.ReadFromSnapshot( { sequence, 7100 + ( sequence - last ) * 33, 100, 1, false } );
			player.Think();
		}
		Check( !player.weaponCatchup, "spectating leaves no catch-up standing" );
		player.spectating = false;
		Snapshot join = { last + 7, 8000, 100, 1, false };
		join.viewSpawnId = 37;
		join.worldSpawnId = 39;
		player.ReadFromSnapshot( join );
		player.Think();
		Check( player.weapon && player.weapon->catchups == 0, "the weapon for a join after spectating is not caught up" );
		Check( player.weapon && player.weapon->viewAnim.blends[ ANIMCHANNEL_ALL ].start == 8016,
			"the weapon for a join after spectating raises with the server's" );
	}

	// A respawn seen across a genuine hitch still catches up.
	{
		idPlayer player;
		Live( player, 21, false );
		Check( player.weapon && player.weapon->catchups == 1, "a respawn across a hitch catches the weapon up" );
	}

	// A catch-up drops a raise time it was given.
	{
		rvWeapon weapon;
		weapon.SetNetRaiseStartTime( 100 );
		weapon.NetCatchup();
		weapon.status = WP_HOLSTERED;
		gameLocal.time = 900;
		weapon.Raise();
		Check( weapon.viewAnim.blends[ ANIMCHANNEL_ALL ].start == 900, "a raise after a catch-up plays from now" );
	}

	// A weapon swap the client missed across a hitch catches up; one it saw does not.
	for ( int hitch = 0; hitch != 2; hitch++ ) {
		idPlayer player;
		player.ReadFromSnapshot( { 30, 3000, 100, 1, true } );
		player.Think();
		player.ReadFromSnapshot( { hitch ? 33 : 31, 3033, 100, 2, true } );
		Check( player.weaponCatchup == ( hitch != 0 ), "only a missed weapon swap catches up" );
	}

	if ( failures ) {
		std::printf( "respawn weapon raise: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "respawn weapon raise: PASS\n" );
	return 0;
}
