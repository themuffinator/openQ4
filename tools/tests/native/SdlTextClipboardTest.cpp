// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// This executable supplies counted SDL doubles. It never accesses a clipboard.
#include "src/sys/sdl3/TextClipboard.h"
#include "src/ui/retained/TextInput.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

static unsigned checks, reads, writes, frees, errorReads, threadReads, clears;
static bool mainThread = true, readFails, emptyReadFails, writeFails, nullError;
static std::string clipboard, diagnostic, written;
static std::string nativeError;
static std::map<void*, std::size_t> allocations;
static void Check(bool okay, const char* why) {
	++checks;
	if (!okay) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
extern "C" bool SDL_IsMainThread() { ++threadReads; return mainThread; }
extern "C" bool SDL_ClearError() { ++clears; nativeError.clear(); return true; }
extern "C" char* SDL_GetClipboardText() {
	++reads;
	if (readFails || emptyReadFails) nativeError = diagnostic;
	if (readFails) return nullptr;
	const auto& readText = emptyReadFails ? std::string{} : clipboard;
	auto* bytes = static_cast<char*>(std::malloc(readText.size()+1));
	Check(bytes != nullptr, "mock allocation succeeded");
	std::memcpy(bytes, readText.c_str(), readText.size()+1);
	allocations.emplace(bytes, readText.size()+1);
	return bytes;
}
extern "C" bool SDL_SetClipboardText(const char* text) {
	++writes; written = text;
	if (writeFails) { nativeError = diagnostic; return false; }
	clipboard = text; return true;
}
extern "C" void SDL_free(void* bytes) {
	Check(bytes != nullptr && allocations.erase(bytes) == 1, "exactly one free of every SDL-owned buffer");
	++frees; std::free(bytes);
}
extern "C" const char* SDL_GetError() { ++errorReads; return nullError ? nullptr : nativeError.c_str(); }
static void Reset() {
	Check(allocations.empty(), "no SDL allocation escapes a call");
	reads = writes = frees = errorReads = threadReads = clears = 0;
	mainThread = true; readFails = emptyReadFails = writeFails = nullError = false;
	clipboard.clear(); diagnostic = "native failure"; written.clear(); nativeError = "stale native error";
}

int main() {
	using namespace openq4;
	std::string output, error;
#if defined(USE_SDL3) && !defined(ID_DEDICATED) && defined(OPENQ4_SDL3_CHECKED_CLIPBOARD) && OPENQ4_SDL3_CHECKED_CLIPBOARD == 1
	using openq4::ui::TextInputMaxBytes;
	for (const auto& text : std::vector<std::string>{"", "\tfirst\r\nsecond\n", "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87",
		"\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xf0\x9f\x98\x80", "e\xcc\x81",
		std::string(TextInputMaxBytes, 'x')}) {
		Reset(); clipboard = text; output = "keep until success"; error = "stale";
		Check(SDL3_ReadTextClipboard(output, error) && output == text && error.empty(), "read preserves complete bounded UTF-8 and empty success");
		Check(reads == 1 && frees == 1 && allocations.empty() && writes == 0 && threadReads == 1 && errorReads == 1 && clears == 1,
			"successful read uses one SDL buffer with exact ownership");
		error = "stale";
		Check(SDL3_WriteTextClipboard(text, error) && written == text && clipboard == text && error.empty(), "write preserves complete bounded UTF-8");
		Check(writes == 1 && errorReads == 1 && clears == 2, "successful writes do not consult stale SDL error state");
	}
	for (const auto& invalid : std::vector<std::string>{"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc2", "x\x80",
		std::string(TextInputMaxBytes+1, 'x'), std::string(TextInputMaxBytes-3, 'x') + "\xf0\x9f\x98\x80"}) {
		Reset(); clipboard = invalid; output = "original buffer";
		Check(!SDL3_ReadTextClipboard(output, error) && output == "original buffer" && !error.empty(), "invalid or oversize read preserves caller storage");
		Check(reads == 1 && frees == 1 && allocations.empty(), "validation failure frees the SDL buffer exactly once");
		Check(!SDL3_WriteTextClipboard(invalid, error) && !error.empty() && writes == 0, "invalid write is rejected before touching clipboard");
	}
	Reset(); clipboard = "prior";
	Check(!SDL3_WriteTextClipboard(std::string("a\0b",3), error) && writes == 0 && clipboard == "prior", "embedded NUL never silently truncates a write");
	Reset(); readFails = true; output = "preserved";
	Check(!SDL3_ReadTextClipboard(output, error) && output == "preserved" && error.find("native failure") != std::string::npos,
		"null SDL result is failure distinct from empty clipboard");
	Check(reads == 1 && frees == 0 && errorReads == 1, "failed native allocation/read has nothing to free");
	for (const auto* reason : {"Video subsystem has not been initialized", "Could not open clipboard", "Could not lock clipboard data"}) {
		Reset(); emptyReadFails = true; diagnostic = reason; output = "preserved";
		Check(!SDL3_ReadTextClipboard(output, error) && output == "preserved" && error.find(reason) != std::string::npos,
			"SDL allocated-empty failure is not mistaken for a successful empty clipboard");
		Check(reads == 1 && frees == 1 && allocations.empty() && clears == 1, "allocated failure result is freed exactly once");
	}
	Reset(); writeFails = true; clipboard = "prior";
	Check(!SDL3_WriteTextClipboard("replacement", error) && clipboard == "prior" && error.find("native failure") != std::string::npos,
		"write reports failure so a future Cut cannot claim successful copy");
	Check(writes == 1 && errorReads == 1, "failed write returns the SDL result and diagnostic");
	for (const bool noDetail : {false, true}) {
		Reset(); readFails = true; nullError = noDetail; diagnostic.assign(10000,'x');
		Check(!SDL3_ReadTextClipboard(output, error) && !error.empty() && error.size() < 600, "missing or unbounded native diagnostics stay bounded");
	}
	Reset(); mainThread = false; output = "owned by caller";
	Check(!SDL3_ReadTextClipboard(output, error) && output == "owned by caller" && !error.empty(), "background read refused without changing output");
	Check(!SDL3_WriteTextClipboard("text", error) && !error.empty(), "background write refused");
	Check(reads == 0 && writes == 0 && frees == 0 && errorReads == 0 && clears == 0 && threadReads == 2, "thread refusal makes no clipboard or error calls");
	// Failure is not sticky: a later valid operation uses its own result.
	mainThread = true; clipboard.clear();
	Check(SDL3_ReadTextClipboard(output, error) && output.empty() && error.empty(), "successful empty read after refusal clears old error");
	Check(allocations.empty(), "all test buffers freed");
	std::printf("SDL text clipboard: %u checks passed using counted doubles; no live clipboard or owner-route claim\n", checks);
#else
	Reset(); output = "prior";
	Check(!SDL3_ReadTextClipboard(output, error) && output == "prior" && !error.empty(), "unsupported read fails atomically");
	Check(!SDL3_WriteTextClipboard("text", error) && !error.empty(), "unsupported write reports failure");
	Check(reads == 0 && writes == 0 && frees == 0 && errorReads == 0 && clears == 0 && threadReads == 0, "unsupported client/dedicated path makes no SDL calls");
	std::printf("Unsupported text clipboard: %u checks passed\n", checks);
#endif
}
