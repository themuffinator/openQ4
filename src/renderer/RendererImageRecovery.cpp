// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "RendererImageRecovery.h"
// ImageOpts also defines inline convenience methods. This pure codec does not
// depend on the engine precompiled header which normally supplies ID_INLINE.
#ifndef ID_INLINE
#define ID_INLINE inline
#define OQ4_RECOVERY_LOCAL_INLINE
#endif
#include "ImageOpts.h"
#ifdef OQ4_RECOVERY_LOCAL_INLINE
#undef OQ4_RECOVERY_LOCAL_INLINE
#undef ID_INLINE
#endif
#include <algorithm>
#include <set>
#include <tuple>

namespace openq4::imageRecovery {
namespace {
bool Fail(std::string& e,const char* text) noexcept {try{e=text;}catch(...){e.clear();}return false;}
struct Writer {
    std::string bytes=std::string(0,'\0');
    void Number(uint32_t n){for(unsigned i=0;i<4;++i)bytes+=char(n>>(8*i));}
    void Wide(uint64_t n){Number(uint32_t(n));Number(uint32_t(n>>32));}
    void Text(const std::string& s){Number(uint32_t(s.size()));bytes+=s;}
    void Digest(const imageContentDigest_t& d){bytes.append(reinterpret_cast<const char*>(d.bytes),32);}
};
struct Reader {
    std::string_view bytes;size_t pos=0;
    bool Read(void* out,size_t n){if(n>bytes.size()-pos)return false;std::memcpy(out,bytes.data()+pos,n);pos+=n;return true;}
    bool Number(uint32_t& n){if(bytes.size()-pos<4)return false;n=U32(bytes,pos);pos+=4;return true;}
    bool Int(int& n){uint32_t v;if(!Number(v)||v>0x7fffffffu)return false;n=int(v);return true;}
    bool Bool(bool& b){uint32_t n;if(!Number(n)||n>1)return false;b=n!=0;return true;}
    bool Wide(uint64_t& n){uint32_t lo,hi;if(!Number(lo)||!Number(hi))return false;n=lo|(uint64_t(hi)<<32);return true;}
    bool Text(std::string& s,size_t max){uint32_t n;if(!Number(n)||n>max||n>bytes.size()-pos)return false;s.assign(bytes.data()+pos,n);pos+=n;return true;}
    bool Digest(imageContentDigest_t& d){return Read(d.bytes,32);}
};
bool Text(const std::string& s,size_t max,bool empty=false){
    if(s.size()>max||(!empty&&s.empty()))return false;
    for(unsigned char c:s)if(c<32||c>126)return false;
    return true;
}
bool ZeroDigest(const imageContentDigest_t& d){for(uint8_t b:d.bytes)if(b)return false;return true;}
bool EmptyContent(const imagePortableContent_t& c){
    const auto& b=c.binary;const auto& r=c.reduction;
    if(c.version||c.scope||c.file.kind||c.file.bytes||!ZeroDigest(c.file.digest)||b.version||b.textureType||b.format||b.colorFormat||
        b.width||b.height||b.levels||b.layers||b.payloadBytes||!ZeroDigest(b.digest)||c.resolved.maxDimension||c.resolved.mipShift||c.resolved.minDimension!=1||
        r.sourceWidth||r.sourceHeight||r.requestedWidth||r.requestedHeight||r.selectedWidth||r.selectedHeight||r.authoredLevels||r.firstLevel||r.status||c.usage||c.mipmaps)return false;
    for(char v:c.file.qpath)if(v)return false;
    return true;
}
bool Policy(const renderImagePolicy_t& p){return p.downSize>=0&&p.downSize<=1&&p.downSizeSpecular>=0&&p.downSizeSpecular<=1&&
    p.downSizeBump>=0&&p.downSizeBump<=1&&p.ignoreHighQuality>=0&&p.ignoreHighQuality<=1&&
    p.downSizeLimit>=0&&p.downSizeLimit<=32768&&p.downSizeSpecularLimit>=0&&p.downSizeSpecularLimit<=32768&&
    p.downSizeBumpLimit>=0&&p.downSizeBumpLimit<=32768&&p.usePrecompressedTextures>=0&&p.usePrecompressedTextures<=2;}
template<class F> void PolicyFields(renderImagePolicy_t& p,F f){f(p.downSize);f(p.downSizeLimit);f(p.downSizeSpecular);f(p.downSizeSpecularLimit);
    f(p.downSizeBump);f(p.downSizeBumpLimit);f(p.usePrecompressedTextures);f(p.ignoreHighQuality);}
bool Content(const imagePortableContent_t& c){
    const auto& b=c.binary;const auto& r=c.reduction;
    if(c.version!=1||!R_ImageFileContentValid(c.file)||b.version!=1||b.width<1||b.width>32768||b.height<1||b.height>32768||
        b.levels<1||b.levels>16||b.format<=FMT_NONE||b.format>FMT_MAX_VALID||b.colorFormat<CFM_DEFAULT||b.colorFormat>CFM_GREEN_ALPHA||
        ((b.textureType!=TT_2D||b.layers!=1)&&(b.textureType!=TT_CUBIC||b.layers!=6))||
        !b.payloadBytes||b.payloadBytes>IMAGE_CONTENT_MAX_BYTES||c.usage<0||c.usage>31)return false;
    if(c.scope==IPC_CACHE_PIXELS_ONLY)return c.file.kind==IFC_OBSERVED_BIMAGE&&!c.mipmaps&&!c.resolved.maxDimension&&!c.resolved.mipShift&&
        c.resolved.minDimension==1&&!r.sourceWidth&&!r.sourceHeight&&!r.requestedWidth&&!r.requestedHeight&&!r.selectedWidth&&!r.selectedHeight&&
        !r.authoredLevels&&!r.firstLevel&&r.status==IR_UNOBSERVED;
    return c.scope==IPC_DIRECT_SOURCE&&c.file.kind==IFC_DIRECT_DDS&&b.textureType==TT_2D&&c.resolved.maxDimension>=0&&c.resolved.maxDimension<=32768&&
        c.resolved.mipShift>=0&&c.resolved.mipShift<=30&&c.resolved.minDimension>=1&&c.resolved.minDimension<=32768&&
        r.sourceWidth>=1&&r.sourceWidth<=32768&&r.sourceHeight>=1&&r.sourceHeight<=32768&&r.status==IR_EXACT&&
        r.requestedWidth==b.width&&r.requestedHeight==b.height&&r.selectedWidth==b.width&&r.selectedHeight==b.height&&
        r.authoredLevels>=1&&r.authoredLevels<=16&&r.firstLevel>=0&&r.firstLevel<r.authoredLevels&&b.levels==(c.mipmaps?r.authoredLevels-r.firstLevel:1);
}
auto Key(const Image& i){return std::tie(i.name,i.filter,i.repeat,i.usage,i.cube,i.flags,i.allowDownSize);}
void WriteContent(Writer& w,const imagePortableContent_t& c){
    w.Number(c.scope);w.Number(c.file.kind);w.Wide(c.file.bytes);w.Text(std::string(c.file.qpath));w.Digest(c.file.digest);
    const auto& b=c.binary;for(int n:{b.textureType,b.format,b.colorFormat,b.width,b.height,b.levels,b.layers})w.Number(n);
    w.Wide(b.payloadBytes);w.Digest(b.digest);
    for(int n:{c.resolved.maxDimension,c.resolved.mipShift,c.resolved.minDimension,c.reduction.sourceWidth,c.reduction.sourceHeight,
        c.reduction.requestedWidth,c.reduction.requestedHeight,c.reduction.selectedWidth,c.reduction.selectedHeight,
        c.reduction.authoredLevels,c.reduction.firstLevel})w.Number(n);
    w.Number(c.reduction.status);w.Number(c.usage);w.Number(c.mipmaps);
}
bool ReadContent(Reader& r,imagePortableContent_t& c){
    c.version=c.binary.version=1;std::string qpath(0,'\0');
    if(!r.Number(c.scope)||!r.Number(c.file.kind)||!r.Wide(c.file.bytes)||!r.Text(qpath,511)||!r.Digest(c.file.digest))return false;
    std::memcpy(c.file.qpath,qpath.data(),qpath.size());
    auto& b=c.binary;for(int* n:{&b.textureType,&b.format,&b.colorFormat,&b.width,&b.height,&b.levels,&b.layers})if(!r.Int(*n))return false;
    if(!r.Wide(b.payloadBytes)||!r.Digest(b.digest))return false;
    for(int* n:{&c.resolved.maxDimension,&c.resolved.mipShift,&c.resolved.minDimension,&c.reduction.sourceWidth,&c.reduction.sourceHeight,
        &c.reduction.requestedWidth,&c.reduction.requestedHeight,&c.reduction.selectedWidth,&c.reduction.selectedHeight,
        &c.reduction.authoredLevels,&c.reduction.firstLevel})if(!r.Int(*n))return false;
    return r.Number(c.reduction.status)&&r.Int(c.usage)&&r.Bool(c.mipmaps)&&Content(c);
}
}
bool SameImageKey(const Image& a,const Image& b) noexcept{return Key(a)==Key(b);}
bool SameMaterial(const Material& a,const Material& b) noexcept{return a.name==b.name&&a.file==b.file&&a.state==b.state&&a.line==b.line&&
    a.implicit==b.implicit&&a.observed==b.observed&&a.ignoreHighQuality==b.ignoreHighQuality&&a.makingBuild==b.makingBuild&&
    a.sourceBytes==b.sourceBytes&&!std::memcmp(a.source.bytes,b.source.bytes,32);}
bool Encode(const Data& d,std::string& output,std::string& error){
    try{
        if(!Attempt(d.attempt)||(d.direction!=1&&d.direction!=2)||!Policy(d.policy)||d.images.size()>32768||d.materials.size()>32768)
            return Fail(error,"Invalid image recovery header");
        Writer w;w.bytes=Magic;w.Number(d.direction);w.Number(0);w.bytes+=d.attempt;
        w.Number(uint32_t(d.images.size()));w.Number(uint32_t(d.materials.size()));
        auto policy=d.policy;PolicyFields(policy,[&](int n){w.Number(n);});
        for(const auto& dep:d.dependencies){if(!Text(dep.value,4096,true))return Fail(error,"Unsupported image-loader dependency spelling");w.Text(dep.value);}
        const Image* previous=nullptr;
        for(const auto& i:d.images){
            if(!Text(i.name,511)||i.filter<0||i.filter>31||i.repeat<0||i.repeat>31||i.usage<0||i.usage>31||i.cube<0||i.cube>31||
                (previous&&!(Key(*previous)<Key(i)))||(i.resident&&(!Content(i.content)||i.content.usage!=i.usage)))return Fail(error,"Invalid or unordered recovery image");
            previous=&i;w.Text(i.name);for(int n:{i.filter,i.repeat,i.usage,i.cube})w.Number(n);w.Number(i.flags);w.Number(i.allowDownSize);w.Number(i.resident);
            if(i.resident)WriteContent(w,i.content);
            else if(!EmptyContent(i.content))return Fail(error,"An unloaded image carries content authority");
            if(w.bytes.size()>RawMaxBytes-4)return Fail(error,"Complete image inventory exceeds its durable budget");
        }
        std::string_view last;
        for(const auto& m:d.materials){
            if(!Text(m.name,511)||!Text(m.file,511,true)||m.state<0||m.state>3||m.line<0||m.sourceBytes>256*1024||
                (!last.empty()&&last>=m.name)||(!m.observed&&(m.ignoreHighQuality||m.makingBuild||m.sourceBytes||!ZeroDigest(m.source)))||
                (m.observed&&!m.sourceBytes))return Fail(error,"Invalid or unordered recovery material");
            last=m.name;w.Text(m.name);w.Text(m.file);w.Number(m.state);w.Number(m.line);w.Number(m.implicit);w.Number(m.observed);
            w.Number(m.ignoreHighQuality);w.Number(m.makingBuild);w.Number(m.sourceBytes);w.Digest(m.source);
            if(w.bytes.size()>RawMaxBytes-4)return Fail(error,"Complete material inventory exceeds its durable budget");
        }
        const uint32_t size=uint32_t(w.bytes.size()+4);for(unsigned n=0;n<4;++n)w.bytes[12+n]=char(size>>(n*8));w.Number(CRC(w.bytes));
        if(!Frame(w.bytes,d.direction,d.attempt))return Fail(error,"Image recovery framing failed");
        output.swap(w.bytes);error.clear();return true;
    }catch(...){return Fail(error,"Image recovery encoding allocation failed");}
}
bool Decode(std::string_view raw,unsigned direction,std::string_view attempt,Record& output,std::string& error){
    try{
        if(!Frame(raw,direction,attempt))return Fail(error,"Invalid image recovery framing");
        auto d=std::make_unique<Data>();d->direction=direction;d->attempt=attempt;
        Reader r{raw.substr(0,raw.size()-4),56};bool good=true;PolicyFields(d->policy,[&](int& n){good&=r.Int(n);});
        if(!good)return Fail(error,"Invalid image recovery policy");
        for(auto& dep:d->dependencies)if(!r.Text(dep.value,4096))return Fail(error,"Truncated image dependency");
        const size_t images=U32(raw,48),materials=U32(raw,52);
        for(size_t n=0;n<images;++n){Image i;
            if(!r.Text(i.name,511)||!r.Int(i.filter)||!r.Int(i.repeat)||!r.Int(i.usage)||!r.Int(i.cube)||!r.Number(i.flags)||
                !r.Bool(i.allowDownSize)||!r.Bool(i.resident)||(i.resident&&!ReadContent(r,i.content)))return Fail(error,"Invalid image recovery entry");
            d->images.push_back(i);
        }
        for(size_t n=0;n<materials;++n){Material m;
            if(!r.Text(m.name,511)||!r.Text(m.file,511)||!r.Int(m.state)||!r.Int(m.line)||!r.Bool(m.implicit)||!r.Bool(m.observed)||
                !r.Bool(m.ignoreHighQuality)||!r.Bool(m.makingBuild)||!r.Number(m.sourceBytes)||!r.Digest(m.source))return Fail(error,"Invalid material recovery entry");
            d->materials.push_back(m);
        }
        std::string canonical(0,'\0');if(r.pos!=r.bytes.size()||!Encode(*d,canonical,error)||canonical!=raw)return Fail(error,"Noncanonical image recovery record");
        output.value=std::move(d);error.clear();return true;
    }catch(...){return Fail(error,"Image recovery decoding allocation failed");}
}
} // namespace openq4::imageRecovery
