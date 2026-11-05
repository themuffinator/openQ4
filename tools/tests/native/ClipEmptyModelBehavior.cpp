#include <cstdio>
#include <cstdlib>

// Minimal stand-ins for the types idClip::GetTraceClipModels touches. The
// production methods are spliced in at @PRODUCTION_METHODS@.
#define ID_INLINE inline
static const int MAX_GENTITIES = 16;
class idBounds {};
struct idCollisionModel { int unused; };
class idClipModel;
class idPhysics {
public:
	idClipModel *clip = nullptr;
	int GetNumClipModels() const { return clip ? 1 : 0; }
	idClipModel *GetClipModel() const { return clip; }
};
class idEntity {
public:
	mutable idPhysics physics;
	idPhysics *GetPhysics() const { return &physics; }
};
class idClipModel {
	friend class idClip;
public:
	idEntity *entity = nullptr;
	idEntity *owner = nullptr;
	idCollisionModel *collisionModel = nullptr;
	int traceModelIndex = -1;
	int renderModelHandle = -1;
	bool IsRenderModel() const { return renderModelHandle != -1; }
	bool HasCollisionModel( void ) const;
	idEntity *GetOwner() const { return owner; }
};
class idClip {
public:
	idClipModel *touching[MAX_GENTITIES] = {};
	int numTouching = 0;
	int ClipModelsTouchingBounds( const idBounds &, int, idClipModel **list, int maxCount ) const {
		int n = numTouching < maxCount ? numTouching : maxCount;
		for ( int i = 0; i < n; i++ ) {
			list[i] = touching[i];
		}
		return n;
	}
	int GetTraceClipModels( const idBounds &bounds, int contentMask, const idEntity *passEntity, idClipModel **clipModelList, const idEntity *passEntity2 ) const;
};

// @PRODUCTION_METHODS@

static int checks = 0;
static void check( bool value, const char *message ) {
	++checks;
	if ( !value ) { std::fprintf( stderr, "FAIL: %s\n", message ); std::exit( 1 ); }
}

int main() {
	idCollisionModel brush = {};
	idEntity world, self, remote, rocket;

	// A client-side actor combat model built from modelDefHandle == -1: no
	// collision model, no trace model, no render handle, yet linked.
	idClipModel placeholder; placeholder.entity = &remote;
	idClipModel trace; trace.entity = &remote; trace.traceModelIndex = 3;
	idClipModel mapModel; mapModel.entity = &world; mapModel.collisionModel = &brush;
	idClipModel render; render.entity = &remote; render.renderModelHandle = 7;
	idClipModel selfBox; selfBox.entity = &self; selfBox.traceModelIndex = 4;
	idClipModel ownRocket; ownRocket.entity = &rocket; ownRocket.traceModelIndex = 5; ownRocket.owner = &self;
	self.physics.clip = &selfBox;

	check( !placeholder.HasCollisionModel(), "placeholder has no collision geometry" );
	check( trace.HasCollisionModel(), "trace model counts as collision geometry" );
	check( mapModel.HasCollisionModel(), "collision model counts as collision geometry" );
	check( !render.HasCollisionModel(), "a render model is not collision geometry" );

	idClip clip;
	idClipModel *models[] = { &placeholder, &trace, &mapModel, &render, &selfBox, &ownRocket };
	clip.numTouching = 6;
	for ( int i = 0; i < clip.numTouching; i++ ) {
		clip.touching[i] = models[i];
	}
	idClipModel *list[MAX_GENTITIES];
	idBounds bounds;

	// Without a pass entity (the early-return path) the placeholder is still dropped.
	int num = clip.GetTraceClipModels( bounds, -1, nullptr, list, nullptr );
	check( num == 6, "list length is preserved for null-checking callers" );
	check( list[0] == nullptr, "placeholder dropped without a pass entity" );
	check( list[1] == &trace && list[2] == &mapModel && list[3] == &render, "collidable models kept without a pass entity" );
	check( list[4] == &selfBox && list[5] == &ownRocket, "no pass entity ignores nothing else" );

	// The crash path: a player's Contents probe passes itself and meets the placeholder.
	num = clip.GetTraceClipModels( bounds, -1, &self, list, nullptr );
	check( num == 6, "list length is preserved with a pass entity" );
	check( list[0] == nullptr, "placeholder dropped with a pass entity" );
	check( list[1] == &trace && list[2] == &mapModel && list[3] == &render, "collidable models kept with a pass entity" );
	check( list[4] == nullptr, "pass entity still ignored" );
	check( list[5] == nullptr, "pass entity's own missiles still ignored" );

	std::printf( "clip empty model behavior: %d checks passed\n", checks );
	return 0;
}
