// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Full production EventLoop.cpp with counted file, allocator and dispatch doubles.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "JournalEventContract.h"

static unsigned checks, allocations, releases, fatalCalls;
static bool allocationFails, readThrows, writeThrows, dispatchThrows, commandThrows, fatalReturns;
static bool requireReleasedAtFatal;
static std::map<void*, std::size_t> owned;
static std::vector<unsigned char> lastReleased;
static std::vector<std::string> order;
static std::deque<sysEvent_t> queued;
static std::string fatalMessage;
static void Check(bool okay, const char* why) {
	++checks;
	if (!okay) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
}
static void* Mem_ClearedAlloc(int length) {
	Check(length > 0 && length <= 1024*1024, "allocation only after bounded length validation");
	++allocations;
	if (allocationFails) return nullptr;
	auto* p = std::calloc(static_cast<std::size_t>(length),1);
	Check(p != nullptr, "test allocation succeeded"); owned.emplace(p,length); return p;
}
static void Mem_Free(void* p) {
	Check(owned.count(p) == 1, "only an owned payload is released exactly once; never a recorded address");
	const auto size = owned.at(p);
	lastReleased.assign(static_cast<unsigned char*>(p),static_cast<unsigned char*>(p)+size);
	owned.erase(p); std::free(p); ++releases;
}
class idFile {
public:
	std::vector<unsigned char> bytes;
	std::size_t cursor = 0;
	unsigned readCalls = 0, writeCalls = 0;
	int shortWriteCall = 0, badReadCall = 0;
	int Read(void* target, int length) {
		++readCalls;
		if (readThrows && readCalls == 2) throw std::runtime_error("read exception");
		if (badReadCall == static_cast<int>(readCalls)) return -1;
		const auto count = (std::min)(static_cast<std::size_t>(length),bytes.size()-cursor);
		if (count) std::memcpy(target,bytes.data()+cursor,count);
		cursor += count; return static_cast<int>(count);
	}
	int Write(const void* data, int length) {
		++writeCalls;
		if (writeThrows && writeCalls == 2) throw std::runtime_error("write exception");
		const int count = shortWriteCall == static_cast<int>(writeCalls) ? length-1 : length;
		const auto* p = static_cast<const unsigned char*>(data);
		bytes.insert(bytes.end(),p,p+count); return count;
	}
};
class idCVar {
public:
	int value = 0;
	template<typename... Args> idCVar(Args...) {}
	int GetInteger() const { return value; }
	void SetInteger(int next) { value = next; }
};
constexpr int CVAR_INIT=1, CVAR_SYSTEM=2, CMD_EXEC_APPEND=1;
class idCmdSystem {
public:
	template<int,int> static void ArgCompletion_Integer() {}
	void ExecuteCommandBuffer() { order.emplace_back("commands"); }
	void BufferCommandText(int mode, const char* text) {
		Check(mode == CMD_EXEC_APPEND, "console command remains append-only at this boundary");
		order.emplace_back(std::string("console:")+text);
		if (commandThrows) throw std::runtime_error("command exception");
	}
};
class MockCommon {
public:
	void FatalError(const char*, const char* message) {
		++fatalCalls; fatalMessage=message;
		if (requireReleasedAtFatal) Check(owned.empty(), "payload released before FatalError can exit without unwinding");
		if (!fatalReturns) throw std::runtime_error("fatal");
	}
	void Printf(const char*) {}
	void StartupVariable(const char*, bool) {}
};
class MockCvars {
public:
	bool initialized = true;
	unsigned queries = 0;
	bool IsInitialized() { return initialized; }
	bool CommandContainsPrivateCVar(const char* text) { ++queries; return std::strstr(text,"secret_password") != nullptr; }
};
class MockSession {
public:
	std::vector<sysEvent_t> events;
	std::vector<std::vector<unsigned char>> payloads;
	void ProcessEvent(const sysEvent_t* event) {
		events.push_back(*event); order.emplace_back("session:"+std::to_string(event->evType));
		if (event->evPtrLength) payloads.emplace_back(static_cast<unsigned char*>(event->evPtr),static_cast<unsigned char*>(event->evPtr)+event->evPtrLength);
		if (dispatchThrows) throw std::runtime_error("dispatch exception");
	}
};
class MockFiles {
public:
	std::deque<idFile*> planned;
	std::vector<std::string> opens;
	std::vector<idFile*> closed;
	idFile* Open(const std::string& operation) {
		opens.push_back(operation);
		if (planned.empty()) return nullptr;
		auto* result=planned.front(); planned.pop_front(); return result;
	}
	idFile* OpenFileWrite(const char* path) { return Open(std::string("write:")+path); }
	idFile* OpenFileRead(const char* path) { return Open(std::string("read:")+path); }
	void CloseFile(idFile* file) { Check(file!=nullptr,"only acquired journal handles are closed"); closed.push_back(file); }
};
class idKeyInput {
public:
	static void PreliminaryKeyEvent(int value,bool down) { order.emplace_back("key:"+std::to_string(value)+":"+(down?"1":"0")); }
	static void PreliminaryMouseEvent(int,int) { order.emplace_back("mouse"); }
	static void PreliminaryJoystickEvent(int) { order.emplace_back("axis"); }
};
static MockCommon commonObject;
static MockCommon* common=&commonObject;
static MockCvars cvarObject;
static MockCvars* cvarSystem=&cvarObject;
static MockSession sessionObject;
static MockSession* session=&sessionObject;
static MockFiles fileObject;
static MockFiles* fileSystem=&fileObject;
static idCmdSystem cmdObject;
static idCmdSystem* cmdSystem=&cmdObject;
static int Sys_Milliseconds() { return 1000; }
static sysEvent_t Sys_GetEvent() {
	if (queued.empty()) return sysEvent_t{};
	const auto event=queued.front(); queued.pop_front(); return event;
}
#define private public
#include "src/framework/EventLoop.h"
#undef private
#include "src/framework/EventLoop.cpp"

static void Reset() {
	Check(owned.empty(), "no event payload remains after case");
	Check(queued.empty(), "all supplied native events consumed");
	Check(fileObject.planned.empty(), "all planned paired file opens consumed");
	allocations=releases=fatalCalls=0;
	allocationFails=readThrows=writeThrows=dispatchThrows=commandThrows=fatalReturns=requireReleasedAtFatal=false;
	cvarObject.initialized=true; cvarObject.queries=0; cvarSystem=&cvarObject;
	lastReleased.clear(); order.clear(); fatalMessage.clear(); sessionObject.events.clear(); sessionObject.payloads.clear();
	fileObject.opens.clear(); fileObject.closed.clear();
	eventLoopLocal.com_journal.SetInteger(0); eventLoopLocal.com_journalFile=nullptr; eventLoopLocal.com_journalDataFile=nullptr;
}
static std::vector<unsigned char> Record(int type, int length, const std::vector<unsigned char>& payload={}, int value=0, int value2=0) {
	std::vector<unsigned char> result(sizeof(sysEvent_t),0xcc);
	std::memcpy(result.data()+offsetof(sysEvent_t,evType),&type,sizeof(type));
	std::memcpy(result.data()+offsetof(sysEvent_t,evValue),&value,sizeof(value));
	std::memcpy(result.data()+offsetof(sysEvent_t,evValue2),&value2,sizeof(value2));
	std::memcpy(result.data()+offsetof(sysEvent_t,evPtrLength),&length,sizeof(length));
	// A historical raw pointer is intentionally invalid in this process.
	const std::uintptr_t recorded=static_cast<std::uintptr_t>(0xfeeddead);
	std::memcpy(result.data()+offsetof(sysEvent_t,evPtr),&recorded,sizeof(recorded));
	result.insert(result.end(),payload.begin(),payload.end()); return result;
}
static std::vector<unsigned char> Text(const char* text) {
	return {reinterpret_cast<const unsigned char*>(text),reinterpret_cast<const unsigned char*>(text)+std::strlen(text)+1};
}
static sysEvent_t Live(int type, const std::vector<unsigned char>& bytes={}) {
	sysEvent_t event={}; event.evType=static_cast<sysEventType_t>(type);
	if (!bytes.empty()) { event.evPtrLength=static_cast<int>(bytes.size()); event.evPtr=Mem_ClearedAlloc(event.evPtrLength); std::memcpy(event.evPtr,bytes.data(),bytes.size()); }
	return event;
}
static bool Same(const sysEvent_t& a,const sysEvent_t& b) {
	return EventLoop_EventType(a)==EventLoop_EventType(b) && a.evValue==b.evValue && a.evValue2==b.evValue2 && a.evPtrLength==b.evPtrLength && a.evPtr==b.evPtr;
}
template<typename F> static void Throws(F callback,const char* why) {
	bool caught=false; try { callback(); } catch(const std::runtime_error&) { caught=true; }
	Check(caught,why);
}
static void ReadCases() {
	for (const int type : {SE_NONE,SE_KEY,SE_CHAR,SE_MOUSE,SE_JOYSTICK_AXIS}) {
		Reset(); idFile file; file.bytes=Record(type,0,{},-1234567,7654321);
		sysEvent_t out={}; Check(EventLoop_ReadJournalEvent(&file,out)==nullptr, "historical payloadless event reads");
		Check(EventLoop_EventType(out)==type && out.evValue==-1234567 && out.evValue2==7654321 && !out.evPtr && !out.evPtrLength,
			"value bits preserved but historical pointer garbage is ignored");
		Check(allocations==0 && releases==0, "zero payload never allocates or frees recorded address");
	}
	for (std::size_t size=0; size<sizeof(sysEvent_t); ++size) {
		Reset(); idFile file; file.bytes=Record(SE_CONSOLE,4); file.bytes.resize(size);
		sysEvent_t out={}; out.evValue=42; const auto before=out;
		Check(EventLoop_ReadJournalEvent(&file,out)!=nullptr && Same(out,before), "every truncated header rejects atomically");
		Check(allocations==0, "no allocation from a partial header");
	}
	for (const auto item : std::vector<std::pair<int,int>>{{-1,0},{999,0},{SE_CONSOLE,-1},{SE_RETAINED_UI,-1},{SE_CONSOLE,1024*1024+1},
		{SE_RETAINED_UI,(std::numeric_limits<int>::max)()},{SE_NONE,1},{SE_KEY,1},{SE_CHAR,1},{SE_MOUSE,1},{SE_JOYSTICK_AXIS,1},{SE_CONSOLE,0},{SE_RETAINED_UI,0}}) {
		Reset(); idFile file; file.bytes=Record(item.first,item.second); sysEvent_t out={}; out.evValue=42; const auto before=out;
		Check(EventLoop_ReadJournalEvent(&file,out)!=nullptr && Same(out,before), "invalid type/length/shape rejects atomically");
		Check(allocations==0 && file.readCalls==1, "invalid header never reads payload or allocates");
	}
	const auto console=Text("echo journal");
	for (std::size_t size=0; size<console.size(); ++size) {
		Reset(); idFile file; file.bytes=Record(SE_CONSOLE,static_cast<int>(console.size()),{console.begin(),console.begin()+size});
		sysEvent_t out={}; out.evValue=42; const auto before=out;
		Check(EventLoop_ReadJournalEvent(&file,out)!=nullptr && Same(out,before), "every truncated payload rejects atomically");
		Check(allocations==1 && releases==1 && owned.empty(), "partial read frees exactly its new allocation");
		Check(std::all_of(lastReleased.begin(),lastReleased.end(),[](unsigned char b){return b==0;}), "failed console payload wiped without scanning unsafe text");
	}
	Reset(); idFile badConsole; badConsole.bytes=Record(SE_CONSOLE,4,{'t','e','s','t'}); sysEvent_t out={};
	Check(EventLoop_ReadJournalEvent(&badConsole,out)!=nullptr && releases==1, "unterminated console payload never becomes a command");
	Reset(); idFile binary; const std::vector<unsigned char> data{0xff,0,7,0x80}; binary.bytes=Record(SE_RETAINED_UI,4,data,17,0);
	Check(EventLoop_ReadJournalEvent(&binary,out)==nullptr && out.evValue==17 && std::memcmp(out.evPtr,data.data(),data.size())==0,
		"retained binary payload remains opaque bounded bytes for its existing consumer");
	eventLoopLocal.ProcessEvent(out); Check(sessionObject.payloads==std::vector<std::vector<unsigned char>>{data} && releases==1, "binary event dispatch owns and frees payload once");
	Reset(); idFile maximum; const std::vector<unsigned char> large(1024*1024,0xab); maximum.bytes=Record(SE_RETAINED_UI,static_cast<int>(large.size()),large);
	Check(EventLoop_ReadJournalEvent(&maximum,out)==nullptr && out.evPtrLength==1024*1024, "documented payload bound is inclusive");
	Mem_Free(out.evPtr);
	for (const int failedRead : {1,2}) {
		Reset(); idFile file; file.bytes=Record(SE_CONSOLE,static_cast<int>(console.size()),console); file.badReadCall=failedRead;
		Check(EventLoop_ReadJournalEvent(&file,out)!=nullptr && owned.empty(), "negative file result rejects with no retained allocation");
	}
	Reset(); idFile file; file.bytes=Record(SE_CONSOLE,static_cast<int>(console.size()),console); allocationFails=true;
	Check(EventLoop_ReadJournalEvent(&file,out)!=nullptr && file.readCalls==1 && owned.empty(), "allocation refusal never attempts payload read");
	Reset(); file.cursor=0; file.readCalls=0; readThrows=true;
	Throws([&]{EventLoop_ReadJournalEvent(&file,out);},"file exception propagates"); Check(releases==1 && owned.empty(), "exception during read frees candidate allocation");
}
static void WriteCases() {
	for (const bool privateCommand : {false,true}) {
		Reset(); idFile file; const auto bytes=Text(privateCommand?"set secret_password value":"echo normal"); auto event=Live(SE_CONSOLE,bytes);
		const auto before=event; Check(EventLoop_WriteJournalEvent(&file,event)==nullptr && Same(event,before), "write never replaces live event ownership");
		Check(std::memcmp(event.evPtr,bytes.data(),bytes.size())==0, "recording leaves original command available for one live dispatch");
		std::uintptr_t pointer=1; std::memcpy(&pointer,file.bytes.data()+offsetof(sysEvent_t,evPtr),sizeof(pointer));
		Check(pointer==0, "new journals never contain process addresses");
		const auto expected=privateCommand?Text(""):bytes;
		Check(std::vector<unsigned char>(file.bytes.begin()+sizeof(sysEvent_t),file.bytes.end())==expected, "private command is redacted only in persisted payload");
		sysEvent_t replay={}; Check(EventLoop_ReadJournalEvent(&file,replay)==nullptr, "new record replays through historical layout");
		Check(replay.evPtrLength==static_cast<int>(expected.size()) && std::memcmp(replay.evPtr,expected.data(),expected.size())==0, "record length matches redaction or original bytes");
		Mem_Free(replay.evPtr); eventLoopLocal.ProcessEvent(event);
		Check(order.front()==std::string("console:")+reinterpret_cast<const char*>(bytes.data()), "live private command still executes its original content exactly once");
		Check(std::all_of(lastReleased.begin(),lastReleased.end(),[](unsigned char b){return b==0;}), "live console storage wiped before free");
	}
	Reset(); auto event=Live(SE_CONSOLE,Text("echo failure")); idFile file;
	event.evPtrLength=-1; Check(EventLoop_WriteJournalEvent(&file,event)!=nullptr && file.writeCalls==0, "invalid live length rejects before any write or text scan");
	event.evPtrLength=13; Mem_Free(event.evPtr);
	Reset(); event={}; event.evType=SE_CONSOLE; event.evPtrLength=3;
	Check(EventLoop_WriteJournalEvent(&file,event)!=nullptr && file.writeCalls==0, "missing live payload rejects before file writes");
	Reset(); event=Live(SE_CONSOLE,{'b','a','d'});
	Check(EventLoop_WriteJournalEvent(&file,event)!=nullptr && file.writeCalls==0 && cvarObject.queries==0, "unterminated live console never reaches privacy parser or file");
	Mem_Free(event.evPtr);
	Reset(); event=Live(SE_NONE,{1}); event.evPtrLength=0;
	Check(EventLoop_WriteJournalEvent(&file,event)!=nullptr && file.writeCalls==0, "non-null zero-length live ownership is rejected, not silently leaked");
	Mem_Free(event.evPtr);
}
static void IntegrationCases() {
	for (const int failure : {1,2}) {
		Reset(); idFile file; file.shortWriteCall=failure; queued.push_back(Live(SE_CONSOLE,Text("secret_password value")));
		eventLoopLocal.com_journal.SetInteger(1); eventLoopLocal.com_journalFile=&file; requireReleasedAtFatal=true;
		Throws([&]{eventLoopLocal.GetRealEvent();},"short journal write is fatal");
		Check(fatalCalls==1 && releases==1 && owned.empty(), "failed record releases original event before fatal shutdown");
	}
	Reset(); idFile file; queued.push_back(Live(SE_CONSOLE,Text("echo exception"))); writeThrows=true;
	eventLoopLocal.com_journal.SetInteger(1); eventLoopLocal.com_journalFile=&file;
	Throws([&]{eventLoopLocal.GetRealEvent();},"journal write exception propagates"); Check(owned.empty() && releases==1, "write exception releases live payload");
	Reset(); file={}; file.bytes=Record(SE_CONSOLE,4,{'b','a'}); eventLoopLocal.com_journal.SetInteger(2); eventLoopLocal.com_journalFile=&file;
	requireReleasedAtFatal=true; Throws([&]{eventLoopLocal.GetRealEvent();},"truncated playback is fatal");
	Check(fatalCalls==1 && releases==1 && owned.empty(), "playback failure cleans candidate before fatal shutdown");
	Reset(); file={}; file.bytes=Record(999,0); eventLoopLocal.com_journal.SetInteger(2); eventLoopLocal.com_journalFile=&file; fatalReturns=true;
	const auto fallback=eventLoopLocal.GetRealEvent(); Check(fallback.evType==SE_NONE && !fallback.evPtr && fatalCalls==1, "defensive failure return cannot dispatch stale/uninitialized data");
	Reset(); auto bad=Live(SE_NONE,{1}); queued.push_back(bad); requireReleasedAtFatal=true;
	Throws([&]{eventLoopLocal.GetRealEvent();},"invalid payload shape is rejected even with journaling off"); Check(releases==1, "invalid live shape discards its owned buffer");
	Reset(); dispatchThrows=true; queued.push_back(Live(SE_RETAINED_UI,{1,2,3}));
	Throws([&]{eventLoopLocal.RunEventLoop();},"session callback exception propagates"); Check(releases==1 && owned.empty(), "session exception cannot leak event payload");
	Reset(); commandThrows=true; queued.push_back(Live(SE_CONSOLE,Text("echo failure")));
	Throws([&]{eventLoopLocal.RunEventLoop();},"command callback exception propagates"); Check(releases==1 && owned.empty(), "command exception cannot leak console payload");
	Reset(); auto key=Live(SE_KEY); key.evValue=27; key.evValue2=1; queued.push_back(key);
	queued.push_back(Live(SE_CONSOLE,Text("echo ordered"))); queued.push_back(Live(SE_RETAINED_UI,{9}));
	Check(eventLoopLocal.RunEventLoop()==0, "normal event loop terminates on empty event");
	Check(order==std::vector<std::string>{"commands","key:27:1","session:1","commands","console:echo ordered","console:\n","commands","session:6","commands"},
		"commands precede each event, physical bookkeeping precedes session, and console routing remains unchanged");
	Check(releases==2 && owned.empty(), "successful loop releases each payload once");
	Reset(); file={}; file.bytes=Record(SE_NONE,0); eventLoopLocal.com_journal.SetInteger(2); eventLoopLocal.com_journalFile=&file;
	Check(eventLoopLocal.RunEventLoop(false)==0 && order.empty() && releases==0, "historical no-event pointer garbage neither dispatches nor frees at loop exit");
	Reset(); eventLoopLocal.com_journal.SetInteger(2); requireReleasedAtFatal=true;
	Throws([&]{eventLoopLocal.GetRealEvent();},"missing journal file is rejected before dereference");
}
static void PairedOpenCases() {
	for (const int mode : {1,2}) {
		for (unsigned available=0; available<4; ++available) {
			Reset(); idFile events,data;
			fileObject.planned={available&1?&events:nullptr,available&2?&data:nullptr};
			eventLoopLocal.com_journal.SetInteger(mode); eventLoopLocal.Init();
			const std::string operation=mode==1?"write:":"read:";
			Check(fileObject.opens==std::vector<std::string>{operation+"journal.dat",operation+"journaldata.dat"}, "paired journal names and read/write modes remain historical");
			if (available==3) {
				Check(eventLoopLocal.JournalLevel()==mode && eventLoopLocal.com_journalFile==&events && eventLoopLocal.com_journalDataFile==&data && fileObject.closed.empty(),
					"successful paired open retains both handles and journal mode");
				eventLoopLocal.Shutdown();
				Check(fileObject.closed==std::vector<idFile*>{&events,&data}, "normal shutdown closes each acquired handle once");
			} else {
				std::vector<idFile*> expected;
				if (available&1) expected.push_back(&events);
				if (available&2) expected.push_back(&data);
				Check(eventLoopLocal.JournalLevel()==0 && !eventLoopLocal.com_journalFile && !eventLoopLocal.com_journalDataFile,
					"partial open disables journaling and clears both published handles");
				Check(fileObject.closed==expected, "every successfully opened half is closed before it is forgotten");
				eventLoopLocal.Shutdown();
				Check(fileObject.closed==expected, "shutdown after failed pair cannot close a handle twice");
			}
			Check(!eventLoopLocal.com_journalFile && !eventLoopLocal.com_journalDataFile, "closed pair leaves no stale handles");
		}
	}
	Reset(); eventLoopLocal.Init();
	Check(fileObject.opens.empty() && fileObject.closed.empty(), "disabled journaling performs no file operations");
}
static void KeyMetadataCases() {
	for (unsigned flags=0; flags<16; ++flags) {
		Reset(); const openq4::KeyEventMetadata value{(flags&1)!=0,(flags&2)!=0,(flags&4)!=0,(flags&8)!=0};
		const auto encoded=openq4::EncodeKeyEventMetadata(value);
		const std::vector<unsigned char> bytes(encoded.begin(),encoded.end());
		auto event=Live(SE_KEY,bytes);event.evValue=65;event.evValue2=1;idFile file;
		Check(EventLoop_WriteJournalEvent(&file,event)==nullptr,"key modifier record writes through actual journal");
		sysEvent_t decoded={};Check(EventLoop_ReadJournalEvent(&file,decoded)==nullptr,"key modifier record reads through actual journal");
		openq4::KeyEventMetadata restored;
		Check(openq4::DecodeKeyEventMetadata(decoded.evPtr,decoded.evPtrLength,restored) && restored.control==value.control &&
			restored.shift==value.shift && restored.alt==value.alt && restored.repeated==value.repeated,"all modifier and repeat combinations survive playback");
		eventLoopLocal.ProcessEvent(event);eventLoopLocal.ProcessEvent(decoded);
		Check(releases==2 && owned.empty(),"live and replayed key metadata each release exactly once");
	}
	const auto good=openq4::EncodeKeyEventMetadata({true,true,true,true});
	for (std::size_t index=0;index<good.size();++index) for(unsigned byte=0;byte<256;++byte) {
		auto damaged=good;damaged[index]=static_cast<unsigned char>(byte);
		const bool valid=index==4 ? byte<=7 : index==5 ? byte<=1 : byte==good[index];
		Reset();idFile file;file.bytes=Record(SE_KEY,8,{damaged.begin(),damaged.end()},65,1);
		sysEvent_t out={};out.evValue=42;const auto before=out;
		if(valid) {Check(EventLoop_ReadJournalEvent(&file,out)==nullptr,"legal metadata byte accepted");Mem_Free(out.evPtr);}
		else Check(EventLoop_ReadJournalEvent(&file,out)!=nullptr && Same(out,before),"damaged key metadata rejects atomically");
		Check(owned.empty() && releases==1,"rejected key metadata storage released");
	}
	for(std::size_t bytes=0;bytes<good.size();++bytes) {
		Reset();idFile file;file.bytes=Record(SE_KEY,8,{good.begin(),good.begin()+bytes});sysEvent_t out={};
		Check(EventLoop_ReadJournalEvent(&file,out)!=nullptr && owned.empty() && releases==1,"truncated key metadata has no partial publication");
	}
}
int main() {
	ReadCases(); WriteCases(); IntegrationCases(); PairedOpenCases(); KeyMetadataCases(); Reset();
	std::printf("Event journal: %u checks passed (native historical layout, counted file/ownership/dispatch doubles)\n",checks);
}
