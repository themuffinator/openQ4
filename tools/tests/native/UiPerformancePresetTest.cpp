// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/framework/PerformancePreset.h"
#include "src/ui/application/SettingsTransaction.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif
using namespace openq4;
using namespace openq4::ui;
// One-shot allocation failures exercise every actual staged allocation without
// making error reporting itself impossible. Never changes FP/device state.
static long failAllocation=-1;
void* operator new(std::size_t size) {
 if(failAllocation==0){failAllocation=-1;throw std::bad_alloc();}
 if(failAllocation>0)--failAllocation;
 if(void* p=std::malloc(size?size:1))return p;
 throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
static int checks=0;
static void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static void Profiles(){
 const std::vector<std::string> keys={"com_machineSpec","r_rendererBenchmarkPreset","r_screenFraction","r_multiSamples","r_postAA","com_maxfps","image_anisotropy","image_usePrecompressedTextures","image_downSize","image_downSizeLimit","image_downSizeSpecular","image_downSizeBump","image_downSizeSpecularLimit","image_downSizeBumpLimit","image_ignoreHighQuality","image_writeGeneratedImages","s_maxSoundsPerShader","r_useShadowMap","r_shadowMapSize","r_shadowMapMaxUpdatesPerView","r_bloom","r_ssao","r_hdrToneMap","r_motionBlur","r_crt","r_useLightGrid","r_rendererUploadMegs","r_rendererUploadFrameBuffers","s_numberOfSpeakers","s_useEAXReverb","s_maxEmitterChannels","com_performancePreset"};
 // Independent golden setter values from the pre-refactor production profiles.
 const std::vector<std::vector<int>> numbers={
 {0,50,0,0,30,1,1,1,512,1,1,64,256,1,1,1,0,512,1,0,0,0,0,0,1,8,3,2,0,24},
 {0,75,0,0,30,1,1,1,1024,1,1,64,256,1,1,1,0,512,1,0,0,0,0,0,1,8,3,2,0,32},
 {1,85,0,1,60,2,1,0,0,0,0,64,0,0,1,0,0,1024,2,0,0,0,0,0,1,16,4,2,0,40},
 {2,100,2,1,120,4,1,0,0,0,0,64,0,0,1,0,0,1024,0,0,0,0,0,0,1,16,4,6,1,48},
 {3,100,4,1,144,8,1,0,0,0,0,64,0,0,1,0,0,1024,0,0,0,0,0,0,1,32,4,6,1,48},
 {3,100,8,1,240,16,1,0,0,0,0,64,0,0,1,0,0,2048,0,0,0,0,0,0,1,32,4,6,1,48}};
 const char* names[]={"minimum","lowpower","performance","balanced","quality","ultra"};
 const char* renderer[]={"low","low","baseline","baseline","modern","high-end"};
 std::set<std::string> targets;for(const auto* key:PerformancePresetTargets())targets.insert(key);
 Check(targets.size()==32,"exact unique target count");
 for(int n=0;n<6;++n){
  Check(FindPerformancePreset(names[n])==&PerformancePresets()[n],"canonical lookup");
  PerformancePresetAssignments out;std::string error;
  Check(ExpandPerformancePreset(PerformancePresets()[n],out,error),"expand actual profile");
  std::set<std::string> seen;size_t index=0;
  for(size_t i=0;i<out.size();++i){
   Check(out[i].key==keys[i],"exact legacy setter order");seen.insert(out[i].key);
   if(i==1)Check(std::get<std::string>(out[i].value)==renderer[n],"benchmark classification");
   else if(i==31)Check(std::get<std::string>(out[i].value)==names[n],"marker last");
   else Check(std::get<int>(out[i].value)==numbers[n][index++],"golden profile value");
  }Check(seen==targets,"all and only declared targets");
 }
 Check(FindPerformancePreset("PeRfOrMaNcE")==FindPerformancePreset("performance"),"legacy case folding");
 for(const auto& name:std::vector<std::string>{"","missing","balanced ",std::string("quality\0evil",12),std::string(10000,'x')})Check(!FindPerformancePreset(name),"unknown name rejected");
 PerformancePresetAssignments output;std::string error;Check(ExpandPerformancePreset(PerformancePresets()[0],output,error),"sentinel");
 auto saved=output;auto invalid=PerformancePresets()[1];invalid.name=nullptr;
 Check(!ExpandPerformancePreset(invalid,output,error),"invalid source name");
 for(size_t i=0;i<32;++i)Check(output[i].key==saved[i].key && output[i].value==saved[i].value,"failed expansion unchanged");
 auto normalization=PerformancePresets()[2];normalization.anisotropy=17;
 Check(ExpandPerformancePreset(normalization,output,error)&&std::get<int>(output[6].value)==17,"legacy normalization probe reaches setter unchanged");
}
static void Detection(){
 const std::vector<int> ram={-1,0,1,4095,4096,4097,8191,8192,8193,16383,16384,16385,4194304,4194305};
 const std::vector<int> video={-1,0,1,1023,1024,1025,2047,2048,2049,6143,6144,6145,262144,262145};
 for(int flags=0;flags<32;++flags)for(int r:ram)for(int v:video){
  PerformancePresetSignals s;s.explicitLowPower=flags&1;s.raspberryPi=flags&2;s.steamDeck=flags&4;s.legacyRenderer=flags&8;s.arm64=flags&16;s.systemRamMB=r;s.videoRamMB=v;
  const int er=r<=0||r>4194304?8192:r,ev=v<=0||v>262144?2048:v;
  const bool constrained=er<=4096||ev<=1024;
  const char* expected=s.explicitLowPower||s.raspberryPi?"lowpower":s.steamDeck?"performance":s.legacyRenderer?"minimum":constrained?"lowpower":s.arm64?"performance":er<=8192||ev<=2048?"performance":er>=16384&&ev>=6144?"quality":"balanced";
  const auto result=DetectPerformancePreset(s);
  Check(result.preset && std::string(result.preset->name)==expected,"detection priority/boundary matrix");
  Check(result.systemRamMB==er&&result.videoRamMB==ev,"bounded memory fallback");
 }
}
struct Host:SettingsHost{
 StateValues live={{"number",1.0},{"flag",true},{"label",std::string("custom")}};
 std::function<void()> validation;int writes=0,reads=0;bool reject=false;
 bool Read(StateValues& out,std::string&)override{++reads;out=live;return true;}
 bool Defaults(StateValues& out,std::string&)override{out=live;return true;}
 bool Validate(const StateValues&,const StateValues& value,std::string&)override{if(validation)validation();return !reject&&std::get<double>(value.at("number"))<=2;}
 bool Write(const StateValues&,std::string&)override{++writes;return false;}
 bool NeedsConfirmation(const StateValues&,const StateValues&)const override{return false;}
};
static void Generated(){
 Host host;SettingsTransaction tx(host);int called=0;
 SettingsTransaction::EditGenerator good=[&](StateValues& out,std::string&){++called;out={{"number",1.5}};return true;};
 Check(tx.EditGenerated(1,good).code==SettingsCode::NotOpen&&called==0,"closed never observes capability");
 Check(tx.Begin(1).code==SettingsCode::Ok,"begin");const auto baseline=tx.Baseline();
 for(auto owner:{0u,2u})Check(tx.EditGenerated(owner,good).code!=SettingsCode::Ok&&called==0,"wrong owner never invokes generator");
 Check(tx.EditGenerated(1,{}).code==SettingsCode::Invalid,"empty producer");
 Check(tx.EditGenerated(1,good).code==SettingsCode::Ok,"valid generated edit");const auto saved=tx.Draft();
 Check(saved.at("number")==StateValue(1.5)&&saved.at("label")==baseline.at("label")&&host.writes==0&&host.reads==1,"draft-only change");
 for(const auto& partial:std::vector<StateValues>{{{"unknown",true}},{{"number",true}},{{"number",std::numeric_limits<double>::infinity()}},{{"number",std::numeric_limits<double>::quiet_NaN()}},{{"number",3.0}},{{"label",std::string("x\0y",3)}}}){
  auto result=tx.EditGenerated(1,[&](StateValues& out,std::string&){out=partial;return true;});
  Check(result.code==SettingsCode::Invalid&&SettingsValuesEqual(tx.Draft(),saved),"invalid complete candidate unchanged");
 }
 for(int failure=0;failure<3;++failure){
  host.reject=failure==2;
  auto result=tx.EditGenerated(1,[&](StateValues& out,std::string&)->bool{out={{"number",1.9}};if(failure==1)throw std::runtime_error("generator");return failure!=0;});
  Check(result.code==SettingsCode::Invalid&&SettingsValuesEqual(tx.Draft(),saved),"generator/validation failure atomic");
 }host.reject=false;
 const std::vector<std::function<SettingsResult()>> nested={
 [&]{return tx.Begin(1);},[&]{return tx.Edit(1,{});},[&]{return tx.EditGenerated(1,good);},[&]{return tx.Defaults(1);},[&]{return tx.Cancel(1);},[&]{return tx.Apply(1,0);},[&]{return tx.Confirm(1);},[&]{return tx.Revert(1);},[&]{return tx.Tick(0);},[&]{return tx.Abandon(1);},
 [&]{SettingsAttempt a;return tx.PrepareApply(1,0,a);},[&]{return tx.ExecuteApply(1,1);},[&]{return tx.CompleteApply(1,1,0);},[&]{return tx.CancelPreparedApply(1,1);},[&]{SettingsAttempt a;return tx.PrepareRestore(1,1,a);},[&]{return tx.ExecuteRestore(1,1);},[&]{return tx.CompleteRestore(1,1);},[&]{SettingsAttempt a;return tx.PrepareConfirm(1,1,0,a);},[&]{return tx.CompleteConfirm(1,1);},[&]{return tx.CancelPreparedConfirm(1,1);}};
 for(const auto& enter:nested)for(bool duringValidation:{false,true}){
  auto reenter=[&]{Check(enter().code==SettingsCode::Busy,"all nested mutations refused");};
  if(duringValidation)host.validation=reenter;
  auto result=tx.EditGenerated(1,[&](StateValues& out,std::string&){if(!duringValidation)reenter();out={{"number",1.9}};return true;});
  host.validation={};Check(result.code==SettingsCode::Busy&&SettingsValuesEqual(tx.Draft(),saved),"ignored reentry invalidates outer generated edit");
 }
 // Ordinary Edit still permits its historical ignored Busy callback response.
 host.validation=[&]{Check(tx.Cancel(1).code==SettingsCode::Busy,"legacy callback busy");};
 Check(tx.Edit(1,{{"number",1.5}}).code==SettingsCode::Ok,"ordinary edit behavior unchanged");host.validation={};
 bool succeeded=false;int failures=0;
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
 constexpr bool debugReturnOnly=true;
#else
 constexpr bool debugReturnOnly=false;
#endif
 for(int n=0;n<256&&!succeeded;++n){
  SettingsTransaction::EditGenerator allocating=[](StateValues& out,std::string&){out.emplace("number",1.9);out.emplace("label",std::string(600,'p'));return true;};
  SettingsCode code=SettingsCode::Invalid;
  // MSVC debug string moves allocate inside noexcept. The normal STL sweep
  // covers construction; debug mode targets the final real return-storage
  // proxy by arming at the last read-only validation callback, before publish.
  if(debugReturnOnly)host.validation=[&]{failAllocation=n;};else failAllocation=n;
  try{code=tx.EditGenerated(1,allocating).code;}catch(const std::bad_alloc&){failAllocation=-1;std::fprintf(stderr,"allocation failure index %d, draft equal %d\n",n,SettingsValuesEqual(tx.Draft(),saved));Check(false,"allocation exception escaped generated edit");}
  const auto remaining=failAllocation;failAllocation=-1;host.validation={};
  if(code==SettingsCode::Ok){succeeded=true;Check(remaining==0,"success reached only after sweeping actual return allocation");}else{++failures;Check(SettingsValuesEqual(tx.Draft(),saved),"each allocation failure preserves draft");Check(tx.LastResult().code==code,"failure result is not a published success");}
 }
 Check(succeeded&&failures>=(debugReturnOnly?1:6)&&tx.LastResult().code==SettingsCode::Ok&&tx.LastResult().diagnostic.empty(),"every prepublication/return allocation fault reached; final success published");
 std::printf("generated edit swept %d failing allocation positions before complete return/publication (debug return-only=%d)\n",failures,debugReturnOnly);
 Check(host.writes==0&&host.live==baseline&&tx.Owner()==1&&tx.Phase()==SettingsPhase::Editing,"no host effects throughout failures");
 SettingsAttempt attempt;Check(tx.PrepareApply(1,0,attempt).code==SettingsCode::Ok,"freeze for device");const int before=called;
 Check(tx.EditGenerated(1,good).code==SettingsCode::Busy&&called==before,"frozen device transaction never observes profile");
}
int main(){
#if defined(_WIN32) && defined(_DEBUG)
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
 _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
#endif
 Profiles();Detection();Generated();std::printf("performance profiles and generated transaction: %d checks passed\n",checks);}
