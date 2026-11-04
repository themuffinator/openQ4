// Test doubles surround methods extracted from the actual game sources.
// Observable outcomes, timer boundaries, damage gates, restore ordering and
// target validity are asserted below; gameplay algorithms are not reimplemented.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
static bool Near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
template<class T> static T Max(T a,T b){return std::max(a,b);}
static const char* va(const char* format,...){static char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);return text;}
struct idStr {static int Icmp(const char* a,const char* b){return std::strcmp(a,b);}};
#define MS2SEC(x) ((x)*0.001f)
#define SEC2MS(x) int((x)*1000)
#define DEG2RAD(x) ((x)*0.0174532925199433f)
enum { ANIMCHANNEL_ALL, ANIMCHANNEL_TORSO, ANIMCHANNEL_LEGS, ANIM_NumAnimChannels, ANIM_MaxAnimsPerChannel=3 };
enum { INVALID_JOINT=-1, MOVE_STATUS_DONE=1, MASK_SHOT_RENDERMODEL=2, SND_CHANNEL_ANY=3, SND_CHANNEL_VOICE=4, SND_CHANNEL_VOICE2=5, SND_CHANNEL_DAMAGE=6, PMOD_PROJECTILE_DEATHPUSH=7 };
static int EV_DamageOverTime, EV_DamageOverTimeEffect;
struct idMath {
    static int ClampInt(int lo,int hi,int n){return std::clamp(n,lo,hi);}
    static float ClampFloat(float lo,float hi,float f){return std::clamp(f,lo,hi);}
    static float Cos(float f){return std::cos(f);}
};
struct idVec3 {
    float x=0,y=0,z=0;
    idVec3()=default;
    idVec3(float a,float b,float c):x(a),y(b),z(c){}
    idVec3 operator+(const idVec3& v)const{return {x+v.x,y+v.y,z+v.z};}
    idVec3 operator-(const idVec3& v)const{return {x-v.x,y-v.y,z-v.z};}
    idVec3 operator*(float f)const{return {x*f,y*f,z*f};}
    float operator*(const idVec3& v)const{return x*v.x+y*v.y+z*v.z;}
    float Length()const{return std::sqrt(*this * *this);}
    float Normalize(){const float n=Length();if(n){x/=n;y/=n;z/=n;}return n;}
};
static idVec3 vec3_origin;
struct idDict {
    std::map<std::string,std::string> values;
    const char* GetString(const char* key,const char* fallback="")const{auto i=values.find(key);return i==values.end()?fallback:i->second.c_str();}
    float GetFloat(const char* k,const char* d="0")const{return float(std::atof(GetString(k,d)));}
    int GetInt(const char* k,const char* d="0")const{return std::atoi(GetString(k,d));}
    bool GetBool(const char* k)const{return GetInt(k)!=0;}
    idVec3 GetVector(const char* k,const char* d)const{idVec3 v;std::istringstream(GetString(k,d))>>v.x>>v.y>>v.z;return v;}
    void Set(const char* k,const char* v){values[k]=v;}
};
struct idMaterial {};
struct Declarations {
    std::map<std::string,idMaterial> materials;
    const idMaterial* FindMaterial(const char* name,bool=true){return &materials[name];}
} declarations;
static Declarations* declManager=&declarations;
struct Cvar {bool b=false;bool GetBool()const{return b;}int GetInteger()const{return b?1:0;}};
static Cvar g_perfTest_aiUndying,g_debugVehicle;
struct idAnim {float rate=1;float GetPlaybackRate()const{return rate;}};
struct idAnimBlend {
    float rate=1;idAnim anim;
    void SetPlaybackRate(int,float r){rate=r;}
    float GetPlaybackRate()const{return rate;}
    const idAnim* Anim()const{return &anim;}
};
struct idAnimator {
    float rateMultiplier=1,playbackRateScale=1;
    idAnimBlend channels[ANIM_NumAnimChannels][ANIM_MaxAnimsPerChannel];
    void SetPlaybackRate(float r){rateMultiplier=r;}
    void SetPlaybackRateScale(float);
    idAnimBlend* CurrentAnim(int channel){return &channels[channel][0];}
    int GetJointHandle(const char*){return 0;}
    void StartAnim(int channel,float authored=1){channels[channel][0].rate=authored*rateMultiplier*playbackRateScale;}
};
struct Physics {
    struct Bounds {idVec3 center;idVec3 GetCenter()const{return center;}} bounds;
    idVec3 origin,velocity{100,0,0};
    const Bounds& GetAbsBounds()const{return bounds;}
    const idVec3& GetOrigin()const{return origin;}
    const idVec3& GetLinearVelocity()const{return velocity;}
    void SetLinearVelocity(const idVec3& v){velocity=v;}
};
struct idAI;struct idRestoreGame;struct idSaveGame;
using SavedValue=std::variant<int,float,const idMaterial*,idAI*>;
struct idSaveGame {
    std::vector<SavedValue> values;
    void WriteFloat(float x){values.emplace_back(x);}
    void WriteInt(int x){values.emplace_back(x);}
    void WriteMaterial(const idMaterial* x){values.emplace_back(x);}
};
struct idRestoreGame {
    const std::vector<SavedValue>& values;size_t cursor=0;int version=4;
    explicit idRestoreGame(const idSaveGame& save):values(save.values){}
    int GetOpenQ4SaveGameCompatibilityVersion()const{return version;}
    void ReadFloat(float& x){x=std::get<float>(values.at(cursor++));}
    void ReadInt(int& x){x=std::get<int>(values.at(cursor++));}
    void ReadMaterial(const idMaterial*& x){x=std::get<const idMaterial*>(values.at(cursor++));}
    template<class... T> [[noreturn]] void Error(T...){throw std::runtime_error("corrupt status");}
};
template<class T> struct idEntityPtr {
    T* ptr=nullptr;
    T* GetEntity()const{return ptr && !ptr->removed?ptr:nullptr;}
    T* operator->()const{return GetEntity();}
    idEntityPtr& operator=(T* p){ptr=p;return *this;}
    void Save(idSaveGame* f)const{f->values.emplace_back(GetEntity());}
    void Restore(idRestoreGame* f){ptr=std::get<T*>(f->values.at(f->cursor++));}
};
struct idEntity {
    virtual ~idEntity()=default;
    int health=100,entityNumber=0;bool hidden=false,removed=false;
    struct {bool takedamage=true,notarget=false,quickBurn=false;} fl;
    struct Model {unsigned int GetSurfaceMask(const char* surface)const{return std::string(surface).back()=='l'?1:2;}} model;
    struct {const idMaterial* overlayShader=nullptr;Model* hModel=nullptr;unsigned int suppressSurfaceMask=0;} renderEntity;
    void HideSurface(const char* surface){if(renderEntity.hModel)renderEntity.suppressSurfaceMask|=renderEntity.hModel->GetSurfaceMask(surface);}
    idDict spawnArgs;Physics physics;std::string name="test";
    virtual bool IsType(int type)const{return type==0;}
    Physics* GetPhysics(){return &physics;}const Physics* GetPhysics()const{return &physics;}
    bool IsHidden()const{return hidden;}
    const char* GetName()const{return name.c_str();}
    void UpdateVisuals(){}
    template<class... T> void DamageFeedback(T...){}
    template<class... T> void StartSound(T...){}
    template<class... T> void PostEventMS(T...){}
    template<class... T> void ProcessEvent(T...){}
    template<class... T> void PlayEffect(T...){++effects;}
    template<class... T> void StopEffect(T...){++stops;}
    int effects=0,stops=0;
};
struct idActor : idEntity {
    static constexpr int Type=1;static int GetClassType(){return Type;}
    bool IsType(int type)const override{return type==Type||idEntity::IsType(type);}
    int team=1,kills=0,gibs=0,pains=0,adjustments=0;float damageDealtScale=1,damageTakenScale=1;
    bool gibbed=false,inDamageEvent=false;std::string painType;
    struct Node {idActor* next=nullptr;idActor* Next(){return next;}} teamNode;
    virtual float GetDamageDealtScale()const{return damageDealtScale;}
    virtual float GetDamageTakenScale()const{return damageTakenScale;}
    virtual void AdjustHealthByDamage(int damage){++adjustments;health-=damage;}
    virtual void Killed(idEntity*,idEntity*,int,const idVec3&,int){++kills;}
    virtual int GetDamageForLocation(int damage,int){return damage;}
    void Damage(idEntity*,idEntity*,const idVec3&,const char*,float,int);
    bool SpawnDamageEntity(const idDict*,idEntity*,int){return false;}
    void OnFriendlyFire(idActor*){}
    template<class... T> void Pain(T...){++pains;}
    template<class... T> void Gib(T...){gibbed=true;++gibs;}
    template<class... T> void InitDeathPush(T...){}
};
struct idPlayer : idActor {
    static int GetClassType(){return 3;}
    bool IsType(int type)const override{return type==3||idActor::IsType(type);}
    int GetCurrentWeapon(){return 0;}float PowerUpModifier(int){return 1;}
};
struct idProjectile : idEntity {
    static int GetClassType(){return 4;}
    bool IsType(int type)const override{return type==4||idEntity::IsType(type);}
    int methodOfDeath=0,thinks=0;Physics physicsObj;
    void Think(){++thinks;}
};
struct idAI : idActor {
    static int GetClassType(){return 2;}
    bool IsType(int type)const override{return type==2||idActor::IsType(type);}
    struct {bool undying=false,dead=false,killerGuard=false;} aifl;
    idAnimator animator;
    float freezeFactor=0;int frozenSolidTime=-1,frozenLocation=INVALID_JOINT,disabled=0,movesStopped=0;
    const idMaterial *freezeOverlay=nullptr,*retchBuffOverlay=nullptr,*statusBaseOverlay=nullptr;
    idEntityPtr<idAI> retchBuffOwner;float retchDamageDealt=1,retchDamageTaken=1;
    void AdjustHealthByDamage(int) override;
    bool IsFrozenSolid()const;
    void SetFreezeFactor(float);void SetFreezeOverlay(const char*);void UpdateStatusOverlay();
    bool ApplyRetchBuff(idAI*,float,float,const idMaterial*);void ClearRetchBuff(idAI*);
    void SaveAwakeningState(idSaveGame*)const;void RestoreAwakeningState(idRestoreGame*);
    void AddFreeze(float);void FreezeSolid(int);void UpdateFreeze();
    idAI* GetRetchBuffOwner()const{return retchBuffOwner.GetEntity();}
    float GetDamageDealtScale()const override{return idActor::GetDamageDealtScale()*retchDamageDealt;}
    float GetDamageTakenScale()const override{return idActor::GetDamageTakenScale()*retchDamageTaken;}
    bool DebugActions()const{return false;}
    void DisableAnimState(int){++disabled;}void StopMove(int){++movesStopped;}
    idAnimator* GetAnimator(){return &animator;}
    void Killed(idEntity*,idEntity*,int,const idVec3&,int)override{if(!aifl.dead)++kills;aifl.dead=true;}
    void TurnToward(const idVec3&){}
};
struct stateParms_t {int stage=0,blendFrames=0;};using stateResult_t=int;
struct riMonsterPainLord : idAI {
    bool stunned=false,enraged=false,atProcessingStation=false,interrupted=false;int dudeHealth=100,dudeMaxHealth=100;float maxHealth=100;
    struct Callback {int calls=0;void CallFunc(void*){++calls;}} stunnedFunc,enragedFunc,unstunnedFunc,processingBrokenFunc,processingDoneFunc;
    struct {int threshold=10;} pain;
    const char* GetDamageGroup(int location){return location==1?"dl":location==2?"dr":"head";}
    bool animDone=true;std::string lastAnim;
    void PlayAnim(int,const char* anim,int){lastAnim=anim;}
    bool AnimDone(int,int){return animDone;}void OverrideAnim(int){}
    void AdjustHealthByDamage(int)override;
    bool DudeAttached(char)const;void DetachDude(char);void DamageDude(int,const char*);
    int GetDamageForLocation(int,int)override;void Enrage();void Event_AtProcessingStation();
    stateResult_t State_Torso_Stun(const stateParms_t&);
    stateResult_t State_Torso_ProcessingStation(const stateParms_t&);
};
struct riMonsterValkaryne : idAI {
    bool docked=false,undockRequested=false;
    struct {struct {bool disabled=false;} fl;} actionRangedAttack;
    bool DamageProtected()const;void AdjustHealthByDamage(int)override;
};
struct riMonsterRetch : idAI {
    static int GetClassType(){return 5;}
    bool IsType(int type)const override{return type==5||idAI::IsType(type);}
    idEntityPtr<idAI> buffTarget;bool defenseBuff=false;
    bool ApplyBuff(idAI*);void ClearBuff();bool FindBuffTarget();void Event_RestoreBuff();
    void StopBuffBeam(){}
};
using aiTeam_t=int;
struct AIManager {idActor* allies=nullptr;idActor* enemies=nullptr;idActor* GetAllyTeam(int){return allies;}idActor* GetEnemyTeam(int){return enemies;}} aiManager;
struct trace_t {float fraction=1;struct {int entityNum=-1;} c;};
struct Game {
    int time=1000,msec=16;idEntity worldEntity;idEntity* world=&worldEntity;idPlayer* player=nullptr;
    std::map<std::string,idDict> defs;
    std::vector<idVec3> blockedPoints;
    struct {int RandomInt(int){return 0;}} random;
    int GetTime(){return time;}idPlayer* GetLocalPlayer(){return player;}
    const idDict* FindEntityDefDict(const char* name,bool){auto i=defs.find(name);return i==defs.end()?nullptr:&i->second;}
    template<class... T> [[noreturn]] void Error(T...){throw std::runtime_error("missing def");}
    template<class... T> void Printf(T...){}
    void TracePoint(idEntity*,trace_t& tr,const idVec3&,const idVec3& end,int,idEntity*){
        for(const auto& p:blockedPoints)if((p-end).Length()<0.001f){tr.fraction=0.5f;tr.c.entityNum=999;}
    }
} gameLocal;
template<class... T> void gameDebugLogDamage(T...){}
struct riProjectileSpaceRocket : idProjectile {
    int homingStartTime=0;idEntityPtr<idEntity> target;idVec3 targetOffset,targetPosition;bool hasTargetPosition=false;
    void SetTarget(idEntity*);void SetTargetPosition(const idVec3&);void Think();
};
struct Position {
    idPlayer* driver=nullptr;idVec3 eye;idVec3 axes[3]={{1,0,0},{0,1,0},{0,0,1}};
    idPlayer* GetDriver(){return driver;}idVec3 GetEyeOrigin(){return eye;}const idVec3* GetEyeAxis(){return axes;}
};
struct rvVehicleWeapon {
    int count=1,jointIndex=0,fired=0;bool failFire=false;
    bool Fire(){if(failFire)return false;fired+=count;return true;}
};
struct riVehicleCockpitWeapon : rvVehicleWeapon {
    Position ownPosition;Position* position=&ownPosition;idEntityPtr<idEntity> parent;
    idActor* FindEnemyNearCrosshair(float,float,bool&)const;
};
enum { CROSSHAIR_FRIENDLY,CROSSHAIR_OUT_OF_RANGE,CROSSHAIR_DEFAULT,CROSSHAIR_LOCKED,CROSSHAIR_ACQUIRING };
struct Guide {idEntity* target=nullptr;void Update(const idDict&,idEntity* p,idPlayer*){target=p;}void Stop(){target=nullptr;}};
struct riVCWMissileTurret : riVehicleCockpitWeapon {
    enum lockState_t {LOCK_NONE,LOCK_ACQUIRING,LOCK_LOCKED};
    lockState_t lockState=LOCK_NONE;idEntityPtr<idActor> lockTarget;
    int lockStartTime=0,timeToHardLock=250,crosshair=CROSSHAIR_DEFAULT;float lockRange=14000;
    Guide closestGuide,lockedGuide,acquiringGuide,outOfRangeGuide;idDict spawnArgs;idVec3 aimPoint;
    static const int MAGAZINE_SIZE=5;
    int rocketsLoaded=0,nextLoadTime=0,loadDelay=500;
    const idVec3* launchAxis=nullptr;
    std::vector<std::string> events,sounds;
    void SetLockState(lockState_t);void UpdateTargeting();
    void SetRocketsLoaded(int);void UpdateMagazine();bool Fire();
    idPlayer* GetPlayerDriver(){return position->driver;}
    idEntity* GetAimedEntity(float,idVec3&){return nullptr;}bool IsFriendly(idEntity*){return false;}
    void StopGuides(){closestGuide.Stop();lockedGuide.Stop();acquiringGuide.Stop();outOfRangeGuide.Stop();}
    const char* GetClassname(){return "turret";}
    void PlaySound(const char* s){sounds.push_back(s);}void SendGuiEvent(const char* s){events.push_back(s);}
};
enum {WP_OUTOFAMMO,WP_READY,SRESULT_DONE=2,SRESULT_WAIT=3,SRESULT_ERROR=4};
static int SRESULT_STAGE(int stage){return stage+10;}
struct WeaponNapalmGun {
    int ammo=0,status=-1,clipSize=0,clip=0,nextAttackTime=0;bool zoomed=false;
    struct {bool zoom=false,attack=false,reload=false,netReload=false,lowerWeapon=false;} wsfl;
    std::string state;
    int AmmoAvailable(){return ammo;}int ClipSize(){return clipSize;}int AmmoInClip(){return clip;}
    bool AutoReload(){return true;}void SetStatus(int s){status=s;}
    void PlayCycle(int,const char*,int){}void SetState(const char* s,int){state=s;}
    stateResult_t State_Idle(const stateParms_t&);
};

// @PRODUCTION_CONSTANTS@
// @PRODUCTION_METHODS@

static void Damage(idAI& ai,const char* def,float scale=1){ai.Damage(nullptr,nullptr,{0,0,1},def,scale,7);}
static void Reset(){gameLocal.time=1000;gameLocal.msec=16;gameLocal.blockedPoints.clear();g_perfTest_aiUndying.b=false;gameLocal.player=nullptr;}
static float Torso(idAI& ai){return ai.animator.CurrentAnim(ANIMCHANNEL_TORSO)->GetPlaybackRate();}
int main(){
    gameLocal.defs["freeze"].values={{"damage","10"},{"filter_freeze","1"}};
    gameLocal.defs["ordinary"].values={{"damage","10"}};
    gameLocal.defs["damage_freezegib"].values={{"damage","100"},{"gib","1"}};
    // Lethal damage and a one-HP target enter ice once; repeated ice hits must
    // not restart the shatter timer. The actual actor damage function runs.
    for(int hp:{1,5,11}){Reset();idAI ai;ai.health=hp;Damage(ai,"freeze");CHECK(ai.health==1 && ai.IsFrozenSolid());
        CHECK(ai.adjustments==1 && ai.disabled==2 && ai.frozenLocation==7 && Near(Torso(ai),0.001f));
        gameLocal.time+=1000;Damage(ai,"freeze");CHECK(ai.frozenSolidTime==1000 && ai.disabled==2);
        gameLocal.time=4999;ai.UpdateFreeze();CHECK(ai.kills==0);
        gameLocal.time=5000;ai.UpdateFreeze();CHECK(ai.kills==1 && ai.gibs==1 && !ai.IsFrozenSolid());
        ai.UpdateFreeze();CHECK(ai.kills==1 && ai.gibs==1);
    }
    Reset();{idAI ai;gameLocal.time=0;ai.health=1;Damage(ai,"freeze");CHECK(ai.IsFrozenSolid());}
    Reset();{idAI ai;ai.health=1;ai.aifl.undying=true;Damage(ai,"freeze");CHECK(ai.health==1&&!ai.IsFrozenSolid());}
    Reset();{idAI ai;ai.health=5;g_perfTest_aiUndying.b=true;Damage(ai,"freeze");CHECK(ai.health==1&&!ai.IsFrozenSolid());}
    Reset();{riMonsterPainLord ai;ai.stunned=true;ai.health=1;Damage(ai,"freeze");CHECK(ai.health==1&&!ai.IsFrozenSolid());
        ai.stunned=false;ai.atProcessingStation=true;Damage(ai,"freeze");CHECK(ai.health==1&&!ai.IsFrozenSolid());}
    // A lethal body hit cannot bypass either authored shield phase. Stun
    // callbacks fire once, and stale interruptions cannot cancel a new stun.
    Reset();{riMonsterPainLord ai;ai.health=ai.maxHealth=8000;ai.interrupted=true;
        ai.AdjustHealthByDamage(100000);CHECK(ai.health==5333&&ai.stunned&&ai.stunnedFunc.calls==1&&!ai.interrupted);
        ai.AdjustHealthByDamage(100000);CHECK(ai.health==5333&&ai.stunnedFunc.calls==1);
        ai.stunned=false;ai.AdjustHealthByDamage(100000);CHECK(ai.health==2666&&ai.stunnedFunc.calls==2);
        ai.stunned=false;ai.enraged=true;ai.AdjustHealthByDamage(3000);CHECK(ai.health<0&&ai.stunnedFunc.calls==2);}
    Reset();{riMonsterPainLord ai;ai.aifl.undying=true;ai.AdjustHealthByDamage(10000);
        CHECK(ai.health==100&&!ai.stunned&&ai.stunnedFunc.calls==0);}
    // A detached captive cannot drain the survivor's shield through its old
    // collision joints. The existing saved render mask is authoritative.
    for(char first:{'l','r'}){Reset();riMonsterPainLord ai;ai.renderEntity.hModel=&ai.model;ai.stunned=true;
        const char* side=first=='l'?"left":"right";const char* other=first=='l'?"right":"left";
        ai.DamageDude(51,side);CHECK(ai.dudeHealth==49&&!ai.DudeAttached(first)&&ai.interrupted&&ai.effects==1);
        ai.interrupted=false;ai.DamageDude(20,side);CHECK(ai.dudeHealth==49&&!ai.interrupted);
        CHECK(ai.GetDamageForLocation(10,first=='l'?1:2)==0&&ai.dudeHealth==49);
        ai.DamageDude(49,other);CHECK(ai.dudeHealth==0&&ai.enraged&&!ai.stunned&&ai.effects==2);
        CHECK(!ai.DudeAttached('l')&&!ai.DudeAttached('r')&&ai.enragedFunc.calls==1);
        CHECK(ai.GetDamageForLocation(10,1)==0&&ai.GetDamageForLocation(10,2)==0);
        CHECK(ai.GetDamageForLocation(10,0)==10); // Visible body still takes damage.
        ai.DamageDude(100,other);CHECK(ai.effects==2&&ai.enragedFunc.calls==1);}
    Reset();{riMonsterPainLord ai;ai.renderEntity.hModel=&ai.model;ai.DamageDude(1000,"left");
        CHECK(ai.dudeHealth==0&&ai.effects==2&&ai.enraged&&ai.enragedFunc.calls==1);}
    Reset();{riMonsterPainLord ai;ai.DamageDude(-10,"left");CHECK(ai.dudeHealth==100);
        ai.atProcessingStation=true;ai.DamageDude(26,"left");CHECK(ai.dudeHealth==74&&ai.interrupted);
        ai.Event_AtProcessingStation();CHECK(ai.atProcessingStation&&!ai.interrupted);
        ai.atProcessingStation=false;ai.health=0;ai.Event_AtProcessingStation();CHECK(!ai.atProcessingStation);}
    for(bool rage:{false,true}){riMonsterPainLord ai;ai.stunned=true;ai.interrupted=true;ai.enraged=rage;
        CHECK(ai.State_Torso_Stun({2,0})==SRESULT_STAGE(4));
        CHECK(ai.lastAnim==(rage?"stunned_end_rage":"stunned_end")&&!ai.interrupted&&!ai.stunned&&ai.unstunnedFunc.calls==1);}
    for(bool interrupt:{false,true}){riMonsterPainLord ai;ai.atProcessingStation=true;ai.animDone=false;
        CHECK(ai.State_Torso_ProcessingStation({0,4})==SRESULT_STAGE(1)&&ai.lastAnim=="console_idle");
        CHECK(ai.State_Torso_ProcessingStation({1,4})==SRESULT_WAIT&&ai.processingDoneFunc.calls==0);
        ai.interrupted=interrupt;ai.animDone=true;
        CHECK(ai.State_Torso_ProcessingStation({1,4})==SRESULT_DONE&&!ai.atProcessingStation);
        CHECK(ai.processingBrokenFunc.calls==int(interrupt)&&ai.processingDoneFunc.calls==int(!interrupt));}
    Reset();{riMonsterValkaryne ai;ai.docked=true;ai.health=1;Damage(ai,"freeze");CHECK(ai.health==1&&!ai.IsFrozenSolid());}
    Reset();{riMonsterValkaryne ai;ai.health=100;ai.undockRequested=true;
        ai.AdjustHealthByDamage(20);CHECK(ai.health==100);
        ai.undockRequested=false;ai.spawnArgs.Set("requestDocking","dock");ai.actionRangedAttack.fl.disabled=true;
        ai.AdjustHealthByDamage(20);CHECK(ai.health==100);
        ai.spawnArgs.Set("openq4_dockingComplete","1");ai.AdjustHealthByDamage(20);CHECK(ai.health==80);}
    Reset();{idAI ai;ai.health=1;ai.damageTakenScale=0;Damage(ai,"freeze");CHECK(ai.health==1&&!ai.IsFrozenSolid());}
    Reset();{idPlayer player;gameLocal.player=&player;idAI ally;ally.health=1;
        ally.Damage(&player,&player,{0,0,1},"freeze",1,7);CHECK(ally.health==1&&!ally.IsFrozenSolid());}

    // Chill scales every active blend, preserves authored speeds and the
    // Pain Lord's base multiplier, and affects new animations before thaw.
    Reset();{idAI ai;ai.animator.SetPlaybackRate(1.75f);ai.animator.StartAnim(ANIMCHANNEL_TORSO,1.2f);
        ai.animator.StartAnim(ANIMCHANNEL_LEGS);ai.AddFreeze(0.8f);CHECK(Near(Torso(ai),2.1f*0.28f));
        ai.animator.StartAnim(ANIMCHANNEL_ALL);CHECK(Near(ai.animator.channels[0][0].rate,1.75f*0.28f));
        gameLocal.msec=3000;ai.UpdateFreeze();CHECK(ai.freezeFactor==0&&Near(Torso(ai),2.1f));
        CHECK(Near(ai.animator.channels[0][0].rate,1.75f)&&ai.animator.rateMultiplier==1.75f);
    }
    // Ownership survives a shader change, and removing one effect leaves the
    // other effect plus any original overlay/damage modifiers intact.
    Reset();{riMonsterRetch retch,other;idAI ally;idMaterial original;
        retch.defenseBuff=true;retch.spawnArgs.Set("defenseBoostMult","0.25");ally.damageTakenScale=0.5f;
        ally.renderEntity.overlayShader=&original;retch.buffTarget=&ally;CHECK(retch.ApplyBuff(&ally));
        CHECK(Near(ally.GetDamageTakenScale(),0.125f));ally.AddFreeze(0.8f);ally.UpdateFreeze();
        const idMaterial* ice=ally.renderEntity.overlayShader;
        CHECK(!other.ApplyBuff(&ally));other.buffTarget=&ally;other.ClearBuff();CHECK(ally.GetRetchBuffOwner()==&retch);
        retch.ClearBuff();CHECK(ally.renderEntity.overlayShader==ice&&Near(ally.GetDamageTakenScale(),0.5f));
        gameLocal.msec=4000;ally.UpdateFreeze();CHECK(ally.renderEntity.overlayShader==&original);
        CHECK(retch.ApplyBuff(&ally));ally.AddFreeze(0.8f);ally.UpdateFreeze();gameLocal.msec=4000;ally.UpdateFreeze();
        CHECK(ally.renderEntity.overlayShader==declManager->FindMaterial(RETCH_DEFENSE_OVERLAY));
        retch.spawnArgs.Set("defenseBoostMult","0.75");CHECK(retch.ApplyBuff(&ally));
        CHECK(Near(ally.GetDamageTakenScale(),0.125f));
    }
    // v4 round trips the actual field writer/reader without advancing its
    // clock or applying slowdown twice. Also reject malformed state.
    Reset();{riMonsterRetch owner;idAI original,restored;original.health=1;
        CHECK(original.ApplyRetchBuff(&owner,2,1,declManager->FindMaterial(RETCH_DAMAGE_OVERLAY)));
        Damage(original,"freeze");gameLocal.time+=750;
        idSaveGame save;original.SaveAwakeningState(&save);restored.animator=original.animator;restored.health=1;restored.spawnArgs=original.spawnArgs;
        idRestoreGame reader(save);restored.RestoreAwakeningState(&reader);
        CHECK(reader.cursor==save.values.size()&&restored.frozenSolidTime==1000&&restored.frozenLocation==7);
        CHECK(restored.GetRetchBuffOwner()==&owner&&restored.GetDamageDealtScale()==2&&Near(Torso(restored),0.001f));
        CHECK(restored.renderEntity.overlayShader==declManager->FindMaterial(AI_FROZEN_OVERLAY));
        gameLocal.time=5000;restored.UpdateFreeze();CHECK(restored.kills==1&&restored.gibs==1);
        save.values[0]=2.0f;idRestoreGame bad(save);bool rejected=false;try{restored.RestoreAwakeningState(&bad);}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
    }
    Reset();{idAI ai;ai.health=1;ai.spawnArgs.Set("gib","1");ai.renderEntity.overlayShader=declManager->FindMaterial(AI_FROZEN_OVERLAY);
        ai.animator.channels[ANIMCHANNEL_TORSO][0].rate=0.001f;idSaveGame empty;idRestoreGame old(empty);old.version=3;
        ai.RestoreAwakeningState(&old);CHECK(ai.IsFrozenSolid()&&Near(Torso(ai),0.001f)&&old.cursor==0);
        gameLocal.time+=4000;ai.UpdateFreeze();CHECK(ai.kills==1&&ai.gibs==1);
    }
    Reset();{idAI ai;ai.renderEntity.overlayShader=declManager->FindMaterial(AI_FREEZE_OVERLAY);
        ai.animator.channels[ANIMCHANNEL_TORSO][0].rate=0.26f;idSaveGame empty;idRestoreGame old(empty);old.version=3;
        ai.RestoreAwakeningState(&old);CHECK(!ai.IsFrozenSolid()&&Near(Torso(ai),1)&&ai.renderEntity.overlayShader==nullptr);
    }
    Reset();{riMonsterRetch first,second;idAI ally;aiManager.allies=&ally;ally.AddFreeze(0.8f);ally.UpdateFreeze();
        CHECK(first.FindBuffTarget());CHECK(!second.FindBuffTarget());first.Event_RestoreBuff();CHECK(ally.GetRetchBuffOwner()==&first);
        CHECK(ally.GetDamageDealtScale()==1);first.ClearBuff();CHECK(second.FindBuffTarget());
        first.buffTarget=&ally;first.Event_RestoreBuff();CHECK(ally.GetRetchBuffOwner()==&second&&first.buffTarget.GetEntity()==nullptr);
    }
    // Offset belongs to the actor, with no rotation, even if the rocket's
    // definition carries a different value. Switching modes drops old targets.
    Reset();{idAI target;target.physics.bounds.center={1000,0,100};target.spawnArgs.Set("projectileTargetOffset","0 0 350");
        riProjectileSpaceRocket rocket;rocket.spawnArgs.Set("projectileTargetOffset","0 0 -900");rocket.SetTarget(&target);
        CHECK(Near(AwakeningProjectileTargetPoint(&target).z,450));rocket.Think();CHECK(rocket.physicsObj.velocity.z>0);
        CHECK(Near(rocket.physicsObj.velocity.Length(),100));rocket.SetTargetPosition({10000,0,0});CHECK(rocket.target.GetEntity()==nullptr);
        rocket.SetTarget(&target);CHECK(!rocket.hasTargetPosition);rocket.SetTarget(nullptr);CHECK(rocket.target.GetEntity()==nullptr);
    }
    // A distant centered enemy cannot mask a nearer off-center one. Walls,
    // dead/hidden/notarget actors and cone/range edges determine eligibility.
    Reset();{idPlayer player;riVCWMissileTurret gun;gun.position->driver=&player;
        idAI far,near;far.entityNumber=10;near.entityNumber=11;far.physics.bounds.center={20000,0,0};near.physics.bounds.center={10000,1000,0};
        far.teamNode.next=&near;aiManager.enemies=&far;bool out=true;
        CHECK(gun.FindEnemyNearCrosshair(30,14000,out)==&near&&!out);
        gun.UpdateTargeting();CHECK(gun.lockState==gun.LOCK_ACQUIRING&&gun.lockTarget.GetEntity()==&near);
        gameLocal.time+=249;gun.UpdateTargeting();CHECK(gun.lockState==gun.LOCK_ACQUIRING);
        ++gameLocal.time;gun.UpdateTargeting();CHECK(gun.lockState==gun.LOCK_LOCKED&&gun.lockedGuide.target==&near);
        gameLocal.blockedPoints.push_back(near.physics.bounds.center);gun.UpdateTargeting();CHECK(gun.lockState==gun.LOCK_NONE&&gun.lockTarget.GetEntity()==nullptr);
        CHECK(gun.outOfRangeGuide.target==&far);gameLocal.blockedPoints.push_back(far.physics.bounds.center);
        CHECK(gun.FindEnemyNearCrosshair(30,14000,out)==nullptr&&!out);
        gameLocal.blockedPoints.clear();near.health=0;CHECK(gun.FindEnemyNearCrosshair(30,14000,out)==&far&&out);
        near.health=100;near.hidden=true;CHECK(gun.FindEnemyNearCrosshair(30,14000,out)==&far&&out);
        near.hidden=false;near.fl.notarget=true;CHECK(gun.FindEnemyNearCrosshair(30,14000,out)==&far&&out);
        near.fl.notarget=false;near.physics.bounds.center={14000,0,0};CHECK(gun.FindEnemyNearCrosshair(30,14000,out)==&near&&!out);
        near.physics.bounds.center={0,10000,0};CHECK(gun.FindEnemyNearCrosshair(30,0,out)==&far&&!out);
        gun.position->driver=nullptr;CHECK(gun.FindEnemyNearCrosshair(30,14000,out)==nullptr&&!out);
    }
    // Five loading indicators describe real rockets. Empty/acquiring attempts
    // must not fire or reset a pending reload; partial/full charges fire as shown.
    Reset();{gameLocal.time=0;riVCWMissileTurret gun;
        CHECK(!gun.Fire()&&gun.fired==0&&gun.sounds.empty());
        gun.UpdateMagazine();CHECK(gun.rocketsLoaded==1);
        gameLocal.time=499;gun.UpdateMagazine();CHECK(gun.rocketsLoaded==1);
        gameLocal.time=500;gun.UpdateMagazine();CHECK(gun.rocketsLoaded==2);
        gun.lockState=gun.LOCK_ACQUIRING;CHECK(!gun.Fire()&&gun.rocketsLoaded==2);
        gun.lockState=gun.LOCK_LOCKED;gun.failFire=true;
        CHECK(!gun.Fire()&&gun.rocketsLoaded==2&&gun.count==1);
        gun.failFire=false;CHECK(gun.Fire()&&gun.fired==2&&gun.rocketsLoaded==0&&gun.count==1);
        CHECK(gun.events.back()=="rocketEmpty");CHECK(!gun.Fire()&&gun.fired==2);
        gameLocal.time=3000;gun.UpdateMagazine();CHECK(gun.rocketsLoaded==5);
        CHECK(gun.events.back()=="fifthRocketLoaded");
        gun.lockState=gun.LOCK_NONE;CHECK(gun.Fire()&&gun.fired==7&&gun.rocketsLoaded==0);
        gun.loadDelay=0;gun.nextLoadTime=gameLocal.time;gun.UpdateMagazine();CHECK(gun.rocketsLoaded==5);
    }
    // Both firing modes and both ammo models expose ready/out-of-ammo correctly.
    for(bool alt:{false,true})for(int ammo:{0,1,10})for(int clip:{0,8}){
        WeaponNapalmGun gun;gun.wsfl.zoom=alt;gun.ammo=ammo;gun.clipSize=clip;gun.clip=ammo;
        CHECK(gun.State_Idle({0,0})==SRESULT_STAGE(1));CHECK(gun.status==(ammo?WP_READY:WP_OUTOFAMMO));
    }
    std::printf("Awakening status/targeting: %d behavioral checks passed\n",checks);
}
