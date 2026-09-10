// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "retained/Interaction.h"

// Process-local editor target, independent of native text/IME session tokens.
// The manager additionally binds the registered OUTER allocation when draining.
// Never persist these requests or authorize them from caller dictionaries.
struct uiNumberEditorTarget_t {
	std::uint64_t backend = 0, document = 0, modal = 0;
	std::string control;
	openq4::ui::NumberEditIdentity edit;
	bool operator==(const uiNumberEditorTarget_t&) const = default;
};
struct uiNumberEditorSnapshot_t {
	uiNumberEditorTarget_t target;
	openq4::ui::NumberEditView editor;
};
enum class uiClipboardOperation_t { Copy, Cut, Paste };
struct uiClipboardRequest_t {
	uiClipboardOperation_t operation = uiClipboardOperation_t::Copy;
	uiNumberEditorTarget_t target;
};
