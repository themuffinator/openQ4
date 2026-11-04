#!/usr/bin/env python3
"""Exercise production event-queue restore/cleanup under faults and bad records."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
#define ID_INLINE inline
using byte=unsigned char;
using idStr=std::string;
using idAllocError=std::runtime_error;
constexpr int MAX_EVENTS=8, MA_EVENT=1, D_EVENT_MAXARGS=8, MAX_STRING_LEN=128;
constexpr size_t ID_HEAP_MAX_SIZE=0x7fffffff;
constexpr int EVENT_SAVE_FORMAT_TYPED=-2;
constexpr char D_EVENT_FLOAT='f', D_EVENT_INTEGER='d', D_EVENT_INTEGER64bit='y',
    D_EVENT_ENTITY='e', D_EVENT_ENTITY_NULL='E', D_EVENT_VECTOR='v', D_EVENT_STRING='s', D_EVENT_TRACE='t';
constexpr int E_EVENT_SIZEOF_VEC=12;
struct idVec3 { float xyz[3]; };
struct idMat3 { float rows[9]; };
struct rvDeclMatType {} fixtureMaterialType;
int materialLookups=0,materialTypeReads=0;
bool failMaterialLookup=false;
struct idMaterial {
    const rvDeclMatType* GetMaterialType() const;
} fixtureMaterial;
const rvDeclMatType* idMaterial::GetMaterialType() const {
    assert(this==&fixtureMaterial); ++materialTypeReads; return &fixtureMaterialType;
}
struct Decls {
    const idMaterial* FindMaterial(const char* name,bool) {
        ++materialLookups;
        if(failMaterialLookup) throw std::runtime_error("material lookup failed");
        assert(std::memchr(name,'\0',MAX_STRING_LEN));
        assert(!std::strcmp(name,"test/material") || (name[0]=='x' && std::strlen(name)==MAX_STRING_LEN-1));
        return &fixtureMaterial;
    }
} fixtureDecls;
Decls* declManager=&fixtureDecls;
@TRACE_TYPES@
std::set<void*> legacyLive, eventLive;
int allocation=0,failAllocation=-1;
void* Allocate(size_t size,std::set<void*>& live) {
    if(allocation++==failAllocation) return nullptr;
    void* result=std::malloc(size); assert(result); assert(live.insert(result).second); return result;
}
void* Mem_Alloc(size_t size,int tag) { assert(tag==MA_EVENT); return Allocate(size,legacyLive); }
void Mem_Free(void* ptr) { if(ptr) { assert(legacyLive.erase(ptr)==1); std::free(ptr); } }
struct Allocator {
    byte* Alloc(int size) { return static_cast<byte*>(Allocate(size,eventLive)); }
    void Free(byte* ptr) { assert(eventLive.erase(ptr)==1); std::free(ptr); }
};
struct idEventDef {
    const char* name;
    const char* format;
    bool supported;
    static size_t Slot(char kind) {
        const size_t alignment=alignof(trace_t)>alignof(intptr_t)?alignof(trace_t):alignof(intptr_t);
        const size_t traceOffset=(sizeof(bool)+alignof(trace_t)-1)&~(alignof(trace_t)-1);
        size_t size=kind=='s'?MAX_STRING_LEN:kind=='t'?traceOffset+sizeof(trace_t)+MAX_STRING_LEN:sizeof(intptr_t);
        return (size+alignment-1)&~(alignment-1);
    }
    size_t GetArgSize() const { return GetArgOffset(GetNumArgs()); }
    const char* GetName() const { return name; }
    const char* GetArgFormat() const { return format; }
    int GetNumArgs() const { return int(std::strlen(format)); }
    size_t GetArgOffset(int i) const {
        size_t size=0; for(int j=0;j<i;++j) size+=Slot(format[j]); return size;
    }
    static const idEventDef* FindEvent(const idStr&);
};
const idEventDef defs[]={{"integer","d",true},{"empty","",true},{"unsupported","d",false},
    {"legacyString","s",true},{"legacyTrace","t",true},{"legacyMixed","st",true}};
const idEventDef* idEventDef::FindEvent(const idStr& name) {
    for(auto& def:defs) if(name==def.name) return &def;
    return nullptr;
}
struct idTypeInfo { const char* classname; } fixtureType={"target"};
struct idClass {
    bool compatible=true;
    bool RespondsTo(const idEventDef& def) const { return def.supported; }
    static const idTypeInfo* GetClass(const idStr& name) { return name=="target"?&fixtureType:nullptr; }
} fixtureTarget;
struct Errors {
    [[noreturn]] void Error(const char* format,...) {
        char text[512]; va_list args; va_start(args,format);
        vsnprintf(text,sizeof(text),format,args); va_end(args);
        throw std::runtime_error(text);
    }
} gameLocal;
struct idRestoreGame : Errors {
    std::vector<byte> bytes;
    size_t position=0;
    int steps=0,failAt=-1;
    void Read(void* dest,int size) {
        assert(size>=0 && position+size<=bytes.size());
        std::memcpy(dest,bytes.data()+position,size); position+=size;
        if(steps++==failAt) throw std::runtime_error("interrupted field");
    }
    void ReadInt(int& value) { Read(&value,sizeof(value)); }
    void ReadString(idStr& value) {
        int size; ReadInt(size); assert(size>=0 && size<1024);
        value.resize(size); Read(value.data(),size);
    }
    void ReadObject(idClass*& value,const idTypeInfo&,const char*) {
        int index; ReadInt(index);
        if(index<0 || index>1) Error("invalid object index");
        value=index?&fixtureTarget:nullptr;
        if(value && !value->compatible) Error("wrong object type");
    }
    template<class T> void Put(T value) {
        const byte* ptr=reinterpret_cast<const byte*>(&value); bytes.insert(bytes.end(),ptr,ptr+sizeof(value));
    }
    void String(const char* value) {
        Put(int(std::strlen(value))); bytes.insert(bytes.end(),value,value+std::strlen(value));
    }
    void Event(int time,const char* name="integer",int format=-2,int target=1,const char* type="target",intptr_t arg=42) {
        Put(time); String(name); String(type); Put(target); Put(format);
        if(std::strcmp(name,"empty")) {
            if(format==-2) Put(int(arg)); else Put(arg);
        }
    }
};
void idEvent_RestoreTypedArgs(idRestoreGame* input,const idEventDef* def,byte* data) {
    // Argument codecs are covered separately; expose the integer read boundary.
    assert(!std::strcmp(def->GetArgFormat(),"d"));
    int value; input->ReadInt(value); *reinterpret_cast<intptr_t*>(data)=value;
}
'''
EVENT = r'''
struct idEvent {
    int time=0;
    const idEventDef* eventdef=nullptr;
    const idTypeInfo* typeinfo=nullptr;
    idClass* object=nullptr;
    byte* data=nullptr;
    idLinkList<idEvent> eventNode;
    static Allocator eventDataAllocator;
    void Free();
    static void ClearEventList();
    static void Restore(idRestoreGame*);
};
Allocator idEvent::eventDataAllocator;
idLinkList<idEvent> FreeEvents,EventQueue;
idEvent EventPool[MAX_EVENTS];
'''
MAIN = r'''
void reset() {
    idEvent::ClearEventList();
    assert(legacyLive.empty() && eventLive.empty());
    assert(EventQueue.Num()==0 && FreeEvents.Num()==MAX_EVENTS);
    allocation=0; failAllocation=-1; fixtureTarget.compatible=true;
    materialLookups=materialTypeReads=0;
    failMaterialLookup=false;
}
void healthy(int format) {
    int boundaries=0,allocations=0;
    for(int failure=-1;failure<boundaries;++failure) {
        reset(); idRestoreGame input; input.failAt=failure;
        input.Put(4);
        input.Event(INT_MIN,"integer",format);
        input.Event(0,"empty",format==-2?-2:0);
        input.Event(0,"integer",format,1,"target",77);
        input.Event(INT_MAX,"integer",format,1,"target",99);
        bool refused=false;
        try { idEvent::Restore(&input); } catch(const std::runtime_error&) { refused=true; }
        assert(refused==(failure>=0));
        if(failure==-1) {
            boundaries=input.steps; allocations=allocation;
            assert(input.position==input.bytes.size());
            assert(EventQueue.Num()==4 && FreeEvents.Num()==MAX_EVENTS-4);
            auto* event=EventQueue.Next(); assert(event->time==INT_MIN);
            assert(*reinterpret_cast<intptr_t*>(event->data)==42);
            event=event->eventNode.Next(); assert(!event->data);
            event=event->eventNode.Next(); assert(*reinterpret_cast<intptr_t*>(event->data)==77);
            event=event->eventNode.Next(); assert(*reinterpret_cast<intptr_t*>(event->data)==99);
        }
        // The temporary buffer must already have unwound before queue cleanup.
        assert(legacyLive.empty());
        reset();
    }
    for(int failure=0;failure<allocations;++failure) {
        reset(); failAllocation=failure;
        idRestoreGame input; input.Put(4);
        input.Event(INT_MIN,"integer",format);
        input.Event(0,"empty",format==-2?-2:0);
        input.Event(0,"integer",format);
        input.Event(INT_MAX,"integer",format);
        bool refused=false;
        try { idEvent::Restore(&input); } catch(const std::runtime_error&) { refused=true; }
        assert(refused && legacyLive.empty()); reset();
    }
}
void malformed() {
    for(int mode=0;mode<10;++mode) {
        reset(); idRestoreGame input;
        input.Put(mode==0?-1:mode==1?MAX_EVENTS+1:mode==9?2:1);
        const char* expected="";
        switch(mode) {
        case 0: case 1: expected="invalid event count"; break;
        case 2: input.Event(1,"missing"); expected="unknown event"; break;
        case 3: input.Event(1,"integer",-2,1,"missing"); expected="unknown class"; break;
        case 4: input.Event(1,"integer",-2,0); expected="NULL target"; break;
        case 5: input.Event(1,"integer"); fixtureTarget.compatible=false; expected="wrong object type"; break;
        case 6: input.Event(1,"unsupported"); expected="does not support"; break;
        case 7: input.Event(1,"integer",-1); expected="invalid arg size"; break;
        case 8: input.Event(1,"integer",1); expected="doesn't match saved arg size"; break;
        case 9: input.Event(1); input.Event(0); expected="out of order"; break;
        }
        std::string error;
        try { idEvent::Restore(&input); } catch(const std::runtime_error& e) { error=e.what(); }
        assert(!error.empty() && error.find(expected)!=std::string::npos);
        assert(legacyLive.empty()); reset();
        // A healthy retry uses the same pool after each failed attempt.
        idRestoreGame good; good.Put(1); good.Event(0); idEvent::Restore(&good);
        assert(EventQueue.Num()==1); reset();
    }
}
void capacities() {
    for(int count:{0,MAX_EVENTS}) {
        reset(); idRestoreGame input; input.Put(count);
        for(int i=0;i<count;++i) input.Event(0,"empty");
        idEvent::Restore(&input);
        assert(EventQueue.Num()==count && FreeEvents.Num()==MAX_EVENTS-count);
        reset();
    }
}
void legacyArguments() {
    // Exercise the real legacy reader through queue ownership, not just copying.
    // Layout assertions also pin the Windows x64 runtime fixtures.
    static_assert(sizeof(trace_t)==128 && offsetof(trace_t,c)==56,"review trace fixture ABI");
    static_assert(offsetof(contactInfo_t,material)==40 && offsetof(contactInfo_t,materialType)==64,"review contact fixture ABI");
    for(int mode=0;mode<11;++mode) {
        int boundaries=0;
        for(int failure=-1;failure<boundaries;++failure) {
            reset(); idRestoreGame input; input.failAt=failure;
            failMaterialLookup=mode==10;
            const idEventDef* def=idEventDef::FindEvent(mode<3?"legacyString":mode==9?"legacyMixed":"legacyTrace");
            size_t offsets[D_EVENT_MAXARGS]={};
            std::vector<byte> raw(idEvent_GetLegacyArgLayout(def,offsets),0);
            if(mode<3 || mode==9) {
                std::memset(raw.data(),'x',MAX_STRING_LEN);
                if(mode!=2) raw[mode==0?0:MAX_STRING_LEN-1]=0;
            }
            if(mode>=3) {
                const size_t begin=mode==9?offsets[1]:0;
                raw[begin]=mode==3?0:mode==4?2:1;
                trace_t trace; std::memset(&trace,0,sizeof(trace)); trace.fraction=0.25f;
                trace.c.material=mode==5?nullptr:reinterpret_cast<const idMaterial*>(uintptr_t(0x1234));
                trace.c.materialType=reinterpret_cast<const rvDeclMatType*>(uintptr_t(0x5678));
                std::memcpy(raw.data()+begin+sizeof(bool),&trace,sizeof(trace));
                byte* material=raw.data()+begin+sizeof(bool)+sizeof(trace);
                if(mode==7 || mode==3 || mode==5 || mode==8) {
                    std::memset(material,'x',MAX_STRING_LEN);
                    if(mode==8) material[MAX_STRING_LEN-1]=0;
                }
                else std::memcpy(material,"test/material",sizeof("test/material"));
            }
            input.Put(1); input.Put(0); input.String(def->GetName()); input.String("target");
            input.Put(1); input.Put(int(raw.size())); input.bytes.insert(input.bytes.end(),raw.begin(),raw.end());
            std::string error;
            try { idEvent::Restore(&input); } catch(const std::runtime_error& e) { error=e.what(); }
            const bool invalid=mode==2 || mode==4 || mode==7 || mode==10;
            assert((!error.empty())==(invalid || failure>=0));
            if(failure==-1) {
                boundaries=input.steps;
                if(invalid) assert(error.find(mode==10?"material lookup failed":mode==4?"invalid legacy trace flag":"unterminated legacy")!=std::string::npos);
                else {
                    auto* event=EventQueue.Next(); assert(event && event->eventdef==def);
                    if(mode<3 || mode==9) assert(!std::memcmp(event->data,raw.data(),MAX_STRING_LEN));
                    if(mode>=3) {
                        byte* data=event->data+def->GetArgOffset(mode==9?1:0);
                        assert(*reinterpret_cast<bool*>(data)==(mode!=3));
                        if(mode!=3) {
                            auto* trace=reinterpret_cast<trace_t*>(data+EVENT_TRACE_DATA_OFFSET);
                            assert(trace->fraction==0.25f);
                            assert(trace->c.material==(mode==5?nullptr:&fixtureMaterial));
                            assert(trace->c.materialType==(mode==5?nullptr:&fixtureMaterialType));
                            // Immediate typed resave needs live pointers before dispatch.
                            if(trace->c.material) assert(trace->c.material->GetMaterialType()==trace->c.materialType);
                        }
                        assert(materialLookups==((mode==3 || mode==5)?0:1));
                    }
                }
            }
            assert(legacyLive.empty()); reset();
        }
    }
}
int main() {
#if defined(_MSC_VER)
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    healthy(EVENT_SAVE_FORMAT_TYPED); healthy(sizeof(intptr_t)); malformed(); capacities(); legacyArguments();
    reset(); std::puts("event restore, interruptions, allocations, rejection and retry passed");
}
'''


def source(mode):
    text = (ROOT/'src'/mode/'gamesys/Event.cpp').read_text()
    constants = text[text.index('static const size_t EVENT_ARG_ALIGNMENT'):text.index('static size_t idEvent_GetLegacyArgLayout')]
    collision = (ROOT/'src/cm/CollisionModel.h').read_text()
    trace_types = collision[collision.index('// contact type'):collision.index('#define WORLD_MODEL_NAME')]
    return SUPPORT.replace('@TRACE_TYPES@', trace_types) + (ROOT/'src/idlib/containers/LinkList.h').read_text() + EVENT + constants + '\n'.join(
        function_body(text, signature) for signature in (
            'static size_t idEvent_GetLegacyArgLayout(', 'static void idEvent_ConvertLegacyRawArgs(',
            'void idEvent::Free(', 'void idEvent::ClearEventList(', 'void idEvent::Restore(')
    ) + MAIN


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--generate-native':
        Path(sys.argv[3]).write_text(source(sys.argv[2]), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='restore-events-', dir=ROOT/'.tmp') as directory:
        scratch = Path(directory)
        for mode in ('game', 'mpgame'):
            code = source(mode)
            cases = {'production': code}
            if '--mutation-check' in sys.argv:
                for label, before, after in (
                    ('legacy-leak', 'Mem_Free( data );', '(void)data;'),
                    ('callback', '!event->object->RespondsTo( *event->eventdef )', 'false'),
                    ('ordering', 'i > 0 && event->time < previousTime', 'false'),
                    ('queue-cleanup', 'eventDataAllocator.Free( data );', '(void)data;'),
                    ('legacy-string', "memchr( sourcePtr, '\\0', MAX_STRING_LEN ) == NULL", 'false'),
                    ('legacy-trace-flag', '*sourcePtr > 1', 'false'),
                    ('legacy-material-string', "memchr( materialName, '\\0', MAX_STRING_LEN ) == NULL", 'false'),
                    ('legacy-material-pointer', 'trace->c.material = declManager->FindMaterial( materialName, true );', 'trace->c.material = NULL;'),
                    ('legacy-material-type', 'trace->c.materialType = trace->c.material != NULL ? trace->c.material->GetMaterialType() : NULL;', '(void)trace;'),
                ):
                    assert code.count(before) == 1, label
                    cases[label] = code.replace(before, after, 1)
            for name, candidate in cases.items():
                cpp, exe = scratch/f'{mode}.cpp', scratch/f'{mode}.exe'
                cpp.write_text(candidate, encoding='utf-8')
                subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], check=True)
                result = subprocess.run([str(exe)], capture_output=name != 'production', timeout=30)
                if name == 'production':
                    result.check_returncode()
                else:
                    assert result.returncode != 0, f'{mode}: {name} escaped detection'
                    print(f'{mode}: rejected {name}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
