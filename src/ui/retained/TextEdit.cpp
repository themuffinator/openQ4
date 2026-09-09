// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "TextEdit.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <utility>

namespace openq4::ui {
namespace {
bool Fail(std::string& error, const char* message) { error = message; return false; }
bool Boundary(std::string_view text, std::size_t offset) {
	return offset <= text.size() && (offset == text.size() ||
		(static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80);
}
bool ValidPolicy(const TextEditPolicy& policy, std::string& error) {
	if (!policy.maxBytes || policy.maxBytes > TextInputMaxBytes || (policy.allowTab && !policy.multiline))
		return Fail(error, "Invalid text field policy");
	return true;
}
bool FieldText(std::string_view text, const TextEditPolicy& policy, std::string& error) {
	if (!ValidateTextInputUtf8(text,error)) return false;
	if (text.size() > policy.maxBytes) return Fail(error, "Text exceeds the field limit");
	// These UTF-8 sequences are complete Unicode line separators. Validation
	// above guarantees that a match cannot begin inside another scalar.
	if (!policy.multiline && (text.find("\xc2\x85") != std::string_view::npos ||
		text.find("\xe2\x80\xa8") != std::string_view::npos || text.find("\xe2\x80\xa9") != std::string_view::npos))
		return Fail(error, "Single-line text contains a Unicode line separator");
	for (const unsigned char byte : text) {
		if (byte >= 0x20 && byte != 0x7f) continue;
		if (byte == '\n' && policy.multiline) continue;
		if (byte == '\t' && policy.allowTab) continue;
		return Fail(error, "Text contains a disallowed field control character");
	}
	error.clear(); return true;
}
bool Replacement(std::string_view source, const TextEditPolicy& policy, std::string& out, std::string& error) {
	if (!ValidateTextInputUtf8(source,error)) return false;
	std::string candidate;
	candidate.reserve(source.size());
	for (std::size_t i = 0; i < source.size(); ++i) {
		if (source[i] == '\r' && policy.multiline) {
			candidate += '\n';
			if (i+1 < source.size() && source[i+1] == '\n') ++i;
		} else candidate += source[i];
	}
	if (!FieldText(candidate,policy,error)) return false;
	out = std::move(candidate); return true;
}
}

bool TextEditBuffer::Reset(std::string_view text, const TextEditPolicy& candidatePolicy, std::string& error) {
	if (!ValidPolicy(candidatePolicy,error) || !FieldText(text,candidatePolicy,error)) return false;
	TextEditState candidate{std::string(text),text.size(),text.size()};
	state = std::move(candidate); policy = candidatePolicy;
	undo.clear(); redo.clear(); composition.reset(); error.clear(); return true;
}
std::string TextEditBuffer::PresentedText() const {
	if (!composition) return state.text;
	const auto begin = std::min(state.anchor,state.caret), end = std::max(state.anchor,state.caret);
	return state.text.substr(0,begin) + composition->text + state.text.substr(end);
}
bool TextEditBuffer::SetSelection(std::size_t anchor, std::size_t caret, std::string& error) {
	if (composition) return Fail(error, "Cancel composition before changing the selection");
	if (!Boundary(state.text,anchor) || !Boundary(state.text,caret)) return Fail(error, "Selection splits a scalar or exceeds the buffer");
	state.anchor = anchor; state.caret = caret; error.clear(); return true;
}
bool TextEditBuffer::ReplaceSelection(std::string_view replacement, std::string& error) {
	return Replace(replacement,false,error);
}
bool TextEditBuffer::Replace(std::string_view replacement, bool fromCommit, std::string& error) {
	return ReplaceAt(state.anchor,state.caret,replacement,fromCommit,error);
}
bool TextEditBuffer::ReplaceRange(std::size_t anchor, std::size_t caret, std::string_view replacement, std::string& error) {
	return ReplaceAt(anchor,caret,replacement,false,error);
}
bool TextEditBuffer::ReplaceAt(std::size_t anchor, std::size_t caret, std::string_view replacement,
	bool fromCommit, std::string& error) {
	if (composition && !fromCommit) return Fail(error, "Cancel composition before editing the buffer");
	if (!Boundary(state.text,anchor) || !Boundary(state.text,caret)) return Fail(error, "Replacement range splits a scalar or exceeds the buffer");
	std::string normalized;
	if (!Replacement(replacement,policy,normalized,error)) return false;
	const auto begin = std::min(anchor,caret), end = std::max(anchor,caret);
	if (normalized.size() > policy.maxBytes - (state.text.size()-(end-begin)))
		return Fail(error, "Replacement exceeds the field limit");
	TextEditState candidate;
	candidate.text = state.text.substr(0,begin) + normalized + state.text.substr(end);
	candidate.anchor = candidate.caret = begin + normalized.size();
	if (candidate.text != state.text) { undo.push_back(state); redo.clear(); }
	state = std::move(candidate); composition.reset(); TrimHistory(); error.clear(); return true;
}
bool TextEditBuffer::Apply(const TextInputEvent& event, std::string& error) {
	if (!ValidateTextInputEvent(event,error)) return false;
	if (event.kind == TextInputKind::CancelComposition || event.text.empty()) {
		composition.reset(); error.clear(); return true;
	}
	if (event.kind == TextInputKind::Commit) return Replace(event.text,true,error);
	// Preedit indices describe the exact incoming UTF-8 string. Do not normalize
	// its bytes and silently invalidate those indices; field policy can reject it.
	if (!FieldText(event.text,policy,error)) return false;
	const auto selected = std::max(state.anchor,state.caret)-std::min(state.anchor,state.caret);
	if (event.text.size() > policy.maxBytes-(state.text.size()-selected))
		return Fail(error, "Composition exceeds the field limit");
	composition = event; error.clear(); return true;
}
void TextEditBuffer::CancelComposition() { composition.reset(); }
TextEditHistory TextEditBuffer::CaptureHistory() const { return {undo,redo}; }
bool TextEditBuffer::RestoreHistory(const TextEditState& saved, const TextEditHistory& history,
	const TextEditPolicy& savedPolicy, std::string& error) {
	if (!ValidPolicy(savedPolicy,error)) return false;
	if (history.undo.size() > MaxHistoryEntries || history.redo.size() > MaxHistoryEntries-history.undo.size())
		return Fail(error,"Restored text history exceeds the entry limit");
	auto valid = [&](const TextEditState& entry) {
		if (!FieldText(entry.text,savedPolicy,error)) return false;
		if (!Boundary(entry.text,entry.anchor) || !Boundary(entry.text,entry.caret))
			return Fail(error,"Restored text selection splits a scalar or exceeds the buffer");
		return true;
	};
	if (!valid(saved)) return false;
	std::size_t bytes = 0;
	for (const auto* entries : {&history.undo,&history.redo}) for (const auto& entry : *entries) {
		if (!valid(entry)) return false;
		if (entry.text.size() > MaxHistoryTextBytes-bytes) return Fail(error,"Restored text history exceeds the byte limit");
		bytes += entry.text.size();
	}
	TextEditBuffer candidate;
	candidate.policy = savedPolicy; candidate.state = saved;
	candidate.undo = history.undo; candidate.redo = history.redo;
	*this = std::move(candidate); error.clear(); return true;
}
std::size_t TextEditBuffer::HistoryTextBytes() const {
	std::size_t bytes = 0;
	for (const auto& entry : undo) bytes += entry.text.size();
	for (const auto& entry : redo) bytes += entry.text.size();
	return bytes;
}
void TextEditBuffer::TrimHistory() {
	while (HistoryEntries() > MaxHistoryEntries || HistoryTextBytes() > MaxHistoryTextBytes) {
		// Keep the nearest undo and redo endpoints. Each buffer is bounded by
		// 64 KiB, so these two endpoints always fit within the combined budget.
		if (undo.size() > 1) undo.erase(undo.begin());
		else if (redo.size() > 1) redo.erase(redo.begin());
		else break;
	}
}
bool TextEditBuffer::Undo(std::string& error) {
	if (composition) return Fail(error, "Cancel composition before undo");
	if (undo.empty()) return Fail(error, "No text edit to undo");
	redo.push_back(state); state = std::move(undo.back()); undo.pop_back();
	TrimHistory(); error.clear(); return true;
}
bool TextEditBuffer::Redo(std::string& error) {
	if (composition) return Fail(error, "Cancel composition before redo");
	if (redo.empty()) return Fail(error, "No text edit to redo");
	undo.push_back(state); state = std::move(redo.back()); redo.pop_back();
	TrimHistory(); error.clear(); return true;
}

TextNumberStatus ParseTextNumber(std::string_view text, const TextNumberPolicy& policy, double& value) {
	if (!std::isfinite(policy.minimum) || !std::isfinite(policy.maximum) || policy.minimum > policy.maximum)
		return TextNumberStatus::InvalidPolicy;
	if (text.empty()) return TextNumberStatus::Empty;
	if (text.size() > TextInputMaxBytes) return TextNumberStatus::Invalid;
	std::size_t at = 0;
	if (text[at] == '+' || text[at] == '-') ++at;
	bool digit = false;
	while (at < text.size() && text[at] >= '0' && text[at] <= '9') { digit = true; ++at; }
	if (at < text.size() && text[at] == '.') {
		++at;
		while (at < text.size() && text[at] >= '0' && text[at] <= '9') { digit = true; ++at; }
	}
	if (!digit) return at == text.size() ? TextNumberStatus::Incomplete : TextNumberStatus::Invalid;
	if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
		if (!policy.exponent) return TextNumberStatus::Invalid;
		++at;
		if (at < text.size() && (text[at] == '+' || text[at] == '-')) ++at;
		const auto exponentStart = at;
		while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
		if (at == exponentStart) return at == text.size() ? TextNumberStatus::Incomplete : TextNumberStatus::Invalid;
	}
	if (at != text.size()) return TextNumberStatus::Invalid;
	// from_chars deliberately excludes leading '+', which the field accepts.
	if (text.front() == '+') text.remove_prefix(1);
	double candidate = 0;
	const auto parsed = std::from_chars(text.data(),text.data()+text.size(),candidate,std::chars_format::general);
	if (parsed.ec == std::errc::result_out_of_range) return TextNumberStatus::OutOfRange;
	if (parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size() || !std::isfinite(candidate)) return TextNumberStatus::Invalid;
	if (candidate < policy.minimum || candidate > policy.maximum) return TextNumberStatus::OutOfRange;
	value = candidate; return TextNumberStatus::Valid;
}
bool FormatTextNumber(double value, const TextNumberPolicy& policy, std::string& text, std::string& error) {
	if (!std::isfinite(policy.minimum) || !std::isfinite(policy.maximum) || policy.minimum > policy.maximum)
		return Fail(error, "Invalid numeric field policy");
	if (!std::isfinite(value)) return Fail(error, "Cannot edit a non-finite numeric value");
	// Fixed shortest forms of all binary64 values fit within 768 characters,
	// including sign, decimal point and the smallest subnormal's leading zeros.
	char buffer[768];
	// Shortest round-trip form preserves the exact double without exposing
	// unnecessary trailing digits in an editable value such as 1.1.
	const auto result = std::to_chars(buffer,buffer+sizeof(buffer),value,
		policy.exponent ? std::chars_format::general : std::chars_format::fixed);
	if (result.ec != std::errc{}) return Fail(error, "Cannot format the numeric edit value");
	text.assign(buffer,result.ptr); error.clear(); return true;
}

} // namespace openq4::ui
