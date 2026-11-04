#include <cstdio>
#include <cstdlib>

static const int GENTITYNUM_BITS = 4;
static const int MAX_GENTITIES = 1 << GENTITYNUM_BITS;
struct idEntity { int entityNumber = MAX_GENTITIES - 1; };
struct {
	idEntity *entities[MAX_GENTITIES] = {};
	int spawnIds[MAX_GENTITIES] = {};
} gameLocal;
static int PackEntitySpawnId( int generation, int slot ) {
	return static_cast<int>( ( static_cast<unsigned int>( generation ) << GENTITYNUM_BITS ) | slot );
}
class idClipModel {
public:
	idEntity *owner = nullptr;
	unsigned int ownerSpawnId = 0;
	void SetOwner( idEntity * );
	idEntity *GetOwner() const;
};

// @PRODUCTION_METHODS@

static int checks = 0;
static void check( bool value, const char *message ) {
	++checks;
	if ( !value ) { std::fprintf( stderr, "%s\n", message ); std::exit( 1 ); }
}

int main() {
	idEntity aircraft;
	aircraft.entityNumber = 5;
	gameLocal.entities[5] = &aircraft;
	gameLocal.spawnIds[5] = 17;
	idClipModel bullet;
	check( bullet.GetOwner() == nullptr, "new clip has no owner" );
	bullet.SetOwner( &aircraft );
	check( bullet.GetOwner() == &aircraft, "live aircraft is ignored" );
	idClipModel copy = bullet;
	check( copy.GetOwner() == &aircraft, "copied clip keeps a live owner" );
	gameLocal.entities[5] = nullptr;
	gameLocal.spawnIds[5] = -1;
	check( bullet.GetOwner() == nullptr, "deleted aircraft is not returned" );
	gameLocal.entities[5] = &aircraft; // allocator and entity-slot reuse
	gameLocal.spawnIds[5] = 18;
	check( bullet.GetOwner() == nullptr, "new wave at the same address is not ignored" );
	check( copy.GetOwner() == nullptr, "copied clip expires too" );
	bullet.SetOwner( &aircraft );
	check( bullet.GetOwner() == &aircraft, "explicit reassignment accepts the new wave" );
	bullet.SetOwner( nullptr );
	check( bullet.GetOwner() == nullptr, "null assignment clears owner" );

	// Restoring references can precede the owner's own class payload.
	aircraft.entityNumber = MAX_GENTITIES - 1;
	bullet.SetOwner( &aircraft );
	check( bullet.GetOwner() == &aircraft, "restore resolves the populated entity table" );
	aircraft.entityNumber = -1;
	bullet.SetOwner( &aircraft );
	check( bullet.GetOwner() == &aircraft, "invalid cached slot uses restored table" );
	idEntity unregistered;
	bullet.SetOwner( &unregistered );
	check( bullet.GetOwner() == nullptr, "unregistered owner is rejected" );
	aircraft.entityNumber = 5;
	gameLocal.spawnIds[5] = ( 1u << ( 31 - GENTITYNUM_BITS ) ) | 1; // packed high bit
	bullet.SetOwner( &aircraft );
	check( bullet.GetOwner() == &aircraft, "high-bit spawn generation remains valid" );
	std::printf( "clip owner behavior: %d checks passed\n", checks );
}
