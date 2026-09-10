// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/application/SettingsTransaction.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#define TEST_SSE 1
#endif
using namespace openq4::ui;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (false)
static double Bits(std::uint64_t bits) { return std::bit_cast<double>(bits); }
static bool Exact(double a,double b) { return std::memcmp(&a,&b,sizeof(a))==0; }
static void Code(SettingsResult r,SettingsCode c) { if(r.code!=c)std::fprintf(stderr,"code %d expected %d: %s\n",int(r.code),int(c),r.diagnostic.c_str());CHECK(r.code==c); }
struct FloatMode {
#if TEST_SSE
 unsigned previous=_mm_getcsr();
 explicit FloatMode(bool flush) { _mm_setcsr((previous&~0xe040u)|(flush?0x8040u:0)); }
 ~FloatMode() { _mm_setcsr(previous); }
#else
 explicit FloatMode(bool) {}
#endif
};
struct Host final:SettingsHost {
 StateValues live{{"scalar",Bits(1)},{"other",Bits(2)},{"toggle",true},{"label",std::string("value")}};
 std::vector<StateValues> writes; bool confirmation=false,failWrite=false;
 std::function<void(const StateValues&)> afterWrite;
 bool Read(StateValues& out,std::string&) override { out=live;return true; }
 bool Defaults(StateValues& out,std::string&) override { out=live;out["scalar"]=0.0;return true; }
 bool Validate(const StateValues&,const StateValues&,std::string&) override { return true; }
 bool Write(const StateValues& patch,std::string& error) override {
  writes.push_back(patch);for(const auto& [key,value]:patch)live.at(key)=value;
  if(afterWrite)afterWrite(patch);
  if(failWrite){error="recorded partial refusal";return false;}return true;
 }
 bool NeedsConfirmation(const StateValues&,const StateValues&)const override { return confirmation; }
};
static void Comparisons() {
 const std::vector<StateValue> values={0.0,-0.0,Bits(1),Bits(2),Bits(0x8000000000000001ULL),Bits(0xfffffffffffffULL),Bits(0x10000000000000ULL),1.0,std::nextafter(1.0,2.0),false,true,std::string("0"),std::string("value"),std::string("value\0",6)};
 for(size_t a=0;a<values.size();++a)for(size_t b=0;b<values.size();++b){
  const bool equal=a==b||(a<2&&b<2);
  CHECK(SettingsValueEqual(values[a],values[b])==equal);
  CHECK(SettingsValuesEqual({{"key",values[a]}},{{"key",values[b]}})==equal);
 }
 for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
  CHECK(!SettingsValueEqual(invalid,invalid));CHECK(!SettingsValuesEqual({{"key",invalid}},{{"key",invalid}}));
 }
 CHECK(SettingsValuesEqual({},{}));CHECK(!SettingsValuesEqual({},{{"x",true}}));CHECK(!SettingsValuesEqual({{"x",true}},{{"y",true}}));
}
static void Synchronous() {
 for(double original:{Bits(1),Bits(2),Bits(0xfffffffffffffULL),Bits(0x8000000000000001ULL)}) {
  Host host;host.live["scalar"]=original;host.confirmation=true;SettingsTransaction tx(host);Code(tx.Begin(1),SettingsCode::Ok);CHECK(!tx.Dirty());
  Code(tx.Edit(1,{{"scalar",0.0}}),SettingsCode::Ok);CHECK(tx.Dirty());Code(tx.Apply(1,0),SettingsCode::Ok);
  CHECK(host.writes.size()==1&&host.writes[0].size()==1&&Exact(std::get<double>(host.writes[0].at("scalar")),0));CHECK(tx.Phase()==SettingsPhase::Confirming);
  Code(tx.Revert(1),SettingsCode::Ok);CHECK(host.writes.size()==2&&Exact(std::get<double>(host.live.at("scalar")),original));CHECK(!tx.Dirty());
  CHECK(Exact(std::get<double>(host.live.at("other")),Bits(2)));
 }
 // Defaults and synchronous immediate Apply also publish an actual zero patch.
 Host host;SettingsTransaction tx(host);Code(tx.Begin(1),SettingsCode::Ok);Code(tx.Defaults(1),SettingsCode::Ok);CHECK(tx.Dirty());Code(tx.Apply(1,0),SettingsCode::Ok);CHECK(host.writes.size()==1&&!tx.Dirty());
 // A late refused Apply rolls back the observed tiny original and keeps the
 // attempted zero draft. Unowned exact external values remain untouched.
 Host partial;SettingsTransaction p(partial);Code(p.Begin(1),SettingsCode::Ok);Code(p.Edit(1,{{"scalar",0.0}}),SettingsCode::Ok);
 partial.afterWrite=[&](const StateValues&){partial.live["other"]=Bits(3);};partial.failWrite=true;
 Code(p.Apply(1,0),SettingsCode::ApplyFailed);CHECK(p.Phase()==SettingsPhase::Editing&&p.Dirty());CHECK(partial.writes.size()==2);CHECK(Exact(std::get<double>(partial.live.at("scalar")),Bits(1)));CHECK(Exact(std::get<double>(p.Draft().at("other")),Bits(3)));
 // A divergent owned tiny value cannot be mistaken for our written zero.
 Host conflict;conflict.confirmation=true;SettingsTransaction c(conflict);Code(c.Begin(1),SettingsCode::Ok);Code(c.Edit(1,{{"scalar",0.0}}),SettingsCode::Ok);Code(c.Apply(1,0),SettingsCode::Ok);conflict.live["scalar"]=Bits(2);
 Code(c.Revert(1),SettingsCode::Conflict);CHECK(c.Phase()==SettingsPhase::RecoveryRequired&&conflict.writes.size()==1);CHECK(Exact(std::get<double>(conflict.live.at("scalar")),Bits(2)));
 // Confirm must reject a late tiny replacement rather than accepting it.
 Host confirm;confirm.confirmation=true;SettingsTransaction k(confirm);Code(k.Begin(1),SettingsCode::Ok);Code(k.Edit(1,{{"scalar",0.0}}),SettingsCode::Ok);Code(k.Apply(1,0),SettingsCode::Ok);confirm.live["scalar"]=Bits(2);Code(k.Confirm(1),SettingsCode::Conflict);CHECK(k.Phase()==SettingsPhase::RecoveryRequired);
}
struct Async {
 Host host;SettingsTransaction tx{host};SettingsAttempt attempt;
 Async(){Code(tx.Begin(1),SettingsCode::Ok);Code(tx.Edit(1,{{"scalar",0.0}}),SettingsCode::Ok);}
 void Prepare(){Code(tx.PrepareApply(1,0,attempt),SettingsCode::Ok);CHECK(attempt.patch.size()==1);}
 void Execute(){Prepare();Code(tx.ExecuteApply(1,attempt.request),SettingsCode::Ok);}
 void Complete(){Execute();Code(tx.CompleteApply(1,attempt.request,1),SettingsCode::Ok);}
 void Restore(){Complete();SettingsAttempt restore;Code(tx.PrepareRestore(1,attempt.request,restore),SettingsCode::Ok);attempt=restore;CHECK(attempt.patch.size()==1&&Exact(std::get<double>(attempt.patch.at("scalar")),Bits(1)));}
};
static void Asynchronous() {
 {Async f;f.Restore();Code(f.tx.ExecuteRestore(1,f.attempt.request),SettingsCode::Ok);Code(f.tx.CompleteRestore(1,f.attempt.request),SettingsCode::Ok);CHECK(!f.tx.Dirty()&&Exact(std::get<double>(f.host.live.at("scalar")),Bits(1)));}
 {Async f;f.Complete();SettingsAttempt confirm;Code(f.tx.PrepareConfirm(1,f.attempt.request,2,confirm),SettingsCode::Ok);Code(f.tx.CompleteConfirm(1,confirm.request),SettingsCode::Ok);CHECK(!f.tx.Dirty());}
 // Drift at each frozen boundary must stop the operation before its next step.
 {Async f;f.host.live["scalar"]=Bits(2);Code(f.tx.PrepareApply(1,0,f.attempt),SettingsCode::Conflict);CHECK(f.host.writes.empty());}
 {Async f;f.Prepare();f.host.live["scalar"]=Bits(2);Code(f.tx.ExecuteApply(1,f.attempt.request),SettingsCode::Conflict);CHECK(f.host.writes.empty());}
 {Async f;f.Prepare();f.host.afterWrite=[&](const StateValues&){f.host.live["scalar"]=Bits(2);};Code(f.tx.ExecuteApply(1,f.attempt.request),SettingsCode::ApplyFailed);}
 {Async f;f.Execute();f.host.live["scalar"]=Bits(2);Code(f.tx.CompleteApply(1,f.attempt.request,1),SettingsCode::Conflict);}
 {Async f;f.Complete();f.host.live["scalar"]=Bits(2);SettingsAttempt restore;Code(f.tx.PrepareRestore(1,f.attempt.request,restore),SettingsCode::Ok);CHECK(restore.patch.empty());Code(f.tx.ExecuteRestore(1,restore.request),SettingsCode::Conflict);CHECK(f.host.writes.size()==1);}
 {Async f;f.Restore();f.host.live["scalar"]=Bits(2);Code(f.tx.ExecuteRestore(1,f.attempt.request),SettingsCode::Conflict);CHECK(f.host.writes.size()==1);}
 {Async f;f.Restore();f.host.afterWrite=[&](const StateValues&){f.host.live["scalar"]=0.0;};Code(f.tx.ExecuteRestore(1,f.attempt.request),SettingsCode::RollbackFailed);}
 {Async f;f.Restore();Code(f.tx.ExecuteRestore(1,f.attempt.request),SettingsCode::Ok);f.host.live["scalar"]=Bits(2);Code(f.tx.CompleteRestore(1,f.attempt.request),SettingsCode::Conflict);}
 {Async f;f.Complete();f.host.live["scalar"]=Bits(2);SettingsAttempt confirm;Code(f.tx.PrepareConfirm(1,f.attempt.request,2,confirm),SettingsCode::Conflict);}
 {Async f;f.Complete();SettingsAttempt confirm;Code(f.tx.PrepareConfirm(1,f.attempt.request,2,confirm),SettingsCode::Ok);f.host.live["scalar"]=Bits(2);Code(f.tx.CompleteConfirm(1,confirm.request),SettingsCode::Conflict);}
}
static void InvalidValues() {
 for(const StateValue& invalid:std::vector<StateValue>{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),true,std::string("0")}){
  Host host;SettingsTransaction tx(host);Code(tx.Begin(1),SettingsCode::Ok);Code(tx.Edit(1,{{"scalar",invalid}}),SettingsCode::Invalid);CHECK(!tx.Dirty()&&host.writes.empty());
 }
 for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
  Host host;host.live["scalar"]=invalid;SettingsTransaction tx(host);Code(tx.Begin(1),SettingsCode::Invalid);CHECK(tx.Phase()==SettingsPhase::Closed&&host.writes.empty());
 }
 Host host;host.live["scalar"]=-0.0;SettingsTransaction tx(host);Code(tx.Begin(1),SettingsCode::Ok);Code(tx.Edit(1,{{"scalar",0.0}}),SettingsCode::Ok);CHECK(!tx.Dirty());Code(tx.Apply(1,0),SettingsCode::Ok);CHECK(host.writes.empty());
}
int main(){for(bool flush:{false,true}){FloatMode mode(flush);Comparisons();Synchronous();Asynchronous();InvalidValues();}std::printf("UiSettingsExactValueTest: %u checks passed\n",checks);}
