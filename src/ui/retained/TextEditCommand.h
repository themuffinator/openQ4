// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "TextEdit.h"
#include <string_view>

namespace openq4::ui {

enum class TextEditCommand { Left, Right, Home, End, WordLeft, WordRight, SelectAll, Backspace, Delete };

// All offsets are bytes in this exact text, borrowed only during evaluation.
// The text-run provider supplies legal editing boundaries; this helper never
// discovers graphemes, words, bidi runs or keyboard bindings. Scalar-only runs
// therefore provide scalar-only editing. Multiple visual affinities for one
// logical offset are not representable in this bounded contract.
struct TextEditBoundaryMap {
	std::string_view text;
	// Unique offsets in visual traversal order; not necessarily logical order.
	std::vector<std::size_t> visualCarets;
	// Strictly increasing logical deletion boundaries, each also a legal caret.
	std::vector<std::size_t> deletionStops;
	// If present, a nonempty visual-order subsequence including visual run ends.
	// No whitespace or language heuristics are substituted when absent.
	std::optional<std::vector<std::size_t>> visualWordStops;
};

// A value-only proposal. The owner must revalidate the current edit identity
// and apply a replacement range atomically; producing this record acquires no
// input authority and never changes a buffer or accepted application state.
struct TextEditOperation {
	enum class Kind { Selection, Replace } kind = Kind::Selection;
	std::size_t anchor = 0, caret = 0;
	std::string replacement;
	bool changed = false;
};

// Left/Right (and word variants) collapse an unextended selection toward its
// corresponding visual endpoint without an additional step. Home/End use the
// supplied visual run ends, not inferred line structure. SelectAll uses the
// complete logical range. Deletion uses logical stops, never byte +/- 1.
// Extend is valid only for movement commands. At an edge, success returns an
// unchanged selection operation. Malformed/stale maps and unsupported commands
// leave out unchanged. Diagnostic error storage must not alias input/output.
bool EvaluateTextEditCommand(const TextEditState& state, const TextEditBoundaryMap& boundaries,
	TextEditCommand command, bool extendSelection, TextEditOperation& out, std::string& error);

} // namespace openq4::ui
