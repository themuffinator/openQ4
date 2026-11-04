// Production script transforms and boost state transitions with counted doubles.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
struct idStr : std::string {
	using std::string::string;
	using std::string::operator=;
	operator const char *() const { return c_str(); }
	void Clear() { clear(); }
	void BackSlashesToSlashes() { std::replace(begin(),end(),'\\','/'); }
	static int Icmp(const char *a,const char *b) {
		std::string x(a),y(b);
		for (char &c:x) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		for (char &c:y) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return x.compare(y);
	}
	int Icmp(const char *b) const { return Icmp(c_str(),b); }
	int Find(const char *s,bool=true,int start=0) const { auto p=find(s,start); return p==npos?-1:static_cast<int>(p); }
	void Replace(const char *before,const char *after) {
		std::size_t p=0;
		while ((p=find(before,p))!=npos) { replace(p,std::strlen(before),after); p+=std::strlen(after); }
	}
};
static const char *va(const char *format,...) {
	static char buffer[1024]; va_list args; va_start(args,format);
	std::vsnprintf(buffer,sizeof(buffer),format,args); va_end(args); return buffer;
}
struct Game {
	int time=0; bool isMultiplayer=false;
	void Printf(const char *,...) {}
	void Warning(const char *,...) {}
} gameLocal;
struct Files {
	const char *game="q4xbase"; std::map<std::string,std::string> files;
	const char *GetActiveGameDir() const { return game; }
	int ReadFile(const char *name,void **data,void *) {
		auto it=files.find(name); if (it==files.end()) return -1;
		*data=std::malloc(it->second.size()+1); std::memcpy(*data,it->second.c_str(),it->second.size()+1);
		return static_cast<int>(it->second.size());
	}
	void FreeFile(void *data) { std::free(data); }
} files, *fileSystem=&files;

// @MISSION@

#define CLASS_PROTOTYPE(x)
#define CLASS_DECLARATION(a,b)
#define END_CLASS
#define SEC2MS(x) static_cast<int>((x)*1000)
static constexpr int SND_CHANNEL_ANY=0;
static constexpr float OPENQ4_TURBO_VEHICLE_SPEED_SCALE=2.0f;
static bool OpenQ4_TurboModeActive() { return false; }
struct Vec { float n=1; Vec operator*(float f) const { return {n*f}; } };
struct idUserInterface {
	std::map<std::string,float> values;
	void SetStateBool(const char *k,bool v) { values[k]=v; }
	void SetStateInt(const char *k,int v) { values[k]=static_cast<float>(v); }
	void SetStateFloat(const char *k,float v) { values[k]=v; }
	void SetStateString(const char *,const char *) {}
};
struct Physics { int impulses=0; float force=0; void ApplyImpulse(int,Vec,Vec v) { ++impulses; force=v.n; } };
struct rvVehicle {
	bool enabled=true,hidden=false; int health=2000; float fov=0; int starts=0,stops=0;
	idUserInterface hud; Physics physics;
	idUserInterface *GetHud() { return &hud; }
	bool IsMovementEnabled() const { return enabled; }
	bool IsHidden() const { return hidden; }
	void SetFovOffset(float f) { fov=f; }
	void StartSound(const char *name,int,int,bool,void *) { if (!std::strcmp(name,"snd_boost")) ++starts; else ++stops; }
	Physics *GetPhysics() { return &physics; }
};
struct Position { bool occupied=true; struct { int upmove=0; } mInputCmd; bool IsOccupied() const { return occupied; } };
struct Parent { rvVehicle *value=nullptr; rvVehicle *GetEntity() { return value; } };
struct Args { float GetFloat(const char *,const char *fallback) { return std::strtof(fallback,nullptr); } };
struct rvVehiclePart {
	Args spawnArgs; Parent parent; Position *position=nullptr; Vec worldOrigin,worldAxis[3]; bool active=false;
	virtual ~rvVehiclePart()=default;
	virtual void Activate(bool on) { active=on; }
	virtual void RunPostPhysics() {}
	void UpdateOrigin() {}
};
struct Common { const char *KeysFromBinding(const char *) { return "C"; } } commonObject,*common=&commonObject;
struct idSaveGame { std::vector<double> data; void WriteInt(int x) { data.push_back(x); } void WriteFloat(float x) { data.push_back(x); } };
struct idRestoreGame {
	const std::vector<double> &data; std::size_t pos=0;
	void ReadInt(int &x) { x=static_cast<int>(data.at(pos++)); }
	void ReadFloat(float &x) { x=static_cast<float>(data.at(pos++)); }
};

// Test access exposes state, without changing any production method bodies.
#define private public
// @BOOST@
#undef private

static void BoostChecks() {
	rvVehicle vehicle; Position seat; riVehiclePartBoost boost;
	boost.parent.value=&vehicle; boost.position=&seat; boost.Spawn(); boost.Activate(true);
	seat.mInputCmd.upmove=-127; gameLocal.time=100; boost.RunPostPhysics();
	CHECK(boost.state==boost.BOOST_PUSHING && vehicle.starts==1);
	const int deadline=boost.readyTime;
	gameLocal.time=350; boost.RunPostPhysics();
	CHECK(vehicle.physics.impulses==1 && vehicle.physics.force>0 && vehicle.fov>0);
	idSaveGame saved; boost.Save(&saved); riVehiclePartBoost restored;
	restored.parent.value=&vehicle; restored.position=&seat; idRestoreGame reader{saved.data}; restored.Restore(&reader);
	CHECK(reader.pos==saved.data.size() && restored.readyTime==deadline);
	restored.RunPostPhysics(); CHECK(vehicle.physics.impulses==2 && restored.state==boost.BOOST_PUSHING);
	restored.Activate(false);
	CHECK(restored.state==boost.BOOST_RECHARGING && vehicle.fov==0 && vehicle.stops==1);
	CHECK(restored.readyTime==deadline && !restored.active);
	restored.Activate(true); restored.RunPostPhysics(); CHECK(vehicle.physics.impulses==2 && vehicle.starts==1);
	gameLocal.time=deadline; restored.RunPostPhysics(); CHECK(restored.state==boost.BOOST_READY);
	for (int reason=0;reason<4;++reason) {
		vehicle.enabled=true; vehicle.hidden=false; vehicle.health=2000; seat.occupied=true;
		restored.state=boost.BOOST_READY; restored.RunPostPhysics();
		CHECK(restored.state==boost.BOOST_PUSHING);
		if (reason==0) vehicle.enabled=false;
		if (reason==1) vehicle.hidden=true;
		if (reason==2) vehicle.health=0;
		if (reason==3) seat.occupied=false;
		const int impulses=vehicle.physics.impulses;
		restored.RunPostPhysics(); CHECK(restored.state==boost.BOOST_RECHARGING && vehicle.fov==0);
		CHECK(vehicle.physics.impulses==impulses);
		restored.state=boost.BOOST_READY; restored.RunPostPhysics(); CHECK(restored.state==boost.BOOST_READY);
	}
	restored.attackTime=0; restored.sustainTime=100; restored.decayTime=0;
	CHECK(restored.Envelope(0)==1 && restored.Envelope(99)==1 && restored.Envelope(100)==0);
	restored.sustainTime=0; CHECK(restored.Envelope(0)==0 && restored.Envelope(-1)==0);
	restored.attackTime=100; restored.sustainTime=100; restored.decayTime=100;
	CHECK(restored.Envelope(50)==0.5f && restored.Envelope(150)==1 && restored.Envelope(250)==0.5f);
	CHECK(restored.Envelope(300)==0 && std::isfinite(restored.Envelope(300)));
}

int main(int argc,char **argv) {
	BoostChecks();
	if (argc==2) for (int race=0;race<3;++race) {
		const std::string path=std::string(argv[1])+"/"+awakeningM07Sources[race];
		std::ifstream input(path,std::ios::binary); CHECK(input.good());
		const std::string original{std::istreambuf_iterator<char>(input),{}};
		files.files[awakeningM07Sources[race]]=original;
		idStr corrected; CHECK(Awakening_LoadM07MissionScript(awakeningM07Versions[race],corrected));
		CHECK(corrected.Find("oq4_raceFailed")<0);
		CHECK(corrected.Find("boolean oq4_raceComplete;")>=0);
		CHECK(corrected.Find("!oq4_raceComplete")>=0);
		for (int bomber=2;bomber<=(race==0?3:race==1?2:4);++bomber) {
			const std::string expected="sys.waitFor( $strafeBase_"+std::to_string(bomber)+" );\n\t\tremoveFlyerBits(\""+std::to_string(bomber)+"\");";
			CHECK(corrected.Find(expected.c_str())>=0);
		}
		CHECK(!std::strcmp(Awakening_M07MissionScriptName(awakeningM07Sources[race]),awakeningM07Versions3[race]));
		idStr previous2; CHECK(Awakening_LoadM07MissionScript(awakeningM07Versions2[race],previous2));
		CHECK(previous2.Find("oq4_bomberDestroyed_")<0);
		idStr latest; CHECK(Awakening_LoadM07MissionScript(awakeningM07Versions3[race],latest));
		CHECK(latest.Find("boolean oq4_raceFailed;")>=0 && latest!=corrected);
		CHECK(latest.Find("boolean oq4_bomberDestroyed_2;")>=0 && latest!=previous2);
		files.files[awakeningM07Sources[race]]+="\n// setNewTimes($strafeBase_2,\n";
		idStr v3Rejected; CHECK(!Awakening_LoadM07MissionScript(awakeningM07Versions3[race],v3Rejected) && v3Rejected.empty());
		CHECK(Awakening_LoadM07MissionScript(awakeningM07Versions2[race],v3Rejected));
		CHECK(!std::strcmp(Awakening_M07MissionScriptName(awakeningM07Sources[race]),awakeningM07Sources[race]));
		files.files[awakeningM07Sources[race]]=original;
		// An otherwise recognized script with an ambiguous new anchor must not
		// select a partly corrected v2, nor break its existing v1 save identity.
		files.files[awakeningM07Sources[race]]+="\nvoid raceProgress90()\n\t{ }\n";
		idStr rejected; CHECK(!Awakening_LoadM07MissionScript(awakeningM07Versions2[race],rejected) && rejected.empty());
		CHECK(!std::strcmp(Awakening_M07MissionScriptName(awakeningM07Sources[race]),awakeningM07Sources[race]));
		CHECK(Awakening_LoadM07MissionScript(awakeningM07Versions[race],rejected));
		files.files[awakeningM07Sources[race]]=original;
		idStr windows=awakeningM07Versions[race]; windows.Replace("/","\\");
		idStr repeated; CHECK(Awakening_LoadM07MissionScript(windows,repeated) && repeated==corrected);
		CHECK(!Awakening_LoadM07MissionScript(awakeningM07Sources[race],repeated));
		files.game="baseoq4"; CHECK(!Awakening_LoadM07MissionScript(windows,repeated)); files.game="q4xbase";
		gameLocal.isMultiplayer=true; CHECK(!Awakening_LoadM07MissionScript(windows,repeated)); gameLocal.isMultiplayer=false;
		windows=awakeningM07Versions3[race]; windows.Replace("/","\\");
		CHECK(Awakening_LoadM07MissionScript(windows,repeated) && repeated==latest);
		files.game="baseoq4"; CHECK(!Awakening_LoadM07MissionScript(windows,repeated)); files.game="q4xbase";
		gameLocal.isMultiplayer=true; CHECK(!Awakening_LoadM07MissionScript(windows,repeated)); gameLocal.isMultiplayer=false;
		files.files[awakeningM07Sources[race]]+="\nfloat area1TimeLeft;\n";
		CHECK(!Awakening_LoadM07MissionScript(windows,repeated) && repeated.empty());
		CHECK(!std::strcmp(Awakening_M07MissionScriptName(awakeningM07Sources[race]),awakeningM07Sources[race]));
		files.files[awakeningM07Sources[race]]=original;
	}
	std::printf("Awakening M07: %d behavioral checks passed%s\n",checks,argc==2?" (installed scripts included)":" (boost only)");
}
