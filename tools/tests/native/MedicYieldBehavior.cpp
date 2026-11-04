#include <cstdio>
#include <cstdlib>

static const int AITACTICAL_MOVE_PLAYERPUSH = 4;
static const int AITACTICAL_MOVE_PLAYERPUSH_BIT = 1 << AITACTICAL_MOVE_PLAYERPUSH;
struct idActor { int team = 0; int health = 100; const char *GetName() const { return "actor"; } };
static const int ai_debugMove = 0;
struct { template<typename... Args> void Printf(const char *, Args...) {} } gameLocal;
class idAI : public idActor {
public:
    idActor *leader = nullptr, *pusher = nullptr;
    struct { bool dead = false, scripted = false, action = false, pushed = false; } aifl;
    struct { struct { bool done = true, disabled = false; } fl; } move;
    struct { idActor *ent = nullptr; } enemy;
    struct { int tacticalCurrent = 0, tacticalMaskAvailable = AITACTICAL_MOVE_PLAYERPUSH_BIT; } combat;
    bool hidden = false, tethered = false, tacticalUpdateRequested = false;
    bool DebugFilter(int) const { return false; }
    bool IsHidden() const { return hidden; }
    bool IsTethered() const { return tethered; }
    void ForceTacticalUpdate() { tacticalUpdateRequested = true; }
    bool RequestMoveAside( idActor *requester );
};

struct idVec3 {
    float x, y, z;
    idVec3 operator-(const idVec3& v) const { return {x-v.x, y-v.y, z-v.z}; }
    float LengthSqr() const { return x*x+y*y+z*z; }
};
struct idAAS {};
static float Square(float value) { return value*value; }
static float Max(float a, float b) { return a>b ? a : b; }
struct {
    bool valid = true;
    int calls = 0;
    bool ValidateDestination(idAI *, const idVec3&) { ++calls; return valid; }
} aiManager;
class rvAASFindGoalOutOfRange {
public:
    idAI *owner;
    idVec3 avoidOrigin;
    float minimumDistanceSqr;
    rvAASFindGoalOutOfRange(idAI *, const idVec3&, float);
    bool TestPoint(idAAS *, const idVec3&, float = 0);
};

// @PRODUCTION_METHODS@

static int checks = 0;
static void check( bool value, const char *message ) {
    ++checks;
    if ( !value ) { std::fprintf( stderr, "%s\n", message ); std::exit( 1 ); }
}
static void rejected( idAI follower, idActor *requester, const char *reason ) {
    check( !follower.RequestMoveAside( requester ) && !follower.pusher &&
        !follower.aifl.pushed && !follower.tacticalUpdateRequested, reason );
}
int main() {
    idActor medic, other;
    idAI follower;
    follower.leader = &medic;
    rejected( follower, nullptr, "missing requester" );
    rejected( follower, &other, "unrelated squad member cannot issue a move" );
    medic.team = 1;
    rejected( follower, &medic, "former leader on another team" );
    medic.team = 0;
    medic.health = 0;
    rejected( follower, &medic, "dead leader" );
    medic.health = 100;
    idAI guarded = follower; guarded.hidden = true;
    rejected( guarded, &medic, "hidden actor" );
    guarded = follower; guarded.aifl.dead = true;
    rejected( guarded, &medic, "dead actor" );
    guarded = follower; guarded.aifl.scripted = true;
    rejected( guarded, &medic, "scripted choreography is preserved" );
    guarded = follower; guarded.aifl.action = true;
    rejected( guarded, &medic, "active action is preserved" );
    guarded = follower; guarded.move.fl.done = false;
    rejected( guarded, &medic, "existing movement is preserved" );
    guarded = follower; guarded.move.fl.disabled = true;
    rejected( guarded, &medic, "disabled movement is respected" );
    guarded = follower; guarded.tethered = true;
    rejected( guarded, &medic, "tether is respected" );
    guarded = follower; guarded.enemy.ent = &other;
    rejected( guarded, &medic, "combat is not interrupted" );
    guarded = follower; guarded.combat.tacticalCurrent = AITACTICAL_MOVE_PLAYERPUSH;
    rejected( guarded, &medic, "a pending yield is not restarted" );
    guarded = follower; guarded.combat.tacticalMaskAvailable = 0;
    rejected( guarded, &medic, "move-aside capability is respected" );
    check( follower.RequestMoveAside( &medic ) && follower.pusher == &medic &&
        follower.aifl.pushed && follower.tacticalUpdateRequested, "idle follower yields to live medic leader" );
    const idVec3 avoided{10,20,30};
    rvAASFindGoalOutOfRange goal(&follower, avoided, 160);
    check(!goal.TestPoint(nullptr, avoided), "a no-op move cannot clear the pusher");
    check(!goal.TestPoint(nullptr, {169,20,30}), "inside the requested separation is rejected");
    check(aiManager.calls == 0, "occupied-range points are rejected before destination validation");
    check(goal.TestPoint(nullptr, {170,20,30}), "exact requested separation is admissible");
    aiManager.valid = false;
    check(!goal.TestPoint(nullptr, {180,20,30}), "distance does not override occupied destinations");
    aiManager.valid = true;
    rvAASFindGoalOutOfRange unrestricted(&follower, avoided, -1);
    check(unrestricted.TestPoint(nullptr, avoided), "negative separation clamps to zero");
    std::printf( "medic follower yield: %d checks passed\n", checks );
}
