// Exercise the production live-player sweep's filtering and nearest-hit rules.
// The collision service supplies fractions; stock-map replays qualify geometry.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
struct idVec3 {
    float v[3];
    idVec3(float x=0,float y=0,float z=0):v{x,y,z} {}
    float operator[](int i) const { return v[i]; }
    float& operator[](int i) { return v[i]; }
    idVec3 operator+(const idVec3& b) const { return {v[0]+b[0],v[1]+b[1],v[2]+b[2]}; }
    idVec3 operator-(const idVec3& b) const { return {v[0]-b[0],v[1]-b[1],v[2]-b[2]}; }
    idVec3 operator*(float s) const { return {v[0]*s,v[1]*s,v[2]*s}; }
};
struct idBounds {
    idVec3 p[2];
    void FromBoundsTranslation(const idBounds& b,const idVec3& origin,int,const idVec3& delta) {
        for(int i=0;i<3;i++) { p[0][i]=origin[i]+b.p[0][i]+std::min(0.f,delta[i]); p[1][i]=origin[i]+b.p[1][i]+std::max(0.f,delta[i]); }
    }
    bool IntersectsBounds(const idBounds& b) const {
        for(int i=0;i<3;i++) if(p[0][i]>b.p[1][i] || p[1][i]<b.p[0][i]) return false;
        return true;
    }
};
constexpr int MASK_PLAYERSOLID=1,mat3_identity=0;
int navAgentTrace=0;
idBounds navAgentBounds{{{-16,-16,0},{16,16,77}}};
struct trace_t { float fraction=1; idVec3 endpos; struct { int entityNum=-1; } c; };
struct Collision { float fraction=.3f; };
struct idClipModel {
    Collision model;
    bool present=true;
    int contents=MASK_PLAYERSOLID;
    idBounds bounds{{{20,-16,0},{60,16,77}}};
    const Collision* GetCollisionModel() const { return present ? &model : nullptr; }
    int GetContents() const { return contents; }
    const idBounds& GetAbsBounds() const { return bounds; }
    idVec3 GetOrigin() const { return {}; }
    int GetAxis() const { return 0; }
};
struct Physics { idClipModel clip; const idClipModel* GetClipModel() const { return &clip; } };
struct idEntity {
    int entityNumber=0,instance=0,kind=1;
    Physics physics;
    bool IsType(int k) const { return kind==k; }
    int GetInstance() const { return instance; }
    const Physics* GetPhysics() const { return &physics; }
};
struct idPlayer:idEntity { int health=100; bool spectating=false; static int GetClassType() { return 1; } };
struct { int numClients=3; idEntity* entities[3]={}; } gameLocal;
float worldFraction=1;
bool NavAgentTranslation(trace_t& t,const idVec3& from,const idVec3& to) {
    t.fraction=worldFraction; t.endpos=from+(to-from)*worldFraction; t.c.entityNum=4094;
    return t.fraction>=1;
}
struct CollisionManager {
    int calls=0;
    void Translation(trace_t* t,const idVec3& from,const idVec3& to,const int*,int,int,
                     const Collision* model,const idVec3&,int) {
        ++calls; t->fraction=model->fraction; t->endpos=from+(to-from)*t->fraction;
    }
} collisionManager;
auto* collisionModelManager=&collisionManager;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
// PRODUCTION_FUNCTION
int main() {
    idPlayer self,near,far;
    self.entityNumber=0; near.entityNumber=1; far.entityNumber=2;
    self.physics.clip.model.fraction=0; far.physics.clip.model.fraction=.6f;
    gameLocal.entities[0]=&self; gameLocal.entities[1]=&near; gameLocal.entities[2]=&far;
    trace_t t; const idVec3 start,finish(100,0,0);
    CHECK(NavRecoveryTranslation(t,start,finish,nullptr) && collisionManager.calls==0);
    CHECK(!NavRecoveryTranslation(t,start,finish,&self) && t.fraction==.3f && t.c.entityNum==1);
    worldFraction=.2f;
    CHECK(!NavRecoveryTranslation(t,start,finish,&self) && t.fraction==.2f && t.c.entityNum==4094);
    worldFraction=0; const int before=collisionManager.calls;
    CHECK(!NavRecoveryTranslation(t,start,finish,&self) && collisionManager.calls==before);
    worldFraction=1; far.spectating=true;
    near.spectating=true; CHECK(NavRecoveryTranslation(t,start,finish,&self)); near.spectating=false;
    near.health=0; CHECK(NavRecoveryTranslation(t,start,finish,&self)); near.health=100;
    near.instance=1; CHECK(NavRecoveryTranslation(t,start,finish,&self)); near.instance=0;
    near.kind=2; CHECK(NavRecoveryTranslation(t,start,finish,&self)); near.kind=1;
    near.physics.clip.contents=0; CHECK(NavRecoveryTranslation(t,start,finish,&self)); near.physics.clip.contents=1;
    near.physics.clip.present=false; CHECK(NavRecoveryTranslation(t,start,finish,&self)); near.physics.clip.present=true;
    near.physics.clip.bounds={{{20,200,0},{60,230,77}}};
    CHECK(NavRecoveryTranslation(t,start,finish,&self));
    gameLocal.entities[1]=nullptr; CHECK(NavRecoveryTranslation(t,start,finish,&self));
    std::puts("Live recovery traffic: own hull, instance, spectator/dead/non-solid filtering and nearest world/player hit: PASS");
}
