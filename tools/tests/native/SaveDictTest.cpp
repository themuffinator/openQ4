#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <bit>
#include <memory>
#include "src/framework/SaveDictionaryCache.h"
#include "src/framework/SaveUserInterfaceCache.h"
using byte = unsigned char;
struct idStr : std::string {
    using std::string::string;
    idStr(std::string value) : std::string(std::move(value)) {}
    int Length() const { return static_cast<int>(size()); }
};
constexpr int MAX_PRINT_MSG = 4096, MAX_SAVEGAME_DICT_ENTRIES = 16384;
struct TestClock { int calls = 0; double GetClockTicks() { return ++calls; } } testClock;
struct idLib {
    static inline TestClock* sys = &testClock;
    static int SizeToInt(size_t size, const char*) { return static_cast<int>(size); }
};
struct ErrorSink { [[noreturn]] void Error(const char*, ...) { throw std::runtime_error("invalid save"); } } sink;
static ErrorSink* common = &sink;
static ErrorSink& gameLocal = sink;
struct idKeyValue {
    idStr key, value;
    const idStr& GetKey() const { return key; }
    const idStr& GetValue() const { return value; }
};
struct idDict {
    idContentIdentity identity;
    const idContentIdentity& ContentIdentity() const { return identity; }
    std::vector<idKeyValue> values;
    int GetNumKeyVals() const { return static_cast<int>(values.size()); }
    const idKeyValue* GetKeyVal(int index) const { return &values.at(index); }
};
using saveDictionaryCache_t = SaveDictionaryCache<idDict>;
static std::unique_ptr<saveDictionaryCache_t> saveDictionaryCache;
static void Integer(std::vector<byte>& out, int value) {
    auto bits = static_cast<std::uint32_t>(value);
    for (int b=0; b<4; ++b) { out.push_back(static_cast<byte>(bits & 255)); bits /= 256; }
}
using jointHandle_t = int;
static int LittleLong(int value) { return value; }
static short LittleShort(short value) { return value; }
static float LittleFloat(float value) { return value; }
static_assert(std::endian::native == std::endian::little, "test endian stubs require little endian");
struct MemoryFile {
    std::vector<byte> bytes;
    bool fail = false;
    int writes = 0, failOnWrite = -1;
    int Write(const void* data, int size) {
        if ((fail || writes==failOnWrite) && size) --size;
        const auto* begin = static_cast<const byte*>(data);
        bytes.insert(bytes.end(),begin,begin+size); ++writes;
        return size;
    }
    int Tell() { return static_cast<int>(bytes.size()); }
    int Length() { return Tell(); }
    const byte* GetDataPtr() { return memory ? bytes.data() : nullptr; }
    bool memory = true;
};
struct idUserInterface {
    std::string name = "guis/shared.gui";
    std::vector<byte> payload = std::vector<byte>(18000, 37);
    mutable int writes = 0;
    bool fail = false;
    const char* Name() const { return name.c_str(); }
    bool WriteToSaveGame(MemoryFile* out) const {
        ++writes;
        return !fail && out->Write(payload.data(),static_cast<int>(payload.size()))==static_cast<int>(payload.size());
    }
};
#define SAVE_WRITER(name) struct name { \
    MemoryFile output; MemoryFile* file = &output; bool markers = false; \
    byte writeBuffer[16384]; int writeBufferUsed = 0; \
    bool profileDictionaries = false; double dictionaryProfileTicks = 0; \
    int dictionaryProfileCount = 0, dictionaryProfileBytes = 0; \
    int dictionaryCacheMode = 0, dictionaryCacheHits = 0, dictionaryCacheMisses = 0, dictionaryCacheVerified = 0; \
    void WriteSyncId() { if (markers) { WriteInt(0x594134); WriteInt(17); } } \
    void WriteChecked(int,int,const char*,int); void FlushWriteBuffer(); \
    void Write(const void*,int); void WriteInt(int); void WriteJoint(jointHandle_t); \
    void WriteShort(short); void WriteByte(byte); void WriteSignedChar(signed char); \
    void WriteFloat(float); void WriteBool(bool); void WriteString(const char*); \
    void WriteDict(const idDict*); \
    std::unique_ptr<SaveUserInterfaceCache> userInterfaceCache; int userInterfaceCacheMode = 0; \
    void InvalidateUserInterface(const idUserInterface*); void WriteUserInterface(const idUserInterface*,bool); \
};
SAVE_WRITER(idSaveGame_sp)
SAVE_WRITER(idSaveGame_mp)
#undef SAVE_WRITER
#include "SaveDictUnderTest.inc"
static void Check(bool yes) { if (!yes) throw std::runtime_error("wire parity failure"); }
template<class Writer> static void Test() {
        for (bool markers : { false,true }) for (int count : { -1,0,1,17,100,16384 }) {
            idDict dict;
            for (int i=0;i<count;++i) dict.values.push_back({"key"+std::to_string(i),
                i%3 == 0 ? std::string(4095,'x') : i%3 == 1 ? "" : "Unicode \xc3\xa9\xe2\x98\x83"});
            std::vector<byte> expected;
            if (markers) { Integer(expected,0x594134); Integer(expected,17); }
            Integer(expected,count);
            for (const auto& kv : dict.values) for (const auto* s : { &kv.GetKey(), &kv.GetValue() }) {
                Integer(expected,static_cast<int>(s->size())); expected.insert(expected.end(),s->begin(),s->end());
            }
            Writer save; save.markers=markers;
            testClock.calls = 0;
            save.WriteDict(count<0 ? nullptr : &dict); save.FlushWriteBuffer();
            Check(save.output.bytes == expected);
            Check(testClock.calls == 0);
            Writer profiled; profiled.markers=markers; profiled.profileDictionaries=true;
            profiled.WriteDict(count<0 ? nullptr : &dict); profiled.FlushWriteBuffer();
            Check(profiled.output.bytes == expected);
            Check(testClock.calls == (count<0 ? 0 : 2));
            Check(profiled.dictionaryProfileCount == (count<0 ? 0 : 1));
            Check(profiled.dictionaryProfileBytes == (count<0 ? 0 : static_cast<int>(expected.size())-(markers ? 8 : 0)));
            Check(profiled.dictionaryProfileTicks == (count<0 ? 0.0 : 1.0));
            if (count>100) Check(save.output.writes < count);
        }
        for (int invalid : { 0,1,2 }) {
            idDict dict;
            if (invalid == 0) dict.values.resize(MAX_SAVEGAME_DICT_ENTRIES+1);
            else dict.values.push_back({invalid == 1 ? std::string(4096,'k') : "key",
                invalid == 2 ? std::string(4096,'v') : "value"});
            bool refused = false;
            try { Writer save; save.WriteDict(&dict); } catch (const std::runtime_error&) { refused=true; }
            Check(refused);
        }
        Writer failed; failed.output.fail=true; idDict dict; dict.values.push_back({"a","b"});
        bool refused=false;
        try { failed.WriteDict(&dict); failed.FlushWriteBuffer(); } catch (const std::runtime_error&) { refused=true; }
        Check(refused);

        Writer mixed; std::vector<byte> expected;
        for (int i=0;i<40000;++i) {
            mixed.WriteInt(-i); Integer(expected,-i);
            mixed.WriteFloat(1.0f); Integer(expected,0x3f800000);
            mixed.WriteShort(-2); expected.push_back(254); expected.push_back(255);
            mixed.WriteBool(i%2 != 0); expected.push_back(byte(i%2));
            mixed.WriteSignedChar(-1); expected.push_back(255);
            mixed.WriteString("abc"); Integer(expected,3); expected.insert(expected.end(),{'a','b','c'});
        }
        std::vector<byte> large(65537,173);
        mixed.Write(large.data(),static_cast<int>(large.size())); expected.insert(expected.end(),large.begin(),large.end());
        mixed.FlushWriteBuffer(); Check(mixed.output.bytes == expected);
        Check(mixed.output.writes < 100);
        // Exercise scalar writes at every partial-tail size, including a
        // full buffer and a failed flush. Compare to independently encoded bits.
        for (int tail=0; tail<=4; ++tail) {
            Writer edge;
            std::vector<byte> expected(16384-tail, 0xa5);
            edge.Write(expected.data(), static_cast<int>(expected.size()));
            for (std::uint32_t bits : {0u, 0x80000000u, 0xffffffffu, 0x7fffffffu}) {
                edge.WriteInt(std::bit_cast<int>(bits)); Integer(expected,std::bit_cast<int>(bits));
                edge.WriteJoint(std::bit_cast<int>(bits)); Integer(expected,std::bit_cast<int>(bits));
            }
            for (std::uint32_t bits : {0u, 0x80000000u, 0x7f800000u, 0xff800000u, 0x7fc01234u, 1u}) {
                edge.WriteFloat(std::bit_cast<float>(bits)); Integer(expected,std::bit_cast<int>(bits));
            }
            edge.WriteByte(0xff); expected.push_back(255);
            edge.FlushWriteBuffer(); Check(edge.output.bytes == expected);
        }
        for (int tail=0; tail<4; ++tail) {
            Writer edge;
            std::vector<byte> fill(16384-tail, 0x5a);
            // Write() flushes an exactly-full input directly. Fill with small
            // writes here so each failing case actually crosses the buffer.
            for (byte b : fill) edge.WriteByte(b);
            edge.output.fail=true;
            bool rejected=false;
            try { edge.WriteFloat(1.0f); } catch (const std::runtime_error&) { rejected=true; }
            Check(rejected && edge.writeBufferUsed == 16384-tail);
        }
        for (int invalid : { 0,1 }) {
            bool rejected=false;
            try { Writer save; save.Write(nullptr,invalid ? 1 : -1); } catch (const std::runtime_error&) { rejected=true; }
            Check(rejected);
        }
}
template<class Cache> static typename Cache::Population Prime(Cache& cache) {
    typename Cache::Population total;
    for (int pass=0; cache.Pending(); ++pass) {
        Check(pass<10000);
        const auto work=cache.Populate(MAX_PRINT_MSG,[] { return true; });
        Check(work.records<=24 && work.inspected<=128 && work.bytes<=208*1024);
        Check(cache.ResidentBytes()<=Cache::DefaultBudget);
        total.records+=work.records; total.discarded+=work.discarded;
        total.bytes+=work.bytes; total.inspected+=work.inspected;
    }
    return total;
}
template<class Writer> static void TestCache() {
    saveDictionaryCache.reset(new saveDictionaryCache_t);
    idDict dict;
    dict.values = {{"key","first"},{"empty",""},{"nul",std::string("a\0tail",6)}};
    auto write = [](const idDict &value, int mode, int hits, bool markers=false) {
        Writer writer; writer.dictionaryCacheMode=mode; writer.markers=markers;
        writer.WriteDict(&value); writer.FlushWriteBuffer();
        Check(writer.dictionaryCacheHits==hits);
        Check(writer.dictionaryCacheVerified==(mode==2 ? hits : 0));
        return writer.output.bytes;
    };
    for (int mutation=0; mutation<7; ++mutation) {
        if (mutation) dict.identity.Invalidate();
        if (mutation==1) dict.values[0].value="other"; // same length
        if (mutation==2) dict.values[0].value="";
        if (mutation==3) dict.values[0].key="changed-key";
        if (mutation==4) std::swap(dict.values[0],dict.values[1]);
        if (mutation==5) dict.values.pop_back();
        if (mutation==6) dict.values.push_back({"added","value"});
        const auto expected=write(dict,0,0);
        Check(write(dict,1,0)==expected);
        Check(saveDictionaryCache->Pending()==1);
        // The first population pass deliberately does no work in a save frame.
        Check(saveDictionaryCache->Populate(MAX_PRINT_MSG,[] { return true; }).inspected==0);
        Check(write(dict,1,0)==expected); // repeated miss cannot attach twice
        Check(saveDictionaryCache->Count()==1);
        Prime(*saveDictionaryCache);
        Check(write(dict,1,1)==expected);
        Check(write(dict,2,1)==expected);
        Check(write(dict,1,1,true)==write(dict,0,0,true));
    }
    // Destroy/reconstruct at the same address; identical addresses are not proof.
    dict.~idDict(); new (&dict) idDict;
    dict.values={{"replacement","different"}};
    const auto expected=write(dict,0,0);
    Check(write(dict,1,0)==expected); Prime(*saveDictionaryCache); Check(write(dict,1,1)==expected);
    Writer failed; failed.dictionaryCacheMode=1; failed.output.fail=true;
    bool refused=false;
    try { failed.WriteDict(&dict); failed.FlushWriteBuffer(); } catch (const std::runtime_error&) { refused=true; }
    Check(refused && failed.dictionaryCacheHits==1);
    saveDictionaryCache.reset(); // map/module unload releases every allocation
    saveDictionaryCache.reset(new saveDictionaryCache_t);
    Check(write(dict,1,0)==expected);

    using SmallCache=SaveDictionaryCache<idDict,16>;
    const size_t budget=sizeof(SmallCache)+1024;
    SmallCache bounded(budget);
    std::array<idDict,100> dictionaries;
    for (auto &value:dictionaries) {
        value.values={{"key",std::string(300,'x')}};
        auto bytes=write(value,0,0); bytes.erase(bytes.begin(),bytes.begin()+4);
        bounded.Store(value,std::move(bytes));
        Check(bounded.ResidentBytes()<=budget && bounded.Count()<=16);
        Check(bounded.Find(value)!=nullptr);
    }
    // Malformed/truncated internal records never cause unchecked source reads.
    for (const auto &bad:std::vector<std::vector<byte>>{{1},{255,255,255,255},{0,0,0,0,255,0,0,0}}) {
        bounded.Store(dict,std::vector<byte>(bad)); Check(bounded.Find(dict)==nullptr);
    }
    SaveDictionaryRecord record(true); std::vector<byte> large(60000,7);
    record.Append(large.data(),large.size()); Check(record.Valid());
    record.Append(large.data(),10000); Check(!record.Valid() && record.Bytes().empty());
    record.Append(large.data(),1); Check(record.Bytes().empty());
    saveDictionaryCache.reset();
}
static void TestPopulation() {
    saveDictionaryCache_t cache;
    {
        idDict deleted;
        deleted.values={{"dead","source"}};
        cache.Queue(deleted);
    }
    auto work=Prime(cache);
    Check(work.records==0 && work.discarded==1 && cache.Count()==0);
    idDict changed; changed.values={{"changed","old"}}; cache.Queue(changed);
    changed.identity.Invalidate(); changed.values[0].value="new";
    work=Prime(cache); Check(work.records==0 && work.discarded==1);
    std::array<idDict,100> big;
    for (auto& dict:big) {
        for (int i=0;i<15;++i) dict.values.push_back({"k"+std::to_string(i),std::string(4000,'x')});
        cache.Queue(dict);
    }
    cache.Populate(MAX_PRINT_MSG,[] { return true; }); // defer the save frame
    int clockCalls=0;
    work=cache.Populate(MAX_PRINT_MSG,[&] { return clockCalls++<4; });
    Check(work.inspected<=4 && clockCalls<=5);
    work=Prime(cache); Check(work.records>90 && work.discarded==0);
    for (auto& dict:big) Check(cache.Find(dict)!=nullptr);
    idDict oversized;
    for (int i=0;i<20;++i) oversized.values.push_back({"large"+std::to_string(i),std::string(4000,'y')});
    cache.Queue(oversized); work=Prime(cache);
    Check(work.records==1 && work.discarded==1 && cache.Find(oversized)==nullptr);
    idDict malformed; malformed.values={{"invalid",std::string(4096,'z')}};
    cache.Queue(malformed); work=Prime(cache);
    Check(work.records==1 && work.discarded==1 && cache.Find(malformed)==nullptr);
}
template<class Writer> static void TestGui() {
    for (bool markers : {false,true}) for (int mode : {0,1,2}) {
        Writer w; w.markers=markers; w.userInterfaceCacheMode=mode;
        if (mode) w.userInterfaceCache.reset(new SaveUserInterfaceCache);
        idUserInterface gui;
        std::vector<byte> expected;
        for (int i=0;i<8;++i) {
            const bool unique=i%2;
            w.WriteUserInterface(&gui,unique);
            if (markers) { Integer(expected,0x594134); Integer(expected,17); }
            Integer(expected,static_cast<int>(gui.name.size()));
            expected.insert(expected.end(),gui.name.begin(),gui.name.end());
            expected.push_back(unique);
            expected.insert(expected.end(),gui.payload.begin(),gui.payload.end());
        }
        Check(w.output.bytes==expected && gui.writes==(mode==1?1:8));
        if (mode) Check(w.userInterfaceCache->hits==7 && w.userInterfaceCache->misses==1 && w.userInterfaceCache->verified==(mode==2?7:0));
        // A Save method may emit a HUD notification: invalidate before mutation.
        w.InvalidateUserInterface(&gui); gui.payload[0]=91;
        w.WriteUserInterface(&gui,false); Check(w.output.bytes[w.output.bytes.size()-gui.payload.size()]==91);
        w.WriteUserInterface(nullptr,false); w.FlushWriteBuffer();
        Check(w.output.bytes.back()==0);
    }
    for (bool memory : {false,true}) {
        Writer w; w.output.memory=memory; w.userInterfaceCacheMode=1; w.userInterfaceCache.reset(new SaveUserInterfaceCache);
        idUserInterface gui; gui.name=memory?"guis/moving.rui":"guis/shared.gui";
        w.WriteUserInterface(&gui,false); w.WriteUserInterface(&gui,false);
        Check(gui.writes==2 && w.userInterfaceCache->hits==0);
    }
    idUserInterface gui;
    for (int i=0;i<2;++i) { // no cross-snapshot reuse, even at the same GUI address
        Writer w; w.userInterfaceCacheMode=1; w.userInterfaceCache.reset(new SaveUserInterfaceCache);
        w.WriteUserInterface(&gui,false); w.WriteUserInterface(&gui,false);
        Check(gui.writes==i+1);
    }
    for (int mode : {1,2}) {
        Writer w; w.userInterfaceCacheMode=mode; w.userInterfaceCache.reset(new SaveUserInterfaceCache);
        w.WriteUserInterface(&gui,false);
        if (mode==1) w.output.failOnWrite=w.output.writes+1; else gui.payload[0]++;
        bool refused=false;
        try { w.WriteUserInterface(&gui,false); } catch (const std::runtime_error&) { refused=true; }
        Check(refused && (mode!=1 || w.userInterfaceCache->hits==1)); // fail the cached payload after its header succeeds
    }
    Writer failed; failed.userInterfaceCacheMode=1; failed.userInterfaceCache.reset(new SaveUserInterfaceCache);
    gui.fail=true; bool refused=false;
    try { failed.WriteUserInterface(&gui,false); } catch (const std::runtime_error&) { refused=true; }
    Check(refused && !failed.userInterfaceCache->Find(&gui));
}
static void TestGuiBounds() {
    SaveUserInterfaceCache cache;
    std::array<int,300> keys{};
    std::vector<byte> payload(SaveUserInterfaceCache::MaxRecordBytes+1,42);
    cache.Store(&keys[0],payload.data(),payload.size()); Check(!cache.Find(&keys[0]));
    for (int& key:keys) cache.Store(&key,payload.data(),SaveUserInterfaceCache::MaxRecordBytes);
    Check(cache.ResidentBytes()==SaveUserInterfaceCache::MaxBytes);
    cache.Invalidate(&keys[0]); Check(cache.ResidentBytes()==SaveUserInterfaceCache::MaxBytes-SaveUserInterfaceCache::MaxRecordBytes);
    cache.Store(&keys[0],payload.data(),2); Check(cache.Find(&keys[0])->size()==2);
    SaveUserInterfaceCache tiny;
    for (int& key:keys) tiny.Store(&key,payload.data(),1);
    Check(tiny.ResidentBytes()==SaveUserInterfaceCache::MaxEntries);
}
int main() {
    Test<idSaveGame_sp>(); Test<idSaveGame_mp>();
    TestCache<idSaveGame_sp>(); TestCache<idSaveGame_mp>();
    TestPopulation();
    TestGui<idSaveGame_sp>(); TestGui<idSaveGame_mp>(); TestGuiBounds();
    std::puts("Save wire parity, deferred lifetime/deadline/byte bounds, cache mutation, buffer boundaries, GUI aliases/invalidation/oracle/limits and failed writes passed");
}
