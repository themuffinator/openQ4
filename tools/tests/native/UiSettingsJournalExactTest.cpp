// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/application/SettingsJournal.h"
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#define TEST_SSE 1
#endif
using namespace openq4::ui;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
struct Mode {
#if TEST_SSE
 unsigned old=_mm_getcsr();explicit Mode(bool flush){_mm_setcsr((old&~0xe040u)|(flush?0x8040u:0));}~Mode(){_mm_setcsr(old);}
#else
 explicit Mode(bool){}
#endif
};
static unsigned FloatingMode(){
#if TEST_SSE
 return _mm_getcsr();
#else
 return 0;
#endif
}
static const std::map<std::string,size_t> Catalog{{"scalar",0},{"toggle",1}};
static SettingsRecoveryJournal Journal(double original) {
 SettingsRecoveryJournal j;j.attempt="0123456789abcdef0123456789abcdef";j.baseline={{"scalar",original},{"toggle",false}};
 j.target={{"scalar",0.0},{"toggle",true}};j.patch={{"scalar",0.0},{"toggle",true}};
 j.displayRestore={{"width",1280.0}};j.displayTarget={{"width",960.0}};j.placement={{"x",-1920.0}};return j;
}
static std::string OldGeneral(double value) { Mode gradual(false);char text[64];auto r=std::to_chars(text,text+sizeof(text),value,std::chars_format::general,std::numeric_limits<double>::max_digits10);CHECK(r.ec==std::errc{});return {text,r.ptr}; }
static std::string Rechecksum(const std::string& bytes) {
 const auto first=bytes.find('\n'),payloadStart=bytes.find('\n',first+1)+1;std::uint32_t crc=0xffffffffu;
 for(unsigned char c:bytes.substr(payloadStart)){crc^=c;for(unsigned i=0;i<8;++i)crc=(crc&1)?(crc>>1)^0xedb88320u:crc>>1;}
 char hex[9];std::snprintf(hex,sizeof(hex),"%08x",~crc);return bytes.substr(0,first+1)+hex+'\n'+bytes.substr(payloadStart);
}
static std::string Replace(std::string bytes,const std::string& from,const std::string& to) {auto at=bytes.find(from);CHECK(at!=std::string::npos);bytes.replace(at,from.size(),to);return Rechecksum(bytes);}
static void Serialization() {
 std::uint64_t random=0x123456789abcdefULL;
 for(unsigned i=0;i<256;++i) {
  random^=random<<13;random^=random>>7;random^=random<<17;
  const std::uint64_t fraction=(random&0x000fffffffffffffULL)|1;
  const double value=std::bit_cast<double>(fraction|((i&1)?0x8000000000000000ULL:0));
  const auto legacy=OldGeneral(value);
  for(bool flush:{false,true}) {Mode mode(flush);std::string text="untouched";const unsigned originalMode=FloatingMode();CHECK(SettingsNumberText(value,SettingsNumberFormat::GeneralRoundTrip,text)&&text==legacy);CHECK(FloatingMode()==originalMode);
   double parsed=0;auto result=std::from_chars(text.data(),text.data()+text.size(),parsed);CHECK(result.ec==std::errc{}&&result.ptr==text.data()+text.size());CHECK(std::memcmp(&value,&parsed,sizeof(value))==0);
  }
 }
 for(double value:{0.0,-0.0,1.375,.1375,1e-7,1e-100,std::numeric_limits<double>::min(),std::bit_cast<double>(std::uint64_t(0x10000000000001ULL)),1e12}) {
  const auto legacy=OldGeneral(value);for(bool flush:{false,true}){Mode mode(flush);std::string text;const unsigned originalMode=FloatingMode();CHECK(SettingsNumberText(value,SettingsNumberFormat::GeneralRoundTrip,text)&&text==legacy);CHECK((FloatingMode()&~0x3fu)==(originalMode&~0x3fu));}
 }
 for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){std::string text="untouched";CHECK(!SettingsNumberText(invalid,SettingsNumberFormat::GeneralRoundTrip,text)&&text=="untouched");}
 std::string text="untouched";CHECK(!SettingsNumberText(1.0,static_cast<SettingsNumberFormat>(99),text)&&text=="untouched");
}
static void RoundTrip() {
 for(const std::uint64_t bits:{1ULL,2ULL,255ULL,0x8000000000000ULL,0xfffffffffffffULL,0x10000000000000ULL,0x8000000000000001ULL}) {
  const double value=std::bit_cast<double>(bits);const auto legacy=OldGeneral(value);std::string gradual;
  for(bool flush:{false,true}){Mode mode(flush);const unsigned originalMode=FloatingMode();std::string text;CHECK(SettingsNumberText(value,SettingsNumberFormat::GeneralRoundTrip,text)&&text==legacy);CHECK((FloatingMode()&~0x3fu)==(originalMode&~0x3fu));}
  for(bool flush:{false,true}) {Mode mode(flush);auto j=Journal(value);std::string bytes,error;CHECK(EncodeSettingsJournal(j,Catalog,bytes,error));
   CHECK(bytes.find("\"baseline.scalar\":"+legacy)!=std::string::npos);if(!flush)gradual=bytes;else CHECK(bytes==gradual);
   SettingsRecoveryJournal decoded;CHECK(DecodeSettingsJournal(bytes,Catalog,decoded,error));CHECK(SettingsValuesEqual(decoded.baseline,j.baseline)&&SettingsValuesEqual(decoded.target,j.target)&&SettingsValuesEqual(decoded.patch,j.patch));
   CHECK(std::bit_cast<std::uint64_t>(std::get<double>(decoded.baseline.at("scalar")))==bits);
   j.state=SettingsJournalState::Confirmed;CHECK(EncodeSettingsJournal(j,Catalog,bytes,error));CHECK(DecodeSettingsJournal(bytes,Catalog,decoded,error)&&decoded.state==SettingsJournalState::Confirmed);
  }
 }
 // Numeric signed zeros deliberately compare equal and encode as JSON 0.
 auto zero=Journal(-0.0);zero.patch.erase("scalar");std::string bytes,error;CHECK(EncodeSettingsJournal(zero,Catalog,bytes,error));CHECK(bytes.find("\"baseline.scalar\":0")!=std::string::npos);SettingsRecoveryJournal out;CHECK(DecodeSettingsJournal(bytes,Catalog,out,error));CHECK(SettingsValuesEqual(zero.baseline,out.baseline));
}
static void Forged() {
 Mode mode(true);auto original=Journal(std::bit_cast<double>(std::uint64_t(1)));std::string bytes,error;CHECK(EncodeSettingsJournal(original,Catalog,bytes,error));
 auto rejected=[&](const std::string& forged){auto destination=Journal(1.0);destination.attempt=std::string(32,'f');CHECK(!DecodeSettingsJournal(forged,Catalog,destination,error));CHECK(destination.attempt==std::string(32,'f')&&SettingsValueEqual(destination.baseline.at("scalar"),1.0));};
 // A real second changed key makes an omitted tiny reset superficially valid
 // if the validator incorrectly compares the tiny original as floating zero.
 rejected(Replace(bytes,"\"patch.scalar\":0,\n",""));
 original.target["scalar"]=std::bit_cast<double>(std::uint64_t(2));original.patch["scalar"]=original.target["scalar"];CHECK(EncodeSettingsJournal(original,Catalog,bytes,error));
 rejected(Replace(bytes,"\"patch.scalar\":"+OldGeneral(std::bit_cast<double>(std::uint64_t(2))),"\"patch.scalar\":0"));
 rejected(Replace(bytes,"\"target.scalar\":"+OldGeneral(std::bit_cast<double>(std::uint64_t(2))),"\"target.scalar\":0"));
 rejected(Replace(bytes,"\"patch.scalar\":"+OldGeneral(std::bit_cast<double>(std::uint64_t(2))),"\"patch.scalar\":true"));
 rejected(Replace(bytes,"\"schema\":1","\"schema\":0"));
 auto invalid=original;invalid.patch.erase("scalar");std::string untouched="untouched";CHECK(!EncodeSettingsJournal(invalid,Catalog,untouched,error)&&untouched=="untouched");
}
struct Host final:SettingsHost {
 StateValues live;std::vector<StateValues> writes;
 bool Read(StateValues& out,std::string&)override{out=live;return true;}
 bool Defaults(StateValues& out,std::string&)override{out=live;return true;}
 bool Validate(const StateValues&,const StateValues&,std::string&)override{return true;}
 bool Write(const StateValues& patch,std::string&)override{writes.push_back(patch);for(const auto& [k,v]:patch)live.at(k)=v;return true;}
 bool NeedsConfirmation(const StateValues&,const StateValues&)const override{return true;}
};
static void Restore() {
 Mode mode(true);auto j=Journal(std::bit_cast<double>(std::uint64_t(1)));std::string bytes,error;CHECK(EncodeSettingsJournal(j,Catalog,bytes,error));SettingsRecoveryJournal decoded;CHECK(DecodeSettingsJournal(bytes,Catalog,decoded,error));
 Host host;host.live=decoded.baseline;SettingsTransaction tx(host);CHECK(tx.Begin(1).code==SettingsCode::Ok&&tx.Edit(1,decoded.patch).code==SettingsCode::Ok);CHECK(tx.Apply(1,0).code==SettingsCode::Ok);CHECK(SettingsValuesEqual(host.live,decoded.target));CHECK(tx.Revert(1).code==SettingsCode::Ok);CHECK(SettingsValuesEqual(host.live,decoded.baseline)&&host.writes.size()==2&&!tx.Dirty());
}
static void Budget() {
 auto j=Journal(.1375);std::string bytes="untouched",error;
 // Each metadata map remains below its 1 MiB raw budget, while escaped control
 // text pushes the actual canonical record past the independent 4 MiB limit.
 j.placement.clear();for(unsigned i=0;i<15;++i)j.placement["line"+std::to_string(i)]=std::string(65536,'\n');
 CHECK(!EncodeSettingsJournal(j,Catalog,bytes,error)&&bytes=="untouched");CHECK(error.find("byte budget")!=std::string::npos);
 j.placement={{"x",1.0}};CHECK(EncodeSettingsJournal(j,Catalog,bytes,error));SettingsRecoveryJournal out;CHECK(!DecodeSettingsJournal(bytes+std::string(SettingsJournalMaxBytes,'x'),Catalog,out,error));
}
int main(){Serialization();RoundTrip();Forged();Restore();Budget();std::printf("UiSettingsJournalExactTest: %u checks passed\n",checks);}
