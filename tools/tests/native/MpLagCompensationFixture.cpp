// Executes the production multiplayer lag-compensation functions against a mock
// server: the history ring, the rewind estimate, the advertised remote extrapolation
// and the rewind/restore bracket. tools/tests/mp_lag_compensation_state.py splices
// the functions in verbatim from src/mpgame/Game_local.cpp, so there is no second
// implementation of the rewind to drift from the real one.

#include "src/idlib/NumericString.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <string.h>

#ifndef NULL
#define NULL nullptr
#endif

static const int MAX_CLIENTS = 32;

template<class T> static T Min( T a, T b ) { return a < b ? a : b; }
template<class T> static T Max( T a, T b ) { return a > b ? a : b; }

struct idMath {
	static int ClampInt( int min, int max, int value ) {
		return value < min ? min : ( value > max ? max : value );
	}
};

struct idVec3 {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	idVec3() {}
	idVec3( float ax, float ay, float az ) : x( ax ), y( ay ), z( az ) {}
	idVec3 operator+( const idVec3 &o ) const { return idVec3( x + o.x, y + o.y, z + o.z ); }
	idVec3 operator-( const idVec3 &o ) const { return idVec3( x - o.x, y - o.y, z - o.z ); }
	idVec3 operator*( float scale ) const { return idVec3( x * scale, y * scale, z * scale ); }
	friend idVec3 operator*( float scale, const idVec3 &v ) { return v * scale; }
	void Normalize() { const float length = std::sqrt( *this * *this ); if ( length > 0 ) { *this = *this * ( 1.0f / length ); } }
	float operator*( const idVec3 &o ) const { return x * o.x + y * o.y + z * o.z; }
	idVec3 &operator+=( const idVec3 &o ) { x += o.x; y += o.y; z += o.z; return *this; }
	bool Near( const idVec3 &o, float epsilon = 0.001f ) const {
		return std::fabs( x - o.x ) < epsilon && std::fabs( y - o.y ) < epsilon && std::fabs( z - o.z ) < epsilon;
	}
};

struct idMat3 {
	float yaw = 0.0f;
	bool operator==( const idMat3 &o ) const { return yaw == o.yaw; }
};

static const idVec3 vec3_origin;
static const idMat3 mat3_identity;

struct contactInfo_t {
	int entityNum = 0;
	idVec3 point;
	idVec3 normal;
	float dist = 0.0f;
};

struct trace_t {
	float fraction = 0.5f;
	contactInfo_t c;
	idVec3 endpos;
};

class idPhysics {
public:
	idVec3 origin;
	idMat3 axis;
	const idVec3 &GetOrigin() const { return origin; }
	const idMat3 &GetAxis() const { return axis; }
	void SetOrigin( const idVec3 &value ) { origin = value; }
	void SetAxis( const idMat3 &value ) { axis = value; }
};

struct idTypeInfo { int id; };

struct idActor {
	static const idTypeInfo &GetClassType() { static idTypeInfo type = { 2 }; return type; }
};

class idEntity {
public:
	virtual ~idEntity() {}
	int entityNumber = 0;
	virtual bool IsType( const idTypeInfo &type ) const { return false; }
	virtual idPhysics *GetPhysics() const { return physics; }
	idPhysics *physics = nullptr;
};

class idPlayer : public idEntity {
public:
	static const idTypeInfo &GetClassType() { static idTypeInfo type = { 1 }; return type; }
	bool IsType( const idTypeInfo &type ) const override { return type.id == GetClassType().id || type.id == idActor::GetClassType().id; }
	int GetInstance() const { return instance; }
	bool spectating = false;
	int health = 100;
	int instance = 0;
	idPhysics playerPhysics;
	idPhysics ragdollPhysics;
	idPlayer() { physics = &playerPhysics; }
};

class idDict {
public:
	std::map<std::string, std::string> values;
	void Set( const char *key, const char *value ) { values[key] = value; }
	void Clear() { values.clear(); }
	const char *GetString( const char *key, const char *defaultString = "" ) const {
		auto found = values.find( key );
		return found == values.end() ? defaultString : found->second.c_str();
	}
};

class idCVar {
public:
	explicit idCVar( int value ) : value( value ) {}
	int GetInteger() const { return value; }
	bool GetBool() const { return value != 0; }
	void SetInteger( int newValue ) { value = newValue; sets++; }
	int value;
	int sets = 0;
};

static idCVar net_mpLagCompensation( 1 );
static idCVar net_mpLagCompMaxMS( 200 );
static idCVar net_mpLagCompBiasMS( 0 );
static idCVar net_mpLagCompSlackMS( 48 );
static idCVar net_mpLagCompDebug( 0 );
static idCVar net_mpPredictMode( 0 );
static idCVar net_mpPredictMaxFrames( 16 );
static idCVar net_mpRemoteExtrapolation( 0 );

class idNetworkSystemMock {
public:
	int prediction[MAX_CLIENTS] = {};
	int ping[MAX_CLIENTS] = {};
	int ServerGetClientPrediction( int clientNum ) { return prediction[clientNum]; }
	int ServerGetClientPing( int clientNum ) { return ping[clientNum]; }
};
static idNetworkSystemMock networkSystemMock;
static idNetworkSystemMock *networkSystem = &networkSystemMock;

class idCommonMock {
public:
	void DPrintf( const char *, ... ) {}
};
static idCommonMock commonMock;
static idCommonMock *common = &commonMock;

class idGameLocal {
public:
	enum { MP_LAGCOMP_HISTORY = 64 };

// PRODUCTION_STRUCTS

	bool isServer = true;
	bool isMultiplayer = true;
	int time = 0;
	int msec = 16;
	idEntity *entities[MAX_CLIENTS] = {};
	idDict userInfo[MAX_CLIENTS];
	mpLagCompFrame_t mpLagCompHistory[MAX_CLIENTS][MP_LAGCOMP_HISTORY];
	int mpLagCompHistoryHead = -1;

	int GetMSec() const { return msec; }

	void ResetMPLagCompensationHistory( void );
	void InvalidateMPLagCompensationHistory( int clientNum );
	void CaptureMPLagCompensationFrame( void );
	bool SelectMPLagCompensationFrame( int clientNum, int targetTime, mpLagCompFrame_t &outFrame ) const;
	void EstimateMPLagCompensation( int clientNum, mpLagCompEstimate_t &estimate ) const;
	bool ComputeMPLagCompensationRewind( const idPlayer *shooter, int &rewindMS, mpLagCompEstimate_t *estimate = NULL ) const;
	bool BeginMPLagCompensation( const idPlayer *shooter, mpLagCompRestore_t restoreState[MAX_CLIENTS], int &restoreCount );
	void EndMPLagCompensation( mpLagCompRestore_t restoreState[MAX_CLIENTS], int restoreCount );
	bool ResolveMPLagCompensationHit( const idEntity *hit, mpLagCompRestore_t restoreState[MAX_CLIENTS], int &restoreCount, trace_t &tr, idVec3 *extraPoint );
	idVec3 MPLagCompensationOffset( const idEntity *hit, const mpLagCompRestore_t restoreState[MAX_CLIENTS], int restoreCount ) const;
	void ResolvePenetratingShot( trace_t &tr, mpLagCompRestore_t *lagCompRestore, int &lagCompRestoreCount, idVec3 &start, idVec3 &collisionPoint ) {
		const bool lagCompApplied = lagCompRestoreCount > 0;
		idEntity *ent;
		const float penetrate = 0.5f;
		float damageScale = 1.0f;
		idEntity *additionalIgnore = nullptr;
		// PRODUCTION_HITSCAN_RESOLVE
		for ( int once = 0; once < 1; once++ ) {
			// PRODUCTION_HITSCAN_PENETRATE
		}
		if ( damageScale != 0.5f || additionalIgnore != ent ) { std::abort(); }
	}
	int AdvertisedRemoteExtrapolationTics( int clientNum ) const;
	int RemoteExtrapolationMS( int clientNum, int leadMS ) const;
	void UpdateAdvertisedRemoteExtrapolation( void );
};

// PRODUCTION_FUNCTIONS

static int failures = 0;

static void Expect( bool condition, const char *label ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAILED: %s\n", label );
		failures++;
	}
}

static void ExpectInt( int actual, int expected, const char *label ) {
	if ( actual != expected ) {
		std::fprintf( stderr, "FAILED: %s: expected %d, got %d\n", label, expected, actual );
		failures++;
	}
}

struct World {
	idGameLocal game;
	idPlayer shooter;
	idPlayer target;
	idPlayer bystander;

	World() {
		game.ResetMPLagCompensationHistory();
		shooter.entityNumber = 1;
		target.entityNumber = 2;
		bystander.entityNumber = 3;
		game.entities[1] = &shooter;
		game.entities[2] = &target;
		game.entities[3] = &bystander;
		game.time = 10000;
		net_mpLagCompensation.value = 1;
		net_mpLagCompMaxMS.value = 200;
		net_mpLagCompBiasMS.value = 0;
		net_mpLagCompSlackMS.value = 48;
		std::memset( &networkSystemMock, 0, sizeof( networkSystemMock ) );
	}

	// One server frame: the target walks +10 x per frame, the bystander +1 y.
	void Frame() {
		game.time += game.msec;
		target.playerPhysics.origin.x += 10.0f;
		bystander.playerPhysics.origin.y += 1.0f;
		game.CaptureMPLagCompensationFrame();
	}

	void Frames( int count ) {
		for ( int i = 0; i < count; i++ ) {
			Frame();
		}
	}

	void Connection( int lead, int rtt, const char *advertised ) {
		networkSystemMock.prediction[1] = lead;
		networkSystemMock.ping[1] = rtt;
		game.userInfo[1].Clear();
		if ( advertised != nullptr ) {
			game.userInfo[1].Set( "net_mpRemoteExtrapolation", advertised );
		}
	}

	int Rewind() {
		int rewind = -1;
		game.ComputeMPLagCompensationRewind( &shooter, rewind );
		return rewind;
	}
};

static void TestEstimate() {
	World world;
	world.Frames( 40 );

	world.Connection( 100, 90, nullptr );
	ExpectInt( world.Rewind(), 116, "legacy client rewinds its whole lead plus the server frame" );

	world.Connection( 100, 90, "16" );
	ExpectInt( world.Rewind(), 16, "mode-1 client extrapolating its whole lead rewinds one frame" );

	world.Connection( 100, 90, "4" );
	ExpectInt( world.Rewind(), 116 - 64, "partial extrapolation comes off the rewind" );

	world.Connection( 300, 290, "16" );
	ExpectInt( world.Rewind(), 316 - 256, "lead beyond the advertised budget is still rewound" );

	world.Connection( 300, 290, nullptr );
	ExpectInt( world.Rewind(), 200, "legacy rewind is capped by net_mpLagCompMaxMS" );

	world.Connection( 500, 90, nullptr );
	ExpectInt( world.Rewind(), 138, "an inflated lead is capped by the measured round trip plus slack" );

	world.Connection( 500, 90, "16" );
	ExpectInt( world.Rewind(), 0, "an inflated lead can only shorten an extrapolating client's rewind" );

	const char *malformed[] = { "abc", "-5", "65", "1e3", " 16", "", "16 ", "+16", "0x10" };
	for ( const char *text : malformed ) {
		world.Connection( 100, 90, text );
		ExpectInt( world.Rewind(), 116, "malformed or out-of-range advertisement reads as legacy" );
	}
	world.Connection( 100, 90, "64" );
	ExpectInt( world.Rewind(), 16, "the advertisement's own upper bound is accepted" );

	world.Connection( 99999, 99999, nullptr );
	ExpectInt( world.Rewind(), 0, "a slot that is not fully connected rewinds nothing" );
	world.Connection( -40, 90, nullptr );
	ExpectInt( world.Rewind(), 0, "a negative reported lead rewinds nothing" );
	world.Connection( 0, 0, "16" );
	ExpectInt( world.Rewind(), 0, "a listen host or bot rewinds nothing" );
	world.Connection( 100, 0, nullptr );
	ExpectInt( world.Rewind(), 0, "an unmeasured round trip rewinds nothing" );

	world.Connection( 100, 90, nullptr );
	net_mpLagCompBiasMS.value = 20;
	ExpectInt( world.Rewind(), 136, "bias adds to the pose age" );
	world.Connection( 0, 0, "16" );
	ExpectInt( world.Rewind(), 0, "bias never rewinds a listen host or bot" );
	world.Connection( 99999, 99999, nullptr );
	ExpectInt( world.Rewind(), 0, "bias never rewinds a slot that is not fully connected" );
	world.Connection( 100, 0, nullptr );
	ExpectInt( world.Rewind(), 0, "bias never rewinds an unmeasured round trip" );
	world.Connection( 100, 90, nullptr );
	net_mpLagCompBiasMS.value = -200;
	ExpectInt( world.Rewind(), 0, "a negative bias never rewinds into the future" );
	net_mpLagCompBiasMS.value = 0;

	net_mpLagCompMaxMS.value = 0;
	int rewind = -1;
	Expect( !world.game.ComputeMPLagCompensationRewind( &world.shooter, rewind ) && rewind == 0, "a zero window disables the rewind" );
	net_mpLagCompMaxMS.value = 200;

	net_mpLagCompensation.value = 0;
	rewind = -1;
	Expect( !world.game.ComputeMPLagCompensationRewind( &world.shooter, rewind ) && rewind == 0, "disabled lag compensation rewinds nothing" );
	idGameLocal::mpLagCompEstimate_t estimate;
	world.game.EstimateMPLagCompensation( 1, estimate );
	ExpectInt( estimate.poseAgeMS, 116, "the estimate stays available while the rewind is off" );
	net_mpLagCompensation.value = 1;

	world.game.isServer = false;
	rewind = -1;
	Expect( !world.game.ComputeMPLagCompensationRewind( &world.shooter, rewind ) && rewind == 0, "a client never rewinds" );
	world.game.isServer = true;

	world.game.EstimateMPLagCompensation( -1, estimate );
	ExpectInt( estimate.poseAgeMS, 0, "an invalid slot has no estimate" );
	world.game.EstimateMPLagCompensation( MAX_CLIENTS, estimate );
	ExpectInt( estimate.poseAgeMS, 0, "a slot past the table has no estimate" );
}

static void TestAdvertisement() {
	idGameLocal game;
	net_mpPredictMode.value = 0;
	net_mpPredictMaxFrames.value = 16;
	net_mpRemoteExtrapolation.value = 0;
	game.UpdateAdvertisedRemoteExtrapolation();
	ExpectInt( net_mpRemoteExtrapolation.GetInteger(), 0, "legacy prediction advertises no extrapolation" );

	net_mpPredictMode.value = 1;
	game.UpdateAdvertisedRemoteExtrapolation();
	ExpectInt( net_mpRemoteExtrapolation.GetInteger(), 16, "mode 1 advertises its replay budget" );

	net_mpPredictMaxFrames.value = 99;
	game.UpdateAdvertisedRemoteExtrapolation();
	ExpectInt( net_mpRemoteExtrapolation.GetInteger(), 64, "the advertised budget is bounded" );

	net_mpPredictMaxFrames.value = 16;
	net_mpRemoteExtrapolation.value = 7;	// a server echoed something else back
	game.UpdateAdvertisedRemoteExtrapolation();
	ExpectInt( net_mpRemoteExtrapolation.GetInteger(), 16, "an echoed value is corrected on the next frame" );

	const int sets = net_mpRemoteExtrapolation.sets;
	game.UpdateAdvertisedRemoteExtrapolation();
	ExpectInt( net_mpRemoteExtrapolation.sets, sets, "an unchanged advertisement is not re-sent" );
	net_mpPredictMode.value = 0;
}

static void TestBracket() {
	World world;
	world.Frames( 40 );
	world.Connection( 100, 90, nullptr );

	const idVec3 present = world.target.playerPhysics.origin;
	const idVec3 bystanderPresent = world.bystander.playerPhysics.origin;
	idGameLocal::mpLagCompRestore_t restore[MAX_CLIENTS];
	int count = 0;
	Expect( world.game.BeginMPLagCompensation( &world.shooter, restore, count ), "a lagged shooter rewinds" );
	ExpectInt( count, 2, "both other players are rewound" );
	// 116 ms ago is 7.25 frames: the newest sample at or before it is 8 frames back.
	Expect( world.target.playerPhysics.origin.Near( present - idVec3( 80.0f, 0.0f, 0.0f ) ), "the target stands where the shooter saw it" );
	Expect( world.shooter.playerPhysics.origin.Near( vec3_origin ), "the shooter is never rewound" );

	trace_t trace;
	trace.c.point = world.target.playerPhysics.origin + idVec3( 0.0f, 0.0f, 32.0f );
	trace.c.normal = idVec3( 0.0f, 0.0f, 1.0f );
	trace.endpos = trace.c.point;
	idVec3 extra = trace.c.point;
	Expect( world.game.ResolveMPLagCompensationHit( &world.target, restore, count, trace, &extra ), "the hit target resolves" );
	ExpectInt( count, 1, "the resolved target leaves the bracket" );
	Expect( world.target.playerPhysics.origin.Near( present ), "the hit target is back in the present before damage" );
	Expect( trace.c.point.Near( present + idVec3( 0.0f, 0.0f, 32.0f ) ), "the impact moves with the target" );
	Expect( trace.endpos.Near( trace.c.point ) && extra.Near( trace.c.point ), "end point and effect origin move too" );
	Expect( std::fabs( trace.c.dist - trace.c.normal * trace.c.point ) < 0.001f, "the contact plane follows the point" );
	Expect( !world.bystander.playerPhysics.origin.Near( bystanderPresent ), "an unhit player stays rewound until the end" );
	Expect( !world.game.ResolveMPLagCompensationHit( &world.shooter, restore, count, trace, nullptr ), "a player outside the bracket does not resolve" );

	world.game.EndMPLagCompensation( restore, count );
	Expect( world.bystander.playerPhysics.origin.Near( bystanderPresent ), "everyone is restored at the end" );

	// A translation applied while rewound survives the restore.
	world.Connection( 100, 90, nullptr );
	count = 0;
	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	world.target.playerPhysics.origin += idVec3( 0.0f, 5.0f, 0.0f );
	world.game.EndMPLagCompensation( restore, count );
	Expect( world.target.playerPhysics.origin.Near( present + idVec3( 0.0f, 5.0f, 0.0f ) ), "a push inside the bracket is kept" );
	world.target.playerPhysics.origin = present;

	// A kill inside the bracket swaps the corpse onto ragdoll physics: nothing is written back.
	count = 0;
	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	world.target.ragdollPhysics.origin = idVec3( 1.0f, 2.0f, 3.0f );
	world.target.physics = &world.target.ragdollPhysics;
	world.game.EndMPLagCompensation( restore, count );
	Expect( world.target.ragdollPhysics.origin.Near( idVec3( 1.0f, 2.0f, 3.0f ) ), "a ragdoll is never restored onto" );
	world.target.physics = &world.target.playerPhysics;
	world.target.playerPhysics.origin = present;

	// Bracketing a shot from a client with no lead touches nobody.
	world.Connection( 0, 0, nullptr );
	count = 0;
	Expect( !world.game.BeginMPLagCompensation( &world.shooter, restore, count ) && count == 0, "no lead, no bracket" );
}

struct LightningPath {
	idEntity *target = nullptr;
	idVec3 origin, normal;
};

struct LightningPaths : std::vector<LightningPath> {
	int Num() const { return static_cast<int>( size() ); }
};

static void ResolveGauntlet( idGameLocal &gameLocal, trace_t &tr,
		idGameLocal::mpLagCompRestore_t *lagCompRestore, int &lagCompRestoreCount ) {
	const bool lagCompApplied = lagCompRestoreCount > 0;
	// PRODUCTION_GAUNTLET_RESOLVE
}

static void ResolveLightning( idGameLocal &gameLocal, trace_t &tr, LightningPath &currentPath,
		LightningPaths &chainLightning, idGameLocal::mpLagCompRestore_t *lagCompRestore, int &lagCompRestoreCount ) {
	const bool lagCompApplied = lagCompRestoreCount > 0;
	// PRODUCTION_LIGHTNING_RESOLVE
}

static void TestWeaponGeometry() {
	World world;
	world.Frames( 40 );
	world.Connection( 100, 90, nullptr );
	const idVec3 present = world.target.playerPhysics.origin;
	const idVec3 bystanderPresent = world.bystander.playerPhysics.origin;
	idGameLocal::mpLagCompRestore_t restore[MAX_CLIENTS];
	int count = 0;
	const idVec3 contact( 0, 0, 32 );

	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	trace_t tr;
	tr.c.entityNum = world.target.entityNumber;
	tr.c.point = tr.endpos = world.target.playerPhysics.origin + contact;
	tr.c.normal = idVec3( 0, 0, 1 );
	tr.c.dist = 32;
	ResolveGauntlet( world.game, tr, restore, count );
	Expect( tr.endpos.Near( present + contact ), "gauntlet effect attaches to the present target" );
	Expect( tr.c.point.Near( tr.endpos ), "gauntlet contact is translated too" );
	Expect( world.target.playerPhysics.origin.Near( present ) && world.bystander.playerPhysics.origin.Near( bystanderPresent ), "gauntlet restores the whole world before damage/effects" );

	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	LightningPath primary;
	primary.target = &world.target;
	primary.origin = tr.c.point = tr.endpos = world.target.playerPhysics.origin + contact;
	LightningPaths paths;
	paths.push_back( { &world.bystander, world.bystander.playerPhysics.origin + contact, vec3_origin } );
	// Even repeated references must all be translated before restore entries are consumed.
	paths.push_back( { &world.target, world.target.playerPhysics.origin + contact, vec3_origin } );
	ResolveLightning( world.game, tr, primary, paths, restore, count );
	Expect( primary.origin.Near( present + contact ) && tr.endpos.Near( primary.origin ), "lightning beam and damage direction use the present endpoint" );
	Expect( paths[0].origin.Near( bystanderPresent + contact ) && paths[1].origin.Near( present + contact ), "all lightning chain endpoints follow their own target" );
	idVec3 direction = bystanderPresent - present;
	direction.Normalize();
	Expect( paths[0].normal.Near( direction ) && paths[1].normal.Near( direction * -1 ), "chain knockback directions are rebuilt from present endpoints" );
	Expect( world.target.playerPhysics.origin.Near( present ) && world.bystander.playerPhysics.origin.Near( bystanderPresent ), "lightning restores the whole world before damage/effects" );

	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	tr.c.point = tr.endpos = world.target.playerPhysics.origin + contact;
	const idVec3 historicalHit = tr.c.point;
	idVec3 continuation, effect;
	world.game.ResolvePenetratingShot( tr, restore, count, continuation, effect );
	Expect( effect.Near( present + contact ), "rail damage/effect point follows the present target" );
	Expect( continuation.Near( historicalHit ), "rail penetration stays on the historical ray" );
	world.game.EndMPLagCompensation( restore, count );

	// Misses must not interpret the trace's placeholder entity as a hit.
	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	tr.fraction = 1.0f;
	tr.endpos = idVec3( 900, 400, 100 );
	ResolveGauntlet( world.game, tr, restore, count );
	Expect( tr.endpos.Near( idVec3( 900, 400, 100 ) ), "a gauntlet miss keeps its traced endpoint" );
	Expect( world.bystander.playerPhysics.origin.Near( bystanderPresent ), "a miss still restores all players" );
}

static void TestTargets() {
	World world;
	world.Frames( 40 );
	world.Connection( 100, 90, nullptr );
	idGameLocal::mpLagCompRestore_t restore[MAX_CLIENTS];
	int count = 0;

	world.target.health = 0;
	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	ExpectInt( count, 1, "a dead target is not rewound" );
	world.game.EndMPLagCompensation( restore, count );
	world.target.health = 100;

	world.target.spectating = true;
	count = 0;
	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	ExpectInt( count, 1, "a spectator is not rewound" );
	world.game.EndMPLagCompensation( restore, count );
	world.target.spectating = false;

	world.target.instance = 1;
	count = 0;
	world.game.BeginMPLagCompensation( &world.shooter, restore, count );
	ExpectInt( count, 1, "a player in another instance is not rewound" );
	world.game.EndMPLagCompensation( restore, count );
	world.target.instance = 0;
}

static void TestHistory() {
	idGameLocal::mpLagCompFrame_t frame;

	// A teleport or respawn forgets every earlier sample: the rewind can only land on the
	// new position, however far back it reaches.
	{
		World world;
		world.Frames( 40 );
		world.game.InvalidateMPLagCompensationHistory( 2 );
		world.target.playerPhysics.origin = idVec3( 5000.0f, 0.0f, 0.0f );
		world.Frame();
		const idVec3 arrived = world.target.playerPhysics.origin;
		Expect( world.game.SelectMPLagCompensationFrame( 2, world.game.time - 150, frame ), "a teleported target still has its new sample" );
		Expect( frame.origin.Near( arrived ), "the rewind never reaches back across a teleport" );
	}

	// A recycled slot never inherits the previous occupant's positions.
	{
		World world;
		world.Frames( 40 );
		world.game.InvalidateMPLagCompensationHistory( 2 );
		Expect( !world.game.SelectMPLagCompensationFrame( 2, world.game.time - 50, frame ), "a disconnected slot has no history" );
	}

	// After a map load winds the clock back, stamps from the old map are in the future.
	{
		World world;
		world.Frames( 40 );
		world.game.time = 32;
		Expect( !world.game.SelectMPLagCompensationFrame( 2, 0, frame ), "stamps ahead of the clock are ignored" );
		world.game.ResetMPLagCompensationHistory();
		Expect( !world.game.SelectMPLagCompensationFrame( 2, 0, frame ), "a reset ring has nothing" );
	}

	// Never rewind past the window, even when the requested time is older.
	{
		World world;
		world.Frames( 60 );
		Expect( world.game.SelectMPLagCompensationFrame( 2, world.game.time - 900, frame ), "an over-long request falls back" );
		Expect( frame.time >= world.game.time - 200, "the fallback stays inside net_mpLagCompMaxMS" );
	}

	// A competitive pause keeps the clock running with everyone frozen: the rewind lands
	// on the frozen pose rather than anything before the pause.
	{
		World world;
		world.Frames( 20 );
		const idVec3 frozen = world.target.playerPhysics.origin;
		for ( int i = 0; i < 30; i++ ) {
			world.game.time += world.game.msec;
			world.game.CaptureMPLagCompensationFrame();
		}
		Expect( world.game.SelectMPLagCompensationFrame( 2, world.game.time - 116, frame ) && frame.origin.Near( frozen ), "a paused target rewinds onto its frozen pose" );
	}

	// Dead players and spectators are not recorded at all.
	{
		World world;
		world.target.health = 0;
		world.Frames( 5 );
		Expect( !world.game.SelectMPLagCompensationFrame( 2, world.game.time, frame ), "a corpse leaves no samples" );
	}
}

int main() {
	TestEstimate();
	TestAdvertisement();
	TestBracket();
	TestWeaponGeometry();
	TestTargets();
	TestHistory();
	if ( failures != 0 ) {
		std::fprintf( stderr, "mp lag compensation fixture: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "mp lag compensation fixture: ok\n" );
	return 0;
}
