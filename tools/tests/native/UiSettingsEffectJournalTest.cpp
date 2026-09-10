// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/application/SettingsJournal.h"
#include "src/ui/application/SystemSettingsHost.h"
#include "src/ui/application/SystemDisplay.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <exception>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
namespace openq4::ui {
bool BaselineEncodeSettingsJournal(const SettingsRecoveryJournal&,const std::map<std::string,std::size_t>&,std::string&,std::string&);
bool BaselineDecodeSettingsJournal(const std::string&,const std::map<std::string,std::size_t>&,SettingsRecoveryJournal&,std::string&);
}
using namespace openq4::ui;
static unsigned checks,faultPoint;
#define CHECK(x) do { ++checks; if (!(x)) {std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(false)
static long failAfter=-1;static unsigned allocations=0;
#if defined(__GNUC__)
#define TEST_NOINLINE __attribute__((noinline))
#else
#define TEST_NOINLINE __declspec(noinline)
#endif
TEST_NOINLINE void* operator new(std::size_t size) {if(failAfter==0)throw std::bad_alloc();if(failAfter>0)--failAfter;++allocations;if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
TEST_NOINLINE void* operator new[](std::size_t size){return ::operator new(size);}
TEST_NOINLINE void operator delete(void* p) noexcept {std::free(p);}TEST_NOINLINE void operator delete[](void* p) noexcept {std::free(p);}
TEST_NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}TEST_NOINLINE void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
struct FloatMode {
#if defined(__SSE__) || defined(_M_X64)
 unsigned old=_mm_getcsr();explicit FloatMode(bool flush){_mm_setcsr((old&~0xe040u)|(flush?0x8040u:0));}~FloatMode(){_mm_setcsr(old);}
#else
 explicit FloatMode(bool){}
#endif
};
static StateValues Values() {
 StateValues v;for(const auto& [k,f]:SettingsEffectCatalogV1())v[k]=f.type==0?StateValue(0.0):f.type==1?StateValue(false):StateValue(std::string("original"));
 v["r_windowWidth"]=1280.0;v["r_windowHeight"]=720.0;v["r_customWidth"]=1280.0;v["r_customHeight"]=720.0;v["r_screen"]=-1.0;
 return v;
}
static StateValues Placement(const StateValues& a,const StateValues& b) {
 StateValues v;for(const auto* prefix:{"baseline.","target."}) {
  const auto& values=std::strcmp(prefix,"baseline.")==0?a:b;
  for(const auto* k:{"x","y","normalX","normalY"})v[std::string(prefix)+k]=0.0;
  for(const auto* k:{"width","normalWidth"})v[std::string(prefix)+k]=values.at("r_windowWidth");
  for(const auto* k:{"height","normalHeight"})v[std::string(prefix)+k]=values.at("r_windowHeight");
  v[std::string(prefix)+"normalValid"]=true;
 }return v;
}
static SettingsEffectRecoveryJournal Sample(unsigned mask) {
 SettingsEffectRecoveryJournal j;j.attempt="123456789abcdef0123456789abcdef0";j.baseline=Values();j.target=j.baseline;
 for(const auto& [k,f]:SettingsEffectCatalogV1())if((f.effects&mask)!=0) {
  j.target[k]=f.type==0?StateValue(std::get<double>(j.target.at(k))+1):f.type==1?StateValue(true):StateValue(std::string("target"));
 }
 if(!mask)j.target["r_brightness"]=1.25;
 for(const auto& [k,v]:j.target)if(!SettingsValueEqual(j.baseline.at(k),v))j.patch[k]=v;
 std::string e;CHECK(BuildSettingsEffectPlan(j.baseline,j.target,SystemSettingsHost::Schema(),j.plan,e));
 // Deliberately opaque synthetic payload. Envelope success certifies no domain
 // descriptor semantics, topology, audio output or hardware readiness.
 for(auto* m:{&j.displayRestore,&j.displayTarget,&j.placement})if(mask&19)*m={{"sample",std::string("opaque")}};
 if(mask&19)j.placement=Placement(j.baseline,j.target);
 if(mask&2)j.imageRestore=j.imageTarget={{"sample",std::string("opaque")}};
 if(mask&4)j.audioRestore=j.audioTarget={{"sample",std::string("opaque")}};
 if(mask&8)j.deferredRestore=j.deferredTarget={{"sample",std::string("opaque")}};
 if(mask&16)j.resourceRestore=j.resourceTarget={{"sample",std::string("opaque")}};
 return j;
}
static std::string Encode(const SettingsEffectRecoveryJournal& j) {std::string b,e;CHECK(EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),b,e));return b;}
static std::string Checksum(std::string b) {
 auto n=b.find('\n');auto p=b.find('\n',n+1)+1;std::uint32_t crc=0xffffffffu;
 for(unsigned char c:b.substr(p)){crc^=c;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
 char h[9];std::snprintf(h,sizeof(h),"%08x",~crc);return b.substr(0,n+1)+h+'\n'+b.substr(p);
}
static std::string Replace(std::string bytes,const std::string& from,const std::string& to){auto p=bytes.find(from);if(p==std::string::npos)std::fprintf(stderr,"Missing fixture anchor: %s\n",from.c_str());CHECK(p!=std::string::npos);bytes.replace(p,from.size(),to);return Checksum(bytes);}
static void Reject(const std::string& bad) {
 SettingsJournalRecord out;std::string e;CHECK(DecodeSettingsJournalRecord(Encode(Sample(4)),SystemSettingsHost::Schema(),out,e));const auto* prior=out.Value();
 CHECK(!DecodeSettingsJournalRecord(bad,SystemSettingsHost::Schema(),out,e));CHECK(!e.empty()&&out.Value()==prior&&out.Schema()==2);
}
static void CatalogAndPlans() {
 CHECK(SettingsEffectCatalogV1().size()==53);CHECK(SystemSettingsHost::Catalog().size()==53);
 for(const auto& field:SystemSettingsHost::Catalog()){const auto& p=SettingsEffectCatalogV1().at(field.key);CHECK(p.type==field.type&&p.effects==field.effects);}
 for(unsigned mask=0;mask<64;++mask) {
  auto j=Sample(mask);CHECK(j.plan.changeMask==mask&&j.plan.domainMask==(mask&31));
  CHECK(j.plan.completion==((mask&1)?SettingsEffectCompletion::DisplayConfirmed:SettingsEffectCompletion::Automatic));
  CHECK(j.plan.rendererStrategy==((mask&19)?SettingsRendererStrategy::CoalescedDevice:SettingsRendererStrategy::None));
  std::string e;CHECK(ValidateSettingsEffectPlan(j.plan,j.baseline,j.target,SystemSettingsHost::Schema(),e));
  for(unsigned bad=0;bad<6;++bad){auto p=j.plan;if(bad==0)p.version=2;if(bad==1)p.changeMask^=1;if(bad==2)p.domainMask^=4;if(bad==3)p.completion=static_cast<SettingsEffectCompletion>(12);if(bad==4)p.rendererStrategy=static_cast<SettingsRendererStrategy>(12);if(bad==5)p.domainMask|=32;CHECK(!ValidateSettingsEffectPlan(p,j.baseline,j.target,SystemSettingsHost::Schema(),e));}
 }
 auto j=Sample(0);std::string e;SettingsEffectPlan p;p.version=77;const auto old=p;
 auto schema=SystemSettingsHost::Schema();schema["future"]=0;CHECK(!BuildSettingsEffectPlan(j.baseline,j.target,schema,p,e)&&p==old);
 schema=SystemSettingsHost::Schema();schema["r_bloom"]=0;CHECK(!BuildSettingsEffectPlan(j.baseline,j.target,schema,p,e)&&p==old);
 CHECK(!BuildSettingsEffectPlan(j.baseline,j.baseline,SystemSettingsHost::Schema(),p,e)&&p==old);
 j.target.erase("r_bloom");CHECK(!BuildSettingsEffectPlan(j.baseline,j.target,SystemSettingsHost::Schema(),p,e)&&p==old);
}
static void RoundTrips() {
 std::string e;
 for(unsigned mask=0;mask<64;++mask)for(auto state:{SettingsJournalState::Pending,SettingsJournalState::Confirmed}) {
  auto j=Sample(mask);j.state=state;auto bytes=Encode(j);SettingsJournalRecord record;
  CHECK(DecodeSettingsJournalRecord(bytes,SystemSettingsHost::Schema(),record,e));CHECK(record.Schema()==2);
  const auto& value=std::get<SettingsEffectRecoveryJournal>(*record.Value());CHECK(value.plan==j.plan&&value.state==state&&value.attempt==j.attempt);
  CHECK(SettingsValuesEqual(value.baseline,j.baseline)&&SettingsValuesEqual(value.target,j.target)&&SettingsValuesEqual(value.patch,j.patch));
  std::string again;CHECK(EncodeSettingsJournalRecord(record,SystemSettingsHost::Schema(),again,e)&&again==bytes);
  SettingsRecoveryJournal old;old.attempt="untouched";CHECK(!DecodeSettingsJournal(bytes,SystemSettingsHost::Schema(),old,e)&&old.attempt=="untouched");CHECK(!BaselineDecodeSettingsJournal(bytes,SystemSettingsHost::Schema(),old,e)&&old.attempt=="untouched");
  auto moved=std::move(record);CHECK(record.Schema()==0&&moved.Schema()==2);record=std::move(moved);CHECK(!moved.Value()&&record.Schema()==2);
 }
 SettingsJournalRecord empty;std::string bytes="untouched";CHECK(!EncodeSettingsJournalRecord(empty,SystemSettingsHost::Schema(),bytes,e)&&bytes=="untouched");
}
static void Forgery() {
 const auto b=Encode(Sample(63));
 for(const auto& pair:std::initializer_list<std::pair<std::string,std::string>>{
  {"\"schema\":2","\"schema\":1"},{"\"schema\":2","\"schema\":2.0"},{"\"plan.version\":1","\"plan.version\":2"},
  {"\"plan.changeMask\":63","\"plan.changeMask\":31"},{"\"plan.domainMask\":31","\"plan.domainMask\":63"},
  {"\"plan.domainMask\":31","\"plan.domainMask\":30"},{"\"plan.changeMask\":63","\"plan.changeMask\":63.1"},
  {"display-confirmed","automatic"},{"coalesced-device","none"},{"\"plan.version\":1","\"plan.version\":true"},
  {"\"plan.version\":1","\"plan.version\":1,\n\"plan.unknown\":0"},
  {"\"plan.version\":1","\"plan.version\":1,\n\"plan.version\":1"},
  {"\"imageTarget.sample\":\"opaque\",\n",""},{"\"patch.r_borderless\":true,\n",""},
  {"\"patch.r_borderless\":true","\"patch.r_borderless\":false"},{"\"placement.target.width\":1281","\"placement.target.width\":1280"},
  {"\"placement.target.normalValid\":true","\"placement.target.normalValid\":0"},
  {"\"placement.target.normalWidth\":1281","\"placement.target.normalWidth\":0"},
  {"\"placement.target.x\":0","\"placement.target.x\":0.5"},
  {"\"placement.target.x\":0","\"placement.target.x\":2147483648"},
  {"\"placement.target.x\":0","\"placement.target.x\":0,\n\"placement.extra\":0"},
  {"\"audioTarget.sample\":\"opaque\"","\"audioTarget.sample\":[]"},
  {"\"audioTarget.sample\":\"opaque\"","\"audioTarget.sample\":null"},
  {"\"audioTarget.sample\":\"opaque\"","\"audioTarget.sample\":1e309"},
  {"\"schema\":2","\"schema\":2,\n\"unknown.sample\":0"},
  {"123456789abcdef0123456789abcdef0","00000000000000000000000000000000"},
  {"\"state\":\"pending\"","\"state\":\"applied\""},
  {"\"schema\":2","\"schema\":2,\n\"audioRestore.\":0"}})Reject(Replace(b,pair.first,pair.second));
 for(unsigned mask:{0u,2u,4u,8u,16u,32u}) {
  auto j=Sample(mask);const auto bytes=Encode(j);
  Reject(Replace(bytes,"\"schema\":2","\"schema\":2,\n\"surplus.x\":0"));
  if(!(mask&4))Reject(Replace(bytes,"\"schema\":2","\"schema\":2,\n\"audioTarget.x\":0"));
  if(!(mask&19))Reject(Replace(bytes,"\"schema\":2","\"schema\":2,\n\"displayRestore.x\":0"));
 }
 Reject(Replace(b,"recovery 2","recovery 1"));Reject(Replace(b,"recovery 2","recovery 3"));Reject(b+"\n");
 for(std::size_t i=0;i<b.size();i+=37)Reject(b.substr(0,i));
 auto absent=b;absent.erase(absent.find("\"plan.version\""),std::string("\"plan.version\":1,\n").size());Reject(Checksum(absent));
}
static void Bounds() {
 auto j=Sample(4);std::string bytes="untouched",e;
 j.audioTarget={{std::string(96,'x'),true}};CHECK(EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,e));
 j.audioTarget={{std::string(97,'x'),true}};bytes="untouched";CHECK(!EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,e)&&bytes=="untouched");
 j.audioTarget={{"x",std::string(65535,'a')}};CHECK(EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,e));
 j.audioTarget={{"x",std::string(65536,'a')}};CHECK(!EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,e));
 j.audioTarget.clear();for(unsigned i=0;i<512;++i)j.audioTarget["n"+std::to_string(i)]=0.0;CHECK(EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,e));
 j.audioTarget["extra"]=0.0;CHECK(!EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,e));
 j=Sample(63);for(auto* map:{&j.displayRestore,&j.displayTarget,&j.imageRestore,&j.imageTarget,&j.resourceRestore,&j.resourceTarget,&j.audioRestore,&j.audioTarget,&j.deferredRestore,&j.deferredTarget}){map->clear();for(unsigned i=0;i<512;++i)(*map)["n"+std::to_string(i)]=0.0;}
 CHECK(!EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,e));CHECK(e.find("flattened")!=std::string::npos);
 j=Sample(63);for(auto* map:{&j.displayRestore,&j.displayTarget,&j.imageRestore,&j.imageTarget,&j.resourceRestore,&j.resourceTarget,&j.audioRestore,&j.audioTarget,&j.deferredRestore,&j.deferredTarget})*map={{"x",std::string(65535,'\n')}};
 for(const auto* key:{"com_performancePreset","r_renderer","r_rendererBenchmarkPreset"}){j.baseline[key]=std::string(65536,'\n');j.target[key]=std::string(65536,'\t');j.patch[key]=j.target[key];}
 CHECK(!EncodeSettingsEffectJournal(j,SystemSettingsHost::Schema(),bytes,e));CHECK(e.find("byte budget")!=std::string::npos);
}
static void ExactNumbers() {
 for(const auto bits:{1ULL,2ULL,0xfffffffffffffULL,0x10000000000000ULL,0x8000000000000001ULL}) {
  std::string gradual;
  for(bool flush:{false,true}) {FloatMode mode(flush);auto j=Sample(4);j.baseline["r_brightness"]=std::bit_cast<double>(bits);j.target["r_brightness"]=0.0;j.patch["r_brightness"]=0.0;
   auto bytes=Encode(j);if(!flush)gradual=bytes;else CHECK(bytes==gradual);SettingsJournalRecord record;std::string e;CHECK(DecodeSettingsJournalRecord(bytes,SystemSettingsHost::Schema(),record,e));
   const auto& decoded=std::get<SettingsEffectRecoveryJournal>(*record.Value());CHECK(std::bit_cast<std::uint64_t>(std::get<double>(decoded.baseline.at("r_brightness")))==bits);
   Reject(Replace(bytes,"\"patch.r_brightness\":0,\n",""));
   if(bits==1)Reject(Replace(bytes,"\"baseline.r_brightness\":4.9406564584124654e-324","\"baseline.r_brightness\":0"));
  }
 }
 auto j=Sample(0);j.audioTarget.clear();j.baseline["r_forceAmbient"]=-0.0;j.target["r_forceAmbient"]=0.0;
 auto bytes=Encode(j);CHECK(bytes.find("\"baseline.r_forceAmbient\":0")!=std::string::npos);
 FloatMode mode(true);
 auto b=Values(),next=b;next["r_brightness"]=std::bit_cast<double>(std::uint64_t(1));SettingsEffectPlan plan;std::string error;
 CHECK(BuildSettingsEffectPlan(b,next,SystemSettingsHost::Schema(),plan,error)&&plan.changeMask==0);
 auto tiny=Sample(4);tiny.baseline["r_brightness"]=1.0;tiny.target["r_brightness"]=tiny.patch["r_brightness"]=std::bit_cast<double>(std::uint64_t(2));
 Reject(Replace(Encode(tiny),"\"patch.r_brightness\":9.8813129168249309e-324","\"patch.r_brightness\":0"));
 Reject(Replace(Encode(Sample(4)),"\"plan.version\":1","\"plan.version\":4.9406564584124654e-324"));
}
static void SchemaOne() {
 const std::map<std::string,std::size_t> schema{{"brightness",0},{"fullscreen",1},{"mode",2}};
 SettingsRecoveryJournal j;j.attempt="0123456789abcdef0123456789abcdef";j.baseline={{"brightness",1.0},{"fullscreen",false},{"mode",std::string("custom")}};j.target=j.baseline;j.target["brightness"]=1.25;j.patch={{"brightness",1.25}};j.displayRestore={{"x",1280.0}};j.displayTarget={{"x",960.0}};j.placement={{"x",0.0}};
 std::string bytes,e;CHECK(EncodeSettingsJournal(j,schema,bytes,e));
 const std::string payload="{\n\"attempt\":\"0123456789abcdef0123456789abcdef\",\n\"baseline.brightness\":1,\n\"baseline.fullscreen\":false,\n\"baseline.mode\":\"custom\",\n\"displayRestore.x\":1280,\n\"displayTarget.x\":960,\n\"patch.brightness\":1.25,\n\"placement.x\":0,\n\"schema\":1,\n\"state\":\"pending\",\n\"target.brightness\":1.25,\n\"target.fullscreen\":false,\n\"target.mode\":\"custom\"\n}\n";
 CHECK(bytes==Checksum("openQ4 settings recovery 1\n00000000\n"+payload));std::string original;CHECK(BaselineEncodeSettingsJournal(j,schema,original,e)&&original==bytes);
 SettingsJournalRecord record;CHECK(DecodeSettingsJournalRecord(Encode(Sample(4)),SystemSettingsHost::Schema(),record,e));CHECK(DecodeSettingsJournalRecord(bytes,schema,record,e)&&record.Schema()==1);
 std::string again;CHECK(EncodeSettingsJournalRecord(record,schema,again,e)&&again==bytes);
 // Schema 1's pre-existing 113-byte metadata spelling remains accepted.
 j.displayRestore={{std::string(113,'x'),true}};CHECK(EncodeSettingsJournal(j,schema,bytes,e));CHECK(DecodeSettingsJournalRecord(bytes,schema,record,e));
}
static void AllocationAtomicity() {
 auto j=Sample(4);const auto& schema=SystemSettingsHost::Schema();const auto bytes=Encode(j);std::string e;SettingsJournalRecord out;
 CHECK(DecodeSettingsJournalRecord(bytes,schema,out,e));const auto* original=out.Value();
 allocations=0;CHECK(DecodeSettingsJournalRecord(bytes,schema,out,e));const auto count=allocations;
 SettingsRecoveryJournal legacy;legacy.attempt=j.attempt;legacy.baseline=j.baseline;legacy.target=j.target;legacy.patch=j.patch;
 legacy.displayRestore=legacy.displayTarget=legacy.placement={{"sample",true}};std::string oldBytes;
 CHECK(EncodeSettingsJournal(legacy,schema,oldBytes,e)&&DecodeSettingsJournalRecord(oldBytes,schema,out,e)&&out.Schema()==1);original=out.Value();
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
 // JsonCpp's OurReader::document_ default string constructor terminates on a
 // debug-iterator proxy refusal inside noexcept. It is an upstream process-OOM
 // limit, not a returned false that this owned wrapper can catch. Exhaustive
 // decoder denial is qualified with the release STL; encoder denial still runs.
 std::printf("LIMIT debug-STL JsonCpp decoder denial sweep unavailable (%u allocation sites)\n",count);
#else
 for(unsigned n=0;n<count;++n) {faultPoint=n;failAfter=n;const bool ok=DecodeSettingsJournalRecord(bytes,schema,out,e);failAfter=-1;CHECK(!ok&&out.Value()==original&&out.Schema()==1);}
#endif
 CHECK(out.Value()==original&&out.Schema()==1);
 CHECK(DecodeSettingsJournalRecord(bytes,schema,out,e)&&out.Schema()==2);
 std::string text;allocations=0;CHECK(EncodeSettingsEffectJournal(j,schema,text,e));const auto encodes=allocations;
 for(unsigned n=0;n<encodes;++n) {std::string stable="untouched";faultPoint=n;failAfter=n;const bool ok=EncodeSettingsEffectJournal(j,schema,stable,e);failAfter=-1;CHECK(!ok&&stable=="untouched");}
 SettingsEffectPlan plan;plan.version=91;auto invalid=j.baseline;invalid["r_brightness"]=std::numeric_limits<double>::quiet_NaN();failAfter=0;const bool ok=BuildSettingsEffectPlan(invalid,j.target,schema,plan,e);failAfter=-1;CHECK(!ok&&plan.version==91);
}
static void PreserveDisplay() {
 SystemDisplayTopology topo;topo.primary=9;topo.absolutePlacement=true;SystemDisplayDescriptor monitor;monitor.id=9;monitor.name="Observed monitor";monitor.width=1920;monitor.height=1080;monitor.desktop={1920,1080,60};monitor.modes={{1920,1080,60}};topo.displays.push_back(monitor);
 rendererDisplayState_t actual{};actual.moduleEpoch=1;actual.rendererReady=actual.windowValid=true;actual.presentation.generation=2;actual.presentation.available=1;actual.presentation.parametersValid=3;actual.presentation.samples=4;actual.presentation.swapInterval=1;
 auto& w=actual.window;w.displayId=9;w.logicalWidth=w.pixelWidth=1280;w.logicalHeight=w.pixelHeight=720;w.positionValid=true;w.windowX=25;w.windowY=30;w.currentModeValid=true;w.modePixelWidth=1920;w.modePixelHeight=1080;w.refreshRate=60;w.displayScale=w.pixelDensityX=w.pixelDensityY=1;
 auto baseline=Values();baseline["r_fullscreen"]=true;baseline["r_multiSamples"]=16.0;baseline["r_swapInterval"]=0.0;auto target=baseline;target["image_downSize"]=true;
 std::string e;SystemDisplayPlan plan;StateValues saved;CHECK(BuildDisplayRestore(actual,topo,plan,e)&&CaptureDisplayRecovery(plan,topo,saved,e));
 CHECK(ValidateDisplayPreserveActualPair(saved,saved,baseline,target,e));CHECK(!ValidateDisplayRecoveryPair(saved,saved,target,e));
 auto different=actual;different.window.windowX++;SystemDisplayPlan other;StateValues changed;CHECK(BuildDisplayRestore(different,topo,other,e)&&CaptureDisplayRecovery(other,topo,changed,e));CHECK(!ValidateDisplayPreserveActualPair(saved,changed,baseline,target,e));
 target["r_multiSamples"]=4.0;CHECK(!ValidateDisplayPreserveActualPair(saved,saved,baseline,target,e));target=baseline;target["r_rendererUploadMegs"]=32.0;
 auto bad=saved;bad["extra"]=true;CHECK(!ValidateDisplayPreserveActualPair(bad,bad,baseline,target,e));
 target.erase("r_bloom");CHECK(!ValidateDisplayPreserveActualPair(saved,saved,baseline,target,e));
 // Historical descriptors alone do not authorize a missing current monitor.
 SystemDisplayPlan resolved;auto missing=topo;missing.displays.clear();CHECK(!ResolveDisplayRecovery(saved,missing,resolved,e));
}
int main(){
 std::set_terminate([]{std::fprintf(stderr,"FAIL unexpected terminate at check %u, fault point %u, allocation countdown %ld\n",checks,faultPoint,failAfter);std::_Exit(90);});
#if defined(_MSC_VER)
 _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 for(int kind:{_CRT_WARN,_CRT_ERROR,_CRT_ASSERT}){(void)kind;_CrtSetReportMode(kind,_CRTDBG_MODE_FILE);_CrtSetReportFile(kind,_CRTDBG_FILE_STDERR);}
#endif
 CatalogAndPlans();RoundTrips();Forgery();Bounds();ExactNumbers();SchemaOne();PreserveDisplay();AllocationAtomicity();std::printf("PASS %u settings effect journal checks\n",checks);}
