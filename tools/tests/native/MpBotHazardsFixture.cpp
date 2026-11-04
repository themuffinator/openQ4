#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

struct idVec3 {
	float x, y, z;
	idVec3( float a = 0, float b = 0, float c = 0 ) : x( a ), y( b ), z( c ) {}
	float &operator[]( int i ) { return i == 0 ? x : i == 1 ? y : z; }
	float operator[]( int i ) const { return i == 0 ? x : i == 1 ? y : z; }
	idVec3 operator-( const idVec3 &b ) const { return { x - b.x, y - b.y, z - b.z }; }
};
struct idBounds {
	idVec3 corners[2];
	idBounds( idVec3 a = {}, idVec3 b = {} ) : corners{ a, b } {}
	idVec3 &operator[]( int i ) { return corners[i]; }
	const idVec3 &operator[]( int i ) const { return corners[i]; }
	bool IsCleared() const { return corners[0].x > corners[1].x; }
	void TranslateSelf( const idVec3 &v ) {
		for ( auto &c : corners ) for ( int i = 0; i < 3; ++i ) c[i] += v[i];
	}
	void AddBounds( const idBounds &b ) {
		for ( int i = 0; i < 3; ++i ) {
			corners[0][i] = std::min( corners[0][i], b[0][i] );
			corners[1][i] = std::max( corners[1][i], b[1][i] );
		}
	}
	bool IntersectsBounds( const idBounds &b ) const {
		for ( int i = 0; i < 3; ++i ) if ( corners[0][i] > b[1][i] || corners[1][i] < b[0][i] ) return false;
		return true;
	}
};
template<class T> T Max( T a, T b ) { return std::max( a, b ); }
struct Dict {
	std::map<std::string, float> values;
	float GetFloat( const char *key, const char *fallback ) const {
		auto i = values.find( key ); return i == values.end() ? std::strtof( fallback, nullptr ) : i->second;
	}
	bool GetBool( const char *key, const char *fallback ) const { return GetFloat( key, fallback ) != 0; }
	int GetInt( const char *key, const char *fallback ) const { return static_cast<int>( GetFloat( key, fallback ) ); }
};
struct Physics {
	idBounds bounds;
	idVec3 origin;
	const idBounds &GetAbsBounds() const { return bounds; }
	const idVec3 &GetOrigin() const { return origin; }
};
struct idEntity;
struct Node { idEntity *next = nullptr; idEntity *Next() const { return next; } };
struct idEntity {
	int type = 0;
	Dict spawnArgs;
	Physics physics;
	bool physicsPresent = true;
	idEntity *master = nullptr;
	Node spawnNode;
	Physics *GetPhysics() const { return physicsPresent ? const_cast<Physics *>( &physics ) : nullptr; }
	bool IsType( int t ) const { return t == type; }
	bool IsBound() const { return master != nullptr; }
	idEntity *GetBindMaster() const { return master; }
};
struct idTrigger_Hurt : idEntity {
	idTrigger_Hurt() { type = GetClassType(); }
	static int GetClassType() { return 1; }
};
struct idMover : idEntity { static int GetClassType() { return 2; } };
struct idMover_Binary : idEntity {
	idVec3 pos1, pos2;
	idMover_Binary() { type = GetClassType(); }
	static int GetClassType() { return 3; }
	bool GetTranslationBounds( const idBounds &, idBounds & ) const;
};
struct { Node spawnedEntities; } gameLocal;
template<class T> struct List : std::vector<T> {
	void Clear() { this->clear(); }
	int Num() const { return static_cast<int>( this->size() ); }
	void Append( const T &v ) { this->push_back( v ); }
};
struct navHazard_t { idBounds bounds; bool lethal; };
struct rvNavMesh {
	List<navHazard_t> hazards;
	idBounds GetAgentBounds() const { return { { -16, -16, 0 }, { 16, 16, 72 } }; }
	void GatherHazards();
	int HazardAtFloor( const idVec3 & ) const;
	bool lethal( const idVec3 &v ) const { int i = HazardAtFloor( v ); return i >= 0 && hazards[i].lethal; }
};
// PRODUCTION_CONSTANTS
static float NavHurtDamagePerTouch( const idEntity *e, bool &gibs ) {
	gibs = e->spawnArgs.GetBool( "gibs", "0" ); return e->spawnArgs.GetFloat( "test_damage", "0" );
}
static void check( bool ok, const char *message ) {
	if ( !ok ) { std::cerr << message << '\n'; std::exit( 1 ); }
}
// PRODUCTION_FUNCTIONS
int main() {
	rvNavMesh nav;
	idMover_Binary door;
	door.pos1 = { 832, 320, 832 }; door.pos2 = { 832, 320, 656 }; door.physics.origin = door.pos1;
	idTrigger_Hurt hurt;
	hurt.master = &door;
	hurt.physics.bounds = { { 696, 184, 736 }, { 968, 456, 752 } };
	hurt.spawnArgs.values = { { "test_damage", 1000 }, { "on", 0 }, { "delay", 0 } };
	gameLocal.spawnedEntities.next = &hurt;
	nav.GatherHazards();
	check( nav.hazards.Num() == 1 && nav.lethal( { 834, 321, 544 } ), "disabled translating crusher must reserve the floor it will touch" );
	check( nav.lethal( { 834, 321, 604 } ), "recorded CTF5 fatal pose intersects the reserved sweep" );
	check( !nav.lethal( { 670, 321, 544 } ) && !nav.lethal( { 834, 321, 480 } ), "adjacent and lower safe floor stays usable" );
	// Gathering midway or after reversing endpoints must produce the same envelope.
	door.physics.origin.z -= 80; hurt.physics.bounds.TranslateSelf( { 0, 0, -80 } );
	std::swap( door.pos1, door.pos2 );
	nav.GatherHazards();
	check( nav.lethal( { 834, 321, 544 } ) && nav.lethal( { 834, 321, 744 } ), "mid-move/reversed endpoints preserve both extents" );
	idBounds alias = hurt.physics.bounds;
	check( door.GetTranslationBounds( alias, alias ) && alias[0].z == 560 && alias[1].z == 752, "in-place output retains original input for both endpoints" );
	idBounds cleared( { 1, 1, 1 }, { -1, -1, -1 } );
	check( !door.GetTranslationBounds( cleared, alias ), "cleared input has no travel envelope" );
	// Horizontal motion needs the full standing hull, including its leading edge.
	door.pos1 = {}; door.pos2 = { 200, 0, 0 }; door.physics.origin = { 100, 0, 0 };
	hurt.physics.bounds = { { 90, -10, 0 }, { 110, 10, 20 } };
	nav.GatherHazards();
	check( nav.lethal( { 220, 0, 0 } ) && !nav.lethal( { 230, 0, 0 } ), "horizontal sweep uses player hull bounds" );
	// No assumption that a bound parent's local endpoints are world positions.
	idEntity parent; door.master = &parent; nav.GatherHazards();
	check( !nav.hazards[0].lethal, "unmodelled parent transform stays unproven" );
	door.master = nullptr; door.physicsPresent = false; nav.GatherHazards();
	check( !nav.hazards[0].lethal, "missing mover physics stays unproven" );
	door.physicsPresent = true; hurt.master = &parent; nav.GatherHazards();
	check( !nav.hazards[0].lethal, "arbitrary moving parent is not a linear envelope" );
	hurt.master = nullptr; hurt.spawnArgs.values["on"] = 1; nav.GatherHazards();
	check( nav.lethal( { 100, 0, 0 } ), "static live lethal trigger still refuses floor" );
	hurt.spawnArgs.values["on"] = 0; nav.GatherHazards();
	check( !nav.hazards[0].lethal, "static disabled trigger retains existing priced policy" );
	hurt.master = &door; hurt.spawnArgs.values["test_damage"] = 1; hurt.spawnArgs.values["delay"] = 1;
	nav.GatherHazards();
	check( !nav.hazards[0].lethal, "survivable moving pain retains priced policy" );
	hurt.spawnArgs.values["test_damage"] = 0; nav.GatherHazards();
	check( nav.hazards.Num() == 0, "harmless trigger is ignored" );
	hurt.spawnArgs.values["gibs"] = 1; nav.GatherHazards();
	check( nav.hazards.Num() == 1 && nav.hazards[0].lethal, "gib-only moving triggers are deadly" );
	hurt.physicsPresent = false; nav.GatherHazards();
	check( nav.hazards.Num() == 0, "trigger without physics is ignored" );
	std::cout << "moving hazard and standing-hull checks passed\n";
}
