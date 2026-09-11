// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/renderer/RendererImageRecovery.h"
#define ID_INLINE inline
#include "src/renderer/ImageOpts.h"
#include <iostream>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <new>
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
using namespace openq4::imageRecovery;
static size_t checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(false)
static Data Fixture(size_t count=1){
    Data d;d.direction=1;d.attempt="0123456789abcdef0123456789abcdef";d.policy.usePrecompressedTextures=1;
    for(size_t n=0;n<count;++n){Image i;char name[100];std::snprintf(name,sizeof(name),"textures/recovery/synthetic_cohort/room_%06zu_diffuse.dds",n);
        i.name=name;i.resident=true;i.allowDownSize=true;auto& c=i.content;c.version=1;c.scope=IPC_DIRECT_SOURCE;c.file.kind=IFC_DIRECT_DDS;
        std::memcpy(c.file.qpath,name,std::strlen(name)+1);c.file.bytes=208;c.file.digest.bytes[0]=1;c.binary.version=1;c.binary.textureType=TT_2D;
        c.binary.format=FMT_DXT1;c.binary.width=c.binary.height=8;c.binary.layers=1;c.binary.levels=4;c.binary.payloadBytes=56;
        c.binary.digest.bytes[0]=2;c.mipmaps=true;c.reduction={8,8,8,8,8,8,4,0,IR_EXACT};d.images.push_back(i);
    }
    for(size_t n=0;n<count;++n){Material m;char name[100];std::snprintf(name,sizeof(name),"materials/recovery/room_%06zu",n);m.name=name;
        m.file="materials/synthetic_cohort.mtr";m.state=2;m.line=1;m.observed=true;m.sourceBytes=40;m.source.bytes[0]=3;d.materials.push_back(m);}
    return d;
}
static void ReCRC(std::string& s){uint32_t crc=CRC(std::string_view(s).substr(0,s.size()-4));for(unsigned n=0;n<4;++n)s[s.size()-4+n]=char(crc>>(8*n));}
int main(){try{
    auto d=Fixture();std::string raw="unchanged",error="";CHECK(Encode(d,raw,error));Record r;CHECK(Decode(raw,1,d.attempt,r,error));
    CHECK(r.Get()&&r.Get()->images.size()==1&&SameImageKey(d.images[0],r.Get()->images[0])&&SameMaterial(d.materials[0],r.Get()->materials[0]));
    const Data* original=r.Get();for(size_t n=0;n<raw.size();++n){CHECK(!Decode(std::string_view(raw).substr(0,n),1,d.attempt,r,error));CHECK(r.Get()==original);}
    CHECK(!Decode(raw+"x",1,d.attempt,r,error));CHECK(!Decode(raw,2,d.attempt,r,error));CHECK(!Decode(raw,1,std::string(32,'1'),r,error));
    std::string changed=raw;changed[48]=2;ReCRC(changed);CHECK(!Decode(changed,1,d.attempt,r,error));CHECK(r.Get()==original);
    Values fields;CHECK(Pack(raw,1,d.attempt,fields));std::string back="old";CHECK(Unpack(fields,1,d.attempt,back));CHECK(back==raw);
    CHECK(!Frame(raw,2,d.attempt));CHECK(!Pack(raw,2,d.attempt,fields));
    changed=raw;changed[60]^=1;CHECK(!Frame(changed,1,d.attempt));CHECK(!Pack(changed,1,d.attempt,fields));
    auto bad=fields;bad["codec"]=std::string("openq4.image-recovery.2");CHECK(!Unpack(bad,1,d.attempt,back));CHECK(back==raw);
    bad=fields;bad["direction"]=2.0;CHECK(!Unpack(bad,1,d.attempt,back));bad=fields;bad["bytes"]=1.5;CHECK(!Unpack(bad,1,d.attempt,back));
    bad=fields;bad["chunks"]=2.0;CHECK(!Unpack(bad,1,d.attempt,back));bad=fields;bad.erase(Key(0));CHECK(!Unpack(bad,1,d.attempt,back));
    bad=fields;bad[Key(0)]=std::string("0001:")+std::get<std::string>(fields.at(Key(0))).substr(5);CHECK(!Unpack(bad,1,d.attempt,back));
    const std::string good=raw;d.images.push_back(d.images[0]);CHECK(!Encode(d,raw,error));CHECK(raw==good);
    d=Fixture();d.images[0].content.scope=IPC_CACHE_PIXELS_ONLY;CHECK(!Encode(d,raw,error));CHECK(raw==good);
    d=Fixture();d.images[0].resident=false;CHECK(!Encode(d,raw,error));CHECK(raw==good);
    d=Fixture();d.images[0].content.file.qpath[0]='\\';CHECK(!Encode(d,raw,error));CHECK(raw==good);
    for(size_t count:{size_t{1719},size_t{2500}}){auto large=Fixture(count);std::string encoded(0,'\0');CHECK(Encode(large,encoded,error));
        CHECK(Pack(encoded,1,large.attempt,fields));CHECK(Unpack(fields,1,large.attempt,back));CHECK(back==encoded);
        CHECK(Decode(back,1,large.attempt,r,error));CHECK(r.Get()->images.size()==count&&r.Get()->materials.size()==count);
        std::cout<<"inventory "<<count<<" images + "<<count<<" materials raw="<<encoded.size()<<" base64="<<Base64(encoded).size()<<" fields="<<fields.size()<<'\n';
        bad=fields;std::swap(bad.at(Key(0)),bad.at(Key(1)));CHECK(!Unpack(bad,1,large.attempt,back));
        bad=fields;bad[Key(2)]=bad.at(Key(1));CHECK(!Unpack(bad,1,large.attempt,back));
        bad=fields;std::get<std::string>(bad.at(Key(0)))+='A';CHECK(!Unpack(bad,1,large.attempt,back));
    }
    auto tooBig=Fixture(8000);CHECK(!Encode(tooBig,raw,error));CHECK(raw==good);
    for(std::string s:{"A===","AB==","AAA=AAAA","AAAA\n","AAAA====","AA=A","AAB=","!!!!"}){back="pristine";CHECK(!Unbase64(s,back));CHECK(back=="pristine");}
    for(size_t n=1;n<100;++n){std::string bytes(n,'\0');for(size_t k=0;k<n;++k)bytes[k]=char(k);CHECK(Unbase64(Base64(bytes),back));CHECK(back==bytes);}

    // Exact transport limits are independent of renderer semantics: structurally
    // valid padding is transportable but is never accepted as a full record.
    for(size_t n:{size_t{3072},size_t{3073},RawMaxBytes-1,RawMaxBytes}){
        std::string frame(n,'x');std::copy(good.begin(),good.begin()+HeaderBytes,frame.begin());
        for(unsigned k=0;k<4;++k)frame[12+k]=char(n>>(k*8));
        ReCRC(frame);
        CHECK(Frame(frame,1,d.attempt));CHECK(Pack(frame,1,d.attempt,fields));CHECK(Unpack(fields,1,d.attempt,back)&&back==frame);
        const Data* previous=r.Get();CHECK(!Decode(frame,1,d.attempt,r,error)&&r.Get()==previous);
    }
    std::string oversized(RawMaxBytes+1,'x');std::copy(good.begin(),good.begin()+HeaderBytes,oversized.begin());
    for(unsigned k=0;k<4;++k)oversized[12+k]=char(oversized.size()>>(k*8));
    ReCRC(oversized);
    const Values previousFields=fields;CHECK(!Pack(oversized,1,d.attempt,fields)&&fields==previousFields);
    d=Fixture();CHECK(Encode(d,raw,error));
    for(int kind=0;kind<6;++kind){auto invalid=d;auto& c=invalid.images[0].content;
        if(kind==0)c.reduction.selectedWidth=4;
        if(kind==1)c.file.qpath[0]=0;
        if(kind==2)c.usage=1;
        if(kind==3)c.binary.layers=6;
        if(kind==4)c.file.kind=IFC_OBSERVED_BIMAGE;
        if(kind==5)invalid.materials[0].observed=false;
        const std::string previous=raw;CHECK(!Encode(invalid,raw,error)&&raw==previous);
    }
    // Sweep through every fallible construction, including the successful
    // return boundary under MSVC's allocating debug iterator proxies.
    bool encoded=false,decoded=false;
    for(long point=0;point<1000;++point){std::string stable="untouched";failAfter=point;
        bool ok=Encode(d,stable,error);failAfter=-1;if(ok){CHECK(stable==raw);encoded=true;break;}CHECK(stable=="untouched");}
    for(long point=0;point<1000;++point){const Data* previous=r.Get();failAfter=point;
        bool ok=Decode(raw,1,d.attempt,r,error);failAfter=-1;if(ok){CHECK(r.Get()!=previous);decoded=true;break;}CHECK(r.Get()==previous);}
    CHECK(encoded&&decoded);
    std::cout<<"PASS "<<checks<<" image recovery codec checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<checks<<" "<<e.what()<<'\n';return 1;}}
