#!/usr/bin/env python3
"""Exercise decimal SYSTEM writes through the actual CVar normalization methods.

No engine/native device calls. The host and CVar bodies are extracted unchanged;
only allocation/container and registry boundaries are counted stand-ins.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from filesystem_case_segments import function_body
import ui_system_settings_host as original
ROOT=Path(__file__).resolve().parents[2]

CVAR_SUPPORT=r'''
#include <algorithm>
#include <cassert>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <string>
#include <stdexcept>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#define TEST_SSE 1
#endif
#include "src/idlib/NumericString.h"
namespace ActualCVar {
namespace idStrAllocationDetail {size_t SaturatingAdd(size_t a,size_t b){return a+b;}}
class idStr {
 char bytes[2048];char* data=bytes;int len=0;
 void Init(){data=bytes;bytes[0]=0;len=0;}void EnsureAlloced(size_t n){assert(n<sizeof(bytes));}
public:
 idStr(){Init();}idStr(const char* text){Init();*this=text;}idStr(const idStr& other){Init();*this=other.c_str();}
 explicit idStr(float f);explicit idStr(int value){Init();char b[64];std::snprintf(b,sizeof(b),"%d",value);*this=b;}explicit idStr(bool v){Init();*this=v?"1":"0";}
 idStr& operator=(const char* text){assert(std::strlen(text)<sizeof(bytes));std::strcpy(bytes,text);len=int(std::strlen(text));return *this;}
 idStr& operator=(const idStr& other){return *this=other.c_str();}const char* c_str()const{return bytes;}int Length()const{return len;}
 int Icmp(const char* text)const{return std::strcmp(bytes,text);}static int Icmp(const char* a,const char* b){return std::strcmp(a,b);}
 static bool IsNumeric(const char* text){return idNumericString::IsDecimal(text);}static int FindChar(const char* text,char c){auto p=std::strchr(text,c);return p?int(p-text):-1;}
 static int snPrintf(char* out,size_t size,const char* format,float value){return std::snprintf(out,size,format,value);}
};
#define ID_INLINE inline
enum{CVAR_BOOL=1,CVAR_INTEGER=2,CVAR_FLOAT=4};
class idInternalCVar {
public:
 int flags=0,integerValue=0;float floatValue=0,valueMin=1,valueMax=-1;const char** valueStrings=nullptr;idStr valueString;const char* value=nullptr;
 void UpdateValue();
};
'''
CVAR_END=r'''
struct Observed{std::string text;float value;};
static Observed Normalize(const char* text,int flags,float minimum,float maximum){idInternalCVar c;c.flags=flags;c.valueMin=minimum;c.valueMax=maximum;c.valueString=text;c.value=c.valueString.c_str();c.UpdateValue();return {c.value,c.floatValue};}
} // namespace ActualCVar
'''
CASES=r'''
static unsigned precisionChecks;
#define CHECK(x) do{++precisionChecks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
struct FloatMode{
#if TEST_SSE
 unsigned original=_mm_getcsr();FloatMode(){_mm_setcsr(original&~0x8040u);}~FloatMode(){_mm_setcsr(original);}void Flush(){_mm_setcsr((original&~0x6000u)|0x8040u);}
#else
 void Flush(){}
#endif
};
static void ExactSerialization(){
 for(const std::uint64_t bits:{1ULL,2ULL,255ULL,0x8000000000000ULL,0xfffffffffffffULL,0x10000000000000ULL,0x10000000000001ULL,0x8000000000000001ULL,0x800fffffffffffffULL}){
  const double value=std::bit_cast<double>(bits);const auto text=Serialize(StateValue(value));CHECK(!text.empty()&&text.size()<=1077&&text.find_first_of("eE")==std::string::npos);CHECK(idNumericString::IsDecimal(text.c_str()));
  double parsed=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),parsed,std::chars_format::fixed);CHECK(result.ec==std::errc{}&&result.ptr==text.data()+text.size());CHECK(std::memcmp(&value,&parsed,sizeof(value))==0);
 }
}
static void Precision(){
 FloatMode mode;ExactSerialization();Seed();SystemSettingsHost host;StateValues baseline;std::string error;CHECK(host.Read(baseline,error));
 for(double value:{.1375,.075,1e-7,1e-20,double(std::numeric_limits<float>::min()),double(std::numeric_limits<float>::denorm_min()),std::nextafter(1.0,0.0),1.0,0.0}){
  auto target=baseline;target["r_forceAmbient"]=value;CHECK(host.Validate(baseline,target,error));
  const int before=writes;CHECK(host.Write({{"r_forceAmbient",value}},error));StateValues read;CHECK(host.Read(read,error));CHECK(read.at("r_forceAmbient")==StateValue(value));
  const auto& cvar=localCVarSystem.variables.at("r_forceAmbient");CHECK(cvar.value.find_first_of("eE")==std::string::npos);CHECK(idNumericString::IsDecimal(cvar.value.c_str()));CHECK(cvar.actualFloat==static_cast<float>(value));
  if(value!=0)CHECK(cvar.actualFloat!=0);CHECK(writes>=before);baseline=read;
 }
 for(double value:{1.375,std::nextafter(.5,1.0),std::nextafter(2.0,1.0),.5,2.0}){auto target=baseline;target["r_brightness"]=value;CHECK(host.Validate(baseline,target,error));CHECK(host.Write({{"r_brightness",value}},error));CHECK(host.Read(baseline,error));CHECK(std::get<double>(baseline.at("r_brightness"))==value);}
 for(double value:{1e-100,double(std::numeric_limits<float>::denorm_min())*.25,std::numeric_limits<double>::denorm_min()}){
  auto target=baseline;target["r_forceAmbient"]=value;const int before=writes;CHECK(!host.Validate(baseline,target,error));CHECK(error.find("not representable")!=std::string::npos);CHECK(writes==before);
 }
 for(double value:{std::nextafter(.5,0.0),std::nextafter(2.0,3.0),std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){auto target=baseline;target["r_brightness"]=value;const int before=writes;CHECK(!host.Validate(baseline,target,error));CHECK(!host.Write({{"r_brightness",value}},error));CHECK(writes==before);}
 // Restoring an observed custom string is different from authorizing a new
 // underflowing edit. Exact numeric text can be recovered without claiming its
 // historical float-zero cache represents a nonzero renderer effect.
 const double tiny=1e-100;const auto fixed=Serialize(StateValue(tiny));auto& ambient=localCVarSystem.variables.at("r_forceAmbient");ambient.SetString(fixed.c_str());StateValues original;CHECK(host.Read(original,error));CHECK(std::get<double>(original.at("r_forceAmbient"))==tiny&&ambient.actualFloat==0);
 CHECK(host.Write({{"r_forceAmbient",.25}},error));StateValues current;CHECK(host.Read(current,error));CHECK(host.ValidateRollback(original,current,original,error));CHECK(host.Write({{"r_forceAmbient",tiny}},error));StateValues restored;CHECK(host.Read(restored,error));CHECK(restored==original&&ambient.value==fixed&&ambient.actualFloat==0);
 ambient.SetString((std::string("00")+fixed.substr(1)+"0").c_str());const auto customSpelling=ambient.value;const int spellingWrites=writes;CHECK(host.Write({{"r_forceAmbient",tiny}},error));CHECK(ambient.value==customSpelling&&writes==spellingWrites);
 const auto preserved=ambient.value;const int before=writes;CHECK(host.Write({{"r_forceAmbient",tiny}},error));CHECK(ambient.value==preserved&&writes==before);
 // Registered clamping and a later setter refusal remain failures; preflight
 // never publishes an earlier key when any requested key is invalid.
 CHECK(!host.Write({{"r_brightness",1.375},{"r_forceAmbient",-1.0}},error));CHECK(writes==before);
 refuse="r_forceAmbient";CHECK(!host.Write({{"r_forceAmbient",.125}},error));CHECK(ambient.value==preserved);refuse.clear();
#if TEST_SSE
 mode.Flush();ExactSerialization();CHECK(host.Read(baseline,error));
 for(double value:{double(std::numeric_limits<float>::denorm_min()),1e-40,std::numeric_limits<double>::denorm_min()}){auto target=baseline;target["r_forceAmbient"]=value;const bool accepted=host.Validate(baseline,target,error);if(accepted)std::fprintf(stderr,"FTZ accepted bits=%llx baseline=%g csr=%x\n",static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(value)),std::get<double>(baseline.at("r_forceAmbient")),_mm_getcsr());CHECK(!accepted);}
 // Zero and binary64 subnormals remain distinct even under DAZ. A new tiny
 // value is rejected, but an observed original remains recoverable exactly.
 const double smallest=std::numeric_limits<double>::denorm_min();
 CHECK(!SameValue(StateValue(smallest),StateValue(0.0)));CHECK(SameValue(StateValue(-0.0),StateValue(0.0)));
 auto zero=baseline;zero["r_forceAmbient"]=0.0;auto tinyTarget=zero;tinyTarget["r_forceAmbient"]=smallest;
 CHECK(!host.Validate(zero,tinyTarget,error));
 const auto tinyFixed=Serialize(StateValue(smallest));ambient.SetString(tinyFixed.c_str());
 StateValues tinyOriginal;CHECK(host.Read(tinyOriginal,error));CHECK(std::bit_cast<std::uint64_t>(std::get<double>(tinyOriginal.at("r_forceAmbient")))==1);
 CHECK(host.Write({{"r_forceAmbient",0.0}},error));CHECK(ambient.value=="0");CHECK(host.Read(current,error));
 CHECK(host.ValidateRollback(tinyOriginal,current,tinyOriginal,error));CHECK(host.Write({{"r_forceAmbient",smallest}},error));CHECK(ambient.value==tinyFixed);CHECK(tinyFixed.size()==1076);
 const int unchangedWrites=writes;CHECK(host.Write({{"r_forceAmbient",smallest}},error)&&writes==unchangedWrites);
 auto target=baseline;target["r_forceAmbient"]=double(std::numeric_limits<float>::min());CHECK(host.Validate(baseline,target,error));CHECK(host.Write({{"r_forceAmbient",target.at("r_forceAmbient")}},error));CHECK(host.Read(current,error));CHECK(host.ValidateRollback(original,current,original,error));CHECK(host.Write({{"r_forceAmbient",tiny}},error));CHECK(host.Read(restored,error)&&restored==original);
#endif
 std::printf("SYSTEM decimal precision: %u checks passed; real CVar normalization, exact readback, underflow and rollback.\n",precisionChecks);
}
int main(){ExistingHostCases();Precision();}
'''

def source():
    cvars=(ROOT/'src/framework/CVarSystem.cpp').read_text(encoding='utf-8')
    strings=(ROOT/'src/idlib/Str.h').read_text(encoding='utf-8')
    document=(ROOT/'src/ui/retained/Document.cpp').read_text(encoding='utf-8')
    host=(ROOT/'src/ui/application/SystemSettingsHost.cpp').read_text(encoding='utf-8')
    support=original.SUPPORT.replace('int flags=0;', 'int flags=0; float actualFloat=0;')
    old='void SetString(const char* text) { ++writes; if(key!=refuse)value=text; }'
    assert support.count(old)==1
    support=support.replace(old,'void SetString(const char* text) { ++writes; if(key!=refuse) { auto result=ActualCVar::Normalize(text,flags,minimum,maximum); value=result.text; actualFloat=result.value; } }')
    validation='\n'.join(function_body(document,name) for name in ('bool Utf8(', 'bool ValidStateValue('))
    host='\n'.join(line for line in host.splitlines() if not line.startswith('#include '))
    return (CVAR_SUPPORT+function_body(strings,'ID_INLINE idStr::idStr( const float f )')+'\n'+function_body(cvars,'void idInternalCVar::UpdateValue( void )')+CVAR_END+support+
        function_body(cvars,'\nbool CVar_ReadDefault(')+'\nnamespace openq4::ui {\n'+validation+'\n}\n'+host+
        original.MAIN.replace('int main()', 'static void ExistingHostCases()')+CASES)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler');parser.add_argument('--sanitize',action='store_true');options=parser.parse_args()
    compiler=options.compiler or next((p for name in ('clang++','g++','c++') if(p:=shutil.which(name))),None)
    if not compiler:raise RuntimeError('C++20 compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    dependencies=[Path(__file__),ROOT/'tools/tests/ui_system_settings_host.py',ROOT/'tools/tests/filesystem_case_segments.py',ROOT/'src/framework/CVarSystem.cpp',ROOT/'src/idlib/Str.h',ROOT/'src/idlib/NumericString.h',ROOT/'src/ui/application/SystemSettingsHost.cpp',ROOT/'src/ui/application/SystemSettingsHost.h',ROOT/'src/ui/application/SettingsTransaction.h',ROOT/'src/ui/application/SettingsValue.h',ROOT/'src/ui/retained/Presentation.cpp',ROOT/'src/ui/retained/Document.cpp',ROOT/'src/ui/retained/Document.h',ROOT/'src/ui/retained/Vector.h']
    before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in dependencies}
    out=Path(tempfile.mkdtemp(prefix='settings-decimal-',dir=ROOT/'.tmp'));code=out/'host.cpp';code.write_text(source(),encoding='utf-8',newline='\n')
    binary=out/('test.exe' if os.name=='nt' else 'test');args=[compiler,'-std=c++20','-O2','-D_CRT_SECURE_NO_WARNINGS','-I',str(ROOT),str(code),str(ROOT/'src/ui/retained/Presentation.cpp'),'-o',str(binary)]
    if options.sanitize:args[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0']
    env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out));compiled=subprocess.run(args,env=env,capture_output=True,text=True);(out/'compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8');record={'command':args,'compile':compiled.returncode}
    if compiled.returncode:print(compiled.stdout+compiled.stderr);raise RuntimeError('precision compilation failed')
    run=subprocess.run([str(binary)],env=env,capture_output=True,text=True);(out/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');record.update(exit=run.returncode,output=run.stdout+run.stderr);print(run.stdout+run.stderr)
    inputs=[code,binary,*dependencies]
    after={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in dependencies};record['unchanged_sources']=before==after;record['source_before']=before
    record['files']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs};(out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(out/'result.json')
    if run.returncode or before!=after:raise RuntimeError('precision test failed or source changed during execution')
if __name__=='__main__':main()
