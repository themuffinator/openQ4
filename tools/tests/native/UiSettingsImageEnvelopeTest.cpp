// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Reuse the existing complete 53-key/placement fixture and allocation boundary.
#define main PreservedJournalSuiteMain
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4715 4716)
#endif
#include "UiSettingsEffectJournalTest.cpp"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
#undef main
#include "src/renderer/RendererImageRecovery.h"
namespace ir=openq4::imageRecovery;
static const char* allocationPhase="ordinary";
static unsigned allocationPoint=0;
static ir::Data Cohort(size_t count,const std::string& attempt) {
 ir::Data d;d.attempt=attempt;d.direction=1;d.policy.usePrecompressedTextures=1;
 for(size_t n=0;n<count;++n){ir::Image i;char path[100];std::snprintf(path,sizeof(path),"textures/recovery/synthetic_cohort/room_%06zu_diffuse.dds",n);i.name=path;i.allowDownSize=true;i.resident=true;
  auto& c=i.content;c.version=1;c.scope=IPC_DIRECT_SOURCE;c.file.kind=IFC_DIRECT_DDS;c.file.bytes=208;std::memcpy(c.file.qpath,path,std::strlen(path)+1);c.file.digest.bytes[0]=1;
  c.binary.version=1;c.binary.textureType=1;c.binary.format=7;c.binary.width=c.binary.height=8;c.binary.levels=4;c.binary.layers=1;c.binary.payloadBytes=56;c.binary.digest.bytes[0]=2;
  c.mipmaps=true;c.reduction={8,8,8,8,8,8,4,0,IR_EXACT};d.images.push_back(i);
  ir::Material m;std::snprintf(path,sizeof(path),"materials/recovery/room_%06zu",n);m.name=path;m.file="materials/synthetic_cohort.mtr";m.state=2;m.observed=true;m.sourceBytes=40;m.source.bytes[0]=3;d.materials.push_back(m);
 }return d;
}
int main(){
 std::set_terminate([]{std::fprintf(stderr,"FAIL image envelope terminate at check %u phase %s point %u countdown %ld\n",checks,allocationPhase,allocationPoint,failAfter);std::_Exit(90);});
 const auto& schema=SystemSettingsHost::Schema();std::string error;
 for(size_t count:{size_t{1903},size_t{2500}}){auto j=Sample(31);auto d=Cohort(count,j.attempt);std::string restore,target;
  CHECK(ir::Encode(d,restore,error));d.direction=2;CHECK(ir::Encode(d,target,error));
  CHECK(PackSettingsImageRecovery(restore,1,j.attempt,j.imageRestore));CHECK(PackSettingsImageRecovery(target,2,j.attempt,j.imageTarget));
  // Fill every other opaque domain to nearly its existing raw bound. Include
  // both display maps; placement remains its real eighteen-field contract.
  for(auto* map:{&j.displayRestore,&j.displayTarget,&j.resourceRestore,&j.resourceTarget,&j.audioRestore,&j.audioTarget,&j.deferredRestore,&j.deferredTarget})
   *map={{"bounded",std::string(SettingsEffectMetadataMaxBytes-7,'x')}};
  const auto encoded=Encode(j);CHECK(encoded.size()<=SettingsJournalMaxBytes);SettingsJournalRecord decoded;
  CHECK(DecodeSettingsJournalRecord(encoded,schema,decoded,error));CHECK(decoded.Schema()==2);
  const auto& actual=std::get<SettingsEffectRecoveryJournal>(*decoded.Value());std::string back;
  CHECK(UnpackSettingsImageRecovery(actual.imageRestore,1,j.attempt,back)&&back==restore);ir::Record record;CHECK(ir::Decode(back,1,j.attempt,record,error));
  CHECK(record.Get()->images.size()==count&&record.Get()->materials.size()==count);
  std::printf("complete journal %zu images + %zu materials per direction: %zu bytes\n",count,count,encoded.size());
  auto invalid=j;invalid.imageTarget=j.imageRestore;std::string output="preserved";CHECK(!EncodeSettingsEffectJournal(invalid,schema,output,error)&&output=="preserved");
  invalid=j;std::swap(invalid.imageRestore.at(ir::Key(0)),invalid.imageRestore.at(ir::Key(1)));CHECK(!EncodeSettingsEffectJournal(invalid,schema,output,error));
  invalid=j;invalid.imageRestore.erase(ir::Key(1));CHECK(!EncodeSettingsEffectJournal(invalid,schema,output,error));
  invalid=j;invalid.imageRestore["codec"]=std::string("not.the.codec");CHECK(!EncodeSettingsEffectJournal(invalid,schema,output,error));
  // Escaping is counted by the actual final JSON encoder, not raw metadata
  // sizes or optimistic base64 estimates. A valid per-domain input can still
  // exceed the atomic file bound and must preserve the previous bytes.
  invalid=j;for(auto* map:{&invalid.displayRestore,&invalid.displayTarget,&invalid.resourceRestore,&invalid.resourceTarget,&invalid.audioRestore,&invalid.audioTarget,&invalid.deferredRestore,&invalid.deferredTarget})
   *map={{"bounded",std::string(SettingsEffectMetadataMaxBytes-7,'\1')}};
  CHECK(!EncodeSettingsEffectJournal(invalid,schema,output,error)&&output=="preserved");
  invalid=j;invalid.audioRestore={{"codec",std::string("openq4.image-recovery.1")},{"chunk",std::string(65536,'A')}};
  CHECK(!EncodeSettingsEffectJournal(invalid,schema,output,error));
 }
 auto small=Cohort(1,"0123456789abcdef0123456789abcdef");std::string raw;CHECK(ir::Encode(small,raw,error));StateValues output{{"untouched",true}};
 allocations=0;CHECK(PackSettingsImageRecovery(raw,1,small.attempt,output));const auto count=allocations;
 for(unsigned n=0;n<count;++n){StateValues stable{{"untouched",true}};allocationPhase="pack";allocationPoint=n;failAfter=n;const bool ok=PackSettingsImageRecovery(raw,1,small.attempt,stable);failAfter=-1;
  CHECK(!ok&&stable.size()==1&&stable.contains("untouched"));}
 std::string bytes;allocations=0;CHECK(UnpackSettingsImageRecovery(output,1,small.attempt,bytes));const auto reads=allocations;
 for(unsigned n=0;n<reads;++n){std::string stable="untouched";allocationPhase="unpack";allocationPoint=n;failAfter=n;const bool ok=UnpackSettingsImageRecovery(output,1,small.attempt,stable);failAfter=-1;CHECK(!ok&&stable=="untouched");}
 std::printf("PASS %u image journal envelope checks\n",checks);return 0;
}
