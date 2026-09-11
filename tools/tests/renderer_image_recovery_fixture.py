# Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
# Counted CPU boundaries shared by the original restart and owned recovery tests.
def extend_support(base):
 s=base.replace('#include "RendererConsumedPolicy.h"','#include "RendererConsumedPolicy.h"\n#include "RendererImageRecovery.h"\n#include "../idlib/CryptoHash.h"')
 s=s.replace('enum {TT_2D,TT_CUBIC};',r'''
enum {TT_2D=1,TT_CUBIC=2};using textureUsage_t=int;
static bool sourceRefused=false;static int cpuReads=0,cpuTakes=0;
imagePortableContent_t Content(){imagePortableContent_t c{};c.version=1;c.scope=IPC_DIRECT_SOURCE;c.file.kind=IFC_DIRECT_DDS;
 std::memcpy(c.file.qpath,"textures/owned.dds",19);c.file.bytes=208;c.file.digest.bytes[0]=1;c.binary.version=1;c.binary.textureType=TT_2D;
 c.binary.format=7;c.binary.width=c.binary.height=8;c.binary.levels=4;c.binary.layers=1;c.binary.payloadBytes=56;c.binary.digest.bytes[0]=2;
 c.mipmaps=true;c.reduction={8,8,8,8,8,8,4,0,IR_EXACT};return c;}
struct idBinaryImage{imagePortableContent_t value{};explicit idBinaryImage(const char*){};
 bool GetContentIdentity(imageBinaryContent_t& out)const{out=value.binary;return true;}
 void SwapContent(idBinaryImage& other)noexcept{std::swap(value,other.value);}
};
bool R_CompleteConsumedImageUploads(){Boundary("complete");return context;}
uint64_t R_ConsumedPolicyObservationEpoch()noexcept{return 1;}
imageDownsizeInputs_t R_ReadImageDownsizeInputs(){return {};}
void R_ResolveImageDownsizePolicy(const imageDownsizeInputs_t& p,const char*,textureUsage_t,bool,imageDownsizePolicy_t& out){out={p.downSize?p.downSizeLimit:0,0,1};}
bool R_ImageReductionIsExact(const imageDownsizePolicy_t& p,const imageReductionResult_t& r){return r.status==IR_EXACT&&(!p.maxDimension||r.selectedWidth<=p.maxDimension);}
bool R_ReconstructImageContent(const imagePortableContent_t& c,idBinaryImage& out){++cpuReads;Boundary("read");if(sourceRefused)return false;out.value=c;return true;}
bool R_LoadPrecompressedDDS(const char*,idBinaryImage& out,void*,textureUsage_t,const imageDownsizePolicy_t& policy,bool,imageReductionResult_t* r,const imageFileContent_t*){
 ++cpuReads;Boundary("read");if(sourceRefused)return false;out.value=Content();out.value.resolved=policy;
 if(policy.maxDimension==4){out.value.binary.width=out.value.binary.height=4;out.value.binary.levels=3;out.value.reduction={8,8,4,4,4,4,4,1,IR_EXACT};}
 *r=out.value.reduction;return true;}
''')
 s=s.replace('idImageOpts opts;bool loaded',r'''std::string name="image";imagePortableContent_t portable=Content();bool observable=true;
 const char* GetName()const{return name.c_str();}imageDeclaredPolicy_t GetDeclaredPolicy()const{return {0,0,0,0,0,true};}
 bool GetPortableContent(imagePortableContent_t& out)const{if(!observable)return false;out=portable;return true;}
 idImageOpts opts;bool loaded''')
 s=s.replace('++reloads;',r'''++reloads;
  if(R_ImagePolicyUsesPreparedContent()){const idBinaryImage* binary=nullptr;imagePortableContent_t d{};
   if(R_ImagePolicyBorrowPreparedContent(this,binary,d)!=1)return;++cpuTakes;portable=d;opts.width=d.binary.width;opts.height=d.binary.height;opts.numLevels=d.binary.levels;}
''')
 s=s.replace('bool IsImplicit()const{return implicit;}', 'bool GetConsumedPolicy(materialConsumedPolicy_t& out)const{out={};out.parsed=true;return state==DS_PARSED;}bool IsImplicit()const{return implicit;}')
 return s
