// Lightweight world fixtures; decision functions below are taken verbatim from
// production by mp_bot_regressions.py. No second implementation of goal policy.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>
#undef INFINITY
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
template<class T> T Min(T a, T b) { return std::min(a,b); }
template<class T> T Max(T a, T b) { return std::max(a,b); }
float Square(float x) { return x*x; }
float DEG2RAD(float x) { return x*0.01745329252f; }
float MS2SEC(int x) { return x*0.001f; }
int ANGLE2SHORT(float x) { return int(x*65536.0f/360.0f)&65535; }
struct idMath {
    static constexpr float INFINITY = std::numeric_limits<float>::infinity();
    static constexpr float FLOAT_EPSILON = 0.000001f;
    static float ClampFloat(float lo, float hi, float x) { return std::clamp(x,lo,hi); }
    static int ClampInt(int lo, int hi, int x) { return std::clamp(x,lo,hi); }
    static float Fabs(float x) { return std::fabs(x); }
    static float Sqrt(float x) { return std::sqrt(x); }
    static float Cos(float x) { return std::cos(x); }
    static float Sin(float x) { return std::sin(x); }
    static float Ceil(float x) { return std::ceil(x); }
    static int ClampChar(int x) { return std::clamp(x,-128,127); }
    static float AngleNormalize180(float x) { while(x>180) x-=360; while(x<-180) x+=360; return x; }
};
struct idVec3 {
    float x=0,y=0,z=0;
    idVec3() = default;
    idVec3(float a,float b,float c):x(a),y(b),z(c) {}
    void Set(float a,float b,float c) { x=a; y=b; z=c; }
    idVec3 operator+(const idVec3& b) const { return {x+b.x,y+b.y,z+b.z}; }
    idVec3& operator+=(const idVec3& b) { x+=b.x; y+=b.y; z+=b.z; return *this; }
    idVec3 operator-(const idVec3& b) const { return {x-b.x,y-b.y,z-b.z}; }
    idVec3 operator-() const { return {-x,-y,-z}; }
    idVec3 operator*(float f) const { return {x*f,y*f,z*f}; }
    idVec3& operator*=(float f) { x*=f; y*=f; z*=f; return *this; }
    float operator*(const idVec3& b) const { return x*b.x+y*b.y+z*b.z; }
    float LengthSqr() const { return *this * *this; }
    float Length() const { return std::sqrt(LengthSqr()); }
    float Normalize() { float n=Length(); if(n>0) *this=*this*(1/n); return n; }
    void Zero() { *this={}; }
    bool Compare(const idVec3& b) const { return x==b.x && y==b.y && z==b.z; }
    const char* ToString(int) const { return "fixture"; }
};
const idVec3 vec3_zero;
const idVec3 vec3_origin;
struct idAngles {
    float pitch=0,yaw=0,roll=0;
    idAngles(float p=0,float y=0,float r=0):pitch(p),yaw(y),roll(r) {}
    void ToVectors(idVec3* forward,idVec3* right,void*) const {
        const float angle=DEG2RAD(yaw);
        *forward={std::cos(angle),std::sin(angle),0}; *right={std::sin(angle),-std::cos(angle),0};
    }
    idVec3 ToForward() const { return {std::cos(DEG2RAD(yaw)),std::sin(DEG2RAD(yaw)),0}; }
};
struct idBounds {
    idVec3 center;
    float radius=1;
    idBounds() = default;
    idBounds(idVec3 c,float r):center(c),radius(r) {}
    idBounds(idVec3 lo,idVec3 hi):center((lo+hi)*.5f),radius((hi-lo).Length()*.5f) {}
    idVec3 operator[](int i) const { const float r=i?radius:-radius; return center+idVec3(r,r,r); }
    idVec3 GetCenter() const { return center; }
    float GetRadius() const { return radius; }
    float ShortestDistance(idVec3 p) const { return Max(0.0f,(p-center).Length()-radius); }
    idBounds operator+(idVec3 p) const { return {center+p,radius}; }
    bool IntersectsBounds(const idBounds& b) const { return (center-b.center).Length()<=radius+b.radius; }
};
struct idMat3 { idVec3 forward{1,0,0}; idVec3 operator[](int) const { return forward; } };
struct idCollisionModel {};
struct idClipModel {
    idCollisionModel model;
    idCollisionModel* GetCollisionModel() const { return const_cast<idCollisionModel*>(&model); }
    idVec3 GetOrigin() const { return {}; }
    idMat3 GetAxis() const { return {}; }
};
template<class T> struct idList : std::vector<T> {
    int Num() const { return int(this->size()); }
    void SetGranularity(int) {}
    void Insert(T x,int i) { this->insert(this->begin()+i,x); }
    void RemoveIndex(int i) { this->erase(this->begin()+i); }
    void SetNum(int n,bool=true) { this->resize(n); }
    int Append(const T& x) { this->push_back(x); return Num()-1; }
    T* Ptr() { return this->data(); }
};
struct idStr : std::string { using std::string::string; };
// Keyed like an idDict: an unset key answers the caller's default.
struct Args {
    std::map<std::string,std::string> values;
    void Set(const char* k,const char* v) { values[k]=v; }
    const char* GetString(const char* k,const char* def) const {
        auto it=values.find(k); return it==values.end() ? def : it->second.c_str();
    }
    int GetInt(const char* k,const char* def="0") const { return std::atoi(GetString(k,def)); }
    bool GetBool(const char* k,const char* def) const { return std::atoi(GetString(k,def))!=0; }
    float GetFloat(const char* k,const char* def) const { return std::atof(GetString(k,def)); }
};
using idDict=Args;
struct Physics {
    idVec3 origin,velocity,gravity;
    idClipModel clip;
    float radius=1;
    int knockback=0;
    int GetKnockBack() const { return knockback; }
    const idVec3& GetOrigin() const { return origin; }
    const idVec3& GetLinearVelocity() const { return velocity; }
    const idVec3& GetGravity() const { return gravity; }
    idBounds GetAbsBounds() const { return {origin,radius}; }
    idBounds GetBounds() const { return {{},radius}; }
    const idClipModel* GetClipModel() const { return &clip; }
    int ClipContents(const idClipModel*) const { return 1; }
    idMat3 GetAxis() const { return {}; }
};
struct usercmd_t;
struct idPhysics_Player : Physics {
    bool walking=true; float playerSpeed=320;
    float CmdScale(const usercmd_t&,bool) const;
};
struct idEntity;
struct Link { idEntity* next=nullptr; idEntity* Next() const { return next; } };
struct idEntity {
    idPhysics_Player physics;
    Args spawnArgs;
    Link spawnNode;
    int kind=0,instance=0,entityNumber=0;
    bool hidden=false;
    Physics* GetPhysics() { return &physics; }
    const Physics* GetPhysics() const { return &physics; }
    bool IsType(int type) const { return kind==type; }
    bool IsHidden() const { return hidden; }
    int GetInstance() const { return instance; }
};
enum { NAVTRAVEL_WALK,NAVTRAVEL_JUMP,NAVTRAVEL_JUMPPAD,NAVTRAVEL_DROP,POWERUP_INVISIBILITY,POWERUP_QUADDAMAGE,POWERUP_DOUBLER,BUTTON_ATTACK,BOTCHAT_ITEM_DENIED,BUTTON_RUN,PMOD_SPEED,NAVTRAVEL_TELEPORT };
struct idPlayer : idEntity {
    Physics* GetPlayerPhysics() { return &physics; }
    float deltaViewAngles[3]={};
    const float* GetDeltaViewAngles() const { return deltaViewAngles; }
    float speedModifier=1;
    float PowerUpModifier(int) const { return speedModifier; }
    int health=100,visibilityCalls=0,powerups=0;
    float targetScore=1;
    bool visible=true,invisible=false;
    bool spectating=false,godmode=false;
    int damageCalculations=0,godCounter=0;
    struct { bool onGround=true,noFallingDamage=false; } pfl;
    int spectator=-1;
    struct Inventory { int maxHealth=100,armor=0; } inventory;
    idPlayer() { kind=1; }
    static int GetClassType() { return 1; }
    void GetViewPos(idVec3& eye,idMat3& axis) { eye=physics.origin; axis={}; }
    bool PowerUpActive(int p) const { return p==POWERUP_INVISIBILITY ? invisible : ( powerups & ( 1<<p ) )!=0; }
    // A deterministic damage service: policy must ask it, not assume that
    // every pad costs a fixed HP amount or that armour prevents all damage.
    void CalcDamagePoints(idEntity* inflictor,idEntity* attacker,const idDict* def,float,int,int* damage,int* armor) {
        CHECK(inflictor==attacker); ++damageCalculations;
        int raw=def->GetInt("damage","0");
        if(godmode && !def->GetBool("noGod","0")) { godCounter+=raw; raw=0; }
        *armor=def->GetBool("noArmor","0") ? 0 : Min(inventory.armor,Min(Max(0,raw-1),int(std::ceil(raw*0.66f))));
        *damage=raw-*armor;
    }
};
struct rvJumpPad : idEntity {
    rvJumpPad() { kind=4; }
    static int GetClassType() { return 4; }
    bool GetForceFieldBounds(idBounds& out) const { out=physics.GetAbsBounds(); return true; }
    const idClipModel* GetForceFieldClipModel() const { return &physics.clip; }
};
struct idItem : idEntity {
    bool pickedUp=false;
    int pickedUpByClientNum=-1,claims=0,pickupTime=-1,knownRespawn=0;
    float utility=1;
    idItem() { kind=2; }
    static int GetClassType() { return 2; }
};
struct idProjectile : idEntity {
    float splash=0;
    idEntity* owner=nullptr;
    idProjectile() { kind=3; }
    static int GetClassType() { return 3; }
    idEntity* GetOwner() const { return owner; }
};
template<class T> struct idEntityPtr {
    T* ptr=nullptr;
    T* GetEntity() const { return ptr; }
    idEntityPtr& operator=(T* p) { ptr=p; return *this; }
};
enum { BOTGOAL_NONE,BOTGOAL_ROAM,BOTGOAL_ITEM,BOTGOAL_ENEMY,BOTGOAL_OBJECTIVE };
enum { BOTOBJ_NONE,BOTOBJ_CAPTURE,BOTOBJ_INTERCEPT,BOTOBJ_RETURN,BOTOBJ_ESCORT,BOTOBJ_FETCH,BOTOBJ_RESCUE,BOTOBJ_CONTROL,BOTOBJ_DEFEND };
const int MAX_CLIENTS=64;
const int MAX_GENTITIES=64;
enum { DEMO_NONE,DEMO_RECORDING,GAME_UNRELIABLE_RECORD_CLIENTNUM,GAME_UNRELIABLE_RECORD_AREAS_INSTANCE,GAME_UNRELIABLE_RECORD_AREAS };
using byte=unsigned char;
struct idBitMsg {
    int size=8;
    byte data[16]={};
    void Init(byte*,int) { size=0; }
    void WriteByte(int) { ++size; }
    void WriteChar(int) { ++size; }
    void WriteLong(int) { size+=4; }
    int GetSize() const { return size; }
    const byte* GetData() const { return data; }
};
struct idMsgQueue {
    int bytes=0,messages=0;
    int GetTotalSize() const { return bytes; }
    bool Add(const byte*,int size,bool) { bytes+=size; ++messages; return true; }
    void AddConcat(const byte*,int header,const byte* data,int size,bool b) { Add(data,size+header,b); }
};
struct Common {
    int warnings=0;
    template<class... T> void Warning(const char*,T...) { ++warnings; }
} commonObject;
Common* common=&commonObject;
// PRODUCTION_CONSTANTS
struct navCorner_t { idVec3 origin; int travelType=NAVTRAVEL_WALK,actionEntityNum=-1; };
struct navFieldTarget_t { int node; float weight,minWeight,extra; };
struct rvNavPath {
    std::vector<navCorner_t> corners;
    void Clear() { corners.clear(); }
    int Num() const { return int(corners.size()); }
    bool IsEmpty() const { return corners.empty(); }
    const navCorner_t& operator[](int i) const { return corners[i]; }
    void Append(idVec3 p,int type=NAVTRAVEL_WALK,int action=-1) { corners.push_back({p,type,action}); }
    float LengthFrom(idVec3 p) const {
        float result=0; for (auto c:corners) { result+=(c.origin-p).Length(); p=c.origin; } return result;
    }
};
// A navmesh whose node 0 is wherever the last cost field started and whose
// other nodes are the entities snapped onto it.  Field costs are straight-line
// distances; x positions in `blocked` refuse routes, in `unreachable` refuse
// the field too.  A field given targets records how many it was asked for.
struct Nav {
    bool slideCoastSafe=false;
    bool slideInputSafe=false;
    mutable idVec3 lastSlideOrigin,lastSlideVelocity;
    mutable int slideChecks=0,lastSlideKnock=0;
    bool valid=true;
    bool KnockSlideLandingSafe(const idVec3& origin,const idVec3& velocity,int knock,float,const idVec3& wish=vec3_origin) const {
        ++slideChecks; lastSlideOrigin=origin; lastSlideVelocity=velocity; lastSlideKnock=knock;
        return slideCoastSafe || (slideInputSafe && wish.x>0.70f && wish.x<0.71f && wish.y>0.70f);
    }
    bool IsValid() const { return valid; }
    int probes=0,searches=0,fields=0,snaps=0,lastTargets=-1;
    bool roam=false,fieldLive=false;
    int transport=-1;
    bool naturalPad=false;
    idVec3 padLanding;
    bool dodgeClear=false;
    bool dodgeFlightSafe=true;
    int dodgeFlightChecks=0;
    bool AirInputLandingSafe(const idVec3&,const idVec3&,const idVec3&,float) {
        ++dodgeFlightChecks; return dodgeFlightSafe;
    }
    int dodgeChecks=0;
    idVec3 lastDodgeDirection;
    float lastDodgeSpeed=0;
    bool DodgeJumpClear(const idVec3&,const idVec3&,const idVec3& direction,float,float speed) {
        ++dodgeChecks; lastDodgeDirection=direction; lastDodgeSpeed=speed; return dodgeClear;
    }
    int landingChecks=0;
    float landingImpact=0,lastImpactLimit=idMath::INFINITY;
    idVec3 lastLandingFrom;
    std::map<float,float> impactAtX; // a corner whose x is listed lands with that impact instead
    bool LandingSurvivable(const idVec3& from,const navCorner_t& corner,float limit) {
        ++landingChecks; lastLandingFrom=from;
        auto it=impactAtX.find(corner.origin.x);
        return (it==impactAtX.end() ? landingImpact : it->second)<=limit;
    }
    bool continuationClear=true,directionalContinuation=false;
    bool jumpTakeoffClear=false;
    int jumpTakeoffChecks=0;
    idVec3 lastJumpOrigin,lastJumpVelocity,lastJumpLanding,lastJumpFirstWish;
    float lastJumpSpeed=0,lastJumpFirstSpeed=0;
    bool JumpTakeoffClear(const idVec3& origin,const idVec3& velocity,const idVec3& landing,float,float speed,const idVec3* firstWish,float firstSpeed) {
        ++jumpTakeoffChecks; lastJumpOrigin=origin; lastJumpVelocity=velocity;
        lastJumpLanding=landing; lastJumpSpeed=speed; lastJumpFirstWish=*firstWish;
        lastJumpFirstSpeed=firstSpeed; return jumpTakeoffClear;
    }
    bool recoveryStepClear=false;
    int recoveryStepChecks=0;
    bool RecoveryStepClear(const idVec3&,const idVec3& target,idVec3* supported=nullptr) {
        ++recoveryStepChecks; if(supported) *supported=target; return recoveryStepClear;
    }
    bool LandingContinuationClear(const idVec3&,const idVec3& next) {
        return continuationClear && (!directionalContinuation || next.x<=0);
    }
    int TransportAt(const idBounds&) const { return transport; }
    std::vector<std::pair<int,float>> volumesAhead;
    mutable int transportMoves=0;
    idVec3 volumeCentre{100,10,0};
    bool TransportCentre(int,idVec3& centre) const { centre=volumeCentre; return true; }
    int TransportOnMove(const idVec3&,const idVec3& to,int ignore) const {
        ++transportMoves;
        for(auto& v:volumesAhead) if(v.first!=ignore && to.x>=v.second) return v.first;
        return -1;
    }
    bool NaturalPadLanding(int entity,idVec3& out) const {
        if(entity!=transport || !naturalPad) return false; out=padLanding; return true;
    }
    std::vector<float> blocked,unreachable,hazardous;
    struct Node { int area=1; idVec3 origin; };
    std::vector<idVec3> snapped{ idVec3() };
    bool FindPath(idVec3,idVec3 goal,rvNavPath& path,bool=false,float limit=idMath::INFINITY) {
        lastImpactLimit=limit;
        ++searches; fieldLive=false; path.Clear();
        if(std::find(blocked.begin(),blocked.end(),goal.x)!=blocked.end()) return false;
        if(std::find(hazardous.begin(),hazardous.end(),goal.x)!=hazardous.end() && landingImpact>limit) return false;
        path.Append(goal); return true;
    }
    int FindNearestNode(idVec3 origin,float,bool) { ++probes; return origin.z<0 ? -1 : 0; }
    // Below -1000 is the void's kill volume.
    bool IsHazardous(idVec3 floor,bool) const { return floor.z < -1000.0f; }
    int SnapNode(idVec3 point) { ++probes; if(point.z<0) return -1; snapped.push_back(point); return int(snapped.size())-1; }
    int NodeForEntity(const idEntity* e) { ++snaps; return SnapNode(e->physics.origin); }
    Node GetNode(int i) const { Node n; n.origin=snapped[i]; return n; }
    int BuildCostField(idVec3 origin,float,const idList<int>* targets=nullptr) {
        ++fields; lastTargets=targets ? targets->Num() : -1;
        if(origin.z<0) { fieldLive=false; return -1; }
        snapped[0]=origin; fieldLive=true; return 0;
    }
    // The ranked form prices everything here; early termination is the
    // navmesh's business and the decisions must be the same either way.
    int BuildCostField(idVec3 origin,float maxCost,const navFieldTarget_t*,int numTargets,int) {
        ++fields; lastTargets=numTargets;
        if(origin.z<0) { fieldLive=false; return -1; }
        snapped[0]=origin; fieldLive=true; (void)maxCost; return 0;
    }
    float CostFieldDistance(int node) const {
        if(!fieldLive || node<0) return idMath::INFINITY;
        const idVec3& p=snapped[node];
        if(std::find(unreachable.begin(),unreachable.end(),p.x)!=unreachable.end()) return idMath::INFINITY;
        return (p-snapped[0]).Length();
    }
    bool RandomReachablePoint(idVec3,idVec3& target,rvNavPath* path,float limit=idMath::INFINITY) {
        lastImpactLimit=limit;
        if(!roam) return false; target={800,0,0}; path->Append(target); return true;
    }
} navMesh;
struct Command { int buttons=0; };
struct SpeedCVar { float GetFloat() const { return 320; } } pm_speed;
struct usercmd_t { int upmove=0,forwardmove=0,rightmove=0,buttons=0; int angles[3]={}; };
struct rvGameState { bool IsType(int) const { return false; } };
struct rvFreezeTagGameState : rvGameState {
    static int GetClassType() { return 1; }
    bool CanReachToThaw(idPlayer*,idPlayer*,int) { return true; }
};
struct trace_t { float fraction=1; idVec3 endpos; struct { idVec3 normal{0,0,1}; } c; };
const int MASK_PLAYERSOLID=1;
idVec3 lastEdgeProbe;
struct idGameLocal {
    int time=1000,numClients=0,framenum=0;
    int GetMSec() const { return 16; }
    Args serverInfo;
    struct { rvGameState* GetGameState() { return nullptr; } } mpGame;
    idVec3 GetCurrentGravity(const idPlayer*) const { return {0,0,-800}; }
    float edgeFloor=0;
    void TraceBounds(idPlayer*,trace_t& trace,const idVec3& start,const idVec3&,const idBounds&,int,idPlayer*) {
        lastEdgeProbe=start-idVec3(0,0,8);
        trace.fraction=start.x>0 ? 1.0f : .5f;
        trace.endpos={start.x,start.y,edgeFloor};
    }
    idEntity* entities[64]={};
    idEntity* world=nullptr;
    std::map<std::string,idDict> damageDefs;
    const idDict* FindEntityDefDict(const char* name,bool) const {
        auto found=damageDefs.find(name); return found==damageDefs.end() ? nullptr : &found->second;
    }
    idPlayer* GetClientByNum(int i) { return entities[i] && entities[i]->IsType(idPlayer::GetClassType()) ? static_cast<idPlayer*>(entities[i]) : nullptr; }
    Command commands[64];
    Command* usercmds=commands;
    Link spawnedEntities;
    bool padContact=true;
    int contactTests=0;
    bool ContentsModel(const idEntity*,const idVec3&,const idClipModel*,const idMat3&,int,
                       idCollisionModel*,const idVec3&,const idMat3&) { ++contactTests; return padContact; }
    int localClientNum=-1,demoState=DEMO_NONE,relayed=0;
    bool isRepeater=false;
    idMsgQueue unreliableMessages[MAX_CLIENTS+1];
    struct PVSHandle { int i=-1; } clientsPVS[MAX_CLIENTS];
    struct PVS {
        int queries=0;
        bool InCurrentPVS(PVSHandle,const int*,int) { ++queries; return true; }
    } pvs;
    void QueueUnreliableMessage(int,const idBitMsg&);
    void SendUnreliableMessage(const idBitMsg&,int);
    void SendUnreliableMessagePVS(const idBitMsg&,const idEntity*,int=-1,int=-1);
    void RepeaterUnreliableMessage(const idBitMsg&,int,const idBitMsg*) { ++relayed; }
    void RepeaterUnreliableMessagePVS(const idBitMsg&,const int*,int,const idBitMsg*) { ++relayed; }
    struct Random {
        float RandomFloat() { return 0.5f; }
        float CRandomFloat() { return 0.5f; }
    } random;
    template<class... T> void Printf(const char*,T...) {}
} gameLocal;
struct Debug { int GetInteger() const { return 0; } } bot_debug;
struct botObjective_t {
    int kind=BOTOBJ_NONE;
    idEntityPtr<idEntity> entity;
    idVec3 origin;
    float priority=0;
    bool holdPosition=false;
} fixtureObjective;
bool BotFindObjective(idPlayer*,botObjective_t& out) { out=fixtureObjective; return out.kind!=BOTOBJ_NONE; }
idVec3 BotObjectiveRouteOrigin(idPlayer*,const botObjective_t& o) { return o.origin; }
idStr BotReadableName(const Args&) { return "item"; }
const char* BotDisplayName(const idPlayer*) { return "player"; }
float BotRunSpeed() { return 320.0f; }
struct rvBot;
struct rvBotManager {
    struct botNoise_t { idVec3 origin; float range=0; int time=0; };
    bool bots[MAX_CLIENTS]={};
    botNoise_t noises[MAX_CLIENTS];
    idList<idItem*> items;
    bool IsBot(int i) const { return i>=0 && i<MAX_CLIENTS && bots[i]; }
    int GoalClaimCount(const rvBot*,idEntity* e) { return static_cast<idItem*>(e)->claims; }
    const idList<idItem*>& GetItems() const { return items; }
    const botNoise_t& GetNoise(int i) const { return noises[i]; }
    int KnownItemRespawn(const idItem* item,int) const { return item->knownRespawn; }
    int ItemPickupTime(const idItem* item) const { return item->pickupTime; }
    int decisions=0;
    bool ClaimDecision() { ++decisions; return true; }
    const rvBot* approachBots[MAX_CLIENTS]={};
    bool PadApproachOf(int i,int& pad,int& since,idVec3& launch,idVec3& landing) const;
} botManager;
struct botItemCandidate_t { idItem* item; float utility,score; };
struct botItemOption_t { idItem* item; float utility; int pendingMsec; int node; };
struct rvBot {
    bool probeAirActive=false;
    idVec3 probeAirDirection;
    void ProbeAirControl(idPlayer*,usercmd_t&);
    void OnKnockback(const idVec3&);
    void RefreshKnockbackMove(usercmd_t&);
    idPlayer* GetPlayer() const { return gameLocal.GetClientByNum(clientNum); }
    idStr name="bot";
    int clientNum=0,pathCorner=0,pathTime=0,goalType=BOTGOAL_ROAM,goalGiveUpTime=0;
    int goalCommitUntil=0,goalProgressTime=1000,objectiveKind=BOTOBJ_NONE,repathFailures=0,combatSteeringTime=0;
    int enemyPathTime=0,nextGoalSelectTime=0,holdUntil=0,noRouteSince=0;
    int traversalCorner=-1,stuckPathCorner=0,goalSelectedTime=0;
    int padHoldSince=0,stuckTime=0,stuckChecks=0,unstickUntil=0,traversalStartTime=0;
    int padApproachEntity=-1,padApproachSince=0,padApproachTime=0,airRecoverReported=0,airRecoverLanded=0;
    bool airRecoverSliding=false;
    bool airRecovering=false;
    int knockbackTime=0,airborneSince=0,airRecoverPlanTime=0,knockbackFrameTime=-1;
    idVec3 knockbackFrameKick;
    bool airborneVoluntary=false;
    idVec3 airRecoverWish;
    int LiquidAtFeet() const { return 0; }
    idVec3 padApproachLaunch,padApproachLanding;
    float goalUtility=0,goalBestDistance=idMath::INFINITY,stuckCornerDistance=idMath::INFINITY;
    bool objectiveHoldPosition=false,traversalEntered=false,goalItemPending=false;
    bool traversalStarted=false;
    int landingCheckPathTime=-1,landingCheckCorner=-1;
    float landingCheckLimit=idMath::INFINITY;
    bool dodgeHopActive=false,dodgeHopAirborne=false;
    int dodgeHopStartTime=0,dodgeHopCheckTime=0;
    idVec3 dodgeHopDirection;
    float dodgeHopWishSpeed=0;
    int recoverSearchTime=0,recoverWanderUntil=0;
    bool recoverValid=false;
    idVec3 recoverTarget;
    float recoverWanderYaw=0;
    idAngles aimAngles;
    bool outmatched=false,selfStrong=false;
    float advantage=1.0f,weaponPower=1.0f;
    idVec3 goalOrigin,traversalStartOrigin;
    idEntityPtr<idEntity> goalEntity;
    rvNavPath path;
    idEntityPtr<idPlayer> enemy,lastAttacker;
    int lastAttackerTime=0,enemyLastSeenTime=0,enemyLastHeardTime=0,nextHearingTime=0;
    int nextMistakeTime=0,onTargetTime=0;
    bool targetPickBest=true,aimBeliefValid=false,aimTrackValid=false;
    idVec3 enemyVisiblePoint,enemyLastSeenOrigin,enemyLastSeenVelocity;
    struct Traits {
        float itemFocus=0.5f,retreatHealth=0.3f,pursuit=0.5f,aggression=0.5f,patience=0.5f;
        float sightRange=3000,fov=180,targetStickiness=0.5f,reacquireMsec=600,targetSelection=1;
        float hearing=1.0f;
    } traits;
    int denied=0,abandoned=0;
    std::vector<idEntity*> avoided;
    void ResetTraversal() { traversalCorner=-1; traversalEntered=false; traversalStarted=false; landingCheckPathTime=-1; landingCheckCorner=-1; }
    void ApplyMove(const idVec3&,usercmd_t&) const;
    void PressJump(usercmd_t&) const;
    void TryDodgeJump(idPlayer*,const idVec3&,usercmd_t&);
    bool HoldDodgeJump(idPlayer*,usercmd_t&);
    void ResetDodgeJump();
    bool RecoverToNavMesh(idPlayer*,usercmd_t&);
    idVec3 AvoidLiquidHazard(const idVec3& direction) const { return direction; }
    bool AtObjectiveHoldPosition(idPlayer*) const;
    bool WantsHealth(idPlayer* p) const { return p->health < 30; }
    bool Withdrawing(idPlayer* p) const { return outmatched || WantsHealth(p); }
    float CombatAdvantage(idPlayer*,idPlayer*) const { return advantage; }
    float BestWeaponPower(idPlayer*) const { return weaponPower; }
    bool IsAvoided(const idEntity* e) const { return std::find(avoided.begin(),avoided.end(),e)!=avoided.end(); }
    void Avoid(idEntity* e,int) { if(e) { avoided.push_back(e); ++abandoned; } }
    void QueueChat(int,const char*,const char*,const char*) { ++denied; }
    idVec3 EnemyPursuitOrigin() const { return enemyLastSeenOrigin; }
    float ItemUtility(idPlayer*,idItem* item) const { return item->utility; }
    bool IsEnemy(idPlayer* self,idPlayer* other) const { return other && other!=self && other->health>0; }
    bool CanSee(idPlayer*,idEntity* e,idVec3* point) const {
        auto* p=static_cast<idPlayer*>(e); ++p->visibilityCalls;
        if(!p->visible) return false; *point=p->physics.origin; return true;
    }
    float TargetScore(idPlayer* p,float) const { return p->targetScore; }
    void AcquireEnemy(idPlayer*,idPlayer* p,bool) { enemy=p; }
    void RollMistake() {}
    bool PickRoamGoal(idPlayer*,idVec3& roam,rvNavPath& roamPath) { return navMesh.RandomReachablePoint({},roam,&roamPath); }
    void UpdateEnemy(idPlayer*);
    void UpdateHearing(idPlayer*);
    int EnemyLastKnownTime() const;
    int ItemPendingMsec(const idItem*) const;
    bool Repath(idPlayer*,const idVec3&,bool preserveProgress=false);
    float PathDistanceRemaining(const idVec3&) const;
    void AbandonGoal(int);
    idEntity* PickItemGoal(idPlayer*,rvNavPath&,float&);
    void UpdateGoal(idPlayer*);
    bool HoldForPad(idPlayer*,idVec3&);
    bool AlignJumpDropApproach(idPlayer*,usercmd_t&);
    bool HandleJumpTakeoff(idPlayer*,usercmd_t&);
    bool GuardLandingContinuation(idPlayer*,usercmd_t&);
    void AdoptPadLaunch(idPlayer*);
    bool GuardLandingHealth(idPlayer*,usercmd_t&);
    bool RecoverInAir(idPlayer*,usercmd_t&);
};
struct botCombatProjectileImpact_t {
    bool collided=false,terminating=false,hitsSelf=false;
    float time=0;
    idVec3 point;
    idEntity* hit=nullptr;
} fixtureImpact;
int impactSweeps=0;
bool BotCombatValidClientPlayer(idPlayer* p) { return p!=nullptr; }
bool BotCombatProjectileCanDamage(idProjectile*) { return true; }
bool BotCombatHostileProjectileOwner(idPlayer* self,idPlayer* owner) { return self!=owner; }
float BotCombatProjectileSplashRadius(idProjectile* p) { return p->splash; }
// The fixture has no actor class; a hit here is always a player, so it passes through as production does.
idEntity* BotCombatSplashIgnore(idEntity* hit,idEntity* fallback) { return hit ? hit : fallback; }
bool BotCombatProjectileLingers(const idProjectile* p) {
    return p->spawnArgs.GetBool("detonate_on_fuse","0") || p->spawnArgs.GetString("def_emit_damage","")[0] ||
           p->spawnArgs.GetString("def_residual_damage","")[0];
}
botCombatProjectileImpact_t BotCombatFindProjectileImpact(idProjectile*,idPlayer*,float,int) { ++impactSweeps; return fixtureImpact; }
float BotCombatProjectileDistanceAtTime(const idBounds& bounds,idVec3 sv,idVec3 p,idVec3 v,idVec3 g,float t,idVec3* point) {
    *point=p+v*t+g*(0.5f*t*t); return (bounds+sv*t).ShortestDistance(*point);
}
bool BotCombatSplashThreatensPlayer(idPlayer* p,idEntity*,idVec3 impact,float radius,float t) {
    return radius>0 && (p->physics.GetAbsBounds()+p->physics.velocity*t).ShortestDistance(impact)<=radius;
}
// A deterministic cliff at x=0. Actual hull/support geometry is exercised by
// MpBotJumpTrajectoriesFixture; here we verify every recovery command asks the
// production edge guard, including the nearest-node and wander branches.
// The floor/trace half of a step's safety is MpBotJumpTrajectoriesFixture's;
// here a sidestep is safe unless the test says otherwise.
bool moveDirectionSafe=true;
// Whether a free fall lands on nothing is BotDeadlyDropAt's geometry; here it is given.
bool airLandingUnsafe=false;
bool BotAirLandingUnsafe(idPlayer*,idVec3& touchdown) { touchdown={}; return airLandingUnsafe; }
// The fall simulation is the navmesh's; here a chosen input may be imposed.
bool airChoiceSet=false; idVec3 airChoice; int airChoices=0;
bool BotChooseAirInput(idPlayer* self,idVec3& wish) {
    ++airChoices; if(airChoiceSet) { wish=airChoice; return true; }
    wish=idVec3(-self->physics.velocity.x,-self->physics.velocity.y,0); wish.Normalize(); return true;
}
bool BotMoveDirectionSafe(idPlayer*,const idVec3&) { return moveDirectionSafe; }
struct botCombatShotModel_t {
    float splashRadius=128,selfKnockback=80,power=1,attackerScale=0.5f,selfPushScale=2;
};
struct { float GetFloat() const { return 1000; } } g_knockback;
bool selfBlastExposed=true;
bool BotCombatPredictedBoundsExposed(idPlayer*,idEntity*,const idVec3&,float) { return selfBlastExposed; }
// PRODUCTION_FUNCTIONS
// The manager publishes only an approach confirmed within the stale window.
bool rvBotManager::PadApproachOf(int i,int& pad,int& since,idVec3& launch,idVec3& landing) const {
    const rvBot* b=(i>=0 && i<MAX_CLIENTS && bots[i]) ? approachBots[i] : nullptr;
    if(!b || b->padApproachEntity<0 || gameLocal.time-b->padApproachTime>BOT_PAD_APPROACH_STALE_MSEC) return false;
    pad=b->padApproachEntity; since=b->padApproachSince; launch=b->padApproachLaunch; landing=b->padApproachLanding;
    return true;
}

void Reset() {
    gameLocal=idGameLocal(); gameLocal.usercmds=gameLocal.commands;
    botManager=rvBotManager(); commonObject.warnings=0;
    navMesh=Nav(); fixtureObjective={}; fixtureImpact={}; impactSweeps=0; moveDirectionSafe=true; airLandingUnsafe=false;
    airChoiceSet=false; airChoices=0;
}
void GoalRoute(rvBot& b,idEntity* e,int type) {
    b.goalType=type; b.goalEntity=e; b.goalOrigin=e->physics.origin;
    b.path.Append({50,10,0}); b.path.Append(b.goalOrigin);
    b.goalBestDistance=b.path.LengthFrom({}); b.goalProgressTime=100;
    b.goalGiveUpTime=13000; b.goalUtility=20; b.pathTime=777;
}
void TestGoals() {
    Reset(); idPlayer self; idItem old; old.physics.origin={100,0,0};
    idEntity flag; flag.physics.origin={500,0,0};
    rvBot bot; GoalRoute(bot,&old,BOTGOAL_ITEM);
    fixtureObjective={BOTOBJ_CAPTURE,{},flag.physics.origin,100,false}; fixtureObjective.entity=&flag;
    navMesh.blocked={500};
    bot.UpdateGoal(&self);
    CHECK(bot.goalEntity.GetEntity()==&old && bot.goalType==BOTGOAL_ITEM);
    CHECK(bot.path.Num()==2 && bot.path[0].origin.y==10 && bot.pathTime==777);
    CHECK(bot.goalOrigin.x==100 && bot.goalProgressTime==100 && bot.goalGiveUpTime==13000);
    CHECK(bot.repathFailures==0);
    // A later reachable objective replaces the complete commitment together.
    gameLocal.time+=300; navMesh.blocked.clear(); bot.UpdateGoal(&self);
    CHECK(bot.goalEntity.GetEntity()==&flag && bot.goalType==BOTGOAL_OBJECTIVE);
    CHECK(bot.path.Num()==1 && bot.path[0].origin.x==500 && bot.goalOrigin.x==500);
    CHECK(bot.goalProgressTime==gameLocal.time && bot.goalGiveUpTime>gameLocal.time);
    CHECK(bot.goalSelectedTime==gameLocal.time);
    // Failed refreshes preserve the existing path as well as progress.
    int progress=bot.goalProgressTime; navMesh.blocked={600};
    CHECK(!bot.Repath(&self,{600,0,0},true));
    CHECK(bot.path[0].origin.x==500 && bot.goalProgressTime==progress);
    // Repeated rebuilds of the SAME unreachable-in-practice goal must time out.
    bot.goalBestDistance=500; bot.goalProgressTime=100; bot.goalGiveUpTime=6000;
    for(int t=1600;t<=7000;t+=300) {
        gameLocal.time=t; bot.path.Clear(); bot.UpdateGoal(&self);
    }
    CHECK(bot.IsAvoided(&flag) && bot.abandoned==1);
    // The same stall spent fighting the whole time is not a failure to get
    // there: combat footwork shortens no route.
    rvBot fighter; GoalRoute(fighter,&flag,BOTGOAL_OBJECTIVE); fighter.objectiveKind=BOTOBJ_CAPTURE;
    fighter.goalBestDistance=500; fighter.goalProgressTime=100; fighter.goalGiveUpTime=6000;
    for(int t=1600;t<=7000;t+=300) {
        gameLocal.time=t; fighter.combatSteeringTime=t; fighter.path.Clear(); fighter.UpdateGoal(&self);
    }
    CHECK(!fighter.IsAvoided(&flag) && fighter.abandoned==0);
    // A moving carrier can make every new route longer than the best distance
    // on an earlier route. Count movement on each route without crediting the
    // refresh itself; a stationary pursuer still has to abandon the target.
    for (int advance : {32, 8, 0}) {
        Reset(); idPlayer pursuer, carrier; carrier.physics.origin={500,0,0};
        rvBot chase; GoalRoute(chase,&carrier,BOTGOAL_OBJECTIVE);
        chase.objectiveKind=BOTOBJ_INTERCEPT;
        chase.goalBestDistance=100; chase.goalProgressTime=100; chase.goalGiveUpTime=6000;
        fixtureObjective={BOTOBJ_INTERCEPT,{},carrier.physics.origin,100,false};
        fixtureObjective.entity=&carrier;
        for (int t=1500;t<=7500 && !chase.abandoned;t+=300) {
            gameLocal.time=t;
            carrier.physics.origin.x+=80;
            fixtureObjective.origin=carrier.physics.origin;
            const int progressBefore=chase.goalProgressTime, deadlineBefore=chase.goalGiveUpTime;
            CHECK(chase.Repath(&pursuer,carrier.physics.origin,true));
            CHECK(chase.goalProgressTime==progressBefore && chase.goalGiveUpTime==deadlineBefore);
            pursuer.physics.origin.x+=advance;
            chase.nextGoalSelectTime=t+100;
            gameLocal.time=t+16;
            chase.UpdateGoal(&pursuer);
            if (advance==32) CHECK(chase.goalProgressTime==gameLocal.time);
            if (advance==8 && t>=2400) CHECK(gameLocal.time-chase.goalProgressTime<=900);
        }
        CHECK(advance ? !chase.IsAvoided(&carrier) : chase.IsAvoided(&carrier));
    }
    // Invalid pickups cannot leave an old route live when all searches fail.
    Reset(); rvBot invalid; GoalRoute(invalid,&old,BOTGOAL_ITEM); old.hidden=true;
    invalid.UpdateGoal(&self); CHECK(invalid.path.IsEmpty() && invalid.repathFailures==1);
    old.hidden=false;
    // A full inventory releases the old pickup's commitment immediately.
    Reset(); rvBot stocked; GoalRoute(stocked,&old,BOTGOAL_ITEM);
    stocked.goalUtility=100; stocked.goalCommitUntil=5000; old.utility=0;
    idPlayer foe; foe.physics.origin={400,0,0}; stocked.enemy=&foe;
    stocked.enemyLastSeenOrigin=foe.physics.origin; stocked.enemyLastSeenTime=gameLocal.time;
    stocked.UpdateGoal(&self);
    CHECK(stocked.goalType==BOTGOAL_ENEMY && stocked.goalEntity.GetEntity()==&foe);
    // The same foe falling through a kill volume is dying where no route
    // reaches: it is not chased.
    Reset(); rvBot doomed; GoalRoute(doomed,&old,BOTGOAL_ITEM);
    doomed.goalUtility=100; doomed.goalCommitUntil=5000;
    idPlayer faller; faller.physics.origin={400,0,-2000}; doomed.enemy=&faller;
    doomed.enemyLastSeenOrigin=faller.physics.origin; doomed.enemyLastSeenTime=gameLocal.time;
    doomed.UpdateGoal(&self);
    CHECK(doomed.goalType!=BOTGOAL_ENEMY);
    old.utility=1;
    // A fresh wander must replace the previous goal's distance baseline.
    Reset(); navMesh.roam=true; rvBot wander; wander.goalBestDistance=10;
    wander.UpdateGoal(&self); CHECK(wander.goalBestDistance==800);
    // Taking one's own pickup is not item denial. Another player's pickup is,
    // but only when it happened after this bot set out for the item: a timed
    // run toward an item that was already gone is not a denial.
    for(int taker : {0,1,999}) {
        Reset(); gameLocal.numClients=2; old.pickedUp=true; old.pickedUpByClientNum=taker;
        old.pickupTime=gameLocal.time;
        rvBot picker; GoalRoute(picker,&old,BOTGOAL_ITEM); picker.UpdateGoal(&self);
        CHECK(picker.denied==(taker==0?0:1));
    }
    Reset(); gameLocal.numClients=2; old.pickedUp=true; old.pickedUpByClientNum=1; old.pickupTime=500;
    rvBot late; GoalRoute(late,&old,BOTGOAL_ITEM); late.goalSelectedTime=900; late.UpdateGoal(&self);
    CHECK(late.denied==0);
    old.pickedUp=false; old.pickupTime=-1;
    // Outmatched: a losing trade turns a chase into a run for armour.
    Reset(); idItem armor; armor.physics.origin={300,0,0}; armor.spawnArgs.Set("inv_armor","50");
    armor.utility=2; botManager.items.push_back(&armor);
    rvBot weak; weak.enemy=&foe; weak.enemyLastSeenOrigin=foe.physics.origin;
    weak.enemyLastSeenTime=gameLocal.time; weak.advantage=0.2f;
    weak.UpdateGoal(&self);
    CHECK(weak.outmatched && weak.goalType==BOTGOAL_ITEM && weak.goalEntity.GetEntity()==&armor);
    Reset(); botManager.items.push_back(&armor);
    rvBot strong; strong.enemy=&foe; strong.enemyLastSeenOrigin=foe.physics.origin;
    strong.enemyLastSeenTime=gameLocal.time; strong.advantage=1.5f;
    strong.UpdateGoal(&self);
    CHECK(!strong.outmatched && strong.goalType==BOTGOAL_ENEMY);
    // Under-armed: a real weapon outranks a standing defence role.
    Reset(); idItem gun; gun.physics.origin={900,0,0}; gun.spawnArgs.Set("inv_weapon","weapon_rocketlauncher");
    gun.utility=5; botManager.items.push_back(&gun);
    idEntity base; base.physics.origin={0,0,0};
    fixtureObjective={BOTOBJ_DEFEND,{},base.physics.origin,54,true}; fixtureObjective.entity=&base;
    rvBot spawned; spawned.weaponPower=0.5f; spawned.UpdateGoal(&self);
    CHECK(spawned.goalType==BOTGOAL_ITEM && spawned.goalEntity.GetEntity()==&gun);
    rvBot armed; armed.weaponPower=1.0f; armed.UpdateGoal(&self);
    CHECK(armed.goalType==BOTGOAL_OBJECTIVE);
    // A pickup beside the defended base is part of holding it.
    Reset(); idItem nearArmor; nearArmor.physics.origin={200,0,0}; nearArmor.utility=1.5f;
    botManager.items.push_back(&nearArmor);
    fixtureObjective={BOTOBJ_DEFEND,{},base.physics.origin,54,true}; fixtureObjective.entity=&base;
    rvBot defender; defender.goalType=BOTGOAL_OBJECTIVE; defender.objectiveKind=BOTOBJ_DEFEND;
    defender.goalEntity=&base; defender.goalUtility=54; defender.goalCommitUntil=5000;
    defender.UpdateGoal(&self);
    CHECK(defender.goalType==BOTGOAL_ITEM && defender.goalEntity.GetEntity()==&nearArmor);
    std::puts("Goals: replacement, failed refresh, stall recovery, combat is not a stall, wander baseline, denial timing, fight-or-flight, arming and duty pickups: PASS");
}

void TestPerception() {
    // Exhaust all orderings with an occluded high-priority target, visible
    // alternatives and a current target. The optimal and nearest choices must
    // agree with exhaustive visibility even after pruning inferior candidates.
    idPlayer self;
    std::vector<int> order={0,1,2,3};
    int checked=0;
    do {
        for(bool best : {false,true}) for(int current=-1;current<4;++current) {
            Reset(); idPlayer players[4];
            const float distances[]={100,200,300,400}, scores[]={10,1,4,40};
            for(int i=0;i<4;++i) {
                players[i].physics.origin={distances[i],0,0}; players[i].targetScore=scores[i];
                players[i].visible=i!=1; players[i].entityNumber=i;
                gameLocal.entities[i]=&players[order[i]];
            }
            gameLocal.numClients=4; rvBot b; b.targetPickBest=best;
            if(current>=0) { b.enemy=&players[current]; b.enemyLastSeenTime=gameLocal.time; }
            idPlayer* expected=best ? &players[2] : &players[0];
            if(current>=0 && players[current].visible && expected!=&players[current] &&
               expected->targetScore>players[current].targetScore*0.75f) expected=&players[current];
            b.UpdateEnemy(&self);
            CHECK(b.enemy.GetEntity()==expected);
            CHECK(b.enemyVisiblePoint.x==expected->physics.origin.x);
            if(current>=0 && players[current].visible) CHECK(players[current].visibilityCalls==1);
            ++checked;
        }
    } while(std::next_permutation(order.begin(),order.end()));
    Reset(); idPlayer nearest,farther; nearest.physics.origin={100,0,0}; farther.physics.origin={500,0,0};
    nearest.targetScore=1; farther.targetScore=10; gameLocal.entities[0]=&nearest;
    gameLocal.entities[1]=&farther; gameLocal.numClients=2; rvBot pruned; pruned.UpdateEnemy(&self);
    CHECK(nearest.visibilityCalls==1 && farther.visibilityCalls==0);
    std::printf("Perception: %d exhaustive-order comparisons; inferior target uses zero visibility queries: PASS\n",checked);
}

void TestHearing() {
    // Nothing in view: the nearest audible enemy becomes a contact located near
    // its noise, with no sighting recorded - a sound never grants a shot.
    Reset(); idPlayer self; idPlayer loud,far; loud.visible=far.visible=false;
    loud.physics.origin={600,0,0}; far.physics.origin={5000,0,0};
    gameLocal.entities[0]=&loud; gameLocal.entities[1]=&far; gameLocal.numClients=2;
    botManager.noises[0]={loud.physics.origin,1700,gameLocal.time};
    botManager.noises[1]={far.physics.origin,1700,gameLocal.time};
    rvBot b; b.UpdateEnemy(&self);
    CHECK(b.enemy.GetEntity()==&loud);
    CHECK(b.enemyLastHeardTime==gameLocal.time && b.enemyLastSeenTime==0);
    CHECK((b.enemyLastSeenOrigin-loud.physics.origin).Length() <= 600*0.12f*1.5f+1);
    // The contact is remembered from the sound alone, then forgotten.
    gameLocal.time+=1000; botManager.noises[0].time=0; b.nextHearingTime=0; b.UpdateEnemy(&self);
    CHECK(b.enemy.GetEntity()==&loud);
    gameLocal.time+=10000; b.nextHearingTime=0; b.UpdateEnemy(&self);
    CHECK(b.enemy.GetEntity()==nullptr);
    // A deaf bot and a stale noise hear nothing.
    Reset(); gameLocal.entities[0]=&loud; gameLocal.numClients=1;
    botManager.noises[0]={loud.physics.origin,1700,gameLocal.time};
    rvBot deaf; deaf.traits.hearing=0; deaf.UpdateEnemy(&self); CHECK(deaf.enemy.GetEntity()==nullptr);
    botManager.noises[0].time=gameLocal.time-5000;
    rvBot late; late.UpdateEnemy(&self); CHECK(late.enemy.GetEntity()==nullptr);
    // Footsteps carry a short way only.
    botManager.noises[0]={loud.physics.origin,420,gameLocal.time};
    rvBot quiet; quiet.UpdateEnemy(&self); CHECK(quiet.enemy.GetEntity()==nullptr);
    loud.physics.origin={300,0,0}; botManager.noises[0].origin=loud.physics.origin;
    rvBot close; close.UpdateEnemy(&self); CHECK(close.enemy.GetEntity()==&loud);
    std::puts("Hearing: nearest audible contact, sound-only memory, deafness, staleness and short footsteps: PASS");
}

void TestItems() {
    Reset(); idPlayer self; idItem items[80];
    for(int i=0;i<80;++i) {
        items[i].physics.origin={float(100+i*5),0,0}; items[i].utility=1;
        botManager.items.push_back(&items[i]);
    }
    rvBot b; rvNavPath path; float utility;
    CHECK(b.PickItemGoal(&self,path,utility)==&items[0]);
    // One cost field, told exactly which nodes to price, serves every pickup;
    // only the winner pays for a route.
    CHECK(navMesh.fields==1 && navMesh.searches==1 && navMesh.snaps==80 && navMesh.lastTargets==80);
    // An unreachable high-utility item must not displace a valid candidate.
    items[79].utility=100; navMesh.unreachable={items[79].physics.origin.x};
    CHECK(b.PickItemGoal(&self,path,utility)==&items[0]);
    // Reorder the list: a later higher-ranked reachable candidate still wins.
    items[78].utility=10;
    CHECK(b.PickItemGoal(&self,path,utility)==&items[78]);
    // A mover blocking the winner's route falls through to the next best.
    navMesh.blocked={items[78].physics.origin.x}; navMesh.searches=0;
    CHECK(b.PickItemGoal(&self,path,utility)==&items[0] && navMesh.searches==2);
    navMesh.blocked.clear(); items[78].utility=1; items[79].utility=1;
    // A taken item is a candidate only when this bot knows its timer, and only
    // when it will be back about when the bot arrives.
    items[5].pickedUp=true; items[5].utility=5;
    CHECK(b.PickItemGoal(&self,path,utility)==&items[0]);
    items[5].knownRespawn=gameLocal.time+1500;
    CHECK(b.PickItemGoal(&self,path,utility)==&items[5]);
    items[5].knownRespawn=gameLocal.time+60000;
    CHECK(b.PickItemGoal(&self,path,utility)==&items[0]);
    // Minor pickups are never worth waiting for.
    items[5].utility=1; items[5].knownRespawn=gameLocal.time+500;
    CHECK(b.PickItemGoal(&self,path,utility)==&items[0]);
    // A bot that needs nothing searches nothing.
    for(auto& item : items) item.utility=0;
    const int fields=navMesh.fields, searches=navMesh.searches;
    CHECK(b.PickItemGoal(&self,path,utility)==nullptr);
    CHECK(navMesh.fields==fields && navMesh.searches==searches);
    std::puts("Items: one cost field for 80 pickups, blocked winners fall through, timed respawns: PASS");
}

void TestThreats() {
    Reset(); idPlayer self; idProjectile grenade;
    grenade.physics.origin={150,0,0}; grenade.splash=200; grenade.spawnArgs.Set("detonate_on_fuse","1");
    gameLocal.spawnedEntities.next=&grenade;
    idProjectile* threat=nullptr; float when=0; idVec3 where;
    CHECK(BotCombatFindIncomingProjectileThreat(&self,0.85f,52,threat,when,where));
    CHECK(threat==&grenade && when==0.25f);
    // Explosion reach extends beyond the projectile's maximum travel.
    grenade.spawnArgs.Set("detonate_on_fuse","0"); grenade.physics.origin={1000,0,0}; grenade.physics.velocity={-1000,0,0};
    fixtureImpact.collided=true; fixtureImpact.terminating=true; fixtureImpact.time=0.85f; fixtureImpact.point={150,0,0};
    CHECK(BotCombatFindIncomingProjectileThreat(&self,0.85f,52,threat,when,where));
    CHECK(threat==&grenade && when==0.85f);
    // A bouncing grenade's first landing inside its blast is already a threat.
    grenade.spawnArgs.Set("detonate_on_fuse","1"); grenade.physics.origin={450,0,0}; grenade.physics.velocity={-400,0,0};
    fixtureImpact.terminating=false; fixtureImpact.time=0.5f; fixtureImpact.point={150,0,0};
    CHECK(BotCombatFindIncomingProjectileThreat(&self,0.85f,52,threat,when,where));
    CHECK(threat==&grenade && when==0.5f);
    // Remote projectiles still never enter the expensive impact sweep.
    impactSweeps=0; grenade.physics.origin={3000,0,0}; grenade.physics.velocity={};
    grenade.spawnArgs.Set("detonate_on_fuse","0"); fixtureImpact={};
    CHECK(!BotCombatFindIncomingProjectileThreat(&self,0.85f,52,threat,when,where));
    CHECK(impactSweeps==0 && threat==nullptr && when==0);
    std::puts("Threat broad phase: stationary fuse, distant rocket splash, grenade bounce, cheap remote rejection: PASS");
}
void TestNetwork() {
    Reset(); idPlayer bot,observer,other; observer.spectating=true; observer.spectator=1;
    gameLocal.entities[1]=&bot; gameLocal.entities[2]=&observer; gameLocal.entities[3]=&other;
    gameLocal.numClients=4; botManager.bots[1]=true; gameLocal.demoState=DEMO_RECORDING;
    idBitMsg message;
    // Personal feedback for a bot still reaches its human spectator and demo.
    gameLocal.SendUnreliableMessage(message,1);
    CHECK(gameLocal.unreliableMessages[1].messages==0);
    CHECK(gameLocal.unreliableMessages[2].messages==1);
    CHECK(gameLocal.unreliableMessages[3].messages==0);
    CHECK(gameLocal.unreliableMessages[MAX_CLIENTS].messages==1);
    CHECK(gameLocal.relayed==1);
    // Broadcast reaches every remote human, even one not following anybody.
    gameLocal.SendUnreliableMessage(message,-1);
    CHECK(gameLocal.unreliableMessages[1].messages==0);
    CHECK(gameLocal.unreliableMessages[2].messages==2 && gameLocal.unreliableMessages[3].messages==1);
    // PVS queries themselves need not run for bots; recording stays active.
    gameLocal.clientsPVS[1].i=gameLocal.clientsPVS[2].i=gameLocal.clientsPVS[3].i=0;
    gameLocal.SendUnreliableMessagePVS(message,nullptr,0);
    CHECK(gameLocal.pvs.queries==2 && gameLocal.unreliableMessages[1].messages==0);
    CHECK(gameLocal.unreliableMessages[2].messages==3 && gameLocal.unreliableMessages[3].messages==2);
    CHECK(gameLocal.unreliableMessages[MAX_CLIENTS].messages==3 && gameLocal.relayed==3);
    // Direct queue callers cannot accumulate bot traffic either.
    for(int i=0;i<2000;++i) gameLocal.QueueUnreliableMessage(1,message);
    CHECK(gameLocal.unreliableMessages[1].GetTotalSize()==0 && commonObject.warnings==0);
    gameLocal.QueueUnreliableMessage(MAX_CLIENTS,message);
    CHECK(gameLocal.unreliableMessages[MAX_CLIENTS].messages==4);
    gameLocal.QueueUnreliableMessage(-1,message); gameLocal.QueueUnreliableMessage(MAX_CLIENTS+1,message);
    std::puts("Bot message queues: no accumulation or PVS work; spectator, broadcast, demo and repeater delivery retained: PASS");
}
void TestPadHold() {
    Reset(); idPlayer self,opposing; rvJumpPad pad;
    self.physics.origin={0,-80,0}; self.physics.velocity={0,320,0}; self.physics.radius=16;
    pad.physics.origin={0,0,0}; pad.physics.radius=32;
    opposing.physics.origin={0,800,128}; opposing.physics.velocity={0,-646,528}; opposing.pfl.onGround=false;
    gameLocal.entities[0]=&self; gameLocal.entities[1]=&opposing; gameLocal.entities[8]=&pad; gameLocal.numClients=2;
    rvBot bot; bot.path.Append({0,0,0}); bot.path.corners.push_back({{0,960,0},NAVTRAVEL_JUMPPAD,8});
    idVec3 move{0,1,0};
    CHECK(bot.HoldForPad(&self,move) && move.y<0 && bot.stuckTime==gameLocal.time);
    // Reserving the expanded trigger bounds is not a launch. The former
    // !traversalEntered guard released the wait at precisely this point.
    bot.traversalEntered=true; self.physics.origin={0,-55,0};
    CHECK(bot.HoldForPad(&self,move) && move.y<0);
    // A long blockage asks for a new route but never grants an unsafe launch.
    bot.pathTime=900; bot.nextGoalSelectTime=10000; gameLocal.time+=BOT_PAD_HOLD_MSEC+1;
    CHECK(bot.HoldForPad(&self,move) && bot.pathTime==0 && bot.nextGoalSelectTime==gameLocal.time);
    self.physics.velocity={0,3,0}; CHECK(bot.HoldForPad(&self,move) && move.LengthSqr()==0);
    // A launched bot owns its flight; waiting cannot brake it over the pit.
    bot.traversalStarted=true; move={0,1,0}; CHECK(!bot.HoldForPad(&self,move) && move.y==1);
    bot.traversalStarted=false;
    // A leader can rebound from the far pad into its follower (q4ctf5).
    // Reserve that finite flight too; slow crossings and other corridors
    // remain free, as do players who have actually landed.
    opposing.physics.velocity.y=646; CHECK(bot.HoldForPad(&self,move));
    opposing.physics.velocity.y=40; CHECK(!bot.HoldForPad(&self,move));
    opposing.physics.velocity.y=-646; opposing.instance=1; CHECK(!bot.HoldForPad(&self,move));
    opposing.instance=0; opposing.pfl.onGround=true; CHECK(!bot.HoldForPad(&self,move));
    opposing.pfl.onGround=false; opposing.physics.origin.x=160; CHECK(!bot.HoldForPad(&self,move));
    opposing.physics.origin={0,-160,128}; CHECK(!bot.HoldForPad(&self,move));
    CHECK(bot.padHoldSince==0);
    // Several walking corners can remain inside the pad's braking distance.
    // The flight corridor starts at the final walking corner, not the current
    // corner elsewhere along the approach.
    opposing.physics.origin={0,800,128}; self.physics.velocity={0,320,0};
    bot.path.Clear(); bot.path.Append({500,-80,0}); bot.path.Append({0,0,0});
    bot.path.Append({0,960,0},NAVTRAVEL_JUMPPAD,8);
    CHECK(bot.HoldForPad(&self,move) && move.y<0);
    bot.path.corners[1].travelType=NAVTRAVEL_DROP;
    CHECK(!bot.HoldForPad(&self,move));
    bot.path.corners[1].travelType=NAVTRAVEL_JUMP;
    CHECK(!bot.HoldForPad(&self,move));
    bot.path.corners[1].travelType=NAVTRAVEL_WALK;
    self.physics.origin={0,-200,0}; CHECK(!bot.HoldForPad(&self,move));
    self.physics.origin={0,-55,0}; bot.path.corners.resize(2);
    CHECK(!bot.HoldForPad(&self,move));
    bot.pathCorner=-1; CHECK(!bot.HoldForPad(&self,move));
    bot.pathCorner=2; CHECK(!bot.HoldForPad(&self,move));
    std::puts("Pad corridor: brake through reserved entry, replan while blocked, retain launched flight and clear traffic: PASS");
}
void TestPadReservation() {
    // q4ctf5's pads 3 and 4 throw players at each other over the void. Two
    // bots held at either end both saw the corridor clear on the frame the
    // last flyer landed and launched within 16 ms of each other.
    Reset(); idPlayer a,b; rvJumpPad north,south;
    a.physics.origin={0,-80,0}; a.physics.radius=16; a.entityNumber=0;
    b.physics.origin={0,1080,0}; b.physics.radius=16; b.entityNumber=1;
    south.physics.origin={0,0,0}; south.physics.radius=32;
    north.physics.origin={0,1000,0}; north.physics.radius=32;
    gameLocal.entities[0]=&a; gameLocal.entities[1]=&b; gameLocal.entities[8]=&south; gameLocal.entities[9]=&north;
    gameLocal.numClients=2; botManager.bots[0]=botManager.bots[1]=true;
    rvBot botA,botB; botA.clientNum=0; botB.clientNum=1;
    botManager.approachBots[0]=&botA; botManager.approachBots[1]=&botB;
    botA.path.Append({0,-40,0}); botA.path.corners.push_back({{0,1040,0},NAVTRAVEL_JUMPPAD,8});
    botB.path.Append({0,1040,0}); botB.path.corners.push_back({{0,-40,0},NAVTRAVEL_JUMPPAD,9});
    idVec3 moveA{0,1,0},moveB{0,-1,0};
    int bothGo=0,neither=0;
    for(int frame=0;frame<8;frame++,gameLocal.time+=16) {
        const bool holdA=botA.HoldForPad(&a,moveA), holdB=botB.HoldForPad(&b,moveB);
        if(frame>0) { bothGo+=(!holdA && !holdB); neither+=(holdA && holdB); }
        CHECK(botA.padApproachEntity==8 && botB.padApproachEntity==9);
    }
    // From the frame both approaches are published, exactly one goes: the
    // earlier, and on a tie the lower client number.
    CHECK(bothGo==0 && neither==0);
    CHECK(!botA.HoldForPad(&a,moveA) && botB.HoldForPad(&b,moveB));
    // Whoever came within reach first keeps the corridor.
    botA.padApproachSince=gameLocal.time; botB.padApproachSince=gameLocal.time-500;
    CHECK(botA.HoldForPad(&a,moveA) && !botB.HoldForPad(&b,moveB));
    // A slow climb hop has not launched yet. Its fresh approach still owns
    // the corridor even though the velocity-based flyer check cannot see it.
    b.pfl.onGround=false; b.physics.velocity={0,-40,180};
    CHECK(botA.HoldForPad(&a,moveA) && !botB.HoldForPad(&b,moveB));
    b.pfl.onGround=true; b.physics.velocity={};
    // An approach the owner stopped confirming lapses; it cannot hold the
    // corridor after its bot died or changed route.
    botB.padApproachTime=gameLocal.time-BOT_PAD_APPROACH_STALE_MSEC-1;
    CHECK(!botA.HoldForPad(&a,moveA));
    botB.padApproachTime=gameLocal.time;
    // q4ctf5's long pads land beyond each other's launch; their flights
    // still share one corridor.
    botB.padApproachLaunch={0,1040,0}; botB.padApproachLanding={0,-600,0};
    botA.padApproachSince=gameLocal.time; botB.padApproachSince=gameLocal.time-500;
    CHECK(botA.HoldForPad(&a,moveA));
    botB.padApproachLanding={0,-40,0};
    // A corridor elsewhere is not shared, nor is a human's unknowable intent.
    botB.padApproachLaunch={600,1040,0}; botB.padApproachLanding={600,-40,0};
    CHECK(!botA.HoldForPad(&a,moveA));
    botB.padApproachLaunch={0,1040,0}; botB.padApproachLanding={0,-40,0};
    botManager.bots[1]=false; CHECK(!botA.HoldForPad(&a,moveA)); botManager.bots[1]=true;
    // Approaching from outside braking range publishes without braking;
    // leaving that range or the route ending withdraws the approach.
    a.physics.origin={0,-200,0}; moveA={0,1,0};
    CHECK(!botA.HoldForPad(&a,moveA) && botA.padApproachEntity==8 && moveA.y==1);
    a.physics.origin={0,-500,0}; CHECK(!botA.HoldForPad(&a,moveA) && botA.padApproachEntity==-1);
    a.physics.origin={0,-80,0}; botA.path.Clear(); CHECK(!botA.HoldForPad(&a,moveA) && botA.padApproachEntity==-1);
    std::puts("Pad reservation: exactly one of two bots at opposing pads launches; first approach wins, lapsed and unshared approaches ignored: PASS");
}
void TestTransportGuard() {
    Reset(); idPlayer self; self.physics.velocity={320,0,0};
    rvNavPath route; route.Append({100,0,0}); route.Append({200,0,0}); route.Append({300,0,0},NAVTRAVEL_JUMPPAD,9);
    CHECK(BotPlannedTransport(route,0)==9 && BotPlannedTransport(route,2)==9 && BotPlannedTransport(route,3)==-1);
    route.corners[1].travelType=NAVTRAVEL_DROP; CHECK(BotPlannedTransport(route,0)==-1);
    route.corners[1].travelType=NAVTRAVEL_TELEPORT; route.corners[1].actionEntityNum=7;
    CHECK(BotPlannedTransport(route,0)==7);
    // A pad 40 units ahead: 0.15 s of running speed plus the steering stride reaches it.
    navMesh.volumesAhead={{9,40}};
    idVec3 move{1,0,0};
    CHECK(BotBrakeForTransport(&self,move,-1) && move.x<-0.99f);
    // The route's own pad is the one volume it may enter.
    move={1,0,0}; CHECK(!BotBrakeForTransport(&self,move,9) && move.x==1);
    // Nothing ahead, or nothing reached yet: no intervention.
    navMesh.volumesAhead={{9,90}}; move={1,0,0}; CHECK(!BotBrakeForTransport(&self,move,-1) && move.x==1);
    // Walking pace slides along the volume, square to the wish, instead of
    // stopping at it; a diagonal would still close on the trigger.
    navMesh.volumesAhead={{9,20}}; self.physics.velocity={50,0,0}; move={1,0,0};
    CHECK(BotBrakeForTransport(&self,move,-1) && std::fabs(move.x)<0.01f && move.y<-0.99f);
    // Only away from the volume's centre: the other side runs along the trigger.
    navMesh.volumeCentre={100,-10,0}; move={1,0,0};
    CHECK(BotBrakeForTransport(&self,move,-1) && std::fabs(move.x)<0.01f && move.y>0.99f);
    navMesh.volumeCentre={100,10,0};
    // With no safe way round, it stops pressing rather than walking in.
    moveDirectionSafe=false; move={1,0,0};
    CHECK(BotBrakeForTransport(&self,move,-1) && move.LengthSqr()==0);
    // Momentum already leaving the volume is never reversed back into it:
    // alternating that with the wish crept a bot onto func_jumppad_14.
    navMesh.volumesAhead={{9,5}}; self.physics.velocity={-50,0,0}; move={1,0,0};
    CHECK(BotBrakeForTransport(&self,move,-1) && move.LengthSqr()==0);
    moveDirectionSafe=true; move={1,0,0};
    CHECK(BotBrakeForTransport(&self,move,-1) && std::fabs(move.x)<0.01f && move.y<-0.99f);
    // Momentum alone, with no input, is still checked; standing still is not.
    moveDirectionSafe=true; navMesh.volumesAhead={{9,40}}; self.physics.velocity={320,0,0}; move={};
    CHECK(BotBrakeForTransport(&self,move,-1) && move.x<-0.99f);
    self.physics.velocity={}; move={}; CHECK(!BotBrakeForTransport(&self,move,-1) && move.LengthSqr()==0);
    // A flight belongs to its own controller.
    self.pfl.onGround=false; self.physics.velocity={320,0,0}; move={1,0,0};
    CHECK(!BotBrakeForTransport(&self,move,-1) && move.x==1);
    std::puts("Transport guard: momentum short of an unplanned pad brakes, a wish slides square or stops, never reverses inward; planned pad and flights untouched: PASS");
}
void TestAirRecovery() {
    // The real hit handler must admit weak impulses. Waiting until the nail
    // thrown WALK is descending loses CTF3's last reachable ledge.
    Reset(); idPlayer weak; gameLocal.entities[0]=&weak;
    weak.pfl.onGround=false; weak.physics.velocity={122,367,105};
    rvBot tapped; tapped.path.Append({2971,3232,-256}); tapped.path.Append({3108,3108,-256});
    tapped.OnKnockback({}); CHECK(tapped.knockbackTime==0);
    tapped.OnKnockback({-49.744f,57.825f,105.27f});
    CHECK(tapped.knockbackTime==gameLocal.time);
    usercmd_t stale; stale.forwardmove=127; stale.upmove=127; stale.buttons=BUTTON_ATTACK;
    tapped.RefreshKnockbackMove(stale);
    CHECK(tapped.airRecovering && stale.forwardmove<0 && stale.upmove==0);
    CHECK((stale.buttons & BUTTON_ATTACK)!=0); // refresh does not regenerate weapon/view input
    // A small hit leaves only two health during DM8's committed jump. Its
    // coast is affordable, but the old lower landing is not. Own that coast.
    rvBot jumper; jumper.path.Append({-76,891,617}); jumper.path.Append({-44,868,328},NAVTRAVEL_JUMP);
    jumper.pathCorner=1; jumper.traversalStarted=true;
    weak.health=2; weak.physics.velocity={25,31,251};
    jumper.OnKnockback({1.514f,34.961f,-0.643f});
    airChoiceSet=true; airChoice={}; airLandingUnsafe=false;
    stale.forwardmove=127; jumper.RefreshKnockbackMove(stale);
    CHECK(jumper.airRecovering && jumper.path.IsEmpty() && stale.forwardmove==0 && stale.rightmove==0);
    // CTF5's pad command was generated before an entity applied the rail.
    // Refresh replaces that obsolete input before the player's physics step;
    // a still-safe pad coast keeps its command and route.
    rvBot pad; pad.path.Append({569,-143,-184}); pad.path.Append({580,884,-184},NAVTRAVEL_JUMPPAD);
    pad.pathCorner=1; pad.traversalStarted=true; pad.traversalStartTime=gameLocal.time-336;
    weak.health=10; weak.physics.velocity={19.531f,146.841f,299.182f};
    pad.OnKnockback({19.531f,-499.058f,-23.655f});
    stale.forwardmove=71; pad.RefreshKnockbackMove(stale);
    CHECK(!pad.airRecovering && stale.forwardmove==71 && pad.path.Num()==2);
    airLandingUnsafe=true; airChoice={0,-1,0}; pad.RefreshKnockbackMove(stale);
    CHECK(pad.airRecovering && stale.forwardmove==0 && stale.rightmove>0 && pad.path.IsEmpty());
    airChoice={1,0,0}; pad.airRecoverPlanTime=gameLocal.time; pad.OnKnockback({1,0,0});
    pad.RefreshKnockbackMove(stale);
    CHECK(stale.forwardmove>0 && stale.rightmove==0); // a second hit replans without the periodic delay
    // The hook is for a hit after this frame's bot command generation only.
    rvBot quiet; stale.forwardmove=127; quiet.RefreshKnockbackMove(stale);
    CHECK(stale.forwardmove==127 && !quiet.airRecovering);
    quiet.OnKnockback({1,0,0}); gameLocal.time+=16; quiet.RefreshKnockbackMove(stale);
    CHECK(stale.forwardmove==127 && !quiet.airRecovering);
    // q4ctf5: a rocket's splash threw a walking bot off the south walkway;
    // its replay lands again with full air input against the throw.
    Reset(); idPlayer self; self.pfl.onGround=false; self.physics.velocity={300,0,180};
    rvBot bot; bot.path.Append({0,0,0}); bot.path.Append({200,0,0});
    usercmd_t cmd; airLandingUnsafe=false;
    // A safe coast needs no recovery for an absent/stale knock or one before
    // the bot's own takeoff. A recent knock protects it from route steering.
    bot.airborneSince=gameLocal.time-84;
    CHECK(!bot.RecoverInAir(&self,cmd) && !bot.airRecovering && cmd.forwardmove==0);
    bot.knockbackTime=gameLocal.time-BOT_AIR_RECOVER_KNOCK_MSEC-1; bot.airborneSince=bot.knockbackTime;
    CHECK(!bot.RecoverInAir(&self,cmd) && !bot.airRecovering);
    bot.knockbackTime=gameLocal.time-400; bot.airborneSince=gameLocal.time-50; bot.airborneVoluntary=true;
    CHECK(!bot.RecoverInAir(&self,cmd) && !bot.airRecovering);
    // Walked off a ledge, unpressed, while the knock still slid it there.
    bot.airborneVoluntary=false; bot.knockbackTime=gameLocal.time-500; bot.airborneSince=gameLocal.time-50;
    CHECK(!bot.RecoverInAir(&self,cmd) && !bot.airRecovering); // 450 ms: its own walk
    bot.knockbackTime=gameLocal.time-300;
    CHECK(bot.RecoverInAir(&self,cmd) && bot.airRecovering);
    bot.airRecovering=false; bot.airborneVoluntary=true; cmd={};
    // Thrown off the ground by it (the knock lands a frame before the bot leaves).
    bot.knockbackTime=gameLocal.time-100; bot.airborneSince=gameLocal.time-84;
    CHECK(bot.RecoverInAir(&self,cmd) && cmd.forwardmove<0 && cmd.rightmove==0 && cmd.upmove==0);
    CHECK(bot.path.Num()==2); // a walking route resumes after the throw
    // A safe knocked coast takes ownership before route steering can spoil it.
    // Zero input clears any prior route command, and no old flight wish leaks in.
    bot.airRecovering=false; airLandingUnsafe=false; airChoiceSet=true; airChoice={};
    bot.airRecoverWish={1,0,0}; cmd.forwardmove=127; cmd.rightmove=127;
    CHECK(bot.RecoverInAir(&self,cmd) && bot.airRecovering && cmd.forwardmove==0 && cmd.rightmove==0);
    CHECK(bot.airRecoverWish.LengthSqr()==0 && bot.path.Num()==2);
    airChoiceSet=false; bot.airRecovering=false;
    self.pfl.onGround=true; self.physics.velocity={700,-300,0}; self.physics.knockback=200;
    navMesh.slideCoastSafe=true;
    cmd.forwardmove=127;
    CHECK(bot.RecoverInAir(&self,cmd) && bot.airRecovering && bot.airRecoverSliding && cmd.forwardmove==0);
    bot.airRecovering=bot.airRecoverSliding=false;
    navMesh.slideCoastSafe=false;
    CHECK(!bot.RecoverInAir(&self,cmd));
    navMesh.slideInputSafe=true; cmd={};
    CHECK(bot.RecoverInAir(&self,cmd) && bot.airRecoverSliding && cmd.forwardmove>0 && cmd.rightmove<0);
    navMesh.slideInputSafe=false; bot.airRecovering=bot.airRecoverSliding=false;
    self.physics.knockback=0;
    CHECK(!bot.RecoverInAir(&self,cmd));
    airLandingUnsafe=true;
    // The ground, and a fall straight down, have nothing to brake.
    self.pfl.onGround=true; CHECK(!bot.RecoverInAir(&self,cmd)); self.pfl.onGround=false;
    self.physics.velocity={0,0,-400}; CHECK(!bot.RecoverInAir(&self,cmd));
    // Off any route the throw is recovered from just the same.
    self.physics.velocity={0,-300,100}; bot.path.Clear(); bot.pathCorner=0; cmd={};
    CHECK(bot.RecoverInAir(&self,cmd) && cmd.rightmove<0 && cmd.forwardmove==0);
    // Once begun, the brake holds while the prediction reads safe, and
    // through the runout after touchdown until the throw has stopped.
    airLandingUnsafe=false; cmd={};
    CHECK(bot.RecoverInAir(&self,cmd) && cmd.rightmove<0);
    self.pfl.onGround=true; self.physics.velocity={0,-200,0}; cmd={};
    CHECK(bot.RecoverInAir(&self,cmd) && cmd.rightmove<0 && bot.airRecoverLanded==gameLocal.time);
    gameLocal.time+=BOT_AIR_RECOVER_RUNOUT_MSEC+1; cmd={};
    CHECK(!bot.RecoverInAir(&self,cmd) && !bot.airRecovering && cmd.rightmove==0);
    rvBot slow; slow.airRecovering=true; self.physics.velocity={0,-40,0};
    cmd={}; CHECK(slow.RecoverInAir(&self,cmd) && slow.airRecovering && cmd.rightmove<0);
    self.physics.velocity={0,-10,0}; cmd={};
    CHECK(!slow.RecoverInAir(&self,cmd) && !slow.airRecovering && cmd.rightmove==0);
    rvBot rim; rim.airRecovering=true; self.physics.velocity={200,-400,0};
    navMesh.continuationClear=false; navMesh.recoveryStepClear=true; cmd={};
    CHECK(rim.RecoverInAir(&self,cmd) && cmd.forwardmove>0 && cmd.rightmove>0);
    navMesh.recoveryStepClear=false; cmd={};
    CHECK(rim.RecoverInAir(&self,cmd) && cmd.forwardmove<0 && cmd.rightmove<0);
    navMesh.continuationClear=true;
    // A knock that turns the walk into the route's drop (q4ctf5): the drop's
    // landing is out of reach, so the route gives way to the brake.
    self.pfl.onGround=false; self.physics.velocity={397,-433,38}; airLandingUnsafe=true; cmd={};
    rvBot dropper; dropper.path.Append({0,0,0}); dropper.path.corners.push_back({{30,-30,-184},NAVTRAVEL_DROP,-1});
    dropper.pathCorner=1; dropper.traversalStarted=true; dropper.traversalStartTime=gameLocal.time-16;
    dropper.knockbackTime=gameLocal.time-100; dropper.airborneSince=gameLocal.time-84;
    dropper.nextGoalSelectTime=gameLocal.time+5000;
    CHECK(dropper.RecoverInAir(&self,cmd) && dropper.path.IsEmpty() && !dropper.traversalStarted);
    CHECK(dropper.nextGoalSelectTime==gameLocal.time && cmd.forwardmove<0);
    // A rail's kick during a pad flight takes the flight over the same way.
    self.physics.velocity={0,-300,100}; cmd={};
    rvBot flyer; flyer.path.Append({0,0,0}); flyer.path.Append({0,1000,0},NAVTRAVEL_JUMPPAD,8);
    flyer.pathCorner=1; flyer.traversalStarted=true; flyer.traversalStartTime=gameLocal.time-300;
    flyer.airborneSince=gameLocal.time-300; flyer.knockbackTime=gameLocal.time-50;
    CHECK(flyer.RecoverInAir(&self,cmd) && flyer.airRecovering && flyer.path.IsEmpty() && cmd.rightmove<0);
    // Whose landing survives the knock keeps its pad controller.
    rvBot safe; safe.path.Append({0,0,0}); safe.path.Append({0,1000,0},NAVTRAVEL_JUMPPAD,8); safe.pathCorner=1;
    safe.traversalStarted=true; safe.traversalStartTime=gameLocal.time-300; safe.airborneSince=gameLocal.time-300;
    safe.knockbackTime=gameLocal.time-50;
    airLandingUnsafe=false; CHECK(!safe.RecoverInAir(&self,cmd) && safe.path.Num()==2 && safe.traversalStarted);
    airLandingUnsafe=true;
    // A pad's volume, or a pad flight that caught the throw, owns the bot:
    // neither starts the brake, and either ends one.
    rvBot caught; caught.path=safe.path; caught.pathCorner=1; caught.traversalStarted=true;
    caught.knockbackTime=gameLocal.time-200; caught.airborneSince=gameLocal.time-216;
    caught.traversalStartTime=gameLocal.time-100;
    CHECK(!caught.RecoverInAir(&self,cmd) && !caught.airRecovering);
    caught.airRecovering=true; CHECK(!caught.RecoverInAir(&self,cmd) && !caught.airRecovering);
    navMesh.volumesAhead={{9,-1000}};
    rvBot inside; inside.knockbackTime=gameLocal.time-100; inside.airborneSince=gameLocal.time-84;
    CHECK(!inside.RecoverInAir(&self,cmd) && !inside.airRecovering);
    inside.airRecovering=true; CHECK(!inside.RecoverInAir(&self,cmd) && !inside.airRecovering);
    navMesh.volumesAhead.clear();
    CHECK(inside.RecoverInAir(&self,cmd) && inside.airRecovering);
    // The input is the fall simulation's choice - here the far side - held
    // between re-plans, and no input at all when that is what lands.
    rvBot chooser; chooser.knockbackTime=gameLocal.time-100; chooser.airborneSince=gameLocal.time-84;
    airChoiceSet=true; airChoice={0,-1,0}; airChoices=0; cmd={};
    CHECK(chooser.RecoverInAir(&self,cmd) && cmd.rightmove>0 && airChoices==1); // velocity (0,-300): forward is -y
    airChoice={0,1,0}; gameLocal.time+=16; cmd={};
    CHECK(chooser.RecoverInAir(&self,cmd) && cmd.rightmove>0 && airChoices==1);
    gameLocal.time+=BOT_AIR_RECOVER_REPLAN_MSEC; cmd={};
    CHECK(chooser.RecoverInAir(&self,cmd) && cmd.rightmove<0 && airChoices==2);
    airChoice={}; gameLocal.time+=BOT_AIR_RECOVER_REPLAN_MSEC; cmd={};
    CHECK(chooser.RecoverInAir(&self,cmd) && cmd.rightmove==0 && cmd.forwardmove==0 && airChoices==3);
    // A grounded brake may lose support on a sloping WALK rim before it
    // stops. Unsafe involuntary flights recover without an enemy hit.
    Reset(); self.pfl.onGround=false; self.physics.velocity={110,52,-60};
    auto walker=[&]() { rvBot b; b.path.Append({0,0,0}); b.path.Append({100,0,0});
        b.pathCorner=1; b.airborneSince=gameLocal.time-16; return b; };
    airLandingUnsafe=true;
    rvBot slipped=walker(); cmd={};
    CHECK(slipped.RecoverInAir(&self,cmd) && slipped.airRecovering && cmd.forwardmove<0);
    CHECK(slipped.path.Num()==2); // retained walking goal resumes after landing
    // A jump ahead of WALK does not own a fall before the bot presses it.
    slipped=walker(); slipped.path.Append({200,0,0},NAVTRAVEL_JUMP); cmd={};
    CHECK(slipped.RecoverInAir(&self,cmd));
    slipped=walker(); slipped.airborneVoluntary=true; cmd={};
    CHECK(!slipped.RecoverInAir(&self,cmd));
    for(int action : {NAVTRAVEL_DROP,NAVTRAVEL_TELEPORT}) {
        slipped=walker(); slipped.path.Append({200,0,0},action); cmd={};
        CHECK(!slipped.RecoverInAir(&self,cmd));
        slipped.pathCorner=2; CHECK(!slipped.RecoverInAir(&self,cmd));
    }
    // A ramp can launch it before the final corner is consumed; waiting for
    // descent loses CTF3's reachable floor. An empty route must not disable
    // recovery on the following frame either.
    slipped=walker(); self.physics.velocity.z=112; cmd={};
    CHECK(slipped.RecoverInAir(&self,cmd) && slipped.airRecovering);
    slipped=walker(); slipped.path.Clear(); cmd={};
    CHECK(slipped.RecoverInAir(&self,cmd) && slipped.airRecovering);
    // An unlaunched pad ahead is only an intention, not ownership of a flight
    // that is already missing its trigger. Real pad volumes/flights above
    // still reject recovery, and safe approaches keep their input.
    slipped=walker(); slipped.path.Append({200,0,0},NAVTRAVEL_JUMPPAD); cmd={};
    CHECK(slipped.RecoverInAir(&self,cmd) && slipped.airRecovering);
    for(bool empty : {false,true}) {
        slipped=walker();
        if(empty) slipped.path.Clear();
        else slipped.path.Append({200,0,0},NAVTRAVEL_JUMPPAD);
        airLandingUnsafe=false; cmd={}; CHECK(!slipped.RecoverInAir(&self,cmd));
        airLandingUnsafe=true; slipped.airborneVoluntary=true;
        CHECK(!slipped.RecoverInAir(&self,cmd));
    }
    std::puts("Air recovery: unsafe unpressed flights protected through ascent, route completion and unlaunched pad approaches; safe arcs and flight owners retained: PASS");
}
void TestUnexpectedPad() {
    Reset(); idPlayer self; rvJumpPad pad; idItem goal;
    self.physics.origin={700,-300,-150}; self.physics.velocity={600,600,600};
    gameLocal.entities[8]=&pad; navMesh.transport=8; navMesh.naturalPad=true;
    navMesh.padLanding={1348,308,35};
    rvBot bot; goal.physics.origin={652,-244,-184}; GoalRoute(bot,&goal,BOTGOAL_ITEM);
    bot.dodgeHopActive=true; bot.dodgeHopWishSpeed=320;
    bot.stuckChecks=2; bot.unstickUntil=3000; bot.padHoldSince=100;
    // An applied launch precedes the next player-physics update, so the
    // preceding frame's onGround flag must not hide its vertical impulse.
    bot.AdoptPadLaunch(&self);
    CHECK(bot.path.Num()==2 && bot.pathCorner==1 && bot.traversalCorner==1);
    CHECK(bot.path[1].travelType==NAVTRAVEL_JUMPPAD && bot.path[1].actionEntityNum==8);
    CHECK(bot.path[1].origin.Compare(navMesh.padLanding));
    CHECK(bot.traversalEntered && bot.traversalStarted && bot.traversalStartTime==gameLocal.time);
    CHECK(bot.stuckChecks==0 && bot.unstickUntil==0 && bot.padHoldSince==0);
    CHECK(bot.goalEntity.GetEntity()==&goal && bot.goalType==BOTGOAL_ITEM);
    CHECK(!bot.dodgeHopActive); // observed pad supersedes a voluntary hop
    // The normal action commitment keeps goal selection from braking the
    // flight toward its old WALK goal; touching the same pad is idempotent.
    gameLocal.time+=100; bot.UpdateGoal(&self); bot.AdoptPadLaunch(&self);
    CHECK(bot.path[1].origin.Compare(navMesh.padLanding) && bot.traversalStartTime==1000);
    CHECK(botManager.decisions==0);
    auto walking=[&]() { rvBot b; GoalRoute(b,&goal,BOTGOAL_ITEM); return b; };
    rvBot ordinary=walking(); self.physics.velocity={320,0,277};
    ordinary.AdoptPadLaunch(&self); CHECK(ordinary.pathCorner==0 && gameLocal.contactTests==1);
    self.physics.velocity={600,600,600};
    // Broad-phase overlap beside a diagonal brush is not activation.
    gameLocal.padContact=false; ordinary.dodgeHopActive=true;
    ordinary.AdoptPadLaunch(&self); CHECK(ordinary.pathCorner==0 && ordinary.dodgeHopActive);
    gameLocal.padContact=true; pad.instance=1;
    ordinary.AdoptPadLaunch(&self); CHECK(ordinary.pathCorner==0); pad.instance=0;
    // A pad whose natural landing is unproven cannot create a recovery path.
    navMesh.naturalPad=false; ordinary.AdoptPadLaunch(&self); CHECK(ordinary.pathCorner==0);
    navMesh.naturalPad=true;
    // The planned pad entry and an airborne planned pad keep their own route,
    // including intentionally steered landings.
    rvBot planned; planned.path.Append({0,0,0}); planned.path.Append({0,1000,0},NAVTRAVEL_JUMPPAD,8);
    planned.dodgeHopActive=true;
    planned.AdoptPadLaunch(&self); CHECK(planned.pathCorner==0 && planned.path[1].origin.x==0);
    CHECK(!planned.dodgeHopActive); // planned pad owns its original steered landing
    planned.pathCorner=1; planned.AdoptPadLaunch(&self); CHECK(planned.path[1].origin.x==0);
    // An unrelated pad really does replace that flight when its brush owns
    // the impulse; normal goal identity is retained for after touchdown.
    planned.path.corners[1].actionEntityNum=9;
    planned.AdoptPadLaunch(&self); CHECK(planned.path[1].actionEntityNum==8);
    std::puts("Unexpected pad: exact touch plus launch impulse adopts proven natural arc, preserves goal and planned flights: PASS");
}
void TestMoveProjection() {
    Reset(); idPlayer self; gameLocal.entities[0]=&self;
    rvBot bot; idPhysics_Player physics;
    for(float yaw : {0.0f,45.0f,90.0f}) {
        bot.aimAngles.yaw=yaw;
        for(float strength : {1.0f,0.25f,-1.0f}) {
            usercmd_t cmd; bot.ApplyMove({strength,0,0},cmd);
            idVec3 forward,right; bot.aimAngles.ToVectors(&forward,&right,nullptr);
            const idVec3 actual=(forward*float(cmd.forwardmove)+right*float(cmd.rightmove))*physics.CmdScale(cmd,false);
            CHECK(std::fabs(actual.x-strength*320)<1.3f && std::fabs(actual.y)<1.3f);
            CHECK(cmd.buttons & BUTTON_RUN);
        }
        usercmd_t idle; bot.ApplyMove({},idle);
        CHECK(idle.forwardmove==0 && idle.rightmove==0 && idle.buttons==0 && physics.CmdScale(idle,false)==0);
    }
    for(float modifier : {1.3f,1.5f}) {
        self.speedModifier=modifier; physics.playerSpeed=320*modifier;
        for(int travel : {NAVTRAVEL_DROP,NAVTRAVEL_JUMP,NAVTRAVEL_JUMPPAD}) {
            bot.path.Clear(); bot.path.Append({}); bot.path.Append({100,0,0},travel);
            for(int corner : {0,1}) {
                bot.pathCorner=corner;
                for(float yaw : {0.0f,45.0f,90.0f}) {
                    bot.aimAngles.yaw=yaw; usercmd_t cmd; bot.ApplyMove({1,0,0},cmd);
                    idVec3 forward,right; bot.aimAngles.ToVectors(&forward,&right,nullptr);
                    const idVec3 actual=(forward*float(cmd.forwardmove)+right*float(cmd.rightmove))*physics.CmdScale(cmd,false);
                    CHECK(actual.x<=320.01f && actual.x>316 && std::fabs(actual.y)<1.4f);
                }
            }
        }
        bot.path.Clear(); bot.pathCorner=0; bot.aimAngles.yaw=0;
        usercmd_t ordinary; bot.ApplyMove({1,0,0},ordinary);
        CHECK(std::fabs(ordinary.forwardmove*physics.CmdScale(ordinary,false)-320*modifier)<0.01f);
        bot.airRecovering=true; usercmd_t recovery; bot.ApplyMove({1,0,0},recovery);
        const float recoverySpeed=recovery.forwardmove*physics.CmdScale(recovery,false);
        CHECK(recoverySpeed<=320.01f && recoverySpeed>316); bot.airRecovering=false;
    }
    std::puts("Movement projection: production ApplyMove/CmdScale preserves full and partial world speed at yaw0/45/90: PASS");
}
void TestActualFloorHealth() {
    Reset(); idPlayer self;
    self.spawnArgs.Set("soft_fall_delta","40"); self.spawnArgs.Set("hard_fall_delta","60");
    self.spawnArgs.Set("fatal_fall_delta","0");
    gameLocal.damageDefs["damage_softfall"].Set("damage","5");
    gameLocal.damageDefs["damage_hardfall"].Set("damage","10");
    self.physics.origin={-206,-819,256.25f}; self.inventory.armor=100; self.health=3;
    gameLocal.edgeFloor=-127.75f; navMesh.landingImpact=61.44f;
    CHECK(BotDeadlyDropAt(&self,{-280,-740,256.25f}));
    CHECK(navMesh.lastLandingFrom.Compare(self.physics.origin));
    self.health=4; CHECK(!BotDeadlyDropAt(&self,{-280,-740,256.25f}));
    self.health=3; navMesh.landingImpact=58.88f; gameLocal.edgeFloor=-111.75f;
    CHECK(!BotDeadlyDropAt(&self,{-280,-740,256.25f}));
    self.inventory.armor=0; CHECK(BotDeadlyDropAt(&self,{-280,-740,256.25f}));
    self.pfl.noFallingDamage=true; CHECK(!BotDeadlyDropAt(&self,{-280,-740,256.25f}));
    CHECK(BotDeadlyDropAt(&self,{1,0,256.25f})); // disabling damage does not waive voids
    std::puts("Actual floor: armour and current health distinguish a raised landing from its lower neighbour: PASS");
}
void TestLandingHealth() {
    Reset(); idPlayer self; idItem goal;
    self.spawnArgs.Set("soft_fall_delta","40"); self.spawnArgs.Set("hard_fall_delta","60");
    self.spawnArgs.Set("fatal_fall_delta","0");
    gameLocal.damageDefs["damage_softfall"].Set("damage","5");
    gameLocal.damageDefs["damage_hardfall"].Set("damage","10");
    self.health=3; CHECK(BotMaxSafeFallDelta(&self)==40);
    self.health=8; CHECK(BotMaxSafeFallDelta(&self)==60);
    self.health=11; CHECK(BotMaxSafeFallDelta(&self)==idMath::INFINITY);
    self.inventory.armor=20; self.health=4;
    CHECK(BotMaxSafeFallDelta(&self)==idMath::INFINITY);
    self.health=2; CHECK(BotMaxSafeFallDelta(&self)==60);
    self.health=1; CHECK(BotMaxSafeFallDelta(&self)==40);
    gameLocal.damageDefs["damage_softfall"].Set("noArmor","1");
    self.health=4; CHECK(BotMaxSafeFallDelta(&self)==40);
    gameLocal.damageDefs["damage_softfall"].Set("noArmor","0");
    gameLocal.damageDefs["damage_softfall"].Set("damage","20");
    self.inventory.armor=0; self.health=12; CHECK(BotMaxSafeFallDelta(&self)==40);
    gameLocal.damageDefs["damage_softfall"].Set("damage","5");
    self.health=3; self.godmode=true; self.damageCalculations=0;
    CHECK(BotMaxSafeFallDelta(&self)==idMath::INFINITY && self.damageCalculations==0 && self.godCounter==0);
    gameLocal.damageDefs["damage_hardfall"].Set("noGod","1");
    CHECK(BotMaxSafeFallDelta(&self)==60 && self.godCounter==0);
    self.pfl.noFallingDamage=true;
    CHECK(BotMaxSafeFallDelta(&self)==idMath::INFINITY);
    self.pfl.noFallingDamage=false; self.godmode=false;
    auto approach=[&]() {
        rvBot b; GoalRoute(b,&goal,BOTGOAL_ITEM); b.path.Clear();
        b.path.Append({0,0,0}); b.path.Append({300,0,0},NAVTRAVEL_JUMPPAD,8); return b;
    };
    usercmd_t cmd; rvBot bot=approach(); navMesh.landingImpact=30;
    for(int i=0;i<120;i++) CHECK(!bot.GuardLandingHealth(&self,cmd));
    CHECK(navMesh.landingChecks==1); // a stable approach never resimulates per frame
    self.health=8; CHECK(!bot.GuardLandingHealth(&self,cmd)); CHECK(navMesh.landingChecks==2);
    self.health=3; bot.pathTime++; navMesh.landingImpact=55; self.physics.velocity={100,0,0}; cmd.upmove=127;
    CHECK(bot.GuardLandingHealth(&self,cmd));
    CHECK(bot.path.IsEmpty() && bot.goalEntity.GetEntity()==nullptr && bot.IsAvoided(&goal));
    CHECK(cmd.forwardmove<0 && cmd.upmove==0 && bot.nextGoalSelectTime==gameLocal.time);
    // Damage can make a retained route lethal while several walking corners
    // remain. Recheck before those corners carry the player onto its trigger.
    bot=approach(); bot.path.Clear(); bot.path.Append({0,0,0}); bot.path.Append({100,0,0});
    bot.path.Append({300,0,0},NAVTRAVEL_JUMPPAD,8);
    CHECK(bot.GuardLandingHealth(&self,cmd));
    CHECK(bot.path.IsEmpty() && navMesh.lastLandingFrom.x==100 && cmd.forwardmove<0);
    bot=approach(); bot.path.corners[1].travelType=NAVTRAVEL_TELEPORT;
    bot.path.Append({300,0,0},NAVTRAVEL_JUMPPAD,8);
    CHECK(!bot.GuardLandingHealth(&self,cmd));
    bot.path.corners[1].travelType=NAVTRAVEL_WALK; bot.path.corners.resize(2);
    CHECK(!bot.GuardLandingHealth(&self,cmd));
    // Neither an airborne route nor an observed launch with a stale grounded
    // flag may lose its controller after damage during the flight.
    bot=approach(); bot.traversalStarted=true;
    CHECK(!bot.GuardLandingHealth(&self,cmd) && bot.path.Num()==2);
    bot.traversalStarted=false; self.pfl.onGround=false;
    CHECK(!bot.GuardLandingHealth(&self,cmd) && bot.path.Num()==2);
    self.pfl.onGround=true; self.physics.velocity={0,0,770};
    CHECK(!bot.GuardLandingHealth(&self,cmd) && bot.path.Num()==2);
    self.physics.velocity={0,0,0};
    CHECK(bot.Repath(&self,{100,0,0}) && navMesh.lastImpactLimit==40);
    idItem valuable,safe; valuable.physics.origin={100,0,0}; valuable.utility=2;
    safe.physics.origin={200,0,0}; safe.utility=1.5f;
    botManager.items.clear(); botManager.items.Append(&valuable); botManager.items.Append(&safe); navMesh.hazardous={100};
    rvNavPath route; float utility=0;
    CHECK(bot.PickItemGoal(&self,route,utility)==&safe);
    self.health=11;
    CHECK(bot.PickItemGoal(&self,route,utility)==&valuable); // graph remains usable when healthy
    // q4ctf5's func_jumppad_14 lands on func_jumppad_15, which throws its
    // flyer across the map: a survivable first landing does not make the
    // chain survivable. A walk between two actions does end the chain.
    self.health=3; self.physics.velocity={100,0,0}; self.pfl.onGround=true;
    navMesh.landingImpact=30; navMesh.impactAtX[900]=55;
    rvBot chain=approach(); chain.path.Append({900,0,0},NAVTRAVEL_JUMPPAD,9); cmd={};
    CHECK(chain.GuardLandingHealth(&self,cmd));
    CHECK(chain.path.IsEmpty() && navMesh.lastLandingFrom.x==300 && cmd.forwardmove<0);
    chain=approach(); chain.path.Append({600,0,0}); chain.path.Append({900,0,0},NAVTRAVEL_JUMPPAD,9);
    CHECK(!chain.GuardLandingHealth(&self,cmd) && chain.path.Num()==4);
    navMesh.impactAtX.clear();
    std::puts("Landing health: live damage/armour tiers, immunity without side effects, cached entry brake, pad chains and retained flight ownership: PASS");
}
void TestDropHandoff() {
    CHECK(!BotPadFlightComplete(true,false,true,1000,false)); // still descending near target
    CHECK(BotPadFlightComplete(true,true,true,1000,false));
    CHECK(BotPadFlightComplete(true,true,false,1000,false)); // off-target touchdown still ends flight
    CHECK(BotPadFlightComplete(true,false,false,1000,true)); // explicit next-pad handoff
    CHECK(!BotPadFlightComplete(false,true,true,1000,true));
    Reset(); idPlayer self; gameLocal.entities[0]=&self;
    rvBot bot; bot.path.Append({0,0,224});
    bot.path.corners.push_back({{24,0,0},NAVTRAVEL_DROP,-1});
    bot.path.Append({160,0,0});
    self.pfl.onGround=true; self.physics.origin={-40,0,224};
    self.physics.velocity={0,330,0}; usercmd_t cmd;
    CHECK(bot.AlignJumpDropApproach(&self,cmd) && cmd.rightmove>0 && cmd.upmove==0);
    self.physics.velocity={320,0,0}; CHECK(!bot.AlignJumpDropApproach(&self,cmd));
    self.physics.velocity={420,0,0}; cmd={};
    CHECK(bot.AlignJumpDropApproach(&self,cmd) && cmd.forwardmove<0); // aligned but outside proof speed
    self.physics.velocity={0,330,0}; self.physics.origin.x=-80;
    CHECK(!bot.AlignJumpDropApproach(&self,cmd));
    self.physics.origin.x=-8; bot.pathCorner=1;
    CHECK(bot.AlignJumpDropApproach(&self,cmd)); // already consumed entry, still on lip
    self.pfl.onGround=false; CHECK(!bot.AlignJumpDropApproach(&self,cmd));
    self.pfl.onGround=true; bot.traversalStarted=true;
    CHECK(!bot.AlignJumpDropApproach(&self,cmd));
    self.physics.origin={96,32,0}; self.physics.velocity={300,0,0};
    CHECK(!bot.GuardLandingContinuation(&self,cmd) && bot.path.Num()==3);
    navMesh.continuationClear=false;
    self.pfl.onGround=false; CHECK(!bot.GuardLandingContinuation(&self,cmd));
    self.pfl.onGround=true; cmd={}; bot.nextGoalSelectTime=10000;
    CHECK(bot.GuardLandingContinuation(&self,cmd));
    CHECK(bot.path.IsEmpty() && !bot.traversalStarted && cmd.forwardmove<0 && cmd.upmove==0);
    CHECK(bot.nextGoalSelectTime==gameLocal.time);
    std::puts("Drop handoff: transverse approach brakes early; aligned/airborne flights retained; unsafe actual continuation brakes and replans: PASS");
}
void TestJumpEntry() {
    Reset(); idPlayer self; gameLocal.entities[0]=&self; self.pfl.onGround=true;
    rvBot bot; bot.path.Append({-300,372,-32}); bot.path.Append({-300,348,-256},NAVTRAVEL_JUMP);
    bot.pathCorner=1; bot.traversalStartTime=gameLocal.time;
    self.physics.origin={-289,377,-32}; self.physics.velocity={-278,-160,0};
    usercmd_t cmd;
    CHECK(bot.AlignJumpDropApproach(&self,cmd) && cmd.forwardmove>0 && cmd.upmove==0);
    CHECK(bot.HandleJumpTakeoff(&self,cmd) && cmd.upmove==0);
    CHECK(navMesh.jumpTakeoffChecks==1);
    CHECK(navMesh.lastJumpOrigin.Compare(self.physics.origin));
    CHECK(navMesh.lastJumpVelocity.Compare(self.physics.velocity));
    CHECK(navMesh.lastJumpLanding.Compare(bot.path[1].origin));
    self.physics.velocity={0,-160,0};
    CHECK(!bot.AlignJumpDropApproach(&self,cmd));
    navMesh.jumpTakeoffClear=true;
    idVec3 wanted=bot.path[1].origin-self.physics.origin; wanted.z=0; wanted.Normalize();
    for(float yaw:{0.0f,45.0f,90.0f}) {
        bot.aimAngles.yaw=yaw; cmd={}; CHECK(bot.HandleJumpTakeoff(&self,cmd) && cmd.upmove==127);
        idVec3 forward,right; bot.aimAngles.ToVectors(&forward,&right,nullptr);
        idVec3 actual=forward*float(cmd.forwardmove)+right*float(cmd.rightmove); actual.Normalize();
        CHECK((actual-wanted).Length()<0.005f); // actual offset changes first-frame input
        CHECK((actual-navMesh.lastJumpFirstWish).Length()<0.0001f);
        idPhysics_Player airborne; airborne.walking=false;
        const float encodedHorizontal=idVec3(float(cmd.forwardmove),float(cmd.rightmove),0).Length();
        CHECK(std::fabs(encodedHorizontal*airborne.CmdScale(cmd,false)-navMesh.lastJumpFirstSpeed)<0.001f);
    }
    self.speedModifier=1.5f; cmd={}; CHECK(bot.HandleJumpTakeoff(&self,cmd));
    idPhysics_Player physics; physics.playerSpeed=480;
    CHECK(std::fabs(navMesh.lastJumpSpeed-480*84.0f/127)<0.01f);
    physics.walking=false;
    CHECK(std::fabs(idVec3(float(cmd.forwardmove),float(cmd.rightmove),0).Length()*physics.CmdScale(cmd,false)-navMesh.lastJumpFirstSpeed)<0.01f);
    idVec3 issued(float(cmd.forwardmove),float(cmd.rightmove),0); cmd.upmove=0;
    CHECK(std::fabs(issued.Length()*physics.CmdScale(cmd,false)-navMesh.lastJumpSpeed)<0.01f);
    const int checks=navMesh.jumpTakeoffChecks;
    gameLocal.framenum=1; cmd={}; CHECK(bot.HandleJumpTakeoff(&self,cmd) && cmd.upmove==0);
    CHECK(navMesh.jumpTakeoffChecks==checks); // no flight sweep when no fresh press can fire
    self.pfl.onGround=false; CHECK(!bot.HandleJumpTakeoff(&self,cmd));
    self.pfl.onGround=true; bot.traversalStarted=true; gameLocal.framenum=0;
    navMesh.jumpTakeoffClear=false; cmd={};
    CHECK(bot.HandleJumpTakeoff(&self,cmd) && cmd.upmove==0);
    CHECK(navMesh.jumpTakeoffChecks==checks+1); // grounded short-landing retry cannot bypass live proof
    bot.traversalStarted=false; gameLocal.framenum=0; navMesh.jumpTakeoffClear=false;
    self.physics.velocity={}; bot.aimAngles.yaw=0; self.speedModifier=1; cmd={};
    CHECK(bot.HandleJumpTakeoff(&self,cmd) && cmd.upmove==0 && cmd.forwardmove<0 && cmd.rightmove>0);
    navMesh.continuationClear=false; cmd={}; CHECK(bot.HandleJumpTakeoff(&self,cmd));
    CHECK(cmd.forwardmove==0 && cmd.rightmove==0); // cannot recenter across unsupported floor
    gameLocal.time+=BOT_TRAVEL_TIMEOUT_MSEC+1;
    CHECK(bot.HandleJumpTakeoff(&self,cmd) && bot.path.IsEmpty());
    // Entering an objective's actual hold volume in mid-flight cannot end
    // landing ownership, even when the foe/aim changes during that crossing.
    idEntity zone; zone.physics.origin=self.physics.origin; idPlayer foe;
    GoalRoute(bot,&zone,BOTGOAL_OBJECTIVE); bot.objectiveKind=BOTOBJ_CONTROL;
    bot.objectiveHoldPosition=true; bot.path.Clear();
    bot.path.Append(self.physics.origin); bot.path.Append({-300,348,-256},NAVTRAVEL_JUMP);
    bot.pathCorner=1; bot.traversalCorner=1; bot.traversalStarted=true;
    self.pfl.onGround=false; bot.enemy=&foe; bot.aimAngles.yaw=90;
    CHECK(!bot.AtObjectiveHoldPosition(&self)); bot.UpdateGoal(&self);
    CHECK(bot.path.Num()==2 && bot.pathCorner==1 && bot.path[1].travelType==NAVTRAVEL_JUMP);
    self.pfl.onGround=true; self.physics.velocity={0,0,1012};
    CHECK(!bot.AtObjectiveHoldPosition(&self)); // pad impulse before ground flag catches up
    self.physics.velocity={}; CHECK(bot.AtObjectiveHoldPosition(&self));
    std::puts("Jump entry: exact live position/momentum, same launch steering, settled recenter, fresh-press cost bound and timeout: PASS");
}

void TestGroundBraking() {
    for(bool probe:{true,false}) for(float yaw:{0.0f,45.0f,90.0f})
    for(float initial:{14.0f,35.6f,72.4f,160.0f,320.0f,600.0f}) {
        Reset(); idPlayer self; gameLocal.entities[0]=&self;
        self.pfl.onGround=true; self.physics.velocity={initial,0,0};
        rvBot bot; bot.aimAngles.yaw=yaw; bot.airRecovering=true;
        navMesh.continuationClear=true;
        for(int frame=0;frame<120;frame++) {
            usercmd_t cmd{};
            if(probe) bot.ProbeAirControl(&self,cmd); else bot.RecoverInAir(&self,cmd);
            idVec3 forward,right; bot.aimAngles.ToVectors(&forward,&right,nullptr);
            idVec3 wish=forward*float(cmd.forwardmove)+right*float(cmd.rightmove);
            const float wishSpeed=wish.Normalize()*self.physics.CmdScale(cmd,false);
            // Flat MP ground: independent friction/acceleration integration
            // exposes reversal and cumulative drift, rather than just checking
            // that the command points against the current velocity.
            const idVec3 before=self.physics.velocity;
            const float speed=before.Length();
            if(speed>0) self.physics.velocity*=Max(0.0f,speed-Max(speed,100.0f)*6.0f*0.016f)/speed;
            const float add=wishSpeed-self.physics.velocity*wish;
            if(add>0) self.physics.velocity+=wish*Min(add,15.0f*0.016f*wishSpeed);
            CHECK(self.physics.velocity*before>=-0.01f);
            CHECK(self.physics.velocity.Length()<=speed+0.01f);
            self.physics.origin+=self.physics.velocity*0.016f;
            gameLocal.time+=16;
        }
        CHECK(self.physics.velocity.Length()<0.01f);
        CHECK(self.physics.origin.Length()<100.0f);
    }
    Reset(); idPlayer self; gameLocal.entities[0]=&self;
    rvBot bot; self.pfl.onGround=false; bot.probeAirDirection={1,0,0};
    usercmd_t cmd{}; bot.ProbeAirControl(&self,cmd); CHECK(cmd.forwardmove==127);
    self.pfl.onGround=true; self.physics.velocity={35,0,0}; self.physics.knockback=50;
    cmd={}; bot.ProbeAirControl(&self,cmd); CHECK(cmd.forwardmove==-127);
    std::puts("Ground braking: probe and recovery settle without reversal or drift; air/knock input retained: PASS");
}

void TestDodgeOwnership() {
    Reset(); idPlayer self; gameLocal.entities[0]=&self;
    rvBot bot; bot.path.Append({500,0,0}); usercmd_t cmd;
    bot.TryDodgeJump(&self,{1,0,0},cmd);
    CHECK(!bot.dodgeHopActive && cmd.upmove==0); // failed proof cannot own movement
    navMesh.dodgeClear=true;
    gameLocal.framenum=1; bot.TryDodgeJump(&self,{1,0,0},cmd);
    CHECK(!bot.dodgeHopActive && cmd.upmove==0); // no fresh press on this frame
    gameLocal.framenum=2; bot.TryDodgeJump(&self,{0.25f,0,0},cmd);
    CHECK(bot.dodgeHopActive && cmd.upmove==127 && cmd.forwardmove==127);
    {   // A strong knock under the hop releases it to the airborne brake.
        rvBot knocked=bot; knocked.knockbackTime=knocked.dodgeHopStartTime+16; usercmd_t held;
        CHECK(!knocked.HoldDodgeJump(&self,held) && !knocked.dodgeHopActive && held.forwardmove==0);
        knocked=bot; knocked.knockbackTime=knocked.dodgeHopStartTime; // the same frame
        CHECK(!knocked.HoldDodgeJump(&self,held) && !knocked.dodgeHopActive);
        knocked=bot; knocked.knockbackTime=knocked.dodgeHopStartTime-16;
        CHECK(knocked.HoldDodgeJump(&self,held) && knocked.dodgeHopActive);
    }
    CHECK(navMesh.lastDodgeDirection.Compare({1,0,0}) && navMesh.lastDodgeSpeed==320);
    {
        rvBot diverted=bot; usercmd_t held;
        self.pfl.onGround=false; self.physics.velocity={320,0,0}; navMesh.dodgeFlightSafe=false;
        CHECK(!diverted.HoldDodgeJump(&self,held));
        CHECK(!diverted.dodgeHopActive && diverted.airRecovering && diverted.path.IsEmpty());
        CHECK(diverted.airRecoverWish.LengthSqr()==0 && held.forwardmove==0);
        CHECK(diverted.RecoverInAir(&self,held));
        self.pfl.onGround=true; navMesh.dodgeFlightSafe=true;
    }
    const int searches=navMesh.searches;
    self.pfl.onGround=false; gameLocal.time+=16;
    // Route/combat decisions and view changes may disagree with the hop.
    // Production HoldDodgeJump must still emit its original world input.
    bot.path.Clear(); bot.path.Append({-500,0,0},NAVTRAVEL_DROP);
    idPlayer foe; foe.physics.origin={0,500,0}; bot.enemy=&foe;
    bot.UpdateGoal(&self); CHECK(navMesh.searches==searches);
    idPhysics_Player physics;
    for(float yaw:{0.0f,45.0f,90.0f}) {
        bot.aimAngles.yaw=yaw; cmd={127,-127,-127,0};
        CHECK(bot.HoldDodgeJump(&self,cmd) && bot.dodgeHopAirborne && cmd.upmove==0);
        idVec3 forward,right; bot.aimAngles.ToVectors(&forward,&right,nullptr);
        const idVec3 actual=(forward*float(cmd.forwardmove)+right*float(cmd.rightmove))*physics.CmdScale(cmd,false);
        CHECK(std::fabs(actual.x-320)<2 && std::fabs(actual.y)<2);
    }
    self.speedModifier=1.5f; physics.playerSpeed=480;
    cmd={}; CHECK(bot.HoldDodgeJump(&self,cmd));
    idVec3 forward,right; bot.aimAngles.ToVectors(&forward,&right,nullptr);
    const idVec3 capped=(forward*float(cmd.forwardmove)+right*float(cmd.rightmove))*physics.CmdScale(cmd,false);
    CHECK(capped.x<=320 && capped.x>316 && std::fabs(capped.y)<2);
    self.pfl.onGround=true; CHECK(!bot.HoldDodgeJump(&self,cmd) && !bot.dodgeHopActive);
    bot.aimAngles.yaw=0; bot.ApplyMove({-1,0,0},cmd); CHECK(cmd.forwardmove<0);
    self.speedModifier=1; bot.path.Clear(); cmd={}; bot.TryDodgeJump(&self,{1,0,0},cmd);
    gameLocal.time+=BOT_DODGE_LAUNCH_MSEC; CHECK(!bot.HoldDodgeJump(&self,cmd));
    cmd={}; bot.TryDodgeJump(&self,{1,0,0},cmd); self.pfl.onGround=false;
    gameLocal.time+=BOT_DODGE_FLIGHT_MSEC; CHECK(!bot.HoldDodgeJump(&self,cmd));
    self.pfl.onGround=true; cmd={}; bot.TryDodgeJump(&self,{1,0,0},cmd); self.health=0;
    CHECK(!bot.HoldDodgeJump(&self,cmd) && !bot.dodgeHopActive);
    self.health=100; cmd={}; bot.TryDodgeJump(&self,{1,0,0},cmd);
    bot.ResetDodgeJump(); CHECK(!bot.dodgeHopActive && !bot.dodgeHopAirborne && bot.dodgeHopWishSpeed==0);
    std::puts("Dodge ownership: fixed world command through route/combat/yaw changes; boost cap, touchdown, failed launch, death and expiry release: PASS");
}

void TestRecoveryEdge() {
    Reset(); idPlayer self; gameLocal.entities[0]=&self;
    moveDirectionSafe=false; // no clear sidestep in these stopping controls
    rvBot bot; bot.recoverSearchTime=gameLocal.time; bot.recoverValid=true;
    bot.recoverTarget={100,0,0}; self.physics.origin={-8,0,0}; self.physics.velocity={100,0,0};
    usercmd_t cmd; CHECK(bot.RecoverToNavMesh(&self,cmd));
    CHECK(cmd.forwardmove<0 && cmd.upmove==0 && navMesh.dodgeChecks==0);
    self.physics.velocity={}; cmd={}; CHECK(bot.RecoverToNavMesh(&self,cmd));
    CHECK(cmd.forwardmove==0 && cmd.upmove==0);
    self.physics.origin={16,0,0}; bot.recoverTarget={-28,63,0}; cmd={};
    CHECK(bot.RecoverToNavMesh(&self,cmd) && cmd.forwardmove==0 && cmd.rightmove==0);
    navMesh.recoveryStepClear=true; cmd={}; cmd.upmove=127;
    bot.stuckChecks=2; bot.stuckTime=0; bot.unstickUntil=gameLocal.time+1000;
    CHECK(bot.RecoverToNavMesh(&self,cmd));
    CHECK(cmd.forwardmove<0 && cmd.rightmove<0 && cmd.upmove==0);
    CHECK(bot.stuckChecks==0 && bot.unstickUntil==0 && bot.stuckTime==gameLocal.time);
    idPhysics_Player creepPhysics;
    idVec3 creep(float(cmd.forwardmove),-float(cmd.rightmove),0);
    creep*=creepPhysics.CmdScale(cmd,false);
    CHECK(creep.Length()>28 && creep.Length()<=BOT_RECOVER_EDGE_SPEED);
    for(float modifier:{1.3f,1.5f}) {
        self.speedModifier=modifier; creepPhysics.playerSpeed=320*modifier;
        cmd={}; bot.RecoverToNavMesh(&self,cmd);
        creep={float(cmd.forwardmove),-float(cmd.rightmove),0};
        creep*=creepPhysics.CmdScale(cmd,false);
        CHECK(creep.Length()>26 && creep.Length()<=BOT_RECOVER_EDGE_SPEED);
    }
    self.speedModifier=1;
    self.physics.velocity={100,0,0}; cmd={}; bot.RecoverToNavMesh(&self,cmd);
    CHECK(cmd.forwardmove<0 && cmd.rightmove==0); // momentum outside the proof still brakes
    self.physics.velocity={};
    // An outer hull rim can still support a bot whose center is over the edge.
    // Recovery toward supported floor must remain possible from that position.
    self.physics.origin={8,0,0}; bot.recoverTarget={-100,0,0}; cmd={};
    CHECK(bot.RecoverToNavMesh(&self,cmd) && cmd.forwardmove<0);
    bot.recoverValid=false; bot.recoverWanderUntil=gameLocal.time+1000; bot.recoverWanderYaw=0;
    self.physics.origin={-8,0,0}; cmd={}; CHECK(!bot.RecoverToNavMesh(&self,cmd));
    CHECK(cmd.forwardmove==0);
    idVec3 ordinaryMove(1,0,0); CHECK(BotBrakeAtEdge(&self,ordinaryMove) && ordinaryMove.LengthSqr()==0);
    // A slow outward drift must decelerate without a full-speed reversal.
    self.physics.velocity={40,0,0}; ordinaryMove={1,0,0};
    CHECK(BotBrakeAtEdge(&self,ordinaryMove) && ordinaryMove.x<0 && ordinaryMove.x>=-0.126f);
    self.physics.velocity={};
    self.physics.origin={-200,0,0}; cmd={}; CHECK(!bot.RecoverToNavMesh(&self,cmd));
    CHECK(cmd.forwardmove>0);
    bot.recoverWanderYaw=60; self.deltaViewAngles[1]=-37;
    cmd.angles[1]=ANGLE2SHORT(bot.aimAngles.yaw-self.deltaViewAngles[1]);
    bot.RecoverToNavMesh(&self,cmd);
    const float actualYaw=cmd.angles[1]*360.0f/65536.0f+self.deltaViewAngles[1];
    CHECK(std::fabs(idMath::AngleNormalize180(actualYaw-bot.aimAngles.yaw))<0.006f);
    CHECK(bot.aimAngles.yaw>2 && bot.aimAngles.yaw<3);
    idVec3 forward,right; idAngles(0,actualYaw,0).ToVectors(&forward,&right,nullptr);
    idPhysics_Player physics;
    idVec3 actual=(forward*float(cmd.forwardmove)+right*float(cmd.rightmove))*physics.CmdScale(cmd,false);
    idVec3 checked=lastEdgeProbe-self.physics.origin; actual.Normalize(); checked.Normalize();
    CHECK((actual-checked).Length()<0.005f);
    self.physics.origin={-8,0,0}; self.pfl.onGround=false; cmd={}; bot.RecoverToNavMesh(&self,cmd);
    CHECK(cmd.forwardmove>0); // ground guard cannot take over an airborne recovery
    std::puts("Recovery edge: nearest-node and wander commands brake before void; rescue toward support and broad floor remain usable: PASS");
}
void TestRecoveryTransport() {
    Reset(); idPlayer self; gameLocal.entities[0]=&self;
    moveDirectionSafe=false;
    rvBot bot; bot.recoverSearchTime=gameLocal.time; bot.recoverValid=true;
    bot.recoverTarget={100,0,0}; self.physics.origin={-200,0,0};
    navMesh.volumesAhead={{9,-190}};
    usercmd_t cmd; CHECK(bot.RecoverToNavMesh(&self,cmd));
    CHECK(cmd.forwardmove==0 && cmd.rightmove==0 && cmd.upmove==0);
    // The same guard must cover a wandering bot without a nearest node.
    bot.recoverValid=false; bot.recoverWanderUntil=gameLocal.time+1000; bot.recoverWanderYaw=0;
    cmd={}; CHECK(!bot.RecoverToNavMesh(&self,cmd));
    CHECK(cmd.forwardmove==0 && cmd.rightmove==0 && cmd.upmove==0);
    navMesh.volumesAhead.clear(); cmd={}; bot.RecoverToNavMesh(&self,cmd);
    CHECK(cmd.forwardmove>0); // ordinary supported wandering still moves
    // Nor may the supported-rim override reintroduce an unplanned volume.
    bot.recoverValid=true; bot.recoverTarget={-28,63,0}; self.physics.origin={16,0,0};
    navMesh.recoveryStepClear=true; navMesh.volumesAhead={{9,-100}};
    cmd={}; CHECK(bot.RecoverToNavMesh(&self,cmd));
    CHECK(cmd.forwardmove==0 && cmd.rightmove==0 && cmd.upmove==0);
    std::puts("Off-route recovery: nearest-node, wander and supported-rim commands stop before unplanned transport: PASS");
}
void TestRimSteering() {
    Reset(); idPlayer self; self.physics.origin={-8,0,0};
    navMesh.directionalContinuation=true;
    idVec3 wish(1,0,0);
    CHECK(BotBrakeAtEdge(&self,wish));
    CHECK(std::fabs(wish.x)<0.001f && wish.y>0 && wish.Length()*pm_speed.GetFloat()<=BOT_RECOVER_EDGE_SPEED+0.01f);
    // Neither a gap, a wall nor an unplanned transport can supply the detour.
    navMesh.continuationClear=false; wish={1,0,0};
    CHECK(BotBrakeAtEdge(&self,wish) && wish.LengthSqr()==0);
    navMesh.continuationClear=true; moveDirectionSafe=false; wish={1,0,0};
    CHECK(BotBrakeAtEdge(&self,wish) && wish.LengthSqr()==0);
    moveDirectionSafe=true; navMesh.volumesAhead={{3,-100}}; wish={1,0,0};
    CHECK(BotBrakeAtEdge(&self,wish) && wish.LengthSqr()==0);
    navMesh.volumesAhead.clear(); self.physics.knockback=50; wish={1,0,0};
    CHECK(BotBrakeAtEdge(&self,wish) && wish.LengthSqr()==0);
    self.physics.knockback=0; self.physics.velocity={0,100,0}; wish={1,0,0};
    CHECK(BotBrakeAtEdge(&self,wish) && wish.y<0 && std::fabs(wish.x)<0.001f);
    self.physics.velocity={}; navMesh.valid=false; wish={1,0,0};
    CHECK(BotBrakeAtEdge(&self,wish) && wish.LengthSqr()==0);
    navMesh.valid=true; self.pfl.onGround=false; wish={1,0,0};
    CHECK(!BotBrakeAtEdge(&self,wish) && wish.x==1);
    std::puts("Rim steering: slow supported sidestep, gaps/walls/transports/knocks refused: PASS");
}
void TestSelfKnockback() {
    idPlayer self; self.physics.origin={}; self.physics.velocity={10,0,0};
    botCombatShotModel_t model; navMesh={}; selfBlastExposed=true;
    CHECK(BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    CHECK(navMesh.lastSlideKnock==80 && navMesh.lastSlideVelocity.Compare({10,0,400}));
    navMesh.slideCoastSafe=true;
    CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    self.pfl.onGround=false;
    CHECK(BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    // Linear projection may move the predicted hull out of the blast even
    // though a touchdown will stop it there. The current hull still matters.
    self.physics.velocity={1000,0,-100};
    CHECK(BotCombatSplashThrowsShooter(&self,nullptr,{},model,0.5f));
    CHECK(BotCombatSplashThrowsShooter(&self,nullptr,{500,0,-50},model,0.5f));
    // The far shot remains available in flight; this is a self-blast guard.
    CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{-500,0,0},model,0.5f));
    model.selfKnockback=0.1f;
    CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    model={};
    self.physics.velocity={10,0,0}; self.pfl.onGround=true;
    navMesh.slideCoastSafe=false; model.attackerScale=0.25f; model.selfPushScale=3;
    CHECK(BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    CHECK(navMesh.lastSlideKnock==50 && navMesh.lastSlideVelocity.Compare({10,0,300}));
    model={}; self.physics.velocity={100,0,0};
    CHECK(BotCombatSplashThrowsShooter(&self,nullptr,{50,0,0},model,0.5f));
    CHECK(navMesh.lastSlideOrigin.Compare({50,0,0}) && navMesh.lastSlideVelocity.Compare({100,0,400}));
    model.selfPushScale=0;
    CHECK(BotCombatSplashThrowsShooter(&self,nullptr,{50,0,0},model,0.5f));
    CHECK(navMesh.lastSlideKnock==80 && navMesh.lastSlideVelocity.Compare({100,0,0}));
    navMesh.slideCoastSafe=true;
    CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{50,0,0},model,0.5f));
    navMesh.slideCoastSafe=false; model={}; self.physics.velocity={};
    CHECK(BotCombatSplashThrowsShooter(&self,nullptr,{0,0,55.875f},model,0));
    CHECK(navMesh.lastSlideVelocity.Compare({}) && navMesh.lastSlideKnock>0);
    const int calls=navMesh.slideChecks;
    CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{500,0,0},model,0));
    selfBlastExposed=false; CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    selfBlastExposed=true;
    model={}; model.attackerScale=0; CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    model={}; model.selfKnockback=0.1f; CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    model={}; navMesh.valid=false; CHECK(!BotCombatSplashThrowsShooter(&self,nullptr,{},model,0));
    CHECK(navMesh.slideChecks==calls);
    navMesh={};
    std::puts("Self splash: supported slide, exposure, future pose, exact push scaling and timer: PASS");
}
int main(int argc,char** argv) {
    if(argc==1 || !std::strcmp(argv[1],"movement")) TestRimSteering();
    if(argc==1 || !std::strcmp(argv[1],"selfknock")) TestSelfKnockback();
    if(argc==1 || !std::strcmp(argv[1],"goals")) TestGoals();
    if(argc==1 || !std::strcmp(argv[1],"perception")) TestPerception();
    if(argc==1 || !std::strcmp(argv[1],"hearing")) TestHearing();
    if(argc==1 || !std::strcmp(argv[1],"items")) TestItems();
    if(argc==1 || !std::strcmp(argv[1],"threats")) TestThreats();
    if(argc==1 || !std::strcmp(argv[1],"network")) TestNetwork();
    if(argc==1 || !std::strcmp(argv[1],"pads")) TestPadHold();
    if(argc==1 || !std::strcmp(argv[1],"pads")) TestPadReservation();
    if(argc==1 || !std::strcmp(argv[1],"pads")) TestTransportGuard();
    if(argc==1 || !std::strcmp(argv[1],"pads")) TestUnexpectedPad();
    if(argc==1 || !std::strcmp(argv[1],"landings")) TestAirRecovery();
    if(argc==1 || !std::strcmp(argv[1],"landings")) TestLandingHealth();
    if(argc==1 || !std::strcmp(argv[1],"landings")) TestActualFloorHealth();
    if(argc==1 || !std::strcmp(argv[1],"landings")) TestDropHandoff();
    if(argc==1 || !std::strcmp(argv[1],"landings")) TestJumpEntry();
    if(argc==1 || !std::strcmp(argv[1],"movement")) TestMoveProjection();
    if(argc==1 || !std::strcmp(argv[1],"movement")) TestDodgeOwnership();
    if(argc==1 || !std::strcmp(argv[1],"movement")) TestGroundBraking();
    if(argc==1 || !std::strcmp(argv[1],"movement")) TestRecoveryEdge();
    if(argc==1 || !std::strcmp(argv[1],"movement")) TestRecoveryTransport();
}
