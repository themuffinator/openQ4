// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "TextClipboard.h"

#ifndef OPENQ4_SDL3_CHECKED_CLIPBOARD
#define OPENQ4_SDL3_CHECKED_CLIPBOARD 0
#endif
#if defined(USE_SDL3) && !defined(ID_DEDICATED) && OPENQ4_SDL3_CHECKED_CLIPBOARD == 1
#include "../../ui/retained/TextInput.h"
#include <SDL3/SDL.h>
#include <memory>
#include <utility>

namespace openq4 {
namespace {

bool ClipboardFailure(std::string& error, const char* operation, const char* detail) {
	error = operation;
	if (detail && *detail) {
		// SDL error strings are diagnostics, not another unbounded text input.
		std::size_t length = 0;
		while (length < 512 && detail[length]) ++length;
		error += ": ";
		error.append(detail, length);
	}
	return false;
}

} // namespace

bool SDL3_ReadTextClipboard(std::string& text, std::string& error) {
	if (!SDL_IsMainThread()) { error = "Clipboard access requires the SDL main thread"; return false; }
	SDL_ClearError();
	const std::unique_ptr<char, decltype(&SDL_free)> bytes(SDL_GetClipboardText(), SDL_free);
	// SDL may return an allocated empty string on native read failure. The pinned
	// SDL clipboard patch reports those failures; null alone is not sufficient.
	const char* detail = SDL_GetError();
	if (!bytes || (detail && *detail)) return ClipboardFailure(error, "SDL clipboard read failed", detail);
	std::size_t length = 0;
	while (length <= ui::TextInputMaxBytes && bytes.get()[length]) ++length;
	if (length > ui::TextInputMaxBytes) { error = "Clipboard exceeds the UTF-8 transport limit"; return false; }
	const std::string_view view(bytes.get(), length);
	if (!ui::ValidateTextInputUtf8(view, error)) return false;
	std::string candidate(view);
	text = std::move(candidate);
	error.clear();
	return true;
}

bool SDL3_WriteTextClipboard(const std::string& text, std::string& error) {
	if (!SDL_IsMainThread()) { error = "Clipboard access requires the SDL main thread"; return false; }
	if (!ui::ValidateTextInputUtf8(text, error)) return false;
	SDL_ClearError();
	if (!SDL_SetClipboardText(text.c_str())) return ClipboardFailure(error, "SDL clipboard write failed", SDL_GetError());
	error.clear();
	return true;
}

} // namespace openq4
#else
namespace openq4 {
bool SDL3_ReadTextClipboard(std::string&, std::string& error) {
	error = "Checked text clipboard requires a verified patched SDL3 client";
	return false;
}
bool SDL3_WriteTextClipboard(const std::string&, std::string& error) {
	error = "Checked text clipboard requires a verified patched SDL3 client";
	return false;
}
} // namespace openq4
#endif
