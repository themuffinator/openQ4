// Actual navigation functions are inserted by mp_bot_jump_trajectories.py.
// This small collision world sweeps the configured player hull against
// axis-aligned solids. Cylinder vertices and route proofs are production code;
// collision is an independent continuous separating-axis test.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#ifndef CM_CLIP_EPSILON
#define CM_CLIP_EPSILON 0.25f // CollisionModel.h
#endif
#include <string>
#include <vector>
#undef INFINITY
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
template<class T> T Min(T a, T b) { return std::min(a,b); }
template<class T> T Max(T a, T b) { return std::max(a,b); }
float Square(float x) { return x*x; }
struct idMath {
    static constexpr float INFINITY=std::numeric_limits<float>::infinity();
    static constexpr float PI=3.14159265358979323846f,TWO_PI=2*PI;
    static int Abs(int x) { return std::abs(x); }
    static float Sin(float x) { return std::sin(x); }
    static float Cos(float x) { return std::cos(x); }
    static float Fabs(float x) { return std::fabs(x); }
    static float Sqrt(float x) { return std::sqrt(x); }
    static float Ceil(float x) { return std::ceil(x); }
    static float ACos(float x) { return std::acos(x); }
    static double Pow64(double x,double y) { return std::pow(x,y); }
    static constexpr float FLOAT_EPSILON=1.192092896e-07f;
};
struct idVec3 {
    float x=0,y=0,z=0;
    idVec3() = default;
    idVec3(float a,float b,float c):x(a),y(b),z(c) {}
    void Set(float a,float b,float c) { x=a; y=b; z=c; }
    idVec3 operator+(const idVec3& b) const { return {x+b.x,y+b.y,z+b.z}; }
    idVec3 operator-(const idVec3& b) const { return {x-b.x,y-b.y,z-b.z}; }
    idVec3 operator-() const { return {-x,-y,-z}; }
    idVec3 operator*(float f) const { return {x*f,y*f,z*f}; }
    idVec3 operator/(float f) const { return *this*(1/f); }
    idVec3& operator+=(const idVec3& b) { *this=*this+b; return *this; }
    idVec3& operator-=(const idVec3& b) { *this=*this-b; return *this; }
    idVec3& operator*=(float f) { *this=*this*f; return *this; }
    float operator*(const idVec3& b) const { return x*b.x+y*b.y+z*b.z; }
    idVec3 Cross(const idVec3& b) const { return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x}; }
    float LengthSqr() const { return *this * *this; }
    float Length() const { return std::sqrt(LengthSqr()); }
    float Normalize() { float n=Length(); if(n>0) *this=*this*(1/n); return n; }
    float& operator[](int i) { return i==0?x:i==1?y:z; }
    float operator[](int i) const { return i==0?x:i==1?y:z; }
};
idVec3 operator*(float f,const idVec3& v) { return v*f; }
struct idBounds {
    idVec3 points[2];
    idBounds()=default;
    idBounds(idVec3 a,idVec3 b) { points[0]=a; points[1]=b; }
    idBounds(std::initializer_list<idVec3> p) { std::copy(p.begin(),p.end(),points); }
    idVec3& operator[](int i) { return points[i]; }
    const idVec3& operator[](int i) const { return points[i]; }
    void Clear() { points[0]={1e30f,1e30f,1e30f}; points[1]={-1e30f,-1e30f,-1e30f}; }
    void AddPoint(const idVec3& p) {
        for(int i=0;i<3;i++) { points[0][i]=Min(points[0][i],p[i]); points[1][i]=Max(points[1][i],p[i]); }
    }
    void TranslateSelf(const idVec3& p) { points[0]=points[0]+p; points[1]=points[1]+p; }
    bool IntersectsBounds(const idBounds& b) const {
        for(int i=0;i<3;i++) if(points[1][i]<b[0][i] || points[0][i]>b[1][i]) return false;
        return true;
    }
};
struct CVar { float value; float GetFloat() const { return value; } int GetInteger() const { return int(value); } };
CVar pm_stepsize{16},pm_jumpheight{48},pm_speed{320},pm_usecylinder{1},pm_frictionoverride{-1};
CVar pm_slidevelocity{1},pm_powerslide{0.09f};
const float OVERCLIP=1.001f;
int missingGroundContacts=0;
float contactsAfterX=-1e30f;
bool NavAgentHasGroundContacts(const idVec3& origin,const idVec3&) {
    if(origin.x<contactsAfterX) return false;
    if(missingGroundContacts>0) { --missingGroundContacts; return false; }
    return true;
}
CVar bot_debug{0};
constexpr int MAX_GENTITIES=64, SURF_NODAMAGE=1, MASK_WATER=2, SURF_SLICK=4;
constexpr int TH_THINK=1;
struct rvJumpPad { static int GetClassType() { return 1; } };
struct idEntity {
    bool live=true;
    int type=1;
    bool IsType(int value) const { return type==value; }
    bool IsActive(int) const { return live; }
    struct Physics { idVec3 origin; const idVec3& GetOrigin() const { return origin; } } physics;
    struct Target { idEntity* entity=nullptr; idEntity* GetEntity() const { return entity; } };
    struct Targets : std::vector<Target> { int Num() const { return int(size()); } } targets;
    Physics* GetPhysics() { return &physics; }
};
static idEntity* repeatedPad=nullptr;
static int repeatedPadTouches=0;
static idEntity* recoveryPad=nullptr;
static idBounds recoveryBounds;
static bool recoveryNatural=true,recoveryTouch=true;
static idEntity* secondRecoveryPad=nullptr;
static idBounds secondRecoveryBounds{{{300,-32,1000},{350,32,2000}}};
static bool SecondRecoveryVolumeAt(const idVec3& position) {
    idBounds body{{{-16,-16,0},{16,16,77}}}; body.TranslateSelf(position);
    return secondRecoveryPad && body.IntersectsBounds(secondRecoveryBounds);
}
static bool RecoveryVolumeAt(const idVec3& position) {
    idBounds body{{{-16,-16,0},{16,16,77}}}; body.TranslateSelf(position);
    return recoveryPad && body.IntersectsBounds(recoveryBounds);
}
static bool NavPadVolumeTouches(idEntity* pad,const idVec3& position) {
    // Contact observations from the sloped CTF5 trigger: the hull is still
    // inside it after its first two upward steps. Geometry itself is checked
    // separately by the transport fixture and original-map replay.
    if (pad==repeatedPad && position.z<40 && position.x<40) { ++repeatedPadTouches; return true; }
    if (pad==recoveryPad) return recoveryTouch && RecoveryVolumeAt(position);
    if (pad==secondRecoveryPad) return SecondRecoveryVolumeAt(position);
    return false;
}
struct GameLocal {
	int time=0;
	template<class... Args> void Printf(const char* format,Args... args) { std::printf(format,args...); }
    idEntity* entities[MAX_GENTITIES]={};
    float waterTop=-1e30f;
    idVec3 GetGravity() const { return {0,0,-800}; }
    idVec3 GetCurrentGravity(idEntity*) const { return GetGravity(); }
    int LiquidContentsAtPoint(const idVec3& p,void*) const { return p.z<=waterTop ? MASK_WATER : 0; }
} gameLocal;
struct Material { int flags=0; int GetSurfaceFlags() const { return flags; } } floorMaterial;
struct trace_t { float fraction=1; idVec3 endpos; struct { idVec3 normal; const Material* material=nullptr; } c; };
enum { NAVTRAVEL_WALK, NAVTRAVEL_DROP, NAVTRAVEL_JUMP, NAVTRAVEL_JUMPPAD, NAVTRAVEL_TELEPORT };
struct navCorner_t { idVec3 origin; int travelType=NAVTRAVEL_WALK,actionEntityNum=-1; };
// PRODUCTION_CONSTANTS

// The exact cylinder builder is inserted below. Edge normals are unused by
// this fixture's separating-axis collision test; face normals and vertices
// are built by the engine function itself.
enum { TRM_BOX,TRM_CYLINDER };
constexpr int MAX_TRACEMODEL_VERTS=32,MAX_TRACEMODEL_EDGES=32,MAX_TRACEMODEL_POLYS=16;
struct Common { void Printf(const char*) {} } common;
namespace idLib { Common* common=&::common; }
struct idTraceModel {
    int type=TRM_BOX,numVerts=0,numEdges=0,numPolys=0;
    idVec3 verts[32],offset;
    struct { int v[2]; } edges[33];
    struct Poly { int numEdges=0,edges[16]; idVec3 normal; float dist=0; idBounds bounds; } polys[16];
    idBounds bounds;
    bool isConvex=true;
    idTraceModel()=default;
    explicit idTraceModel(const idBounds& b):numVerts(8),bounds(b) {
        for(int i=0;i<8;i++) verts[i]={b[(i>>0)&1].x,b[(i>>1)&1].y,b[(i>>2)&1].z};
    }
    idTraceModel(const idBounds& b,int sides,bool alternate) { SetupCylinder(b,sides,alternate); }
    void SetupCylinder(const idBounds&,int,bool);
    void GenerateEdgeNormals() {}
};
using traceModelPoly_t=idTraceModel::Poly;
// PRODUCTION_HULL
idBounds agentBounds{{{-16,-16,0},{16,16,77}}};
idTraceModel agentHull;
idTraceModel& navAgentTrace=agentHull;
idBounds navSupportBounds{{{-NAV_LANDING_SUPPORT,-NAV_LANDING_SUPPORT,0},
    {NAV_LANDING_SUPPORT,NAV_LANDING_SUPPORT,NAV_LANDING_SUPPORT}}};
idTraceModel navSupportTrace(navSupportBounds);

struct Box { idVec3 lo,hi; };
std::vector<Box> solids;
bool lethal=false;
bool slopedFloor=false;
float slopeX=1, slopeY=0;
bool transported=false;

bool Sweep(trace_t& trace,const idVec3& start,const idVec3& end,const idTraceModel& model) {
    trace={}; trace.endpos=end; trace.c.material=&floorMaterial;
    const idVec3 delta=end-start;
    std::vector<idVec3> axes{{1,0,0},{0,1,0},{0,0,1}};
    if (model.type==TRM_CYLINDER) {
        for(int i=0;i<model.numPolys-2;i++) axes.push_back(model.polys[i].normal);
    }
    for (const Box& b:solids) {
        float enter=0,leave=1; idVec3 normal; bool missed=false;
        for (const idVec3& axis:axes) {
            float shapeLo=1e30f,shapeHi=-1e30f,boxLo=0,boxHi=0;
            for(int i=0;i<model.numVerts;i++) {
                shapeLo=Min(shapeLo,model.verts[i]*axis); shapeHi=Max(shapeHi,model.verts[i]*axis);
            }
            for(int i=0;i<3;i++) {
                boxLo+=Min(b.lo[i]*axis[i],b.hi[i]*axis[i]);
                boxHi+=Max(b.lo[i]*axis[i],b.hi[i]*axis[i]);
            }
            const float lo=boxLo-shapeHi,hi=boxHi-shapeLo,origin=start*axis,speed=delta*axis;
            if (std::fabs(speed)<1e-6f) {
                if (origin<=lo || origin>=hi) { missed=true; break; }
                continue;
            }
            float a=(lo-origin)/speed,z=(hi-origin)/speed;
            float sign=-1;
            if (a>z) { std::swap(a,z); sign=1; }
            // A sweep starting exactly on a face still reports that face's
            // normal when moving into it, as the engine collision trace does.
            if (a>enter || (a==enter && normal.LengthSqr()==0)) { enter=a; normal=axis*sign; }
            leave=std::min(leave,z);
            if (enter>leave) { missed=true; break; }
        }
        if (!missed && leave>0 && enter<trace.fraction) {
            trace.fraction=enter; trace.endpos=start+delta*enter; trace.c.normal=normal;
        }
    }
    if (slopedFloor) {
        // Infinite plane z=slopeX*x+slopeY*y, with true hull support.
        idVec3 normal(-slopeX,-slopeY,1); normal.Normalize();
        float support=1e30f;
        for(int i=0;i<model.numVerts;i++) support=Min(support,model.verts[i]*normal);
        const float from=start*normal+support,to=end*normal+support;
        if (from<0 || to<0) {
            const float fraction=from<=0 ? 0 : from/(from-to);
            if(fraction<trace.fraction) {
                trace.fraction=fraction; trace.endpos=start+delta*fraction; trace.c.normal=normal;
            }
        }
    }
    return trace.fraction>=1;
}
bool NavAgentTranslation(trace_t& trace,const idVec3& start,const idVec3& end) {
    return Sweep(trace,start,end,agentHull);
}
struct idPlayer;
// These geometry worlds contain no other players. Physical-traffic replays
// separately exercise the production live-player sweep on stock maps.
bool NavRecoveryTranslation(trace_t& trace,const idVec3& start,const idVec3& end,const idPlayer*) {
    return NavAgentTranslation(trace,start,end);
}
bool NavTraceTranslation(trace_t& trace,const idVec3& start,const idVec3& end,
    const idTraceModel& model,const idBounds&) {
    return Sweep(trace,start,end,model);
}
class rvNavMesh {
public:
    bool SampleFloor(const idVec3&,float,idVec3&) const;
    bool TryStep(const idVec3&,const idVec3&,idVec3&,int&) const;
    bool RunningDropClear(const idVec3&,const idVec3&,const idVec3* = nullptr,const idVec3* = nullptr,float = idMath::INFINITY) const;
    bool LandingContinuationClear(const idVec3&,const idVec3&) const;
    bool RecoveryStepClear(const idVec3&,const idVec3&, idVec3 *supportedOrigin = NULL) const;
    bool GapJumpClear(const idVec3&,const idVec3&,float,float,float,bool=false,const idVec3* = nullptr,float=0,const idVec3* = nullptr,float=0) const;
    bool JumpTakeoffClear(const idVec3&,const idVec3&,const idVec3&,float,float,const idVec3* = nullptr,float=0) const;
    bool DodgeJumpClear(const idVec3&,const idVec3&,const idVec3&,float,float=0) const;
    bool PadLanding(idEntity*,const idVec3&,const idVec3*,idVec3&,float* = nullptr,float* = nullptr) const;
    bool LandingSurvivable(const idVec3&,const navCorner_t&,float) const;
    bool AirInputLandingSafe(const idVec3&,const idVec3&,const idVec3&,float,idVec3* = nullptr,bool* = nullptr,bool* = nullptr,bool* = nullptr,int = 0,const idPlayer* = nullptr) const;
    bool RecoveryBrakeClear(const trace_t&,const idVec3&,float,int) const;
    bool KnockSlideLandingSafe(const idVec3&,const idVec3&,int,float,const idVec3& = idVec3()) const;
    bool steeredPad=false;
    bool IsSteeredPadLanding(int,const idVec3&) const { return steeredPad; }
    static const idBounds& GetAgentBounds() { return agentBounds; }
    int TransportAt(const idBounds&) const { return transported ? 1 : -1; }
    int TransportAtFloor(const idVec3& p) const { return RecoveryVolumeAt(p) ? 1 : SecondRecoveryVolumeAt(p) ? 2 : -1; }
    bool NaturalPadLanding(int,idVec3&) const { return recoveryNatural; }
    bool LineEntersTransport(const idVec3&,const idVec3&) const { return transported; }
    int TransportOnMove(const idVec3&,const idVec3&,int) const { return transported ? 1 : -1; }
    bool IsLethalFloor(const idVec3&) const { return lethal; }
    // The production string-pull proof, on this world's hull sweeps.
    bool WalkableLine(const idVec3& from,const idVec3& to,const char** failure=nullptr) const;
    struct { int Num() const { return 0; } } hazards;
    int HazardAtFloor(const idVec3&) const { return -1; }
    float cellSize=24;
};
// Diagnostics for a refused sweep name entities; this world has none.
static const char* NavWalkBlockedReason(const trace_t&,const trace_t&) { return "blocked"; }
rvNavMesh navMesh;
struct idPlayer {
    struct Physics {
        static idVec3 AdjustVertically(const idVec3&,const idVec3&,int=0);
        idVec3 origin,velocity;
        const idVec3& GetOrigin() const { return origin; }
        const idVec3& GetLinearVelocity() const { return velocity; }
        const idBounds& GetBounds() const { return agentBounds; }
        struct { int movementFlags=0,movementTime=0; } current;
        int GetKnockBack() const;
        void SetKnockBack(int);
        void RestoreKnockBack(int);
    } physics;
    Physics* GetPhysics() { return &physics; }
    Physics* GetPlayerPhysics() { return &physics; }
    int GetInstance() const { return 0; }
};
using idPhysics_Player=idPlayer::Physics;
constexpr int CONTENTS_TRIGGER=0x100;
struct idItem {
    bool pickedUp=false,hidden=false;
    int instance=0;
    bool IsHidden() const { return hidden; }
    int GetInstance() const { return instance; }
    struct { int amount=50; int GetInt(const char*,const char*) const { return amount; } } spawnArgs;
    struct Physics { idBounds bounds; const idBounds& GetAbsBounds() const { return bounds; } } physics;
    const Physics* GetPhysics() const { return &physics; }
    struct Trigger:Physics { int contents=CONTENTS_TRIGGER; int GetContents() const { return contents; } } triggerModel;
    const Trigger* trigger=&triggerModel;
    bool canPickUp=true;
    bool TouchesPickup(const idBounds&) const;
};
template<class T> struct idList:std::vector<T> { int Num() const { return (int)this->size(); } };
struct { idList<idItem*> items; const idList<idItem*>& GetItems() const { return items; } } botManager;
float fallBudget=idMath::INFINITY;
float BotMaxSafeFallDelta(idPlayer*) { return fallBudget; }
#define DEG2RAD(x) ((x)*idMath::PI/180.0f)
const idVec3 vec3_origin;
// PRODUCTION_FUNCTIONS

void Platform(float xmin,float xmax,float z) {
    solids.push_back({{xmin,-256,z-64},{xmax,256,z}});
}
int main() {
    idPhysics_Player timer;
    timer.current.movementFlags=PMF_DUCKED|PMF_TIME_LAND; timer.current.movementTime=250;
    timer.SetKnockBack(50); CHECK(timer.GetKnockBack()==0 && timer.current.movementTime==250);
    timer.RestoreKnockBack(50); CHECK(timer.GetKnockBack()==50);
    CHECK(timer.current.movementFlags==(PMF_DUCKED|PMF_TIME_KNOCKBACK));
    timer.SetKnockBack(200); CHECK(timer.GetKnockBack()==50);
    timer.RestoreKnockBack(200); CHECK(timer.GetKnockBack()==200);
    timer.RestoreKnockBack(0); CHECK(timer.GetKnockBack()==0 && timer.current.movementFlags==PMF_DUCKED);
    rvNavMesh nav;
    agentHull=NavCreateAgentTrace(agentBounds);
    // A box touches the far corner of this floor; the real default player's
    // diagonal footprint does not. A node here drops straight into the pit.
    solids.push_back({{13,13,-64},{128,128,0}});
    trace_t corner;
    CHECK(!Sweep(corner,{0,0,1},{0,0,-20},idTraceModel(agentBounds)));
    CHECK(NavAgentTranslation(corner,{0,0,1},{0,0,-20}));
    CHECK(agentHull.type==TRM_CYLINDER && agentHull.numVerts==16);
    // Box servers and signed/custom cylinder settings remain supported.
    pm_usecylinder.value=0; agentHull=NavCreateAgentTrace(agentBounds);
    CHECK(!NavAgentTranslation(corner,{0,0,1},{0,0,-20}));
    pm_usecylinder.value=-1; agentHull=NavCreateAgentTrace(agentBounds);
    CHECK(std::fabs(agentHull.verts[0].x-16)<0.001f && std::fabs(agentHull.verts[0].y)<0.001f);
    pm_usecylinder.value=4; agentHull=NavCreateAgentTrace(agentBounds);
    CHECK(agentHull.numVerts==8 && std::fabs(agentHull.verts[0].x-std::sqrt(128.0f))<0.001f);
    pm_usecylinder.value=1; agentHull=NavCreateAgentTrace(agentBounds); solids.clear();
    // A DM8 rocket lands on a narrow raised lip. Broad route support fails
    // for both headings, but only one can keep real hull contact while braking.
    // Include the engine trace's quarter-unit floor separation in this
    // fixture's exact separating-axis plane (which has no clip epsilon).
    solids.push_back({{48,-1024,288},{64,1024,352.25f}});
    const idVec3 rocketOrigin{-24.912f,-761.135f,328.25f};
    const idVec3 rocketVelocity{274.678f,-581.521f,241.656f};
    bool rimBraked=false;
    CHECK(!nav.AirInputLandingSafe(rocketOrigin,rocketVelocity,{-0.9239f,0.3827f,0},100,
          nullptr,nullptr,nullptr,&rimBraked,154) && rimBraked);
    CHECK(!nav.AirInputLandingSafe(rocketOrigin,rocketVelocity,{-0.941f,0.337f,0},100,
          nullptr,nullptr,nullptr,&rimBraked,154) && !rimBraked);
    idPlayer rocket; rocket.physics.origin=rocketOrigin; rocket.physics.velocity=rocketVelocity;
    rocket.physics.RestoreKnockBack(154);
    idVec3 rocketWish;
    CHECK(BotChooseAirInput(&rocket,rocketWish));
    nav.AirInputLandingSafe(rocketOrigin,rocketVelocity,rocketWish,100,nullptr,nullptr,nullptr,&rimBraked,154);
    CHECK(rimBraked);
    // The recovery continuation fails closed on absent floor, slippery ground,
    // pending knockback, hazards, transports and a missing landing contact.
    solids.clear(); Platform(-256,0,0);
    trace_t rimImpact; rimImpact.endpos={-4,0,0.25f}; rimImpact.c.normal={0,0,1};
    CHECK(nav.RecoveryBrakeClear(rimImpact,{500,0,0},0,0));
    CHECK(!nav.RecoveryBrakeClear(rimImpact,{500,0,0},0.016f,0));
    CHECK(!nav.RecoveryBrakeClear(rimImpact,{500,0,0},0,100));
    floorMaterial.flags=SURF_SLICK; CHECK(!nav.RecoveryBrakeClear(rimImpact,{500,0,0},0,0)); floorMaterial.flags=0;
    lethal=true; CHECK(!nav.RecoveryBrakeClear(rimImpact,{500,0,0},0,0)); lethal=false;
    transported=true; CHECK(!nav.RecoveryBrakeClear(rimImpact,{500,0,0},0,0)); transported=false;
    missingGroundContacts=1; CHECK(!nav.RecoveryBrakeClear(rimImpact,{500,0,0},0,0)); missingGroundContacts=0;
    contactsAfterX=0; CHECK(!nav.RecoveryBrakeClear(rimImpact,{500,0,0},0,0)); contactsAfterX=-1e30f;
    // Endpoints on two islands cannot bridge an unsupported interval.
    Platform(40,1024,0);
    CHECK(!nav.RecoveryBrakeClear(rimImpact,{4000,0,0},0.016f,0));
    solids.clear(); CHECK(!nav.RecoveryBrakeClear(rimImpact,{500,0,0},0,0));
    // A wall clips the runout before it reaches the far edge. Retain the
    // remaining tangential motion: a wall cannot save a slide off its end.
    Platform(-256,256,0);
    solids.push_back({{8,-256,0},{24,256,128}});
    trace_t wallImpact; wallImpact.endpos={-16,0,0.25f}; wallImpact.c.normal={0,0,1};
    CHECK(nav.RecoveryBrakeClear(wallImpact,{500,0,0},0.016f,0));
    CHECK(nav.RecoveryBrakeClear(wallImpact,{500,100,0},0.016f,0));
    solids[0].hi.y=4;
    CHECK(!nav.RecoveryBrakeClear(wallImpact,{500,500,0},0.016f,0));
    solids.clear();
    // A walk along q4dz3's rim was supported only by the outer two units of
    // the hull; the controller's small lateral error then lost the floor.
    Platform(12,128,0);
    CHECK(!NavAgentTranslation(corner,{0,0,1},{0,0,-20}));
    idVec3 rimFloor;
    CHECK(!nav.SampleFloor({0,0,1},20,rimFloor));
    CHECK(nav.SampleFloor({24,0,1},20,rimFloor));
    solids.clear();
    const float launch=std::sqrt(2.0f*800.0f*48.0f);
    // A pad catches a coast above the void, replacing momentum on each
    // touching step. Its long flight still needs a real, affordable landing.
    idEntity catchPad,catchTarget;
    catchTarget.physics.origin={400,0,1600}; catchPad.targets.push_back({&catchTarget});
    gameLocal.entities[1]=&catchPad; recoveryPad=&catchPad;
    recoveryBounds={{{-16,-32,0},{16,32,64}}};
    Platform(600,1400,0);
    CHECK(nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},idMath::INFINITY));
    CHECK(!nav.AirInputLandingSafe({0,0,32},{200,0,100},{},idMath::INFINITY));
    idEntity secondCatch; secondCatch.targets.push_back({&catchTarget});
    secondRecoveryPad=&secondCatch; gameLocal.entities[2]=&secondCatch;
    CHECK(!nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},idMath::INFINITY));
    secondRecoveryPad=nullptr; gameLocal.entities[2]=nullptr;
    CHECK(!nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},20));
    CHECK(nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{1,0,0},idMath::INFINITY));
    // An adjustment before the catch is followed by the bot's normal pad
    // coast. Holding this opposite input after launch misses the platform.
    CHECK(nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{-1,0,0},idMath::INFINITY));
    catchPad.live=false;
    CHECK(!nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},idMath::INFINITY));
    catchPad.live=true; recoveryNatural=false;
    CHECK(!nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},idMath::INFINITY));
    recoveryNatural=true; recoveryTouch=false;
    CHECK(!nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},idMath::INFINITY));
    recoveryTouch=true; catchPad.type=2;
    CHECK(!nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},idMath::INFINITY));
    catchPad.type=1; catchTarget.physics.origin.x=2000;
    CHECK(!nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},idMath::INFINITY));
    catchTarget.physics.origin.x=400; recoveryPad=nullptr; gameLocal.entities[1]=nullptr;
    CHECK(!nav.AirInputLandingSafe({-80,0,96},{320,0,-100},{},idMath::INFINITY));
    solids.clear();
    // A hit during DM8's long pad flight pushes its ballistic touchdown
    // beyond the destination platform. "Past the node" alone is not proof
    // of a safe coast. Preserve natural arcs that still reach solid ground.
    Platform(2336,2560,256);
    const navCorner_t padImpulseLanding{{2452,76,256},NAVTRAVEL_JUMPPAD,1};
    CHECK(!BotRidesPadArc({1538,108,434},{822,-10,412},padImpulseLanding,800));
    // A touchdown on the far edge still needs room to shed its momentum.
    solids.back().hi.x=2688;
    CHECK(!BotRidesPadArc({1538,108,434},{822,-10,412},padImpulseLanding,800));
    CHECK(BotRidesPadArc({1538,108,434},{770,-10,328},padImpulseLanding,800));
    solids.back().hi.x=3200;
    CHECK(BotRidesPadArc({1538,108,434},{822,-10,412},padImpulseLanding,800));
    navMesh.steeredPad=true;
    CHECK(!BotRidesPadArc({1538,108,434},{822,-10,412},padImpulseLanding,800));
    navMesh.steeredPad=false;
    solids.clear();
    // A lift/cross/down probe reaches the small landing, but a running
    // drop passes its edge. A broad destination still accepts the drop.
    Platform(-256,0,224); Platform(0,48,0);
    CHECK(!nav.RunningDropClear({-12,0,224},{12,0,0}));
    solids.clear(); Platform(-256,0,224); Platform(0,512,0);
    CHECK(nav.RunningDropClear({-12,0,224},{12,0,0}));
    // The live drop exception must prove the current velocity and command,
    // including a standstill at the lip. It never permits void, excessive
    // impact, transport entry or lateral momentum outside the landing.
    const idVec3 stopped, forwardDrop(1,0,0), slowDrop(0.25f,0,0), sideImpulse(0,800,0);
    CHECK(nav.RunningDropClear({-12,0,224},{12,0,0},&stopped,&forwardDrop,50));
    CHECK(!nav.RunningDropClear({-12,0,224},{12,0,0},&stopped,&forwardDrop,10));
    solids.back().lo.y=-32; solids.back().hi.y=32;
    CHECK(!nav.RunningDropClear({-12,0,224},{12,0,0},&sideImpulse,&forwardDrop,50));
    solids.back().lo.y=-256; solids.back().hi.y=256;
    transported=true;
    CHECK(!nav.RunningDropClear({-12,0,224},{12,0,0},&stopped,&forwardDrop,50));
    transported=false; lethal=true;
    CHECK(!nav.RunningDropClear({-12,0,224},{12,0,0},&stopped,&forwardDrop,50));
    lethal=false;
    solids.back().hi.x=48;
    CHECK(!nav.RunningDropClear({-12,0,224},{12,0,0},&stopped,&forwardDrop,50));
    CHECK(nav.RunningDropClear({-12,0,224},{12,0,0},&stopped,&slowDrop,50));
    solids.back().hi.x=512;
    // A grounded eight-unit lip is a legal step before a broad drop, even
    // when a ceiling rules out jumping over it.
    solids.push_back({{-4,-256,224},{0,256,232}});
    solids.push_back({{-256,-256,324},{16,256,384}});
    CHECK(nav.RunningDropClear({-24,0,224},{24,0,0}));
    // Completing on the same Z does not connect two islands across a gap.
    solids.clear(); Platform(-256,0,0); Platform(64,256,0);
    CHECK(!nav.LandingContinuationClear({-24,0,0},{80,0,0}));
    CHECK(nav.LandingContinuationClear({-128,0,0},{-24,0,0}));
    CHECK(nav.LandingContinuationClear({-128,0,0},{-24,0,16})); // an item origin one step above floor
    // The old eight-unit probe could touch a rim without central support.
    CHECK(!nav.LandingContinuationClear({6,0,0},{6,0,0}));
    // Strauss's rounded CTF3 position was sixteen units past the floor edge,
    // aiming diagonally inland. Model genuine sub-unit box contact, not mere
    // tangency, then also exercise the actual default cylinder footprint.
    solids.clear(); Platform(-256,0,0);
    pm_usecylinder.value=0; agentHull=NavCreateAgentTrace(agentBounds);
    CHECK(!nav.LandingContinuationClear({15.75f,0,0},{15.75f,0,0}));
    CHECK(nav.RecoveryStepClear({15.75f,0,0},{-28,63,0}));
    CHECK(!nav.RecoveryStepClear({16,0,0},{-28,63,0})); // zero overlap is not support
    CHECK(!nav.RecoveryStepClear({15.75f,0,0},{80,0,0}));
    Platform(64,256,0);
    CHECK(!nav.RecoveryStepClear({15.75f,0,0},{80,0,0})); // safe endpoint beyond a real gap
    solids.back().lo.x=35;
    CHECK(!nav.RecoveryStepClear({15.75f,0,0},{80,0,0})); // three-unit loss of support between 4u samples
    CHECK(!nav.RecoveryStepClear({15.75f,0,0},{80,12,0}));
    solids.push_back({{-4,8,0},{4,80,100}});
    CHECK(!nav.RecoveryStepClear({15.75f,0,0},{-28,63,0})); // body collides before reaching support
    solids.clear(); Platform(-256,0,0);
    lethal=true; CHECK(!nav.RecoveryStepClear({15.75f,0,0},{-28,63,0})); lethal=false;
    transported=true; CHECK(!nav.RecoveryStepClear({15.75f,0,0},{-28,63,0})); transported=false;
    CHECK(!nav.RecoveryStepClear({-32,0,0},{80,0,0})); // supported starts cannot gain a gap exception
    pm_usecylinder.value=1; agentHull=NavCreateAgentTrace(agentBounds);
    float cylinderRim=0;
    for(int i=0;i<agentHull.numVerts;i++) cylinderRim=Max(cylinderRim,-agentHull.verts[i].x);
    cylinderRim-=0.25f;
    CHECK(nav.RecoveryStepClear({cylinderRim,0,0},{-28,63,0}));
    solids.clear();
    // Only three units of headroom is still a valid standing corridor.
    // Raising the full hull by a step before the floor probe used to fail it.
    Platform(-256,256,0); solids.push_back({{-256,-256,80},{256,256,128}});
    CHECK(nav.LandingContinuationClear({0,0,0},{0,0,0}));
    CHECK(nav.LandingContinuationClear({0,0,0},{48,0,0}));
    CHECK(nav.LandingContinuationClear({0,0,0},{48,0,16}));
    solids.back().lo.z=70;
    CHECK(!nav.LandingContinuationClear({0,0,0},{0,0,0}));
    CHECK(!nav.LandingContinuationClear({0,0,0},{48,0,0}));
    solids.clear();
    auto clear=[&](idVec3 a,idVec3 b) { return nav.GapJumpClear(a,b,320*0.85f,launch,800); };
    // Walking 72 units is safe on this platform, but a full dodge hop flies
    // beyond it. This was never covered by the ordinary strafe-floor probe.
    Platform(-256,96,0);
    CHECK(nav.SampleFloor({72,0,1},20,rimFloor));
    CHECK(!nav.DodgeJumpClear({0,0,0},{320,0,0},{1,0,0},800));
    solids.clear(); Platform(-256,250,0);
    // This catches the touchdown but leaves less than the ground guard's
    // lookahead. A wider floor permits normal speed, not sprint/haste.
    CHECK(!nav.DodgeJumpClear({0,0,0},{0,0,0},{1,0,0},800));
    solids.front().hi.x=300;
    CHECK(nav.DodgeJumpClear({0,0,0},{0,0,0},{1,0,0},800));
    CHECK(!nav.DodgeJumpClear({0,0,0},{0,0,0},{1,0,0},800,416));
    CHECK(!nav.DodgeJumpClear({0,0,0},{0,0,0},{1,0,0},800,480));
    solids.clear(); Platform(-256,512,0);
    CHECK(nav.DodgeJumpClear({0,0,0},{416,0,0},{1,0,0},800,416));
    CHECK(nav.DodgeJumpClear({0,0,0},{480,0,0},{1,0,0},800,480));
    solids.clear(); Platform(-512,512,0);
    CHECK(nav.DodgeJumpClear({0,0,0},{320,0,0},{1,0,0},800));
    // Existing momentum matters even when the requested input points inland.
    solids.clear(); Platform(-48,320,0);
    CHECK(!nav.DodgeJumpClear({0,0,0},{-320,0,0},{1,0,0},800));
    solids.clear(); Platform(-512,512,0); lethal=true;
    CHECK(!nav.DodgeJumpClear({0,0,0},{320,0,0},{1,0,0},800)); lethal=false;
    transported=true;
    CHECK(!nav.DodgeJumpClear({0,0,0},{320,0,0},{1,0,0},800)); transported=false;
    // q4xdm13's recorded high-speed dodge reaches supported ground near the
    // west rim, but still carries enough momentum to leave it immediately.
    solids.clear(); Platform(-417,300,0);
    CHECK(!nav.DodgeJumpClear({0,0,0},{-575,81,0},{-1,0,0},800));
    solids.front().lo.x=-600;
    CHECK(nav.DodgeJumpClear({0,0,0},{-575,81,0},{-1,0,0},800));
    solids.clear();
    // A standing hull and a vertical projection reach this ledge; a real
    // running jump, even braking after passing its corner, overshoots it.
    Platform(-256,16,0); Platform(32,80,-280);
    CHECK(!clear({0,0,0},{48,0,-280}));
    // The same jump over a broad lower floor is still useful and safe.
    solids.clear(); Platform(-256,16,0); Platform(32,512,-280);
    CHECK(clear({0,0,0},{48,0,-280}));
    // Normal gap jumps survive; a narrow island crossed while still rising
    // cannot substitute for a floor under the actual descending trajectory.
    solids.clear(); Platform(-256,16,0); Platform(64,512,0);
    CHECK(clear({0,0,0},{80,0,0}));
    CHECK(nav.JumpTakeoffClear({0,0,0},{320,0,0},{80,0,0},800,320));
    CHECK(!clear({0,0,0},{300,0,0}));
    solids.clear(); Platform(-256,16,0); Platform(64,96,0);
    CHECK(!clear({0,0,0},{80,0,0}));
    // Grounded support does not make a lethal landing valid.
    solids.clear(); Platform(-256,16,0); Platform(64,512,0); lethal=true;
    CHECK(!clear({0,0,0},{80,0,0})); lethal=false;
    // TryStep's jump-height lift must use the trajectory proof when a
    // railing hides a lower ledge, instead of accepting its vertical drop.
    solids.clear(); Platform(-256,16,0); Platform(32,80,-280);
    solids.push_back({{20,-256,0},{24,256,20}});
    idVec3 floor; int travel=-1;
    CHECK(!nav.TryStep({0,0,0},{48,0,0},floor,travel));
    // q4ctf3 also offered a well-supported 28-unit raised decorative lip.
    // A vertical lift reaches its top, but forward momentum carries the
    // player off the far side before touchdown. The broad version is a
    // legitimate stair: touching its vertical side on ascent must not make
    // every raised jump fail.
    solids.clear(); Platform(-256,16,0); Platform(16,40,28);
    CHECK(NavLandingSupported({24,0,28}));
    CHECK(!nav.TryStep({0,0,0},{24,0,0},floor,travel));
    // Multiplayer permits an air step within one step of the floor. That
    // preserves forward speed instead of stalling against the raised face.
    // A medium platform still cannot hold the resulting running touchdown.
    solids.clear(); Platform(-256,16,0); Platform(16,96,28);
    CHECK(!nav.TryStep({0,0,0},{24,0,0},floor,travel));
    solids.clear(); Platform(-256,16,0); Platform(16,512,28);
    CHECK(nav.TryStep({0,0,0},{24,0,0},floor,travel));
    CHECK(travel==NAVTRAVEL_JUMP && std::fabs(floor.z-28)<0.001f);
    CHECK(nav.JumpTakeoffClear({0,0,0},{0,0,0},{24,0,28},800,320));
    // An air step can sit within one unit of its floor-distance limit.
    // Lifting the live origin by the navigation probe offset misses that
    // floor, clips the forward speed against the riser, and approves a
    // landing which the real momentum-preserving step overshoots.
    solids.clear(); Platform(-256,18,0); Platform(18,62,24);
    const idVec3 stepWish(1,0,0);
    CHECK(!nav.JumpTakeoffClear({0,0,0.25f},{40,0,0},{30,0,24},800,320,&stepWish,226.27417f));
    solids.back().hi.x=512;
    CHECK(nav.JumpTakeoffClear({0,0,0.25f},{40,0,0},{30,0,24},800,320,&stepWish,226.27417f));
    // Cortez's recorded CTF3 launch had an11u/5u corner offset and278u/s
    // transverse momentum. The central graph proof was valid but did not
    // cover that state; the live proof must reject it while retaining a
    // settled launch from the same actual position onto a broad strip.
    solids.clear();
    solids.push_back({{-512,360,-96},{-128,640,-32}});
    solids.push_back({{-384,-256,-320},{-256,360,-256}});
    CHECK(nav.GapJumpClear({-300,372,-32},{-300,348,-256},272,launch,800,true));
    CHECK(!nav.JumpTakeoffClear({-289,377,-32},{-278,-160,0},{-300,348,-256},800,320));
    CHECK(nav.JumpTakeoffClear({-289,377,-32},{0,-160,0},{-300,348,-256},800,320));
    CHECK(nav.JumpTakeoffClear({-300,372,-32},{0,-320,0},{-300,348,-256},800,320));
    // The relevant rectangular CTF3 floor extents also reject the recorded
    // launch. This reduced geometry omits trim/decoration, so it does not
    // substitute for the live map test or assert the graph build outcome.
    solids.clear();
    solids.push_back({{-360,368,-48},{-144,528,-32}});
    solids.push_back({{-512,272,-272},{-288,512,-256}});
    solids.push_back({{-288,48,-272},{-160,464,-256}});
    idVec3 firstWish(-11,-29,0); firstWish.Normalize();
    CHECK(!nav.JumpTakeoffClear({-289,377,-32},{-278,-160,0},{-300,348,-256},800,320,&firstWish,232));
    CHECK(nav.JumpTakeoffClear({-289,377,-32},{0,0,0},{-300,348,-256},800,320,&firstWish,232));
    // Rotate the approach diagonally, as on the recorded q4ctf3 route:
    // its takeoff overhangs the old floor by four units and the destination
    // lies on a light housing just beyond that floor's edge.
    solids.clear(); solids.push_back({{-256,-256,-64},{256,-4,0}});
    solids.push_back({{19,-2,-64},{47,33,28}});
    CHECK(NavLandingSupported({24,24,28}));
    CHECK(!nav.TryStep({0,0,0},{24,24,0},floor,travel));
    solids.back().hi={512,512,28};
    CHECK(nav.TryStep({0,0,0},{24,24,0},floor,travel));
    CHECK(travel==NAVTRAVEL_JUMP);
    // Ordinary player-height steps and a low barrier over broad ground
    // remain traversable by their appropriate action.
    solids.clear(); Platform(-256,16,0); Platform(16,512,16);
    CHECK(nav.TryStep({0,0,0},{24,0,0},floor,travel));
    CHECK(travel==NAVTRAVEL_WALK);
    solids.clear(); Platform(-256,512,0);
    solids.push_back({{16,-256,0},{24,256,20}});
    CHECK(nav.TryStep({0,0,0},{48,0,0},floor,travel));
    CHECK(travel==NAVTRAVEL_JUMP);
    // Even the correct hull may graze a lower rim. Drops need enough floor
    // near the destination centre to tolerate normal controller error.
    solids.clear(); Platform(-256,16,324); Platform(62,128,0);
    CHECK(!nav.TryStep({0,0,324},{48,0,324},floor,travel));
    CHECK(nav.SampleFloor({72,0,325},400,floor)); // support exists, but not running touchdown
    CHECK(!nav.TryStep({0,0,324},{72,0,324},floor,travel));
    solids.back().hi.x=512;
    CHECK(nav.TryStep({0,0,324},{72,0,324},floor,travel));
    CHECK(travel==NAVTRAVEL_DROP);
    // q4ctf3's adjacent hop was aimed beside a raised decorative rim. Use a
    // seven-unit overhang so even a zero-epsilon centre box clips the rim,
    // while support around the destination centre still fails.
    solids.clear(); Platform(36,128,0);
    solids.push_back({{4,7,0},{32,32,28}});
    CHECK(NavFloorSupported({0,0,28}));
    CHECK(!NavLandingSupported({0,0,28}));
    CHECK(!nav.TryStep({24,-24,0},{0,0,0},floor,travel));
    // A normal 24-unit-wide landing remains valid, including a four-unit
    // seam through its middle between separately authored floor brushes.
    solids.clear(); solids.push_back({{-12,-64,-64},{12,64,0}});
    CHECK(NavLandingSupported({0,0,0}));
    CHECK(nav.LandingContinuationClear({0,0,0},{0,0,0}));
    solids.clear(); Platform(-128,-2,0); Platform(2,128,0);
    CHECK(NavLandingSupported({0,0,0}));
    CHECK(nav.LandingContinuationClear({0,0,0},{0,0,0}));
    // Side probes must start high enough for a legitimate rising slope.
    // Their hit height is still constrained to the expected landing floor.
    solids.clear(); slopedFloor=true;
    CHECK(nav.SampleFloor({0,0,64},128,floor));
    CHECK(NavLandingSupported(floor));
    CHECK(nav.LandingContinuationClear(floor,floor));
    // Edge braking starts from the small support hull's floor, which sits
    // lower on a slope than the full player hull. Both describe safe ground.
    // Exercise signed/custom cylinders and box players across slope headings.
    for (int cylinder : {0, 1, -1, 4}) {
        pm_usecylinder.value=cylinder; agentHull=NavCreateAgentTrace(agentBounds);
        for (idVec3 gradient : {idVec3(1,0,0),idVec3(-1,0,0),idVec3(0,1,0),
                               idVec3(0,-1,0),idVec3(0.5f,0.5f,0),idVec3(-0.5f,0.5f,0),
                               idVec3(0.25f,0,0)}) {
            slopeX=gradient.x; slopeY=gradient.y;
            CHECK(nav.SampleFloor({0,0,64},128,floor));
            CHECK(NavLandingSupported(floor));
            trace_t supportFloor;
            CHECK(!NavTraceTranslation(supportFloor,{0,0,64},{0,0,-64},navSupportTrace,navSupportBounds));
            CHECK(supportFloor.endpos.z < floor.z);
            CHECK(nav.LandingContinuationClear(supportFloor.endpos,supportFloor.endpos));
            // A whole-step lift would overlap this ceiling. The exact hull
            // contact height leaves three units of real standing headroom.
            solids.push_back({{-256,-256,floor.z+80},{256,256,floor.z+128}});
            CHECK(nav.LandingContinuationClear(supportFloor.endpos,supportFloor.endpos));
            solids.back().lo.z=floor.z+70;
            CHECK(!nav.LandingContinuationClear(supportFloor.endpos,supportFloor.endpos));
            solids.clear();
            lethal=true;
            CHECK(!nav.LandingContinuationClear(supportFloor.endpos,supportFloor.endpos));
            lethal=false;
        }
    }
    pm_usecylinder.value=1; agentHull=NavCreateAgentTrace(agentBounds);
    slopeX=1; slopeY=0; slopedFloor=false;
    // The independent swept landing feeds CrashLand's squared speed and
    // surface/water discounts, not a hardcoded ban on pads or low health.
    trace_t impact; impact.endpos={0,0,0}; impact.c.material=&floorMaterial;
    CHECK(std::fabs(NavLandingImpactDelta(impact,{0,0,-745},{0,0,-800})-55.5025f)<0.001f);
    floorMaterial.flags=SURF_NODAMAGE;
    CHECK(NavLandingImpactDelta(impact,{0,0,-745},{0,0,-800})==0); floorMaterial.flags=0;
    gameLocal.waterTop=2;
    CHECK(std::fabs(NavLandingImpactDelta(impact,{0,0,-745},{0,0,-800})-27.75125f)<0.001f);
    gameLocal.waterTop=50;
    CHECK(std::fabs(NavLandingImpactDelta(impact,{0,0,-745},{0,0,-800})-13.875625f)<0.001f);
    gameLocal.waterTop=80;
    CHECK(NavLandingImpactDelta(impact,{0,0,-745},{0,0,-800})==0); gameLocal.waterTop=-1e30f;
    solids.clear(); Platform(-512,512,0);
    navCorner_t lower{{0,0,0},NAVTRAVEL_DROP};
    CHECK(!nav.LandingSurvivable({0,0,320},lower,40));
    CHECK(nav.LandingSurvivable({0,0,320},lower,60));
    CHECK(nav.LandingSurvivable({0,0,250},lower,40));
    lower.travelType=NAVTRAVEL_JUMP;
    CHECK(!nav.LandingSurvivable({0,0,250},lower,40));
    idEntity pad,target; pad.targets.push_back({&target}); target.physics.origin={0,0,400};
    gameLocal.entities[1]=&pad;
    navCorner_t padEnd{{0,0,0},NAVTRAVEL_JUMPPAD,1};
    CHECK(!nav.LandingSurvivable({0,0,0},padEnd,60));
    CHECK(nav.LandingSurvivable({0,0,0},padEnd,idMath::INFINITY));
    floorMaterial.flags=SURF_NODAMAGE;
    CHECK(nav.LandingSurvivable({0,0,0},padEnd,40)); floorMaterial.flags=0;
    gameLocal.waterTop=50;
    CHECK(nav.LandingSurvivable({0,0,0},padEnd,40)); gameLocal.waterTop=-1e30f;
    target.physics.origin.z=200;
    CHECK(nav.LandingSurvivable({0,0,0},padEnd,40));
    // A single push is below the hard-fall threshold. Reapplying it while the
    // player still touches the launch volume crosses that threshold.
    solids.clear(); solids.push_back({{-2048,-2048,-64},{2048,2048,0}});
    target.physics.origin={497,497,347};
    CHECK(nav.LandingSurvivable({0,0,14},padEnd,60));
    repeatedPad=&pad; repeatedPadTouches=0;
    CHECK(!nav.LandingSurvivable({0,0,14},padEnd,60));
    CHECK(repeatedPadTouches==2);
    floorMaterial.flags=SURF_NODAMAGE;
    CHECK(nav.LandingSurvivable({0,0,14},padEnd,60));
    floorMaterial.flags=0; repeatedPad=nullptr;
    // q4ctf1: a string-pulled walk from the low floor to a node just past
    // the lip of a 64-unit platform.  The raised straight sweep climbs with
    // the line and clears the riser's top, and every interior sample stays
    // within the drop tolerance of the ground under it - but the floor
    // samples jump a whole riser between two of them, which no walk does.
    solids.clear(); Platform(-256,0,0); Platform(0,256,64);
    const char* why="walkable";
    CHECK(!nav.WalkableLine({-110,0,0},{16,0,64},&why));
    CHECK(std::string(why)=="rises more than a step between samples");
    // The same climb as a stair of player-height steps is a walk: each
    // sample catches at most a step more than the one before it.
    solids.clear(); Platform(-256,-72,0); Platform(-72,-48,16); Platform(-48,-24,32); Platform(-24,0,48); Platform(0,256,64);
    CHECK(nav.WalkableLine({-110,0,0},{16,0,64},&why));
    // A corner on the lip itself, above the last floor the line crossed, is
    // a climb onto its end.
    solids.clear(); Platform(-256,0,0); Platform(0,256,48);
    CHECK(!nav.WalkableLine({-92,0,0},{4,0,48},&why));
    CHECK(std::string(why)=="rises more than a step onto its end");
    // Level ground, a single step up and a step down stay walkable.
    solids.clear(); Platform(-256,256,0);
    CHECK(nav.WalkableLine({-100,0,0},{100,0,0}));
    solids.clear(); Platform(-256,0,0); Platform(0,256,16);
    CHECK(nav.WalkableLine({-100,0,0},{24,0,16}));
    solids.clear(); Platform(-256,0,16); Platform(0,256,0);
    CHECK(nav.WalkableLine({-40,0,16},{40,0,0}));
    // A line along a platform's rim, four units past its edge as q4ctf1's
    // platform and q4dz1's catwalk nodes stand, keeps the hull's far half
    // over the drop: the ground a support box's width to either side of the
    // line has to be there too.  Four units inside the edge it is.
    solids.clear(); Platform(-256,0,0);
    CHECK(nav.SampleFloor({4,0,1},20,rimFloor));
    CHECK(!nav.WalkableLine({4,-100,0},{4,100,0},&why));
    CHECK(std::string(why)=="hangs over an edge");
    CHECK(nav.WalkableLine({-4,-100,0},{-4,100,0},&why));
    CHECK(NavDropBeside({4,0,0},{8,0,0}));
    CHECK(!NavDropBeside({-4,0,0},{8,0,0}));
    // A rim node's hull hangs more than eight units past an edge.  A wall
    // beside a node is not an edge, and neither is the next tread of a stair.
    CHECK(NavRimNode({4,0,0}));
    CHECK(NavRimNode({-4,0,0}));
    CHECK(!NavRimNode({-12,0,0}));
    solids.clear(); Platform(-256,256,0); solids.push_back({{16,-256,0},{40,256,100}});
    CHECK(!NavRimNode({0,0,0}));
    solids.clear(); Platform(-256,-24,0); Platform(-24,0,16); Platform(0,256,32);
    CHECK(!NavRimNode({-12,0,16}));
    CHECK(!NavRimNode({-36,0,0}));
    CHECK(!NavDropBeside({-36,0,0},{16,0,0}));
    CHECK(nav.WalkableLine({-44,-100,0},{-44,100,0},&why));
    {   // A knocked fall (q4ctf5/q4dm8): which fixed air input comes down on
        // supported, affordable ground with room to stop, on this world.
        const float inf=std::numeric_limits<float>::infinity();
        // A fast fall just below a ledge hits its side. Lifting the prediction
        // one unit invents clearance and approves a landing the live hull misses.
        solids.clear(); Platform(24,1024,0.5f);
        CHECK(!nav.AirInputLandingSafe({0,0,0},{500,0,0},{},inf));
        CHECK(nav.AirInputLandingSafe({0,0,1},{500,0,0},{},inf));
        // A descending player close to a floor can air-step onto a raised
        // lip. The full step keeps horizontal momentum, including overshoot.
        solids.clear(); Platform(-1024,24,0); Platform(24,1024,12);
        idVec3 stepped;
        CHECK(nav.AirInputLandingSafe({0,0,8},{500,0,-20},{},inf,&stepped));
        CHECK(stepped.z>11 && stepped.x>8);
        solids.clear(); Platform(-1024,24,0); Platform(24,40,12);
        CHECK(!nav.AirInputLandingSafe({0,0,8},{500,0,-20},{},inf));
        // Knockback can carry a grounded player beyond its ordinary stopping
        // reserve before friction returns. Follow the coast to the next floor.
        solids.clear(); Platform(-1024,0,0);
        CHECK(!nav.KnockSlideLandingSafe({-120,0,0.25f},{700,0,0},200,inf));
        Platform(80,1024,-32);
        CHECK(nav.KnockSlideLandingSafe({-120,0,0.25f},{700,0,0},200,inf));
        CHECK(!nav.KnockSlideLandingSafe({-120,0,0.25f},{700,0,0},200,0));
        CHECK(!nav.KnockSlideLandingSafe({-120,0,0.25f},{700,0,20},200,inf));
        transported=true;
        CHECK(!nav.KnockSlideLandingSafe({-120,0,0.25f},{700,0,0},200,inf));
        transported=false;
        // Starting steering during the grounded hit can retain the upper
        // platform even when coasting would leave it over a void.
        solids.clear(); Platform(-1024,0,0);
        CHECK(nav.KnockSlideLandingSafe({-180,0,0.25f},{700,0,0},200,inf,{-1,0,0}));
        // At this rim only the expiration frame's restored friction and
        // ground acceleration retain support. One extra knock frame loses it.
        CHECK(nav.KnockSlideLandingSafe({-136,0,0.25f},{700,0,0},200,inf,{-1,0,0}));
        CHECK(!nav.KnockSlideLandingSafe({-136,0,0.25f},{700,0,0},216,inf,{-1,0,0}));
        CHECK(!nav.KnockSlideLandingSafe({-132,0,0.25f},{700,0,0},200,inf,{-1,0,0}));
        CHECK(!nav.KnockSlideLandingSafe({-180,0,0.25f},{700,0,0},200,inf,{1,0,0}));
        floorMaterial.flags=SURF_SLICK;
        CHECK(!nav.KnockSlideLandingSafe({-180,0,0.25f},{700,0,0},200,inf,{-1,0,0})); floorMaterial.flags=0;
        pm_frictionoverride.value=2;
        CHECK(!nav.KnockSlideLandingSafe({-180,0,0.25f},{700,0,0},200,inf,{-1,0,0})); pm_frictionoverride.value=-1;
        // The splash proof can start above the floor during a descending
        // jump. It must not turn the existing fall into a fresh drop at rest.
        solids.clear(); Platform(-1024,1024,0);
        CHECK(!nav.KnockSlideLandingSafe({0,0,64},{0,0,-900},50,40));
        CHECK(nav.KnockSlideLandingSafe({0,0,64},{0,0,-900},50,inf));
        solids.clear(); Platform(128,1024,0);
        CHECK(nav.KnockSlideLandingSafe({0,0,64},{500,0,0},50,inf));
        CHECK(!nav.KnockSlideLandingSafe({0,0,64},{500,0,-900},50,inf));
        // A sweep can clip velocity before the actor registers any ground
        // contact. Only that missing end-of-step contact permits deferring
        // CrashLand; an ordinary grounded impact keeps its full cost.
        solids.clear(); Platform(-1024,1024,0);
        CHECK(!nav.AirInputLandingSafe({0,0,64},{100,0,-900},{},40));
        missingGroundContacts=1;
        CHECK(nav.AirInputLandingSafe({0,0,64},{100,0,-900},{},40));
        CHECK(missingGroundContacts==0);
        transported=true; missingGroundContacts=1;
        CHECK(!nav.AirInputLandingSafe({0,0,64},{100,0,-900},{},40));
        transported=false; missingGroundContacts=0;
        // An affordable swept touch is not a landing either. A sloped lip
        // can redirect the flight without ever making ground contacts.
        // Neither support nor the last-resort touch may approve that flight.
        bool phantomSupport=false,phantomTouch=false,phantomBrake=false;
        missingGroundContacts=1000;
        CHECK(!nav.AirInputLandingSafe({0,0,64},{100,0,-100},{},inf,nullptr,
              &phantomSupport,&phantomTouch,&phantomBrake));
        CHECK(!phantomSupport && !phantomTouch && !phantomBrake);
        missingGroundContacts=0;
        CHECK(nav.AirInputLandingSafe({0,0,64},{100,0,-100},{},inf));
        // Involuntary recovery may aim for a physical hull contact when
        // every supported landing fails. It remains unsafe for route proof.
        solids.clear(); Platform(-1,0,0);
        bool touched=false;
        CHECK(!nav.AirInputLandingSafe({14,0,10},{0,0,-20},{},inf,nullptr,nullptr,&touched) && touched);
        idPlayer rimFall; rimFall.physics.origin={14,0,10}; rimFall.physics.velocity={0,0,-20};
        idVec3 rimInput;
        CHECK(BotChooseAirInput(&rimFall,rimInput) && rimInput.LengthSqr()==0);
        transported=true;
        CHECK(!nav.AirInputLandingSafe({14,0,10},{0,0,-20},{},inf,nullptr,nullptr,&touched) && !touched);
        transported=false;
        // CTF5 rail recovery: first contact is a rim, but inward momentum
        // retains shared hull support until the whole footprint is on floor.
        solids.clear(); Platform(0,1024,0);
        CHECK(nav.AirInputLandingSafe({-215.628f,0,202.972f},{168.359f,36.745f,31.18f},
                                     {0.9239f,-0.3827f,0},inf));
        CHECK(!nav.AirInputLandingSafe({-215.628f,0,202.972f},{168.359f,36.745f,31.18f},
                                      {-1,0,0},inf));
        lethal=true;
        CHECK(!nav.AirInputLandingSafe({-215.628f,0,202.972f},{168.359f,36.745f,31.18f},
                                      {0.9239f,-0.3827f,0},inf));
        lethal=false;
        const idVec3 thrown{250,0,150};
        idVec3 touch; bool supported=false;
        solids.clear(); Platform(-256,0,0); Platform(-256,1024,-128);
        CHECK(nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},inf,&touch,&supported) && supported);
        CHECK(std::fabs(touch.z+128)<1 && touch.x>150 && touch.x<230); // the coast's own touchdown
        CHECK(!nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},0.0f,nullptr,&supported) && !supported); // unaffordable impact
        lethal=true; CHECK(!nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},inf,nullptr,&supported) && !supported); lethal=false;
        // Landing into a pad's volume is another throw, not a landing.
        transported=true; CHECK(!nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},inf,nullptr,&supported) && !supported);
        transported=false;
        // Nothing below: a fall into the void never lands.
        solids.clear(); Platform(-256,0,0);
        CHECK(!nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},inf,nullptr,&supported) && !supported);
        CHECK(!nav.AirInputLandingSafe({-10,0,0.25f},thrown,{-1,0,0},inf,nullptr,&supported) && !supported);
        // A ledge beyond the coast's touchdown is reached only by input along
        // the throw, and a ledge short of it only by braking: the input decides.
        Platform(215,1024,-128);
        CHECK(!nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},inf));
        CHECK(!nav.AirInputLandingSafe({-10,0,0.25f},thrown,{-1,0,0},inf));
        CHECK(nav.AirInputLandingSafe({-10,0,0.25f},thrown,{1,0,0},inf,&touch) && touch.x>215);
        solids.clear(); Platform(-256,0,0); Platform(40,130,-128);
        CHECK(!nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},inf));
        CHECK(nav.AirInputLandingSafe({-10,0,0.25f},thrown,{-1,0,0},inf,&touch) && touch.x<130);
        // Supported ground without room to stop is reported as such.
        solids.clear(); Platform(-256,0,0); Platform(168,196,-128);
        bool safe=nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},inf,&touch,&supported);
        CHECK(!safe && supported);
        // A wall in the throw's path is slid down, not landed on: the fall
        // still ends on the floor below, in front of the wall.
        solids.clear(); Platform(-256,0,0); Platform(-256,1024,-128); solids.push_back({{60,-256,-128},{80,256,400}});
        nav.AirInputLandingSafe({-10,0,0.25f},thrown,{},inf,&touch);
        CHECK(std::fabs(touch.z+128)<1 && touch.x<60);
        // A narrow lower walkway sits between the old 45-degree headings.
        // Choose against the actual swept hull, rather than mocking which
        // heading the navigation simulation accepts.
        solids.clear();
        solids.push_back({{20,26,-192},{1024,64,-128}});
        idPlayer falling; falling.physics.origin={-10,0,0.25f}; falling.physics.velocity=thrown;
        idVec3 chosen;
        BotChooseAirInput(&falling,chosen);
        bool chosenSupported=false;
        nav.AirInputLandingSafe(falling.physics.origin,thrown,chosen,inf,nullptr,&chosenSupported);
        CHECK(chosenSupported);
        CHECK(chosen.y>0 && chosen.y<0.7f);

        // At critically low health no impact is affordable. Still steer to
        // a supported floor instead of default braking into the void: a
        // pickup may save the landing, while the void cannot.
        fallBudget=0; chosen={};
        BotChooseAirInput(&falling,chosen);
        nav.AirInputLandingSafe(falling.physics.origin,thrown,chosen,inf,nullptr,&chosenSupported);
        CHECK(chosenSupported);
        CHECK(!nav.AirInputLandingSafe(falling.physics.origin,thrown,chosen,fallBudget));
        const idVec3 lastResort=chosen;
        BotChooseAirInput(&falling,chosen);
        CHECK(chosen.x==lastResort.x && chosen.y==lastResort.y);
        fallBudget=inf;
        // A viable held input survives a replan even when braking is also
        // safe. Replacing it each time steers away from its original proof.
        solids.clear(); Platform(-1024,1024,-128);
        chosen={0,1,0};
        BotChooseAirInput(&falling,chosen);
        CHECK(chosen.x==0 && chosen.y==1);
        // Coasting is also a proven input; do not replace it with a brake
        // simply because its vector is zero.
        chosen={}; BotChooseAirInput(&falling,chosen);
        CHECK(chosen.LengthSqr()==0);

        // A nearby live health pickup is preferable when all supported
        // landings exceed the health budget. Taken/hidden/other-instance
        // pickups cannot make a landing look better.
        fallBudget=0;
        idVec3 healthLanding;
        CHECK(nav.AirInputLandingSafe(falling.physics.origin,thrown,{0,1,0},inf,&healthLanding));
        idItem healthItem; healthItem.triggerModel.bounds={{-16,-16,-16},{16,16,16}};
        healthItem.triggerModel.bounds.TranslateSelf(healthLanding);
        botManager.items.push_back(&healthItem);
        CHECK(BotRecoveryHealthAt(&falling,healthLanding)==50);
        chosen={}; BotChooseAirInput(&falling,chosen);
        idVec3 pickedLanding;
        CHECK(nav.AirInputLandingSafe(falling.physics.origin,thrown,chosen,inf,&pickedLanding));
        CHECK(BotRecoveryHealthAt(&falling,pickedLanding)==50);
        healthItem.pickedUp=true; CHECK(BotRecoveryHealthAt(&falling,healthLanding)==0);
        healthItem.pickedUp=false; healthItem.hidden=true; CHECK(BotRecoveryHealthAt(&falling,healthLanding)==0);
        healthItem.hidden=false; healthItem.instance=1; CHECK(BotRecoveryHealthAt(&falling,healthLanding)==0);
        healthItem.instance=0; healthItem.canPickUp=false; CHECK(BotRecoveryHealthAt(&falling,healthLanding)==0);
        healthItem.canPickUp=true; healthItem.triggerModel.contents=0; CHECK(BotRecoveryHealthAt(&falling,healthLanding)==0);
        healthItem.triggerModel.contents=CONTENTS_TRIGGER; healthItem.trigger=nullptr;
        CHECK(BotRecoveryHealthAt(&falling,healthLanding)==0);
        botManager.items.clear(); fallBudget=inf;
        std::puts("Knocked falls: fixed air input lands only on supported, affordable floor with room to stop; void, walls and input choice: PASS");
    }
    std::puts("Player hulls and jump/drop trajectories: unsupported corners/rims rejected, broad landings/gaps retained: PASS");
    return 0;
}
