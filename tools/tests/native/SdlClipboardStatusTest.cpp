// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Counted platform double surrounding extracted, unmodified SDL method bodies.
// Does not call native Windows APIs, initialize SDL or touch a clipboard.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using DWORD = unsigned long;
using HANDLE = void*;
using HWND = void*;
using BOOL = int;
using SIZE_T = std::size_t;
using WCHAR = char16_t;
using LPWSTR = WCHAR*;
using LPTSTR = WCHAR*;
using LPCSTR = const char*;
constexpr int TRUE = 1, FALSE = 0, ERROR_SUCCESS = 0, GMEM_MOVEABLE = 2;
constexpr unsigned CF_UNICODETEXT = 13, CF_TEXT = 1, CF_OEMTEXT = 7, CF_DIBV5 = 17, CF_DIB = 8;
#define SDLCALL
using SDL_ClipboardDataCallback = const void* (*)(void*, const char*, std::size_t*);
using SDL_ClipboardCleanupCallback = void (*)(void*);
struct WindowData { HWND hwnd{}; };
struct Window { WindowData* internal{}; };
struct SDL_VideoData { unsigned clipboard_count{}; };
struct SDL_VideoDevice {
	Window* windows{};
	SDL_VideoData* internal{};
	SDL_ClipboardDataCallback clipboard_callback{};
	SDL_ClipboardCleanupCallback clipboard_cleanup{};
	void* clipboard_userdata{};
	const char* const* clipboard_mime_types{};
	std::size_t num_clipboard_mime_types{};
};
static unsigned checks, openCalls, closeCalls, delayCalls, emptyCalls, transferCalls, globalFrees, sdlFrees, setCalls;
static unsigned failedOpenAttempts;
static bool emptyFails, lockFails, allocationFails, transferFails, duplicateFails, conversionFails, readConversionFails, getFails;
static bool deviceAvailable = true, formatAvailable = true;
static unsigned advertisedFormat = CF_UNICODETEXT;
static std::vector<unsigned> requestedFormats;
static DWORD lastError;
static std::string error;
struct Block { std::vector<unsigned char> bytes; bool transferred = false; };
static std::map<HANDLE, Block> globalBlocks;
static std::map<void*, std::size_t> sdlBlocks;
static std::u16string nativeText = u"native";
static std::u16string transferredText;
static SDL_VideoData videoData;
static WindowData windowData{reinterpret_cast<HWND>(17)};
static Window window{&windowData};
static SDL_VideoDevice video{&window, &videoData};
static void Check(bool okay, const char* why) {
	++checks;
	if (!okay) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static bool SDL_SetError(const char* text) { error = text; return false; }
static bool SDL_OutOfMemory() { return SDL_SetError("out of memory"); }
static bool WIN_SetError(const char* text) {
	error = std::string(text) + " code=" + std::to_string(lastError);
	lastError = 999; // Formatting can alter native last error; primary SDL error must survive.
	return false;
}
static DWORD GetLastError() { return lastError; }
static void SetLastError(DWORD value) { lastError = value; }
static BOOL OpenClipboard(HWND hwnd) {
	Check(hwnd == windowData.hwnd || hwnd == nullptr, "open uses the actual window or no window");
	++openCalls;
	if (openCalls <= failedOpenAttempts) { lastError = 5; return FALSE; }
	return TRUE;
}
static void SDL_Delay(unsigned ms) { Check(ms == 10, "retry interval unchanged"); ++delayCalls; lastError = 777; }
static void CloseClipboard() { ++closeCalls; lastError = 888; }
static BOOL EmptyClipboard() {
	++emptyCalls;
	if (emptyFails) { lastError = 6; return FALSE; }
	for (auto it = globalBlocks.begin(); it != globalBlocks.end();) {
		if (it->second.transferred) { std::free(it->first); it = globalBlocks.erase(it); }
		else ++it;
	}
	return TRUE;
}
static HANDLE GlobalAlloc(unsigned flags, std::size_t size) {
	Check(flags == GMEM_MOVEABLE, "movable clipboard storage retained");
	if (allocationFails) return nullptr;
	auto* handle = std::malloc(1); Check(handle != nullptr, "test handle allocated");
	globalBlocks.emplace(handle, Block{std::vector<unsigned char>(size, 0xab), false});
	return handle;
}
static void* GlobalLock(HANDLE handle) {
	if (lockFails) { lastError = 7; return nullptr; }
	if (handle == nativeText.data()) return nativeText.data();
	return globalBlocks.at(handle).bytes.data();
}
static void GlobalUnlock(HANDLE) { lastError = 111; }
static void GlobalFree(HANDLE handle) {
	Check(globalBlocks.count(handle) == 1 && !globalBlocks.at(handle).transferred, "only untransferred native storage is freed by writer");
	globalBlocks.erase(handle); std::free(handle); ++globalFrees; lastError = 444;
}
static HANDLE SetClipboardData(unsigned format, HANDLE handle) {
	++transferCalls; Check(format == CF_UNICODETEXT, "text uses Unicode native format");
	if (transferFails) { lastError = 8; return nullptr; }
	auto& block = globalBlocks.at(handle); block.transferred = true;
	transferredText = reinterpret_cast<const WCHAR*>(block.bytes.data());
	return handle;
}
static unsigned GetClipboardSequenceNumber() { return 23; }
static void* SDL_malloc(std::size_t size) {
	auto* data = std::malloc(size); Check(data != nullptr, "mock SDL allocation succeeded");
	sdlBlocks.emplace(data, size); return data;
}
static void SDL_free(void* data) {
	if (!data) return;
	Check(sdlBlocks.erase(data) == 1, "SDL-owned allocation freed exactly once");
	std::free(data); ++sdlFrees;
}
static char* SDL_strdup(const char* text) {
	if (duplicateFails) return nullptr;
	const auto size = std::strlen(text)+1;
	auto* result = static_cast<char*>(SDL_malloc(size)); std::memcpy(result, text, size); return result;
}
static std::size_t SDL_strlen(const char* text) { Check(text != nullptr, "read failure fallback does not pass null to strlen"); return std::strlen(text); }
static int SDL_strcmp(const char* a, const char* b) { return std::strcmp(a,b); }
static void SDL_memcpy(void* a, const void* b, std::size_t size) { std::memcpy(a,b,size); }
static void* SDL_iconv_string(const char*, const char*, const char* bytes, std::size_t size) {
	if (conversionFails) return nullptr;
	auto* result = static_cast<WCHAR*>(SDL_malloc((size+1)*sizeof(WCHAR)));
	for (std::size_t i = 0; i < size; ++i) result[i] = static_cast<unsigned char>(bytes[i]);
	result[size] = 0; return result; // ASCII status fixtures only; no transcoder claim.
}
static bool SDL_IsTextMimeType(const char* text) { return std::strncmp(text,"text",4) == 0; }
static bool WIN_SetClipboardImage(SDL_VideoDevice*, const char*) { Check(false, "text fixture does not execute image path"); return false; }
static BOOL IsClipboardFormatAvailable(unsigned format) { return format == advertisedFormat && formatAvailable; }
static HANDLE GetClipboardData(unsigned format) {
	requestedFormats.push_back(format);
	if (getFails || format != CF_UNICODETEXT) { lastError = 9; return nullptr; }
	return nativeText.data(); // Models Windows' synthesized Unicode, including legacy advertised formats.
}
static char* WIN_StringToUTF8W(const WCHAR* text) {
	if (readConversionFails) return nullptr;
	std::string bytes;
	while (*text) {
		const unsigned scalar = *text++;
		if (scalar < 0x80) bytes += static_cast<char>(scalar);
		else if (scalar < 0x800) { bytes += static_cast<char>(0xc0 | (scalar >> 6)); bytes += static_cast<char>(0x80 | (scalar & 63)); }
		else { bytes += static_cast<char>(0xe0 | (scalar >> 12)); bytes += static_cast<char>(0x80 | ((scalar >> 6) & 63)); bytes += static_cast<char>(0x80 | (scalar & 63)); }
	}
	return SDL_strdup(bytes.c_str()); // BMP fixtures; this double does not qualify SDL's transcoder.
}
static void* WIN_ConvertDIBtoBMP(HANDLE, std::size_t*) { Check(false, "no image conversion in text fixtures"); return nullptr; }
static unsigned GetClipboardFormatPNG() { return 77; }
static std::size_t GlobalSize(HANDLE) { return 0; }
static void* SDL_GetInternalClipboardData(SDL_VideoDevice*, const char*, std::size_t*) { return nullptr; }
static bool SDL_HasInternalClipboardData(SDL_VideoDevice*, const char*) { return false; }
static SDL_VideoDevice* SDL_GetVideoDevice() { return deviceAvailable ? &video : nullptr; }
static bool SDL_UninitializedVideo() { return SDL_SetError("Video subsystem not initialized"); }
static const char* const* SDL_GetTextMimeTypes(SDL_VideoDevice*, std::size_t* count) {
	static const char* const names[]{"text/plain;charset=utf-8"}; *count = 1; return names;
}
bool WIN_SetClipboardData(SDL_VideoDevice*);
void* WIN_GetClipboardData(SDL_VideoDevice*, const char*, std::size_t*);
static void ReleaseCallback() {
	if (video.clipboard_cleanup) video.clipboard_cleanup(video.clipboard_userdata);
	video.clipboard_callback = nullptr; video.clipboard_cleanup = nullptr; video.clipboard_userdata = nullptr;
	video.num_clipboard_mime_types = 0; video.clipboard_mime_types = nullptr;
}
static bool SDL_SetClipboardData(SDL_ClipboardDataCallback callback, SDL_ClipboardCleanupCallback cleanup,
	void* userdata, const char* const* names, std::size_t count) {
	++setCalls; ReleaseCallback();
	video.clipboard_callback = callback; video.clipboard_cleanup = cleanup; video.clipboard_userdata = userdata;
	video.clipboard_mime_types = names; video.num_clipboard_mime_types = count;
	return WIN_SetClipboardData(&video); // Models SDL adopting callback ownership before native writes.
}
static bool SDL_ClearClipboardData() { return SDL_SetClipboardData(nullptr,nullptr,nullptr,nullptr,0); }
static void* SDL_GetClipboardData(const char* type, std::size_t* size) { return WIN_GetClipboardData(&video,type,size); }

// PRODUCTION_METHODS

static void Reset() {
	ReleaseCallback();
	Check(sdlBlocks.empty(), "all temporary and callback SDL storage released");
	for (const auto& entry : globalBlocks) {
		Check(entry.second.transferred, "no leaked untransferred HGLOBAL remains after operation");
		std::free(entry.first);
	}
	globalBlocks.clear();
	openCalls = closeCalls = delayCalls = emptyCalls = transferCalls = globalFrees = sdlFrees = setCalls = 0;
	failedOpenAttempts = 0; lastError = 0;
	emptyFails = lockFails = allocationFails = transferFails = duplicateFails = conversionFails = readConversionFails = getFails = false;
	deviceAvailable = formatAvailable = true; video.windows = &window;
	advertisedFormat = CF_UNICODETEXT; requestedFormats.clear();
	error.clear(); transferredText.clear(); nativeText = u"native";
}
int main() {
	for (unsigned failures = 0; failures <= 3; ++failures) {
		Reset(); failedOpenAttempts = failures;
		Check(static_cast<bool>(WIN_OpenClipboard(&video)) == (failures < 3), "retry success/exhaustion truthfully returned");
		Check(openCalls == (failures < 3 ? failures+1 : 3) && delayCalls == failures, "bounded retry count and delay preserved");
		Check(failures < 3 ? error.empty() : error.find("code=5") != std::string::npos, "only exhausted open publishes original native error");
	}
	Reset(); video.windows = nullptr; Check(WIN_OpenClipboard(&video), "windowless SDL device can open clipboard");
	Reset(); Check(SDL_SetClipboardText("first\nsecond\r\n"), "ordinary text succeeds through actual shared and Windows setters");
	Check(transferredText == u"first\r\nsecond\r\n", "native Windows newline conversion remains intact");
	Check(setCalls == 1 && openCalls == 1 && closeCalls == 1 && emptyCalls == 1 && transferCalls == 1 && globalFrees == 0,
		"success transfers HGLOBAL once and closes clipboard without freeing transferred storage");
	Check(sdlBlocks.size() == 1, "SDL retains exactly the callback copy until replacement");
	Check(SDL_SetClipboardText("next"), "replacement succeeds with prior callback and native storage cleanup");
	Check(sdlBlocks.size() == 1 && globalBlocks.size() == 1 && transferredText == u"next", "replacement does not leak prior storage");
	Reset(); duplicateFails = true;
	Check(!SDL_SetClipboardText("copy"), "copy allocation failure rejects before native work");
	Check(error == "out of memory" && setCalls == 0 && openCalls == 0 && emptyCalls == 0 && sdlBlocks.empty(), "failed strdup never publishes callback or clears clipboard");
	Reset(); allocationFails = true;
	Check(!SDL_SetClipboardText("copy") && error == "out of memory" && transferCalls == 0 && closeCalls == 1, "native allocation failure is observable and closes open clipboard");
	Reset(); conversionFails = true;
	Check(!SDL_SetClipboardText("copy") && error.find("convert") != std::string::npos && globalBlocks.empty() && closeCalls == 1, "conversion failure remains an ordinary failed write");
	Reset(); lockFails = true;
	Check(!SDL_SetClipboardText("copy") && error.find("code=7") != std::string::npos, "failed native lock preserves its original diagnostic");
	Check(transferCalls == 0 && globalFrees == 1 && globalBlocks.empty() && closeCalls == 1, "failed lock frees untransferred HGLOBAL and never publishes garbage");
	Reset(); transferFails = true;
	Check(!SDL_SetClipboardText("copy") && error.find("code=8") != std::string::npos, "failed transfer preserves its original diagnostic through cleanup");
	Check(transferCalls == 1 && globalFrees == 1 && globalBlocks.empty() && closeCalls == 1, "failed transfer frees storage whose ownership remained local");
	Reset(); emptyFails = true;
	Check(!SDL_SetClipboardText("copy") && error.find("code=6") != std::string::npos, "failed clear is not reported as successful replacement");
	Check(emptyCalls == 1 && closeCalls == 1 && transferCalls == 0 && globalBlocks.empty(), "failed clear closes once and performs no later allocation or transfer");
	Reset(); failedOpenAttempts = 3;
	Check(!SDL_SetClipboardText("copy") && error.find("code=5") != std::string::npos, "outer writer preserves the opener's original error");
	Check(emptyCalls == 0 && closeCalls == 0 && transferCalls == 0, "unopened clipboard is neither mutated nor closed");
	Reset(); Check(SDL_SetClipboardText(""), "empty text still clears clipboard via its explicit route");
	Check(emptyCalls == 1 && transferCalls == 0 && sdlBlocks.empty(), "empty write allocates no callback text");
	Reset(); emptyFails = true;
	Check(!SDL_SetClipboardText("") && error.find("empty clipboard") != std::string::npos, "empty-text write also reports clear failure");
	for (unsigned mode = 0; mode < 6; ++mode) {
		Reset();
		if (mode == 0) nativeText.clear();
		if (mode == 1) failedOpenAttempts = 3;
		if (mode == 2) lockFails = true;
		if (mode == 3) getFails = true;
		if (mode == 4) deviceAvailable = false;
		if (mode == 5) formatAvailable = false;
		char* read = SDL_GetClipboardText();
		Check(read != nullptr && read[0] == 0, "actual SDL read returns allocated empty for both empty content and ordinary native failures");
		Check(error.empty() == (mode == 0 || mode == 5), "patched read errors distinguish failure from successful empty content");
		SDL_free(read); Check(sdlBlocks.empty(), "allocated-empty results retain ordinary SDL caller ownership");
		if (mode == 1) Check(error.find("code=5") != std::string::npos, "failed read open preserves retry error through fallback allocation");
	}
	Reset(); char* read = SDL_GetClipboardText();
	Check(std::strcmp(read,"native") == 0 && error.empty() && closeCalls == 1, "successful native read remains unchanged");
	SDL_free(read); Reset(); duplicateFails = true;
	Check(SDL_GetClipboardText() == nullptr && sdlBlocks.empty() && closeCalls == 1,
		"failed text conversion and empty-fallback allocations return null without dereferencing it");
	for (const auto format : {CF_UNICODETEXT, CF_TEXT, CF_OEMTEXT}) {
		Reset(); advertisedFormat = format; nativeText = u"A\u00e9";
		Check(WIN_HasClipboardData(&video,"text/plain;charset=utf-8"), "all Windows convertible text formats are advertised");
		read = SDL_GetClipboardText();
		Check(read && std::string(read) == "A\xc3\xa9" && error.empty(), "legacy-advertised text uses synthesized Unicode without guessing its code page");
		Check(requestedFormats == std::vector<unsigned>{CF_UNICODETEXT}, "only Unicode data is requested even if ANSI or OEM was advertised");
		SDL_free(read);
		for (const bool failConversion : {false, true}) {
			Reset(); advertisedFormat = format; getFails = !failConversion; readConversionFails = failConversion;
			read = SDL_GetClipboardText();
			Check(read && read[0] == 0 && !error.empty(), "unavailable Unicode retrieval or conversion is an observable read failure");
			Check(requestedFormats == std::vector<unsigned>{CF_UNICODETEXT}, "failure never retries legacy bytes as UTF-8");
			SDL_free(read);
		}
	}
	Reset();
	std::printf("Patched SDL clipboard status: %u checks passed (counted Windows doubles; no OS clipboard claim)\n", checks);
}
