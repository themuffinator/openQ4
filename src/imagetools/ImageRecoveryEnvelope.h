// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

// Transport grammar shared by the renderer and the atomic settings journal.
// This validates framing/integrity, NEVER image semantics, VFS authority or GPU
// completion. Process-local lease identities are deliberately absent.
namespace openq4::imageRecovery {
constexpr size_t EncodedMaxBytes = 1536 * 1024;
constexpr size_t RawMaxBytes = EncodedMaxBytes / 4 * 3;
constexpr size_t ChunkBytes = 4096;
constexpr size_t HeaderBytes = 88;
constexpr char Magic[] = "OQ4IMG01";
using Value = std::variant<double,bool,std::string>;
using Values = std::map<std::string,Value>;
inline uint32_t U32(std::string_view s,size_t at) noexcept {
    uint32_t n=0; for(unsigned i=0;i<4;++i)n|=uint32_t(uint8_t(s[at+i]))<<(8*i); return n;
}
inline uint32_t CRC(std::string_view s) noexcept {
    uint32_t c=~uint32_t{0}; for(unsigned char b:s){c^=b;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));} return ~c;
}
inline bool Attempt(std::string_view s) noexcept {
    return s.size()==32 && s.find_first_not_of("0123456789abcdef")==s.npos && s.find_first_not_of('0')!=s.npos;
}
inline bool Frame(std::string_view s,unsigned direction,std::string_view attempt) noexcept {
    return (direction==1||direction==2) && Attempt(attempt) && s.size()>=HeaderBytes+4 && s.size()<=RawMaxBytes &&
        s.substr(0,8)==Magic && U32(s,8)==direction && U32(s,12)==s.size() && s.substr(16,32)==attempt &&
        U32(s,48)<=32768 && U32(s,52)<=32768 && CRC(s.substr(0,s.size()-4))==U32(s,s.size()-4);
}
inline std::string Key(size_t index) {
    char s[]="chunk.0000"; for(unsigned n=0;n<4;++n){s[9-n]="0123456789abcdef"[index&15];index>>=4;}
    return std::string(s,sizeof(s)-1);
}
inline std::string Base64(std::string_view raw) {
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out((raw.size()+2)/3*4,'='); size_t j=0;
    for(size_t i=0;i<raw.size();i+=3){uint32_t x=uint32_t(uint8_t(raw[i]))<<16;
        if(i+1<raw.size())x|=uint32_t(uint8_t(raw[i+1]))<<8;
        if(i+2<raw.size())x|=uint8_t(raw[i+2]);
        out[j++]=alphabet[x>>18];out[j++]=alphabet[(x>>12)&63];
        if(i+1<raw.size())out[j]=alphabet[(x>>6)&63];
        ++j;
        if(i+2<raw.size())out[j]=alphabet[x&63];
        ++j;
    }
    // Do not depend on optional NRVO: MSVC debug string moves can allocate
    // iterator proxies inside noexcept. This explicit copy can throw normally.
    return std::string(out);
}
inline bool Unbase64(std::string_view text,std::string& out) {
    if(text.empty()||text.size()%4||text.size()>EncodedMaxBytes)return false;
    const auto digit=[](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;
        if(c>='0'&&c<='9')return c-'0'+52;
        if(c=='+')return 62;
        if(c=='/')return 63;
        return -1;};
    std::string raw(0,'\0');raw.reserve(text.size()/4*3);
    for(size_t i=0;i<text.size();i+=4){const int a=digit(text[i]),b=digit(text[i+1]),c=digit(text[i+2]),d=digit(text[i+3]);
        const bool pad2=text[i+2]=='=',pad1=text[i+3]=='=';
        if(a<0||b<0||(!pad2&&c<0)||(!pad1&&d<0)|| (pad2&&!pad1)||((pad1||pad2)&&i+4!=text.size())||
            (pad2&&(b&15))||(!pad2&&pad1&&(c&3)))return false;
        raw+=char((a<<2)|(b>>4));if(!pad2)raw+=char((b<<4)|(c>>2));if(!pad1)raw+=char((c<<6)|d);
    }out.swap(raw);return true;
}
inline bool Pack(std::string_view raw,unsigned direction,std::string_view attempt,Values& output) {
    if(!Frame(raw,direction,attempt))return false;
    try {
        const std::string encoded=Base64(raw);const size_t count=(encoded.size()+ChunkBytes-1)/ChunkBytes;
        Values candidate;
        // Construct node keys and variant strings directly through throwing
        // constructors, without temporary variant/string noexcept moves.
        const auto text=[&](std::string_view key,std::string_view value){
            candidate.emplace(std::piecewise_construct,std::forward_as_tuple(key.data(),key.size()),
                std::forward_as_tuple(std::in_place_type<std::string>,value.data(),value.size()));
        };
        const auto number=[&](std::string_view key,double value){
            candidate.emplace(std::piecewise_construct,std::forward_as_tuple(key.data(),key.size()),
                std::forward_as_tuple(std::in_place_type<double>,value));
        };
        text("codec","openq4.image-recovery.1");text("attempt",attempt);
        number("direction",double(direction));number("chunks",double(count));number("bytes",double(raw.size()));
        for(size_t i=0;i<count;++i){const std::string key=Key(i);std::string value(key,6);value+=':';
            value.append(encoded,i*ChunkBytes,ChunkBytes);text(key,value);}
        output.swap(candidate);return true;
    }catch(...){return false;}
}
inline bool Unpack(const Values& input,unsigned direction,std::string_view attempt,std::string& output) {
    try {
        if(!Attempt(attempt)||input.size()<6||input.size()>5+EncodedMaxBytes/ChunkBytes)return false;
        const auto str=[&](const char* key)->const std::string*{const auto i=input.find(key);return i==input.end()?nullptr:std::get_if<std::string>(&i->second);};
        const auto num=[&](const char* key)->const double*{const auto i=input.find(key);return i==input.end()?nullptr:std::get_if<double>(&i->second);};
        const auto codec=str("codec"),id=str("attempt");const auto dir=num("direction"),chunks=num("chunks"),bytes=num("bytes");
        const size_t count=input.size()-5;
        if(!codec||*codec!="openq4.image-recovery.1"||!id||*id!=attempt||!dir||*dir!=direction||!chunks||*chunks!=double(count)||
            !bytes||!(*bytes>=double(HeaderBytes+4)&&*bytes<=double(RawMaxBytes)))return false;
        std::string text(0,'\0');text.reserve(count*ChunkBytes);
        for(size_t i=0;i<count;++i){const auto key=Key(i);const auto item=input.find(key);
            if(item==input.end())return false;
            const auto value=std::get_if<std::string>(&item->second);
            if(!value||value->size()<=5||value->size()>ChunkBytes+5||value->compare(0,4,key,6,4)!=0||(*value)[4]!=':'||
                (i+1<count&&value->size()!=ChunkBytes+5))return false;
            text.append(*value,5,value->size()-5);
        }
        std::string raw(0,'\0');if(!Unbase64(text,raw)||double(raw.size())!=*bytes||!Frame(raw,direction,attempt))return false;
        output.swap(raw);return true;
    }catch(...){return false;}
}
} // namespace openq4::imageRecovery
