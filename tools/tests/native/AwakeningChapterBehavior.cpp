// Production methods are injected by awakening_chapter_behavior.py.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

template<class T> T Max(T a, T b) { return std::max(a,b); }
struct idVec3 { float x=0,y=0,z=0; };
struct idMath {
    static float ClampFloat(float lo,float hi,float v) { return std::clamp(v,lo,hi); }
};
struct idDict {
    std::map<std::string,float> numbers;
    std::map<std::string,std::string> strings;
    const char* GetString(const char* key) const {
        auto i=strings.find(key);return i==strings.end()?"":i->second.c_str();
    }
    idVec3 radius{320,320,320};
    int GetInt(const char* key,const char* fallback="0") const { return int(GetFloat(key,fallback)); }
    float GetFloat(const char* key,const char* fallback="0") const {
        auto i=numbers.find(key);return i==numbers.end()?std::stof(fallback):i->second;
    }
    bool GetBool(const char* key) const { return GetInt(key)!=0; }
    idVec3 GetVector(const char*,const char*) const { return radius; }
};
struct idEntity {
    virtual ~idEntity()=default;
    idDict spawnArgs;
    int health=100;
    virtual bool IsType(int type) const { return type==0; }
};
struct idLight : idEntity {
    idVec3 radius{320,320,320};
    static int GetClassType() { return 1; }
    bool IsType(int type) const override { return type==1; }
    idVec3 GetRadius() const { return radius; }
    void SetRadiusXYZ(float x,float y,float z) { radius={x,y,z}; }
};
struct EntityRef { idEntity* e; idEntity* GetEntity() const { return e; } };
struct Targets : std::vector<EntityRef> {
    using std::vector<EntityRef>::operator=;
    int Num() const { return int(size()); }
};
struct Game {
    int time=100;
    std::map<std::string,idDict> damageDefs;
    int warnings=0;
    const idDict* FindEntityDefDict(const char* key,bool) {
        auto i=damageDefs.find(key);return i==damageDefs.end()?nullptr:&i->second;
    }
    void Warning(const char*,const char*) { ++warnings; }
} gameLocal;
struct idTrigger : idEntity {
    void Damage(idEntity*,idEntity*,const idVec3&,const char* name,float,int) {
        health-=gameLocal.FindEntityDefDict(name,false)->GetInt("damage");
    }
};
constexpr int CONTENTS_TRIGGER=4096,CONTENTS_PROJECTILE=512;
struct Physics { int contents=0; void SetContents(int v) { contents=v; } };
struct riFireFX : idTrigger {
    enum { FIRE_LARGE,FIRE_SMALL,FIRE_OUT };
    int stage=FIRE_LARGE;
    bool burning=true;
    Physics physics;
    Targets targets;
    int starts[3]={0,0,0},stops=0,activations=0;
    Physics* GetPhysics() { return &physics; }
    void StartStageEffect() { ++starts[stage]; }
    void StopEffect(const char*) { ++stops; }
    void StopStageEffects() { ++stops; }
    void ActivateTargetsWhenOut() { ++activations; }
    void Damage(idEntity*,idEntity*,const idVec3&,const char*,float,int);
    void UpdateContents();
    void ShrinkTargetLights(int);
    void hit(const char* def) { Damage(nullptr,nullptr,{},def,1,-1); }
};

struct idStr { static int Icmp(const char* a,const char* b) { return std::strcmp(a,b); } };
constexpr int ANIMCHANNEL_ALL=0,SRESULT_WAIT=1;
using stateResult_t=int;
struct stateParms_t {};
struct idAnim {
    std::string name;
    const char* Name() const { return name.c_str(); }
};
struct AnimBlend {
    idAnim anim;
    const idAnim* Anim() const { return anim.name.empty()?nullptr:&anim; }
};
struct Animator {
    AnimBlend blend;
    int starts=0;
    AnimBlend* CurrentAnim(int) { return &blend; }
    int GetAnim(const char* name) const { return std::string(name)=="idle"?1:2; }
    void CycleAnim(int,int anim,int,int) { blend.anim.name=anim==1?"idle":"scan"; ++starts; }
};
struct riMonsterTurret {
    idDict spawnArgs;
    struct { idEntity* ent=nullptr; } enemy;
    bool visible=false;
    int searches=0,faces=0,actions=0;
    Animator animator;
    void CheckForEnemy(bool) { ++searches; }
    bool IsEnemyVisible() const { return enemy.ent && visible; }
    void FaceEnemy() { ++faces; }
    void UpdateAction() { ++actions; }
    stateResult_t State_Combat(const stateParms_t&);
};

// @PRODUCTION_METHODS@

int main() {
    gameLocal.damageDefs["ordinary"].numbers={{"damage",30}};
    gameLocal.damageDefs["freeze"].numbers={{"damage",30},{"filter_freeze",1}};
    gameLocal.damageDefs["overkill"].numbers={{"damage",400},{"filter_freeze",1}};
    idLight light;
    light.spawnArgs.radius=light.radius={90,120,150};
    riFireFX fire;
    fire.targets={{nullptr},{&light}};
    fire.UpdateContents();
    assert(fire.physics.contents==(CONTENTS_TRIGGER|CONTENTS_PROJECTILE));
    fire.hit("ordinary");
    assert(fire.health==100 && light.radius.x==90);
    fire.hit("freeze");
    assert(fire.health==70 && fire.stage==riFireFX::FIRE_LARGE);
    assert(std::abs(light.radius.x-63)<0.001f);
    fire.hit("freeze");
    assert(fire.health==40 && fire.starts[riFireFX::FIRE_SMALL]==1);
    fire.hit("overkill");
    assert(fire.stage==riFireFX::FIRE_OUT && fire.activations==1);
    assert(fire.starts[riFireFX::FIRE_OUT]==1 && fire.physics.contents==0);
    assert(light.radius.x==0 && light.radius.y==0 && light.radius.z==0);
    fire.hit("freeze");
    assert(fire.activations==1 && fire.starts[riFireFX::FIRE_OUT]==1);
    riFireFX disabled;
    disabled.burning=false;
    disabled.UpdateContents();
    disabled.hit("freeze");
    assert(disabled.health==100 && disabled.physics.contents==0);
    riFireFX instant;
    instant.hit("overkill");
    assert(instant.starts[riFireFX::FIRE_SMALL]==0 && instant.activations==1);
    idLight shared;
    shared.spawnArgs.numbers["fireCount"]=2;
    riFireFX a,b;
    a.targets={{&shared}}; b.targets={{&shared}};
    a.hit("overkill");
    assert(shared.radius.x==160); // overkill consumes only this fire's share
    b.hit("overkill");
    assert(shared.radius.x==0);
    disabled.hit("missing");
    assert(gameLocal.warnings==1);
    std::puts("PASS: freeze filtering, fractional light dimming, shared lights, overkill, stages, one-shot targets, active/off/out collision");
    riMonsterTurret camera;
    camera.spawnArgs.strings["scanAnim"]="scan";
    camera.enemy.ent=&light; // teammate supplied a distant, unseen target before wakeup
    assert(camera.State_Combat({})==SRESULT_WAIT);
    assert(camera.animator.blend.anim.name=="scan" && camera.faces==0 && camera.actions==0);
    for(int i=0;i<100;++i) camera.State_Combat({});
    assert(camera.animator.starts==1); // the sweep must be allowed to advance
    camera.visible=true;
    camera.State_Combat({});
    assert(camera.animator.blend.anim.name=="idle" && camera.faces==1 && camera.actions==1);
    camera.State_Combat({});
    assert(camera.animator.starts==2);
    camera.visible=false;
    camera.State_Combat({});
    assert(camera.animator.blend.anim.name=="scan" && camera.faces==2);
    camera.enemy.ent=nullptr;
    camera.State_Combat({});
    assert(camera.searches==1 && camera.animator.starts==3);
    riMonsterTurret visibleOnWake;
    visibleOnWake.spawnArgs.strings["scanAnim"]="scan";
    visibleOnWake.enemy.ent=&light; visibleOnWake.visible=true;
    visibleOnWake.State_Combat({});
    assert(visibleOnWake.animator.blend.anim.name=="idle");
    riMonsterTurret combatTurret; // no scanAnim: retain the normal turret path
    combatTurret.State_Combat({});
    assert(combatTurret.searches==1 && combatTurret.faces==1 && combatTurret.actions==1);
    assert(combatTurret.animator.starts==0);
    std::puts("PASS: camera wake pose, uninterrupted sweep, visible/occluded/lost target transitions, normal turret control");
}
