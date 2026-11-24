// Random-graph proofs for the bot navmesh search core.  The rvNavMesh methods
// under test are spliced in verbatim from src/game/bots/NavMesh.cpp by
// mp_bot_navmesh_search.py; this file supplies a minimal class with the same
// members and checks the methods against brute force on generated graphs.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <queue>
#include <vector>
#undef INFINITY
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
template<class T> T Min(T a, T b) { return a < b ? a : b; }
template<class T> T Max(T a, T b) { return a > b ? a : b; }
struct idMath {
    static constexpr float INFINITY = std::numeric_limits<float>::infinity();
    static constexpr float FLOAT_EPSILON = 0.000001f;
    static int ClampInt(int lo, int hi, int x) { return x < lo ? lo : (x > hi ? hi : x); }
};
struct idVec3 {
    float x=0,y=0,z=0;
    idVec3() = default;
    idVec3(float a,float b,float c):x(a),y(b),z(c) {}
    idVec3 operator-(const idVec3& b) const { return {x-b.x,y-b.y,z-b.z}; }
    float Length() const { return std::sqrt(x*x+y*y+z*z); }
};
template<class T> struct idList : std::vector<T> {
    int Num() const { return int(this->size()); }
    void SetGranularity(int) {}
    void SetNum(int n,bool=true) { this->resize(n); }
    void AssureSize(int n,const T& v) { if (Num() < n) this->resize(n, v); }
    int Append(const T& x) { this->push_back(x); return Num()-1; }
    void Clear() { this->clear(); }
    T* Ptr() { return this->data(); }
    const T* Ptr() const { return this->data(); }
};
struct Sys { double GetClockTicks() const { return 0.0; } };
struct idLib { static Sys* sys; };
Sys sysObject; Sys* idLib::sys=&sysObject;
struct GameLocal { template<class... T> void Warning(const char*,T...) {} } gameLocal;

// PRODUCTION_CONSTANTS

typedef struct navNode_s { idVec3 origin; int gx=0,gy=0,firstLink=-1,area=-1,component=-1,flags=0; } navNode_t;
typedef struct navLink_s { int node,next; float cost; short travelType,actionEntityNum; } navLink_t;
typedef struct navFieldTarget_s { int node; float weight,minWeight,extra; } navFieldTarget_t;
typedef struct navStats_s { int pathSearches=0,pathFailures=0,quickRejects=0,snapFailures=0,searchFailures=0,legFailures=0,repairSearches=0,nodesExpanded=0,fieldSearches=0,fieldNodes=0; double searchTicks=0,fieldTicks=0; } navStats_t;

class rvNavMesh {
public:
    typedef struct navHeapEntry_s { int node; float key; } navHeapEntry_t;
    idList<navNode_t> nodes;
    idList<navLink_t> links;
    int numComponents=0,componentWords=0,numLandmarks=0;
    idList<unsigned int> componentReach;
    idList<int> landmarkNodes;
    idList<float> landmarkCost;
    idList<int> reverseOffset,reverseSource;
    idList<float> reverseCost;
    float heuristicScale=1.0f;
    mutable idList<float> gScore,fScore;
    mutable idList<int> cameFrom,openTag,closedTag,fieldTargetTag,fieldPricedTag;
    mutable idList<float> fieldPricedCost;
    mutable bool fieldPriced=false;
    mutable idList<navHeapEntry_t> heap;
    mutable int searchSerial=0,fieldSerial=0;
    mutable navStats_t stats;

    int SnapNode(const idVec3& point) const {
        for (int i=0;i<nodes.Num();i++) if (nodes[i].origin.x==point.x && nodes[i].origin.y==point.y && nodes[i].origin.z==point.z) return i;
        return -1;
    }
    void BeginSearch() const;
    void HeapPush(int node,float key) const;
    int HeapPop() const;
    void Dijkstra(int source,bool reverse,float* distances) const;
    void BuildComponents();
    bool NodeCanReach(int fromNode,int toNode) const;
    void BuildLandmarks();
    float Heuristic(int node,int goal) const;
    int BuildCostField(const idVec3& origin,float maxCost,const idList<int>* targets) const;
    int BuildCostField(const idVec3& origin,float maxCost,const navFieldTarget_t* targets,int numTargets,int keepBest) const;
    float CostFieldDistance(int node) const;
    float PathCost(int startNode,int goalNode,float maxCost) const;
    void RankedSettle(int startNode,float maxCost,float startLeg,const navFieldTarget_t* targets,const int* remaining,
                      int numRemaining,bool ranked,int keepBest,float* best,int& numBest,idList<int>& pricedNode,
                      idList<float>& pricedCost) const;
};

// PRODUCTION_FUNCTIONS

struct Rng { unsigned s; explicit Rng(unsigned seed):s(seed) {} unsigned Next() { s=s*1664525u+1013904223u; return s>>8; } float Unit() { return (Next()&0xffff)/65535.0f; } int Below(int n) { return int(Next()%unsigned(n)); } };

void AddLink(rvNavMesh& nav,int from,int to,float cost) {
    navLink_t l; l.node=to; l.next=nav.nodes[from].firstLink; l.cost=cost; l.travelType=0; l.actionEntityNum=-1;
    nav.nodes[from].firstLink=nav.links.Append(l);
    const float length=(nav.nodes[to].origin-nav.nodes[from].origin).Length();
    if (length>idMath::FLOAT_EPSILON) nav.heuristicScale=Min(nav.heuristicScale,Max(0.0f,cost/length));
}

// Two floors of a grid: walls removed at random, one-way drops from the upper
// floor, a few cheap long-range transports back up (which collapse the
// straight-line heuristic scale), and sealed pits a drop leads into.
void BuildGraph(rvNavMesh& nav,unsigned seed,int size) {
    Rng rng(seed);
    nav=rvNavMesh();
    auto id=[&](int floor,int x,int y){ return floor*size*size+y*size+x; };
    for (int floor=0;floor<2;floor++) for (int y=0;y<size;y++) for (int x=0;x<size;x++) {
        navNode_t n; n.origin=idVec3(float(x*24),float(y*24),float(floor*160)); n.gx=x; n.gy=y; nav.nodes.Append(n);
    }
    const int dx[8]={1,1,0,-1,-1,-1,0,1}, dy[8]={0,1,1,1,0,-1,-1,-1};
    for (int floor=0;floor<2;floor++) for (int y=0;y<size;y++) for (int x=0;x<size;x++) for (int d=0;d<8;d++) {
        const int nx=x+dx[d], ny=y+dy[d];
        if (nx<0||ny<0||nx>=size||ny>=size) continue;
        if (rng.Unit()<0.12f) continue;	// a wall
        const int a=id(floor,x,y), b=id(floor,nx,ny);
        AddLink(nav,a,b,(nav.nodes[b].origin-nav.nodes[a].origin).Length());
    }
    for (int i=0;i<size*2;i++) {
        const int x=rng.Below(size), y=rng.Below(size);
        const int a=id(1,x,y), b=id(0,x,y);
        AddLink(nav,a,b,(nav.nodes[b].origin-nav.nodes[a].origin).Length()+96.0f);	// drop
    }
    for (int i=0;i<3;i++) {
        const int a=id(0,rng.Below(size),rng.Below(size)), b=id(1,rng.Below(size),rng.Below(size));
        AddLink(nav,a,b,128.0f);	// jump pad
    }
    // Sealed pits: a few nodes whose outgoing links are removed after a drop in.
    for (int i=0;i<4;i++) {
        const int pit=id(0,rng.Below(size),rng.Below(size));
        nav.nodes[pit].firstLink=-1;
    }
    // Weak areas, as rvNavMesh::BuildAreas computes them.
    std::vector<std::vector<int>> und(nav.nodes.Num());
    for (int i=0;i<nav.nodes.Num();i++) for (int l=nav.nodes[i].firstLink;l!=-1;l=nav.links[l].next) { und[i].push_back(nav.links[l].node); und[nav.links[l].node].push_back(i); }
    int area=0;
    for (int i=0;i<nav.nodes.Num();i++) {
        if (nav.nodes[i].area!=-1) continue;
        std::vector<int> stack{i}; nav.nodes[i].area=area;
        while (!stack.empty()) { int v=stack.back(); stack.pop_back(); for (int w:und[v]) if (nav.nodes[w].area==-1) { nav.nodes[w].area=area; stack.push_back(w); } }
        area++;
    }
    const int n=nav.nodes.Num();
    nav.gScore.SetNum(n); nav.fScore.SetNum(n); nav.cameFrom.SetNum(n);
    nav.openTag.assign(n,0); nav.closedTag.assign(n,0); nav.fieldTargetTag.assign(n,0);
    nav.fieldPricedTag.assign(n,0); nav.fieldPricedCost.assign(n,0.0f);
    nav.BuildComponents();
    nav.BuildLandmarks();
}

std::vector<float> Exact(const rvNavMesh& nav,int source) {
    std::vector<float> d(nav.nodes.Num(),std::numeric_limits<float>::infinity());
    typedef std::pair<float,int> E; std::priority_queue<E,std::vector<E>,std::greater<E>> q;
    d[source]=0; q.push({0,source});
    while (!q.empty()) {
        auto [g,v]=q.top(); q.pop(); if (g>d[v]) continue;
        for (int l=nav.nodes[v].firstLink;l!=-1;l=nav.links[l].next) { const int w=nav.links[l].node; if (g+nav.links[l].cost<d[w]) { d[w]=g+nav.links[l].cost; q.push({d[w],w}); } }
    }
    return d;
}

int main() {
    long checkedPairs=0, checkedEdges=0, rankedRuns=0; double fullSettled=0, rankedSettled=0;
    for (unsigned seed=1;seed<=6;seed++) {
        rvNavMesh nav; BuildGraph(nav,seed,26);
        CHECK(nav.numLandmarks>0 && nav.numComponents>1);
        Rng rng(seed*7919u);
        for (int sample=0;sample<40;sample++) {
            const int from=rng.Below(nav.nodes.Num());
            const std::vector<float> exact=Exact(nav,from);
            for (int t=0;t<25;t++) {
                const int to=rng.Below(nav.nodes.Num());
                const bool reachable=exact[to]<std::numeric_limits<float>::infinity();
                // Component closure is exact directed reachability.
                CHECK(nav.NodeCanReach(from,to)==reachable);
                // Goal pricing is exact, and refuses what has no route.
                const float priced=nav.PathCost(from,to,1.0e9f);
                if (reachable) CHECK(std::fabs(priced-exact[to])<=0.01f*std::max(1.0f,exact[to]));
                else CHECK(priced>=NAV_UNREACHED);
                // The heuristic never overestimates a real route.
                const float h=nav.Heuristic(from,to);
                if (reachable) { CHECK(h<=exact[to]+0.01f*std::max(1.0f,exact[to])); CHECK(h<NAV_UNREACHED); }
                checkedPairs++;
            }
            // Consistency on every edge out of the sample node toward one goal.
            const int goal=rng.Below(nav.nodes.Num());
            for (int l=nav.nodes[from].firstLink;l!=-1;l=nav.links[l].next) {
                const float hu=nav.Heuristic(from,goal), hv=nav.Heuristic(nav.links[l].node,goal);
                if (hv<NAV_UNREACHED) CHECK(hu<=nav.links[l].cost+hv+0.05f);
                checkedEdges++;
            }
        }
        // The ranked field returns the exhaustive field's top four exactly.
        for (int run=0;run<30;run++) {
            const int start=rng.Below(nav.nodes.Num());
            std::vector<navFieldTarget_t> targets;
            for (int i=0;i<40;i++) { navFieldTarget_t t; t.node=rng.Below(nav.nodes.Num()); t.weight=0.5f+5.0f*rng.Unit(); t.minWeight=t.weight; t.extra=192.0f+64.0f*rng.Unit(); targets.push_back(t); }
            idList<int> plain; for (auto& t:targets) plain.Append(t.node);
            nav.stats=navStats_t();
            CHECK(nav.BuildCostField(nav.nodes[start].origin,1.0e9f,&plain)==start);
            std::vector<std::pair<float,int>> full;
            for (int i=0;i<40;i++) { const float c=nav.CostFieldDistance(targets[i].node); if (c<idMath::INFINITY) full.push_back({targets[i].weight/(c+targets[i].extra),i}); }
            fullSettled+=nav.stats.fieldNodes;
            nav.stats=navStats_t();
            CHECK(nav.BuildCostField(nav.nodes[start].origin,1.0e9f,targets.data(),40,4)==start);
            std::vector<std::pair<float,int>> ranked;
            for (int i=0;i<40;i++) { const float c=nav.CostFieldDistance(targets[i].node); if (c<idMath::INFINITY) ranked.push_back({targets[i].weight/(c+targets[i].extra),i}); }
            rankedSettled+=nav.stats.fieldNodes;
            std::sort(full.rbegin(),full.rend()); std::sort(ranked.rbegin(),ranked.rend());
            const size_t keep=std::min<size_t>(4,full.size());
            CHECK(ranked.size()>=keep);
            for (size_t k=0;k<keep;k++) CHECK(std::fabs(full[k].first-ranked[k].first)<=1.0e-6f*std::max(1.0f,full[k].first));
            rankedRuns++;
        }
    }
#ifdef PRICING_FALLBACK_TEST
    // The budget was forced down, so most pricing fell back to one field: the
    // answers must still be exact.
    std::printf("Ranked goal pricing past its budget: %ld runs match the exhaustive top four (%ld pairs, %ld edges, %.0f of %.0f nodes): PASS\n",
                rankedRuns,checkedPairs,checkedEdges,rankedSettled,fullSettled);
#else
    CHECK(rankedSettled<fullSettled);
    std::printf("Component reachability and landmark admissibility: %ld pairs, %ld edges consistent: PASS\n",checkedPairs,checkedEdges);
    std::printf("Ranked goal pricing: %ld runs match the exhaustive top four, expanding %.1f%% of the field's nodes: PASS\n",rankedRuns,100.0*rankedSettled/fullSettled);
#endif
    return 0;
}
