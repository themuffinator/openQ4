// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <string>

namespace openq4 {

// Engine-private checked UTF-8 clipboard services. These preserve all text,
// including newlines; field filtering belongs to the editor. Client SDL3, its
// main thread and build capability OPENQ4_SDL3_CHECKED_CLIPBOARD=1 are required.
// Only a provider verified to contain the package clipboard-status.patch may
// advertise that capability; a matching version number is insufficient. Other
// providers fail without SDL calls. The read publishes nothing on failure; empty
// clipboard text is a successful read. All SDL-owned storage is freed locally.
// Both methods enforce ui::TextInputMaxBytes and NUL-free scalar UTF-8; successful
// calls clear error. Error and text must be distinct storage. Diagnostics are
// not localized product copy. The byte bound limits accepted/copied text; SDL
// may allocate the native clipboard's complete contents before returning them.
// Native platform newline conversion is unchanged. False writes may already
// have affected the native clipboard; no clipboard rollback is promised.
bool SDL3_ReadTextClipboard(std::string& text, std::string& error);
bool SDL3_WriteTextClipboard(const std::string& text, std::string& error);

// These are transport primitives, not owner authorization. A future active
// editor must check its lease before calling; Cut may delete selection only
// after a successful write. Legacy Sys_*ClipboardData ABI is unchanged. Nothing
// here activates SDL input, calls RmlUi, handles fields or installs a live route.

} // namespace openq4
